// 宿主 exe：把 core 的编排（HostApp）接到 InkingUIKit 的窗口上。
//
// 这个文件只做四件事：
//   1. 解析命令行——**只认窗口层的那几个**，其余原样当 bootMode 交给插件；
//   2. 跑 HostApp 的四阶段：discover → prepare → registerAll →（装配）→ notifyReady；
//   3. 建窗口、装面板根、把界面根交给插件；
//   4. 决定进程退出码。
//
// 它**不认识**任何具体插件、节点名、区域名、页面 id。面板插件换一套，这里一个字节不动
// （验收语：换掉 core 插件，界面就换一套，宿主 exe 一个字节不动）。
//
// 与 0.4.0 之前那版 main.cpp 的差别：不再用 `onRenderBoundTick` 数帧 + 无参
// `RequestClose()` 那套截图脚手架。0.4.0 给了 `Open()` / `RunFrames()` /
// `RequestClose(int)` / `Show()` 返回退出码，非交互模式因此是一段直线。

#include <asset/InkingAsset.h>
#include <config/window_config.h>
#include <prefab/PrefabTheme.h>
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>
#include <window/WindowConfig.h>

#include "Application/PageRegistry.h"
#include "Application/PanelRootRegistry.h"
#include "Application/PluginLibrary.h"
#include "Application/RegionRegistry.h"
#include "Application/ServiceRegistry.h"
#include "Application/SettingsRegistry.h"
#include "HostApp.h"
#include "Tool/PathFinder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

/**
 * 设计画布尺寸（1080p）。**整个工程只在这里写一次。**
 *
 * 面板插件从父节点量尺寸（`parent->GetWidth()/GetHeight()`），自己**不抄**这两个数——
 * 所以改这里就等于改整台机器的设计尺寸，不用动任何插件。反过来，如果哪个插件又写死了
 * 1920/1080，"改设计尺寸"就会变成一次四处对齐的手工活，而漏掉的那处只在换了尺寸之后
 * 才露出来。
 */
constexpr int kDesignWidth = 1920;
constexpr int kDesignHeight = 1080;

/** 退出码：0 全绿；1 启动期硬失败（没到断言那一步）；2 有断言失败。 */
constexpr int kExitOk = 0;
constexpr int kExitFatal = 1;
constexpr int kExitFailed = 2;

struct Options
{
    bool fullscreen = false; // 启动即全屏
    bool bordered = false;   // 要系统标题栏（默认无边框）
    bool help = false;
    /** 跑几帧后退出；**负数 = 不给 `--frames`，常驻**。
     *
     * 用"给没给这个参数"区分两种模式，而不是用一个 `--stay` 开关：双击 exe 的默认
     * 行为必须是"开窗、留着、等我关"，而"跑 N 帧就退"是 CI 与截图脚本的行为。
     * 反过来设默认值（默认跑三帧）会让直接运行时窗口一闪而过——那不是默认值，
     * 那是把测试模式当产品模式。 */
    int frames = -1;
    std::string capture; // 截图路径（空 = 不截）
    std::string bootMode; // 交给插件解释的启动模式串
};

void printUsage()
{
    std::printf(
        "golding_host —— GoldingCore 宿主\n"
        "\n"
        "用法：golding_host [窗口层选项] [界面层模式]\n"
        "\n"
        "窗口层选项（宿主自己解释）：\n"
        "  --fullscreen      启动即全屏\n"
        "  --bordered        要系统标题栏（默认无边框）\n"
        "  --frames=N        跑 N 帧后退出（CI / 截图用；**不给就常驻**）\n"
        "  --capture=PATH    截图（.png 或 .bmp）\n"
        "  -h, --help        这份说明\n"
        "\n"
        "界面层模式（例如 --expanded / --page=0 / --theme=xx）宿主原样经 `bootMode`\n"
        "交给插件——那是什么意思只有面板插件知道。"
        "\n");
}

/** 认得的就吃掉，认不得的当 bootMode（只取第一个）。返回 false = 参数错误。 */
bool parseArgs(int argc, char** argv, Options& options)
{
    for (int index = 1; index < argc; ++index)
    {
        const char* arg = argv[index];
        if (arg == nullptr)
        {
            continue;
        }
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0)
        {
            options.help = true;
        }
        else if (std::strcmp(arg, "--fullscreen") == 0)
        {
            options.fullscreen = true;
        }
        else if (std::strcmp(arg, "--bordered") == 0)
        {
            options.bordered = true;
        }
        else if (std::strncmp(arg, "--frames=", 9) == 0)
        {
            // 严格解析：`atoi` 会把 "abc" 静默当成 0，而 0 是合法值——
            // 一个拼错的参数会变成"跑 0 帧"，看起来像跑过了。
            char* end = nullptr;
            const long value = std::strtol(arg + 9, &end, 10);
            if (end == arg + 9 || *end != '\0' || value < 0 || value > 100000)
            {
                std::printf("[host] --frames 要一个 0..100000 的整数：%s\n", arg + 9);
                return false;
            }
            options.frames = static_cast<int>(value);
        }
        else if (std::strncmp(arg, "--capture=", 10) == 0)
        {
            options.capture = arg + 10;
        }
        else if (std::strncmp(arg, "--", 2) == 0)
        {
            // 宿主不认识 ⇒ 界面层的模式。只留第一个，多给的打一行提示：
            // 只认第一个是有意的——"这一轮跑哪种模式"是单值，多值会让插件各自解释出两套。
            if (options.bootMode.empty())
            {
                options.bootMode = arg;
            }
            else
            {
                std::printf("[host] 忽略多余的界面层模式：%s（已取 %s）\n", arg, options.bootMode.c_str());
            }
        }
        else
        {
            std::printf("[host] 不认识的参数：%s（--help 看用法）\n", arg);
            return false;
        }
    }
    return true;
}

/** 一行几何，诊断用。 */
std::string describe(const inking::InkingWidget* widget)
{
    if (widget == nullptr)
    {
        return "null";
    }
    char buffer[160] = {};
    std::snprintf(buffer, sizeof(buffer), "x=%.0f y=%.0f w=%d h=%d", static_cast<double>(widget->GetX()),
                  static_cast<double>(widget->GetY()), widget->GetWidth(), widget->GetHeight());
    return buffer;
}

void dumpTree(const inking::InkingWidget* node, int depth)
{
    if (node == nullptr)
    {
        return;
    }
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    std::printf("%s- %-18s %s\n", indent.c_str(), node->GetName().c_str(), describe(node).c_str());
    for (const inking::InkingWidget* child : node->GetChildren())
    {
        dumpTree(child, depth + 1);
    }
}

/**
 * 打印插件档案与登记表——"有哪些插件、能往哪儿注册"是产品唯一的可观测面。
 *
 * 这一段不参与判定，只为了"坏了能看出坏在哪"。插件崩了不隔离（`docs/Options.md`），
 * 日志就是唯一的排查面。
 */
void dumpRegistries()
{
    std::printf("\n[host] 插件档案：\n");
    for (int index = 0; index < golding::PluginLibrary::count(); ++index)
    {
        const GoldingPluginInfo* info = golding::PluginLibrary::at(index);
        if (info == nullptr)
        {
            continue;
        }
        const char* role = info->role != nullptr && info->role[0] != '\0' ? info->role : "-";
        std::printf("  %-14s state=%d abi=%d sdk=%s role=%s\n", info->name, info->state, info->abiVersion,
                    golding::PluginLibrary::sdkOf(index), role);
    }

    const auto pages = golding::PageRegistry::allPages();
    if (!pages.empty())
    {
        std::printf("\n[host] 页面表：\n");
        for (const GoldingPageDecl* page : pages)
        {
            std::printf("  %-12s「%s」order=%d\n", page->id, page->title != nullptr ? page->title : "", page->order);
        }
    }

    const auto sections = golding::SettingsRegistry::allSections();
    if (!sections.empty())
    {
        std::printf("\n[host] 设置区块：\n");
        for (const GoldingSettingsDecl* section : sections)
        {
            std::printf("  %-12s「%s」order=%d\n", section->id, section->title != nullptr ? section->title : "",
                        section->order);
        }
    }

    const auto regions = golding::RegionRegistry::allRegions();
    if (!regions.empty())
    {
        std::printf("\n[host] 区域表：\n");
        for (const GoldingRegionDecl* region : regions)
        {
            std::printf("  %-12s「%s」order=%d\n", region->id, region->title != nullptr ? region->title : "",
                        region->order);
            const std::size_t items = golding::RegionRegistry::itemCount(region->id);
            for (std::size_t itemIndex = 0; itemIndex < items; ++itemIndex)
            {
                const GoldingRegionItemDecl* item = golding::RegionRegistry::itemAt(region->id, itemIndex);
                if (item == nullptr)
                {
                    continue;
                }
                std::printf("    - %-10s %s order=%d%s\n", item->id, item->title != nullptr ? item->title : "",
                            item->order, item->create == nullptr ? " [仅动作]" : "");
            }
        }
    }

    const auto services = golding::ServiceRegistry::allNames();
    if (!services.empty())
    {
        std::printf("\n[host] 服务表：");
        for (const std::string& name : services)
        {
            std::printf(" %s", name.c_str());
        }
        std::printf("\n");
    }
}

void check(const char* label, bool ok, const std::string& detail, int& failures)
{
    std::printf("[%s] %-24s %s\n", ok ? "ok" : "FAIL", label, detail.c_str());
    if (!ok)
    {
        ++failures;
    }
}

/**
 * 宿主自己的冒烟检查。
 *
 * **这一组不能做成插件**（`docs/API.md` 的装配时序、`docs/NOTES.md` 的"明确不支持"都写了）：
 * 自检插件没被扫描到、或 dll 加载失败时，"没有任何断言失败"会被误读成"全部通过"，
 * 而那正是最该报错的情形。所以这条依赖关系必须留在宿主里：没有插件也要能说话。
 */
int smokeCheck(const golding::HostApp& app)
{
    int failures = 0;

    const auto& resolved = app.resolved();
    const std::size_t rejected = app.entries().size() - app.nodeCount();
    check("每个插件都有去处",
          resolved.order.size() + resolved.unresolved.size() + rejected == app.entries().size(),
          "可装配 " + std::to_string(resolved.order.size()) + " + 装不上 " + std::to_string(resolved.unresolved.size()) +
              " + 加载否掉 " + std::to_string(rejected) + " = " + std::to_string(app.entries().size()),
          failures);

    if (app.plugins().status == golding::PluginsStatus::NoPlugins)
    {
        // 合法状态（验收清单第一条）：删掉插件目录，程序照样启动、档案为空、什么都不画。
        std::printf("[host] 没有扫到插件：界面为空，这是合法状态\n");
    }
    return failures;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parseArgs(argc, argv, options))
    {
        return kExitFatal;
    }
    if (options.help)
    {
        printUsage();
        return kExitOk;
    }

    std::printf("[host] GoldingCore 宿主 · 设计画布 %dx%d · 界面层模式「%s」\n", kDesignWidth, kDesignHeight,
                options.bootMode.empty() ? "(无)" : options.bootMode.c_str());

    // ---- 阶段 1：扫描 + 加载 + 建档 + 依赖解析 + entry ----
    golding::HostApp app;
    if (!app.discover())
    {
        std::printf("[host] 插件目录扫描失败\n");
        return kExitFatal;
    }
    if (!app.prepare())
    {
        std::printf("[host] prepare 失败（只能跑一次，见 docs/NOTES.md）\n");
        return kExitFatal;
    }
    app.registerAll();

    // 启动模式必须在**装配之前**就交出去：面板插件的 `create` 里要按它决定初始形态
    // （例如 `--collapsed` 一进来就收着侧栏）。放到装配之后再设，插件读到的是空串——
    // 这个顺序错误只在"插件真的读 bootMode"时才暴露。
    app.setBootMode(options.bootMode);

    int failures = smokeCheck(app);

    // 解析结果的可观测面：谁装不上、为什么。
    // `resolved.order` 存的是**节点下标**，不是档案下标——取名字一律走 providerOfNode。
    {
        const auto& resolved = app.resolved();
        std::string order;
        for (const std::size_t index : resolved.order)
        {
            order += app.providerOfNode(index) + " ";
        }
        const std::size_t rejected = app.entries().size() - app.nodeCount();
        std::printf("[host] 依赖解析：可装配 %zu / 装不上 %zu / 加载阶段否掉 %zu（共 %zu），顺序=%s\n",
                    resolved.order.size(), resolved.unresolved.size(), rejected, app.entries().size(), order.c_str());
    }

    const golding::HostApp::LoadedEntry* panel = app.panel();
    dumpRegistries();

    // ---- 阶段 2：建窗口 + 装配骨架 ----
    //
    // 顺序不能换：设计尺寸 → 窗口配置 → 场景 → 装配。
    // `SetDesignSize` 必须在**任何场景 / 窗口构造之前**——设计尺寸在那一刻烘进根锚点，
    // 晚了会拒绝改动（`config/window_config.h`）。
    if (!inking::SetDesignSize(kDesignWidth, kDesignHeight))
    {
        std::printf("[host] SetDesignSize 被拒（已有几何按旧尺寸定型）\n");
        return kExitFatal;
    }

    inking::WindowConfig config;
    config.borderless = !options.bordered;
    config.fullscreen = options.fullscreen;
    // 窗口清屏色取**库里的调色板常量**，不在这里写死数值：它必须与面板底色是同一个值，
    // 否则窗口底和面板底会差一档（letterbox 边、首帧、面板没铺到的地方最先露馅）。
    // 当前面板（GoldingGamestoryShell）是白底，所以这里取 `kSurface`。
    config.background = inking::prefab::theme::kSurface;
    if (inking::ApplyWindowConfig(config) != 0)
    {
        // 不致命：窗口还开得出来，只是某一项没生效。ERROR 已在上面打过。
        std::printf("[host] 警告：有窗口设置没生效（见上面的 ERROR）\n");
    }

    inking::InkingWindow& window = inking::InkingWindow::Instance();

    inking::InkingScene scene("main");
    if (!scene.IsRegistered())
    {
        std::printf("[host] 场景 \"main\" 登记被驳回（名字被占）\n");
        return kExitFatal;
    }
    // 场景名只用于登记；`GetName()` 是另一份（组件名），不设的话树里这一行是空的。
    // 自检与截图脚本按名字找根节点时要靠它（0.4.0 的 `SetName` 对所有 widget 可用）。
    scene.SetName("main");

    /** 装配出来的界面根（面板根工厂的返回值）；没有界面时是 nullptr。 */
    inking::InkingWidget* rootWidget = nullptr;

    if (panel != nullptr)
    {
        const GoldingWidgetClass* factory = golding::PanelRootRegistry::root();
        check("面板根工厂已登记", factory != nullptr,
              "来自 " + std::string(panel->provider), failures);

        if (factory != nullptr)
        {
            // 面板自己的资源根：`assets/` 在它自己的目录里，不在 exe 旁边。
            inking::AssetConfigure(
                inking::AssetConfig{golding::PathFinder::toUtf8(panel->scanned.directory / "assets"), ""});

            // hostContext 用**面板自己的那一份**（`&entry.provider`）：
            // 插件在 create 里拿它调 HostApi 时，身份必须是它自己，不能是宿主临时造的副本。
            rootWidget = static_cast<inking::InkingWidget*>(factory->create(panel->api.hostContext, &scene, ""));
            check("装配面板根", rootWidget != nullptr, panel->provider, failures);
        }
    }
    else
    {
        // 合法状态：删掉面板插件后程序照样启动、档案完整、什么都画不出来。
        std::printf("[host] 没有可用的面板插件：界面为空，插件档案照常\n");
    }

    scene.Relayout();

    std::printf("\n[host] 组件树：\n");
    dumpTree(&scene, 1);

    // ---- 阶段 3：装配后自检 ----
    //
    // 几何断言与"某种模式怎么跑"都在**插件自己**的 `ready` 里：面板断言自己的三栏，
    // 页面插件断言自己的页面，自检插件断言通用契约（根铺满 / 每页装得上 / 条目建得出）。
    // 宿主这边只剩上面那几条冒烟检查。
    app.setUiRoot(rootWidget);
    failures += app.notifyReady();

    const int exitCode = failures == 0 ? kExitOk : kExitFailed;
    std::printf("\n== 失败项：%d ==\n", failures);
    std::fflush(stdout);

    // ---- 阶段 4：跑帧 ----
    //
    // 两条路的分界是"给没给 `--frames`"，不是某个常驻开关：
    //   不给 → 常驻，跑到关窗（双击 exe 的默认行为）
    //   给了 → 跑完 N 帧就退，退出码即信号（CI / 截图）
    //
    // 两条路都走 `Open()` + `stepFrame`，所以"CI 跑过的"与"真机跑的"顺序完全一致。
    if (!window.Open("main"))
    {
        std::printf("[host] 窗口打不开\n");
        return kExitFatal;
    }

    if (options.frames < 0)
    {
        if (!options.capture.empty())
        {
            // 交互模式也要能截图：等两帧让画面稳定，帧末排队一次。
            // 不能在 `Show()` 之前同步 `Capture()`——那时还没画过一帧。
            window.RunAfterFrames(2, [&window, path = options.capture] {
                if (window.RequestCapture(path))
                {
                    std::printf("[host] 截图 → %s\n", path.c_str());
                }
            });
        }

        // 常驻：`Show()` 的内部就是 `Open() + while (PumpOnce())`（0.4.0 的注释原话），
        // 所以这里手动跑同一个循环、**不再回调 `Show()`**——窗口已经打开，
        // 再调一次会按"切场景"把打开流程重走一遍（实测在真实驱动下直接段错误）。
        while (window.PumpOnce())
        {
        }

        // 用户关窗给的是 0，但"已经报过失败"不该被后来的关窗动作抹掉。
        const int shown = window.GetExitCode();
        return failures == 0 ? shown : kExitFailed;
    }

    window.RunFrames(options.frames);

    if (!options.capture.empty())
    {
        if (window.RequestCapture(options.capture))
        {
            window.RunFrames(1); // 截图排的是"下一帧末"，得再跑一帧才落盘
            std::printf("[host] 截图 → %s\n", options.capture.c_str());
        }
        else
        {
            std::printf("[host] 截图请求被拒：%s\n", options.capture.c_str());
        }
    }

    window.RequestClose(exitCode);
    window.RunFrames(1); // 让关窗请求被消费一次，收尾干净
    return exitCode;
}
