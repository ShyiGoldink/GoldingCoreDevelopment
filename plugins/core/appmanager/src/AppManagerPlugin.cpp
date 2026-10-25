// ---------------------------------------------------------------------------
// appmanager —— 程序管理（侧栏 + 页面 + 两个弹窗）。
//
// 三块职责分得很开：
//   - `AppStore.{h,cpp}`     数据层：清单 + 每个应用的小文件夹（不碰界面）
//   - `AppManagerUi.{h,cpp}` 界面层：侧栏盒子 / 页面 / 添加与设置弹窗
//   - 本文件                  接线：注册区域项与页面、拿服务、装配后自检
//
// 依赖面板插件（`requires.plugins`）：区域项要求 `sidebar` 区域**已经存在**，
// 导航与弹窗也由它提供。没有面板时本插件装不上，这是明确的选择——它的全部内容
// 都是"往面板里放东西"。
//
// 为什么侧栏只注册**一个**区域项而不是每个应用一项：`create` 收不到"我是哪一项"，
// 而区域表是装配期一次性扫完的。自管一个盒子之后，运行期导入的应用立刻能出现在
// 侧栏里，不需要面板重扫。
// ---------------------------------------------------------------------------

#include "AppManagerUi.h"
#include "AppStore.h"

#include "GoldingServices.h"

#include <sdk/GoldingPlugin.h>

#include <scene/InkingScene.h>
#include <widget/InkingWidget.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{

golding::Host g_host{nullptr, nullptr};

const GoldingPageDecl kPageDecl = {
    appmanager::kPageId, // id
    "程序管理",          // 抽屉 / 导航里显示的名字
    1000,                // order：排在其他页面之后（面板装配时挂第一个）
    &appmanager::createPage,
};

const GoldingRegionItemDecl kSidebarDecl = {
    "sidebar",                    // 区域：面板插件自己定的约定
    appmanager::kSidebarRegionItemId,
    "程序",                        // 显示名（面板不会自己画，我们填了 create）
    nullptr,                       // 图标：由我们自绘
    10,                            // 排在其他侧栏项之前
    &appmanager::createSidebar,
    nullptr,                       // 只有界面，没有"点一下"这一层
};

int check(const char* label, bool ok, const std::string& detail)
{
    std::printf("[%s] appmanager.%-24s %s\n", ok ? "ok" : "FAIL", label, detail.c_str());
    return ok ? 0 : 1;
}

/** 数据目录可写吗（写一个探针文件再删掉）。 */
bool dataDirWritable()
{
    std::error_code ec;
    const std::filesystem::path root = appmanager::DataRoot();
    std::filesystem::create_directories(root, ec);
    if (ec)
    {
        return false;
    }
    const std::filesystem::path probe = root / L".write-probe";
    {
        std::ofstream stream(probe, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return false;
        }
        stream << "ok";
    }
    std::filesystem::remove(probe, ec);
    return true;
}

int runReady(const golding::Host& host)
{
    (void)host;
    int failures = 0;
    const appmanager::UiState& state = appmanager::State();

    failures += check("数据目录可写", dataDirWritable(),
                      appmanager::ToUtf8(appmanager::DataRoot()));
    failures += check("清单里有应用", !state.apps.empty(),
                      "apps=" + std::to_string(state.apps.size()) + "（预置表 " +
                          std::to_string(appmanager::SeedCount()) + " 项）");
    failures += check("选中了一个应用", appmanager::FindApp(state.selectedId) != nullptr,
                      "selected=" + state.selectedId);

    // 内置图标：预置表里用到的那些名字都该有文件。缺一个就会静默退化成通用图标。
    int missing = 0;
    for (const char* name :
         {"app", "code", "game", "terminal", "music", "video", "folder", "chat", "globe", "menu", "settings"})
    {
        if (appmanager::BuiltinIconPath(name).empty())
        {
            ++missing;
        }
    }
    failures += check("内置图标齐全", missing == 0, "missing=" + std::to_string(missing));

    const int rows = appmanager::SidebarRowCount();
    failures += check("侧栏行数 = 应用数 + 1",
                      rows == static_cast<int>(state.apps.size()) + 1,
                      "rows=" + std::to_string(rows) + " apps=" + std::to_string(state.apps.size()));

    failures += check("右侧页面已装配", state.pageRoot != nullptr,
                      "builds=" + std::to_string(state.pageBuildCount));

    // 默认没有背景：这条断言是"背景只在配了的时候才铺"的观测口。
    const appmanager::AppEntry* selected = appmanager::FindApp(state.selectedId);
    const bool hasBackground =
        selected != nullptr && selected->backgroundType != "none" && !selected->backgroundPath.empty();
    failures += check("背景只在配了时铺", appmanager::BackgroundNode() != nullptr || !hasBackground,
                      std::string("node=") + (appmanager::BackgroundNode() != nullptr ? "有" : "无") + " 配了=" +
                          (hasBackground ? "是" : "否"));

    // 纯函数：导入路径的判定与默认标题。
    const bool launchOk =
        appmanager::IsLaunchablePath(std::filesystem::path(L"C:\\x\\WeChat.exe")) &&
        appmanager::IsLaunchablePath(std::filesystem::path(L"C:\\x\\a.BAT")) &&
        appmanager::IsLaunchablePath(std::filesystem::path(L"C:\\x\\b.lnk")) &&
        !appmanager::IsLaunchablePath(std::filesystem::path(L"C:\\x\\c.png")) &&
        !appmanager::IsLaunchablePath(std::filesystem::path(L"C:\\x\\d.txt"));
    failures += check("导入类型判定", launchOk, "exe/bat/lnk 收，png/txt 拒");

    const std::string title = appmanager::TitleFromPath(std::filesystem::path(L"C:\\Tools\\My App.exe"));
    failures += check("默认标题 = 文件名去扩展", title == "My App", "title=" + title);

    // "进已有进程"的两半：进程枚举真的在工作（explorer 一定在跑），
    // 以及"没有这个程序"必须老实返回 false（不激活、不乱开）。
    const int explorerProcesses = appmanager::RunningInstanceCount("C:\\Windows\\explorer.exe");
    failures += check("能枚举到正在运行的进程", explorerProcesses >= 1,
                      "explorer 实例=" + std::to_string(explorerProcesses));
    const bool noPhantom = !appmanager::ActivateRunning("C:\\GoldingCore\\no-such-app.exe");
    failures += check("不存在的程序不假装激活", noPhantom, "activate=false");

    // 数据层：真走一遍"新建应用"（目录 + ini + 清单追加），然后原地收拾干净。
    // 用副本当容器，清单再用原表重写——自检不能给用户多加一个应用。
    {
        std::vector<appmanager::AppEntry> sandbox = state.apps;
        appmanager::AppEntry created;
        const std::filesystem::path fake = std::filesystem::temp_directory_path() / L"golding-import-probe.exe";
        const bool made = appmanager::CreateApp(fake, sandbox, &created);
        const bool dirOk = made && std::filesystem::is_regular_file(
                                         appmanager::AppDir(created.id) / L"app.ini");
        if (made)
        {
            std::error_code ec;
            std::filesystem::remove_all(appmanager::AppDir(created.id), ec);
        }
        appmanager::SaveManifest(state.apps); // 恢复清单（副本里那条不落盘）
        failures += check("新建应用的目录与配置", made && dirOk,
                          "id=" + created.id + " dir=" + (dirOk ? "有" : "无"));
    }

    // 清单读写：临时目录里走一遍"读（转义 / 嵌套）→ 写 → 再读"，跑完删目录。
    //
    // 三条一次钉全，都是老的手写扫描做不到的事：
    //   - `\uXXXX` 转义要解成汉字（老实现按字面量收，读成 `u4e2d`）；
    //   - 嵌套数组**后面的** id 不能丢（老实现在第一个 `]` 就截断）；
    //   - 写出去的那份必须能被自己读回来（老实现手拼、不做转义）。
    {
        std::error_code ec;
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / L"golding-appmanager-manifest-probe";
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path file = dir / L"apps.json";

        const std::string sample =
            "{\"version\":1,\"apps\":[\"a\",\"\\u4e2d\\u6587\",[\"nested\"],\"b\"]}";
        {
            std::ofstream stream(file, std::ios::binary | std::ios::trunc);
            stream << sample;
        }

        std::vector<std::string> ids;
        const bool read = appmanager::ReadManifestFile(file, ids);
        const bool readOk = read && ids.size() == 3 && ids[0] == "a" && ids[1] == "\xe4\xb8\xad\xe6\x96\x87" &&
                            ids[2] == "b";

        std::vector<std::string> again;
        const bool roundTrip = appmanager::WriteManifestFile(file, ids) &&
                               appmanager::ReadManifestFile(file, again) && again == ids;

        std::filesystem::remove_all(dir, ec);
        failures += check("清单读写：转义/嵌套/往返", readOk && roundTrip,
                          "读到 " + std::to_string(ids.size()) + " 项" +
                              (ids.size() == 3 ? "（a / 中文 / b，嵌套项已跳过）" : "") +
                              " 往返=" + (roundTrip ? "一致" : "不一致"));
    }

    // 点击链：语义点击侧栏第二行 → 选中变了 + 页面重建。走的是与真机同一条回调。
    auto* root = static_cast<inking::InkingWidget*>(host.UiRoot());
    inking::InkingScene* scene = root != nullptr ? root->GetScene() : nullptr;
    if (scene != nullptr && state.apps.size() >= 2)
    {
        const std::string first = state.selectedId;
        const std::string second = state.apps[1].id;
        const int buildsBefore = state.pageBuildCount;
        const bool clicked = scene->Activate(("appmanager.row." + second).c_str());
        const bool switched = clicked && appmanager::State().selectedId == second;
        const bool rebuilt = state.pageBuildCount > buildsBefore;
        failures += check("点侧栏切应用并重建页面", switched && rebuilt,
                          "clicked=" + std::to_string(clicked ? 1 : 0) + " selected=" +
                              appmanager::State().selectedId + " builds=+" +
                              std::to_string(state.pageBuildCount - buildsBefore));
        // 复原：自检不能改变首帧的样子。
        scene->Activate(("appmanager.row." + first).c_str());
    }
    else
    {
        failures += check("点侧栏切应用并重建页面", false, "缺场景或应用不足两个");
    }

    // 两个弹窗：`PopupService` 没有"开着吗"的查询口，所以看 `create` 被调了几次。
    const int settingsBefore = appmanager::SettingsBuildCount();
    appmanager::OpenSettingsPopup(state.selectedId);
    const int settingsAfter = appmanager::SettingsBuildCount();
    if (state.popup != nullptr)
    {
        state.popup->pop(nullptr);
    }
    failures += check("设置弹窗能建", settingsAfter > settingsBefore,
                      "builds=" + std::to_string(settingsAfter));

    const int importBefore = appmanager::ImportBuildCount();
    appmanager::OpenImportPopup();
    const int importAfter = appmanager::ImportBuildCount();
    if (state.popup != nullptr)
    {
        state.popup->pop(nullptr);
    }
    failures += check("添加弹窗能建", importAfter > importBefore, "builds=" + std::to_string(importAfter));

    return failures;
}

class AppManagerPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        g_host = host;
        appmanager::UiState& state = appmanager::State();
        state.host = host;

        state.popup = host.Use<golding::PopupService>(golding::kPopupServiceName, golding::kPopupServiceVersion);
        if (state.popup == nullptr)
        {
            host.Warn("appmanager：没有 golding.popup，添加 / 设置弹窗将不可用");
        }
        state.nav = host.Use<golding::NavigationService>(golding::kNavigationServiceName, 1);
        if (state.nav == nullptr)
        {
            host.Warn("appmanager：没有 golding.navigation，点了侧栏不会切页");
        }

        // 自己的目录：内置图标在 `<插件目录>/assets/icons/`。
        const char* directory = host.PluginDir();
        if (directory != nullptr && directory[0] != '\0')
        {
            appmanager::SetPluginDir(appmanager::FromUtf8(directory));
        }

        bool seeded = false;
        state.apps.clear();
        if (!appmanager::Load(state.apps, &seeded))
        {
            host.Warn("appmanager：数据读不出来（清单 / 预置都失败）");
        }
        if (!state.apps.empty())
        {
            state.selectedId = state.apps.front().id;
        }
        std::printf("[appmanager] 数据：%zu 个应用%s（%s）\n", state.apps.size(),
                    seeded ? "（首次播种）" : "", appmanager::ToUtf8(appmanager::DataRoot()).c_str());

        // 两条顺序都对：面板在 createShell 里先扫区域项再挂页面，两者都在 registerAll 之后。
        bool ok = host.AddPage(kPageDecl);
        ok = host.AddRegionItem(kSidebarDecl) && ok;
        return ok;
    }

    int Ready(const golding::Host& host) override { return runReady(host); }
};

} // namespace

GOLDING_EXPORT_PLUGIN(AppManagerPlugin)
