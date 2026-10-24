// core 的纯逻辑单测：路径解析 + manifest 读入。
//
// 为什么这些必须有单测：`Resolver` / `ManifestLoader` / `PathFinder` 是"只做注册"
// 之后 core 的全部价值所在，而它们此前只被宿主自检间接覆盖——那些自检在
// `GOLDING_WITH_INKING=ON` 才跑。这个目标只链 `golding_core`（零 UI 依赖），
// 两个分支都能编、都能跑。
//
// 验收口径与其它自检一致：末行 `== 失败项：N ==`，退出码 = 失败项数。

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "Application/CapabilityRegistry.h"
#include "Application/ManifestLoader.h"
#include "Application/PageRegistry.h"
#include "Application/PanelRootRegistry.h"
#include "Application/PluginLibrary.h"
#include "Application/RegionRegistry.h"
#include "Application/ServiceRegistry.h"
#include "Application/SettingsRegistry.h"
#include "Tool/PathFinder.h"

namespace
{

namespace fs = std::filesystem;

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& name, const std::string& detail = {})
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
    }
    std::printf("[%s] %s%s%s\n", ok ? "ok" : "FAIL", name.c_str(), detail.empty() ? "" : " · ", detail.c_str());
}

std::string utf8(const fs::path& path)
{
    return golding::PathFinder::toUtf8(path);
}

/**同一路径的两种写法比较：统一走 UTF-8 窄串，避免平台差异带进断言。*/
bool samePath(const fs::path& left, const fs::path& right)
{
    return !left.empty() && !right.empty() && utf8(left.lexically_normal()) == utf8(right.lexically_normal());
}

bool writeFile(const fs::path& path, const std::string& text)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    file << text;
    return file.good();
}

void checkResolve(const fs::path& base)
{
    // 正常：相对路径 → base 之下的绝对路径。
    const fs::path normal = golding::PathFinder::resolve(base, "bin/probe.dll");
    check(samePath(normal, base / "bin/probe.dll"), "resolve 普通相对路径", utf8(normal));
    check(normal.is_absolute(), "resolve 结果一定是绝对路径");

    // 不要求文件存在：存在性是调用方的事。
    check(!fs::exists(normal), "resolve 不检查存在性（文件确实不存在，但仍返回值）");

    // 嵌套与末尾多余分隔符都按词法归一。
    const fs::path nested = golding::PathFinder::resolve(base, "a/b/../c/./x.json");
    check(samePath(nested, base / "a/c/x.json"), "resolve 内部 .. 与 . 按词法归一", utf8(nested));

    // 绝对路径拒绝。
    check(golding::PathFinder::resolve(base, "D:/abs/x.dll").empty(), "resolve 拒绝绝对路径");
    check(golding::PathFinder::resolve(base, "/abs/x.dll").empty(), "resolve 拒绝根路径");

    // 逃出 base 拒绝。
    check(golding::PathFinder::resolve(base, "../x.dll").empty(), "resolve 拒绝 .. 逃逸");
    check(golding::PathFinder::resolve(base, "a/../../x.dll").empty(), "resolve 拒绝深层 .. 逃逸");

    // 前缀陷阱：兄弟目录 `plug2` 不在 `plug` 之下，不能靠字符串前缀判。
    check(golding::PathFinder::resolve(base, "../plug2/x.dll").empty(), "resolve 拒绝同名前缀的兄弟目录");

    // 空输入。
    check(golding::PathFinder::resolve(base, "").empty(), "resolve 拒绝空相对路径");
    check(golding::PathFinder::resolve(fs::path{}, "x.dll").empty(), "resolve 拒绝空 base");
}

void checkUtf8(const fs::path& base)
{
    const fs::path chinese = base / "配置" / "中文.json";
    check(golding::PathFinder::fromUtf8(utf8(chinese)) == chinese, "toUtf8/fromUtf8 往返（中文路径）", utf8(chinese));

    const fs::path ascii = base / "bin" / "probe.dll";
    check(golding::PathFinder::fromUtf8(utf8(ascii)) == ascii, "toUtf8/fromUtf8 往返（ASCII）", utf8(ascii));
}

void checkManifest(const fs::path& root)
{
    using golding::ManifestLoader;
    using golding::ManifestStatus;

    // ---- 正常：dll 的外链路径解析成绝对路径 ----
    const fs::path good = root / "good";
    const std::string body = R"({
  "name": "probe",
  "title": "探针",
  "version": "0.1.0",
  "apiVersion": 3,
  "requires": { "plugins": [] },
  "provides": { "probe.service": 2 },
  "needs": { "probe.base": 1 },
  "targets": { "win64": { "lib": "bin/probe.dll" } }
})";
    check(writeFile(good / "plugin.json", body), "写入 good/plugin.json");

    const auto result = ManifestLoader::read(good);
    check(result.status == ManifestStatus::Pass, "manifest 正常文件 → Pass", result.detail);
    check(samePath(golding::PathFinder::fromUtf8(result.manifest.dllPath), good / "bin/probe.dll"),
          "manifest dllPath 是绝对路径", result.manifest.dllPath);
    check(result.manifest.provides.size() == 1 && result.manifest.provides[0].name == "probe.service" &&
              result.manifest.provides[0].version == 2,
          "provides 解析出名字 + 提供版本");
    check(result.manifest.needs.size() == 1 && result.manifest.needs[0].name == "probe.base" &&
              result.manifest.needs[0].version == 1,
          "needs 解析出名字 + 最小版本");

    // ---- dll 路径逃逸 → 整份 manifest 判 Invalid（不能当成"没配"）----
    const fs::path escape = root / "escape";
    check(writeFile(escape / "plugin.json",
                    R"({ "name": "escape", "title": "t", "version": "0.1.0", "apiVersion": 3,
                         "requires": { "plugins": [] },
                         "targets": { "win64": { "lib": "../probe.dll" } } })"),
          "写入 escape/plugin.json");
    const auto escaped = ManifestLoader::read(escape);
    check(escaped.status == ManifestStatus::Invalid, "dll 路径 .. 逃逸 → Invalid", escaped.detail);

    // ---- 未知顶层键 → Invalid，且 detail 点出是哪个键（T-11 删掉的那个字段
    //      现在就是未知键，这样"写了不生效"会当场变成一句人话，而不是静默忽略）----
    const fs::path unknownKey = root / "unknown-key";
    check(writeFile(unknownKey / "plugin.json",
                    R"({ "name": "uk", "title": "t", "version": "0.1.0", "apiVersion": 3,
                         "requires": { "plugins": [] },
                         "widgets": { "button": "config/button.json" } })"),
          "写入 unknown-key/plugin.json");
    const auto unknown = ManifestLoader::read(unknownKey);
    check(unknown.status == ManifestStatus::Invalid, "未知键 widgets → Invalid", unknown.detail);
    check(unknown.detail.find("widgets") != std::string::npos, "驳回原因点名 widgets", unknown.detail);

    // ---- 绝对路径写在 json 里 → 同样 Invalid ----
    const fs::path absolute = root / "absolute";
    check(writeFile(absolute / "plugin.json",
                    R"({ "name": "abs", "title": "t", "version": "0.1.0", "apiVersion": 3,
                         "requires": { "plugins": [] },
                         "targets": { "win64": { "lib": "D:/elsewhere/probe.dll" } } })"),
          "写入 absolute/plugin.json");
    check(ManifestLoader::read(absolute).status == ManifestStatus::Invalid, "dll 写绝对路径 → Invalid");

    // ---- 缺 dll → Invalid ----
    // "无 dll 的纯数据插件"这个形态在 2026-10-24 取消（Options.md §9）：它进不了
    // 依赖图、不能声明能力，唯一效果是在档案表里多一行。所以缺 targets 是配置错误。
    const fs::path noDll = root / "no-dll";
    check(writeFile(noDll / "plugin.json",
                    R"({ "name": "nodll", "title": "t", "version": "0.1.0", "apiVersion": 3,
                         "requires": { "plugins": [] } })"),
          "写入 no-dll/plugin.json");
    const auto noDllResult = ManifestLoader::read(noDll);
    check(noDllResult.status == ManifestStatus::Invalid, "缺 targets.win64.lib → Invalid", noDllResult.detail);
    check(noDllResult.detail.find("targets") != std::string::npos, "驳回原因点名 targets", noDllResult.detail);

    // ---- 能力表写成数组 → Invalid（形态是对象：能力名 → 版本）----
    const fs::path badCapability = root / "bad-capability";
    check(writeFile(badCapability / "plugin.json",
                    R"({ "name": "bc", "title": "t", "version": "0.1.0", "apiVersion": 3,
                         "requires": { "plugins": [] },
                         "provides": ["golding.overlay"],
                         "targets": { "win64": { "lib": "bin/x.dll" } } })"),
          "写入 bad-capability/plugin.json");
    const auto badCapabilityResult = ManifestLoader::read(badCapability);
    check(badCapabilityResult.status == ManifestStatus::Invalid, "provides 写成数组 → Invalid",
          badCapabilityResult.detail);
    check(badCapabilityResult.detail.find("provides") != std::string::npos, "驳回原因点名 provides",
          badCapabilityResult.detail);

    // ---- 缺必需字段 → Invalid；文件不存在 → Error（两档不能混）----
    const fs::path missingField = root / "missing-field";
    check(writeFile(missingField / "plugin.json", R"({ "name": "mf", "version": "0.1.0", "requires": { "plugins": [] } })"),
          "写入 missing-field/plugin.json");
    check(ManifestLoader::read(missingField).status == ManifestStatus::Invalid, "缺 title → Invalid");
    const auto missingFile = ManifestLoader::read(root / "no-such-dir");
    check(missingFile.status == ManifestStatus::Error, "目录不存在 → Error", missingFile.detail);
    check(!missingFile.detail.empty(), "Error 也带一句原因", missingFile.detail);
}

// ---- 登记表的计数与最后一次驳回原因 ----
//
// 断言的是**决策**（驳回了几次、原因是哪个 id），不是日志文案。原先六张表只有
// `count()`，于是"应当驳回 1 次"在自检里根本无法表达——只能人肉看日志。
void checkRegistryStats()
{
    using namespace golding;

    GoldingWidgetHandle (*noopCreate)(void*, void*) = [](void*, void*) -> GoldingWidgetHandle { return nullptr; };
    GoldingWidgetHandle (*noopWidgetCreate)(void*, void*, const char*) =
        [](void*, void*, const char*) -> GoldingWidgetHandle { return nullptr; };
    static int dummyVtable = 0;

    // ---- 页面表：接受 / 同名驳回 / 空指针 ----
    PageRegistry::clear();
    check(PageRegistry::stats().accepted == 0 && PageRegistry::stats().rejected == 0, "页面表 clear 后计数归零");

    const GoldingPageDecl page{"probe.page", "探针页", 10, noopCreate};
    check(PageRegistry::registerPage(&page, "probe") == PageRegistry::RegisterResult::Ok, "登记探针页");
    check(PageRegistry::stats().accepted == 1 && PageRegistry::stats().rejected == 0, "页面表：接受 1 次");

    check(PageRegistry::registerPage(&page, "probe") == PageRegistry::RegisterResult::IdTaken, "同 id 再登记 → 驳回");
    check(PageRegistry::stats().rejected == 1, "页面表：驳回 1 次");
    check(PageRegistry::stats().lastReject.find("probe.page") != std::string::npos, "驳回原因点名 id",
          PageRegistry::stats().lastReject);

    check(PageRegistry::registerPage(nullptr, "probe") == PageRegistry::RegisterResult::Invalid, "空指针 → Invalid");
    check(PageRegistry::stats().rejected == 2, "页面表：驳回计数累加");
    check(PageRegistry::count() == 1, "被驳回的没进表");

    // ---- 设置表：同一套语义，独立的计数 ----
    SettingsRegistry::clear();
    const GoldingSettingsDecl section{"probe.section", "探针区块", 10, noopCreate};
    SettingsRegistry::registerSection(&section, "probe");
    SettingsRegistry::registerSection(&section, "probe");
    check(SettingsRegistry::stats().accepted == 1 && SettingsRegistry::stats().rejected == 1, "设置表：1 接受 / 1 驳回",
          SettingsRegistry::stats().lastReject);

    // ---- 区域表：区域与条目共用一份计数 ----
    RegionRegistry::clear();
    const GoldingRegionDecl region{"probe.region", "探针区", 10, 0};
    RegionRegistry::registerRegion(&region, "probe");
    check(RegionRegistry::stats().accepted == 1, "区域表：区域登记接受");

    // 条目指向一个不存在的区域——这是装配冲突里最常见的一种，必须点名区域。
    const GoldingRegionItemDecl orphan{"no.such.region", "item.a", "甲", nullptr, 10, nullptr, nullptr};
    check(RegionRegistry::registerItem(&orphan, "probe") == RegionRegistry::RegisterResult::UnknownRegion,
          "条目指向不存在的区域 → UnknownRegion");
    check(RegionRegistry::stats().lastReject.find("no.such.region") != std::string::npos, "驳回原因点名区域",
          RegionRegistry::stats().lastReject);

    const GoldingRegionItemDecl item{"probe.region", "item.a", "甲", nullptr, 10, nullptr, nullptr};
    RegionRegistry::registerItem(&item, "probe");
    RegionRegistry::registerItem(&item, "probe");
    check(RegionRegistry::stats().accepted == 2 && RegionRegistry::stats().rejected == 2,
          "区域表：区域 + 条目共用一个计数",
          "接受=" + std::to_string(RegionRegistry::stats().accepted) + " 驳回=" +
              std::to_string(RegionRegistry::stats().rejected));

    // ---- 服务表：同名驳回（能力目录的运行时面就落在这张表）----
    ServiceRegistry::clear();
    ServiceRegistry::registerService("probe.svc", &dummyVtable);
    check(ServiceRegistry::registerService("probe.svc", &dummyVtable) == ServiceRegistry::RegisterResult::NameTaken,
          "同名服务 → 驳回");
    check(ServiceRegistry::stats().accepted == 1 && ServiceRegistry::stats().rejected == 1, "服务表：1 接受 / 1 驳回");

    // ---- 能力声明面：版本可读；重复声明第一个说了算 ----
    CapabilityRegistry::reset();
    CapabilityRegistry::declare("probe.cap", 2, "probe");
    check(CapabilityRegistry::isDeclared("probe.cap") && CapabilityRegistry::versionOf("probe.cap") == 2,
          "能力声明：名字与版本都可读");
    CapabilityRegistry::declare("probe.cap", 9, "other");
    check(CapabilityRegistry::versionOf("probe.cap") == 2, "能力声明：重复声明不覆盖版本");
    check(std::string(CapabilityRegistry::declarerOf("probe.cap")) == "probe", "能力声明：提供方点名第一个");
    check(CapabilityRegistry::versionOf("probe.absent") == 0, "能力声明：未声明返回 0");

    // ---- 档案表：能力声明带版本（来源是 plugin.json，不是 descriptor）----
    PluginLibrary::reset();
    const std::vector<golding::ManifestCapability> offers{{"probe.cap", 3}};
    const std::vector<golding::ManifestCapability> requests{{"probe.base", 1}};
    golding::ManifestFacts facts;
    facts.provides = &offers;
    facts.needs = &requests;
    PluginLibrary::add("probe", "探针", "0.1.0", "", 0, golding::PluginState::Ready, {}, facts);
    check(PluginLibrary::providesOf(0).size() == 1 && PluginLibrary::providesOf(0)[0].version == 3,
          "档案表：provides 带提供版本");
    check(PluginLibrary::needsOf(0).size() == 1 && PluginLibrary::needsOf(0)[0].version == 1,
          "档案表：needs 带最小版本");
    PluginLibrary::reset();

    // ---- 面板根槽位：只有一个，第二次一律驳回 ----
    PanelRootRegistry::clear();
    const GoldingWidgetClass rootClass{"probe.root", noopWidgetCreate, nullptr};
    const GoldingWidgetClass otherClass{"probe.other", noopWidgetCreate, nullptr};
    check(PanelRootRegistry::setRoot(&rootClass, "probe") == PanelRootRegistry::Result::Ok &&
              PanelRootRegistry::root() == &rootClass && PanelRootRegistry::provider() == "probe",
          "面板根槽位：首次设置成功，取回同一指针");

    check(PanelRootRegistry::setRoot(&otherClass, "other") == PanelRootRegistry::Result::AlreadySet,
          "面板根槽位：第二次（不同名）也驳回");
    check(PanelRootRegistry::stats().accepted == 1 && PanelRootRegistry::stats().rejected == 1 &&
              PanelRootRegistry::stats().lastReject.find("probe") != std::string::npos,
          "面板根槽位：计数正确，驳回原因点名现任提供方", PanelRootRegistry::stats().lastReject);

    const GoldingWidgetClass noCreateClass{"probe.nocreate", nullptr, nullptr};
    check(PanelRootRegistry::setRoot(nullptr, "probe") == PanelRootRegistry::Result::Invalid &&
              PanelRootRegistry::setRoot(&noCreateClass, "probe") == PanelRootRegistry::Result::Invalid,
          "面板根槽位：空指针 / 缺 create → Invalid");

    PanelRootRegistry::clear();
    check(PanelRootRegistry::root() == nullptr && PanelRootRegistry::stats().accepted == 0,
          "面板根槽位：clear 同时清值与计数");

    // ---- 排序规则：页表同 order 按 id；区域条目同 order 先到先得 ----
    // 三张表合成一个 SlotTable 之后，这条差别是**唯一**按表不同的行为，必须钉住。
    PageRegistry::clear();
    const GoldingPageDecl pageC{"c.page", "丙", 30, noopCreate};
    const GoldingPageDecl pageB{"b.page", "乙", 10, noopCreate};
    const GoldingPageDecl pageA{"a.page", "甲", 10, noopCreate};
    PageRegistry::registerPage(&pageC, "probe");
    PageRegistry::registerPage(&pageB, "probe");
    PageRegistry::registerPage(&pageA, "probe");
    const auto sortedPages = PageRegistry::allPages();
    check(sortedPages.size() == 3 && std::string(sortedPages[0]->id) == "a.page" &&
              std::string(sortedPages[1]->id) == "b.page" && std::string(sortedPages[2]->id) == "c.page",
          "页表：order 升序，同 order 按 id 字典序（与注册先后无关）",
          sortedPages.size() == 3 ? std::string(sortedPages[0]->id) + " " + sortedPages[1]->id + " " +
                                        sortedPages[2]->id
                                  : "数量不对");

    RegionRegistry::clear();
    const GoldingRegionDecl sortRegion{"probe.sort", "排序区", 10, 0};
    RegionRegistry::registerRegion(&sortRegion, "probe");
    const GoldingRegionItemDecl itemB{"probe.sort", "item.b", "乙", nullptr, 10, nullptr, nullptr};
    const GoldingRegionItemDecl itemA{"probe.sort", "item.a", "甲", nullptr, 10, nullptr, nullptr};
    RegionRegistry::registerItem(&itemB, "probe");
    RegionRegistry::registerItem(&itemA, "probe");
    const GoldingRegionItemDecl* firstItem = RegionRegistry::itemAt("probe.sort", 0);
    const GoldingRegionItemDecl* secondItem = RegionRegistry::itemAt("probe.sort", 1);
    check(firstItem != nullptr && secondItem != nullptr && std::string(firstItem->id) == "item.b" &&
              std::string(secondItem->id) == "item.a",
          "区域条目：同 order 先到先得（不按 id 重排）",
          firstItem != nullptr ? std::string(firstItem->id) + " " + (secondItem != nullptr ? secondItem->id : "?")
                               : "空");

    // ---- 重名放行：区域声明 allowDuplicateIds=1 时不得驳回 ----
    const GoldingRegionDecl looseRegion{"probe.loose", "可重名区", 20, 1};
    RegionRegistry::registerRegion(&looseRegion, "probe");
    const GoldingRegionItemDecl dup{"probe.loose", "same.id", "一", nullptr, 10, nullptr, nullptr};
    check(RegionRegistry::registerItem(&dup, "probe") == RegionRegistry::RegisterResult::Ok &&
              RegionRegistry::registerItem(&dup, "probe") == RegionRegistry::RegisterResult::Ok,
          "allowDuplicateIds=1 时同名条目不驳回");
    check(std::string(RegionRegistry::itemAt("probe.loose", 1)->id) == "same.id", "重名条目两项都在");

    // ---- clear 必须把计数也清掉，否则下一轮加载的断言会继承上一轮 ----
    PageRegistry::clear();
    check(PageRegistry::stats().accepted == 0 && PageRegistry::stats().rejected == 0 &&
              PageRegistry::stats().lastReject.empty(),
          "clear 同时复位计数与原因");
}

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "golding_core_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec)
    {
        std::printf("[FAIL] 无法创建临时目录：%s\n", ec.message().c_str());
        return 1;
    }

    checkResolve(root / "plug");
    checkUtf8(root);
    checkManifest(root);
    checkRegistryStats();

    fs::remove_all(root, ec);

    std::printf("\n== 检查 %d 项 / 失败项：%d ==\n", g_checks, g_failures);
    return g_failures;
}
