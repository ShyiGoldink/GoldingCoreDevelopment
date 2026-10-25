#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "Application/CapabilityRegistry.h"
#include "Application/ManifestLoader.h"
#include "Application/PageRegistry.h"
#include "Application/PanelRootRegistry.h"
#include "Application/PluginLibrary.h"
#include "Application/PluginLoader.h"
#include "Application/RegionRegistry.h"
#include "Application/Resolver.h"
#include "Application/ServiceRegistry.h"
#include "Application/SettingsRegistry.h"
#include "HostApp.h"
#include "sdk/golding_plugin.h"

namespace golding
{

namespace
{

/**
 * ABI v8 的两条只读通道：装配后的界面根 + 启动模式。
 *
 * 落**文件静态量**（不是 HostApp 成员）：插件的回调只拿得到 `hostContext`
 * （那个 `&entry.provider`），拿不到 HostApp 实例。一次进程一个 HostApp 是
 * 已定的约定（`HostApp.h` 头注释），所以这一份就够。
 */
void*& uiRootSlot()
{
    static void* root = nullptr;
    return root;
}

std::string& bootModeSlot()
{
    static std::string mode;
    return mode;
}

void* hostUiRoot(void* /*context*/)
{
    return uiRootSlot();
}

const char* hostBootMode(void* /*context*/)
{
    return bootModeSlot().c_str();
}

/**
 * @brief provider 名字 → 它的 `hostContext`。
 *
 * `mountPage` / `mountSettingsSection` 是**面板插件**在调，但 `create` 的
 * `hostContext` 应当属于**页面 / 区块的提供方**：页面插件在 `create` 里若用
 * `hostContext` 调 HostApi（比如查自己的目录），拿别人的身份就是错的。
 *
 * 落文件静态量的理由同 `uiRootSlot`：插件回调只拿得到 `hostContext`，拿不到
 * `HostApp`。一次进程一个 `HostApp` 是已定约定，所以一份就够。
 */
std::vector<std::pair<std::string, void*>>& providerContexts()
{
    static std::vector<std::pair<std::string, void*>> rows;
    return rows;
}

void* contextOfProvider(const std::string& provider)
{
    if (provider.empty())
    {
        return nullptr;
    }
    for (const auto& row : providerContexts())
    {
        if (row.first == provider)
        {
            return row.second;
        }
    }
    return nullptr;
}

/**
 * ABI v10：查某个插件的目录。
 *
 * **按名字查，不按 hostContext 查**——`hostContext` 现在是 `&entry.provider`
 * （一个字符串指针），拿它反查目录会需要一张"指针 → 目录"的旁表；名字本来就是
 * 档案表的唯一键，直接查它更短也更稳（`docs/Options.md` 那条 `hostContext` 欠账
 * 因此不必现在动）。
 *
 * 未知名字返回 nullptr，不返回空串：调用方要能区分"没有这个插件"与"有但没登记目录"。
 */
const char* hostPluginDir(void* /*context*/, const char* pluginName)
{
    const int index = PluginLibrary::indexOf(pluginName);
    return index < 0 ? nullptr : PluginLibrary::directoryOf(index);
}

void hostLog(void* /*context*/, int level, const char* message)
{
    const char* tag = "INFO";
    if (level == GOLDING_LOG_ERROR)
    {
        tag = "ERROR";
    }
    else if (level == GOLDING_LOG_WARN)
    {
        tag = "WARN";
    }
    std::printf("[plugin/%s] %s\n", tag, message != nullptr ? message : "");
}

/**
 * 注册界面根工厂。
 *
 * **只有面板插件能注册**。槽位只有一个：普通插件抢先注册会让真正的面板注册
 * 不进来，表现是"界面空、日志全绿"——所以闸门放在注册口，而不是等装配时才发现。
 * 表本身不认识 role，这是编排层的判断。
 *
 * `AlreadySet` 映射到 `GOLDING_REGISTER_NAME_TAKEN`：C 侧返回码不加新值
 * （那会动 SDK 头），代价是插件侧日志说"名字已被占用"。原因在宿主日志里点名。
 */
int hostRegisterWidgetClass(void* context, const GoldingWidgetClass* widgetClass)
{
    const std::string provider = context != nullptr ? *static_cast<std::string*>(context) : std::string("?");

    const int libraryIndex = PluginLibrary::indexOf(provider.c_str());
    const GoldingPluginInfo* info = libraryIndex >= 0 ? PluginLibrary::at(libraryIndex) : nullptr;
    if (info == nullptr || info->role == nullptr || std::strcmp(info->role, "panel") != 0)
    {
        std::printf("[host] %s 不是面板插件（role=%s），不能提供界面根 → 驳回\n", provider.c_str(),
                    info != nullptr && info->role != nullptr ? info->role : "(空)");
        return GOLDING_REGISTER_INVALID;
    }

    switch (PanelRootRegistry::setRoot(widgetClass, provider))
    {
    case PanelRootRegistry::Result::Ok:
        std::printf("[host] 面板根工厂 %s（来自 %s）\n", widgetClass->name, provider.c_str());
        return GOLDING_REGISTER_OK;
    case PanelRootRegistry::Result::AlreadySet:
        std::printf("[host] 面板根工厂已存在，驳回 %s（来自 %s）\n",
                    widgetClass != nullptr && widgetClass->name != nullptr ? widgetClass->name : "(无名)",
                    provider.c_str());
        return GOLDING_REGISTER_NAME_TAKEN;
    case PanelRootRegistry::Result::Invalid:
    default:
        std::printf("[host] 面板根工厂描述不合法，驳回（来自 %s）\n", provider.c_str());
        return GOLDING_REGISTER_INVALID;
    }
}

int hostRegisterPage(void* context, const GoldingPageDecl* page)
{
    const std::string provider = context != nullptr ? *static_cast<std::string*>(context) : std::string("?");
    switch (PageRegistry::registerPage(page, provider))
    {
    case PageRegistry::RegisterResult::Ok:
        std::printf("[host] 注册页面 %s「%s」（来自 %s）\n", page->id,
                    page->title != nullptr ? page->title : "", provider.c_str());
        return GOLDING_REGISTER_OK;
    case PageRegistry::RegisterResult::IdTaken:
        std::printf("[host] 页面 id 冲突，驳回：%s\n", page->id);
        return GOLDING_REGISTER_NAME_TAKEN;
    case PageRegistry::RegisterResult::Invalid:
    default:
        std::printf("[host] 页面描述不合法，驳回\n");
        return GOLDING_REGISTER_INVALID;
    }
}

int hostPageCount(void* /*context*/)
{
    return static_cast<int>(PageRegistry::count());
}

const GoldingPageDecl* hostPageAt(void* /*context*/, int index)
{
    const auto pages = PageRegistry::allPages();
    if (index < 0 || static_cast<std::size_t>(index) >= pages.size())
    {
        return nullptr;
    }
    return pages[static_cast<std::size_t>(index)];
}

GoldingWidgetHandle hostMountPage(void* context, const char* pageId, void* parent)
{
    if (pageId == nullptr || parent == nullptr)
    {
        return nullptr;
    }
    const GoldingPageDecl* page = PageRegistry::findPage(pageId);
    if (page == nullptr)
    {
        return nullptr;
    }
    // `create` 的 hostContext 属于**页面提供方**，不是调用方（面板）。查不到才退回调用方。
    void* owner = contextOfProvider(PageRegistry::providerOf(pageId));
    return page->create(owner != nullptr ? owner : context, parent);
}

int hostRegisterSettingsSection(void* context, const GoldingSettingsDecl* section)
{
    const std::string provider = context != nullptr ? *static_cast<std::string*>(context) : std::string("?");
    switch (SettingsRegistry::registerSection(section, provider))
    {
    case SettingsRegistry::RegisterResult::Ok:
        std::printf("[host] 注册设置区块 %s「%s」（来自 %s）\n", section->id,
                    section->title != nullptr ? section->title : "", provider.c_str());
        return GOLDING_REGISTER_OK;
    case SettingsRegistry::RegisterResult::IdTaken:
        std::printf("[host] 设置区块 id 冲突，驳回：%s\n", section->id);
        return GOLDING_REGISTER_NAME_TAKEN;
    case SettingsRegistry::RegisterResult::Invalid:
    default:
        std::printf("[host] 设置区块描述不合法，驳回\n");
        return GOLDING_REGISTER_INVALID;
    }
}

int hostSettingsSectionCount(void* /*context*/)
{
    return static_cast<int>(SettingsRegistry::count());
}

const GoldingSettingsDecl* hostSettingsSectionAt(void* /*context*/, int index)
{
    const auto sections = SettingsRegistry::allSections();
    if (index < 0 || static_cast<std::size_t>(index) >= sections.size())
    {
        return nullptr;
    }
    return sections[static_cast<std::size_t>(index)];
}

GoldingWidgetHandle hostMountSettingsSection(void* context, const char* sectionId, void* parent)
{
    if (sectionId == nullptr || parent == nullptr)
    {
        return nullptr;
    }
    const GoldingSettingsDecl* section = SettingsRegistry::findSection(sectionId);
    if (section == nullptr)
    {
        return nullptr;
    }
    // 同上：区块提供方的 context，不是调用方的。
    void* owner = contextOfProvider(SettingsRegistry::providerOf(sectionId));
    return section->create(owner != nullptr ? owner : context, parent);
}

/**
 * 注册服务 = "往能力目录里报一个真能力"。
 *
 * 存储后端仍是 `ServiceRegistry`（不推翻那张表），这里额外记下**谁**提供的，
 * 好让 T-08 的交叉校验和日志都能点名。
 */
int hostRegisterService(void* context, const char* name, const void* vtable)
{
    const std::string provider = context != nullptr ? *static_cast<std::string*>(context) : std::string("?");
    switch (CapabilityRegistry::registerCapability(name, vtable, provider.c_str()))
    {
    case CapabilityRegistry::RegisterResult::Ok:
        std::printf("[host] 注册服务 %s（来自 %s）\n", name, provider.c_str());
        return GOLDING_REGISTER_OK;
    case CapabilityRegistry::RegisterResult::NameTaken:
        std::printf("[host] 服务名冲突，驳回：%s\n", name);
        return GOLDING_REGISTER_NAME_TAKEN;
    case CapabilityRegistry::RegisterResult::Invalid:
    default:
        std::printf("[host] 服务描述不合法，驳回（名字为空或缺 vtable）\n");
        return GOLDING_REGISTER_INVALID;
    }
}

const void* hostQueryService(void* /*context*/, const char* name)
{
    return CapabilityRegistry::query(name);
}

int hostPluginCount(void* /*context*/)
{
    return PluginLibrary::count();
}

const GoldingPluginInfo* hostPluginAt(void* /*context*/, int index)
{
    return PluginLibrary::at(index);
}

// ---- ABI v12：依赖与能力查询 ----
//
// 数据全在 `PluginLibrary` 里（`dependsOf` / `needsOf`），这里只是把它暴露给插件。
// `c_str()` 全程有效：库只增不减，字符串存在 deque 里（见 `PluginLibrary` 的注释）。

int hostPluginDependencyCount(void* /*context*/, int index)
{
    if (index < 0 || index >= PluginLibrary::count())
    {
        return 0;
    }
    return static_cast<int>(PluginLibrary::dependsOf(index).size());
}

const char* hostPluginDependencyAt(void* /*context*/, int index, int depIndex)
{
    if (index < 0 || index >= PluginLibrary::count() || depIndex < 0)
    {
        return nullptr;
    }
    const std::vector<std::string>& depends = PluginLibrary::dependsOf(index);
    if (static_cast<std::size_t>(depIndex) >= depends.size())
    {
        return nullptr;
    }
    return depends[static_cast<std::size_t>(depIndex)].c_str();
}

int hostPluginNeedCount(void* /*context*/, int index)
{
    if (index < 0 || index >= PluginLibrary::count())
    {
        return 0;
    }
    return static_cast<int>(PluginLibrary::needsOf(index).size());
}

const char* hostPluginNeedAt(void* /*context*/, int index, int needIndex, unsigned int* outVersion)
{
    if (index < 0 || index >= PluginLibrary::count() || needIndex < 0)
    {
        return nullptr;
    }
    const std::vector<ManifestCapability>& needs = PluginLibrary::needsOf(index);
    if (static_cast<std::size_t>(needIndex) >= needs.size())
    {
        return nullptr;
    }
    const ManifestCapability& need = needs[static_cast<std::size_t>(needIndex)];
    if (outVersion != nullptr)
    {
        *outVersion = need.version;
    }
    return need.name.c_str();
}

int hostRegisterRegion(void* context, const GoldingRegionDecl* region)
{
    const std::string provider = context != nullptr ? *static_cast<std::string*>(context) : std::string("?");
    switch (RegionRegistry::registerRegion(region, provider))
    {
    case RegionRegistry::RegisterResult::Ok:
        std::printf("[host] 声明区域 %s（来自 %s）\n", region->id, provider.c_str());
        return GOLDING_REGISTER_OK;
    case RegionRegistry::RegisterResult::NameTaken:
        std::printf("[host] 区域 id 冲突，驳回：%s\n", region->id);
        return GOLDING_REGISTER_NAME_TAKEN;
    case RegionRegistry::RegisterResult::Invalid:
    default:
        std::printf("[host] 区域描述不合法，驳回\n");
        return GOLDING_REGISTER_INVALID;
    }
}

int hostRegionCount(void* /*context*/)
{
    return static_cast<int>(RegionRegistry::regionCount());
}

const GoldingRegionDecl* hostRegionAt(void* /*context*/, int index)
{
    const auto regions = RegionRegistry::allRegions();
    if (index < 0 || static_cast<std::size_t>(index) >= regions.size())
    {
        return nullptr;
    }
    return regions[static_cast<std::size_t>(index)];
}

int hostRegisterRegionItem(void* context, const GoldingRegionItemDecl* item)
{
    const std::string provider = context != nullptr ? *static_cast<std::string*>(context) : std::string("?");
    switch (RegionRegistry::registerItem(item, provider))
    {
    case RegionRegistry::RegisterResult::Ok:
        std::printf("[host] 区域项 %s/%s（来自 %s）\n", item->regionId, item->title != nullptr ? item->title : "",
                    provider.c_str());
        return GOLDING_REGISTER_OK;
    case RegionRegistry::RegisterResult::NameTaken:
        std::printf("[host] 区域 %s 内 id 冲突，驳回：%s\n", item->regionId, item->id);
        return GOLDING_REGISTER_NAME_TAKEN;
    case RegionRegistry::RegisterResult::UnknownRegion:
        std::printf("[host] 区域 %s 不存在，驳回：%s（来自 %s）\n", item->regionId, item->id, provider.c_str());
        return GOLDING_REGISTER_INVALID;
    case RegionRegistry::RegisterResult::Invalid:
    default:
        std::printf("[host] 区域项描述不合法，驳回\n");
        return GOLDING_REGISTER_INVALID;
    }
}

int hostRegionItemCount(void* /*context*/, const char* regionId)
{
    if (regionId == nullptr)
    {
        return 0;
    }
    return static_cast<int>(RegionRegistry::itemCount(regionId));
}

const GoldingRegionItemDecl* hostRegionItemAt(void* /*context*/, const char* regionId, int index)
{
    if (regionId == nullptr || index < 0)
    {
        return nullptr;
    }
    return RegionRegistry::itemAt(regionId, static_cast<std::size_t>(index));
}

} // namespace

void HostApp::fillHostApi(GoldingHostApi& api, void* hostContext)
{
    api.abiVersion = GOLDING_PLUGIN_ABI_VERSION;
    api.hostContext = hostContext;
    api.log = &hostLog;
    api.registerWidgetClass = &hostRegisterWidgetClass;
    api.registerPage = &hostRegisterPage;
    api.pageCount = &hostPageCount;
    api.pageAt = &hostPageAt;
    api.mountPage = &hostMountPage;
    api.registerSettingsSection = &hostRegisterSettingsSection;
    api.settingsSectionCount = &hostSettingsSectionCount;
    api.settingsSectionAt = &hostSettingsSectionAt;
    api.mountSettingsSection = &hostMountSettingsSection;
    api.pluginCount = &hostPluginCount;
    api.pluginAt = &hostPluginAt;
    api.registerService = &hostRegisterService;
    api.queryService = &hostQueryService;
    api.registerRegion = &hostRegisterRegion;
    api.regionCount = &hostRegionCount;
    api.regionAt = &hostRegionAt;
    api.registerRegionItem = &hostRegisterRegionItem;
    api.regionItemCount = &hostRegionItemCount;
    api.regionItemAt = &hostRegionItemAt;
    api.uiRoot = &hostUiRoot;
    api.bootMode = &hostBootMode;
    api.pluginDir = &hostPluginDir;
    api.pluginDependencyCount = &hostPluginDependencyCount;
    api.pluginDependencyAt = &hostPluginDependencyAt;
    api.pluginNeedCount = &hostPluginNeedCount;
    api.pluginNeedAt = &hostPluginNeedAt;
}

void HostApp::setUiRoot(void* root)
{
    uiRootSlot() = root;
}

void* HostApp::uiRoot() const
{
    return uiRootSlot();
}

void HostApp::setBootMode(std::string mode)
{
    bootModeSlot() = std::move(mode);
}

const std::string& HostApp::bootMode() const
{
    return bootModeSlot();
}

int HostApp::notifyReady()
{
    int failures = 0;
    for (const std::size_t orderIndex : _resolved.order)
    {
        const std::size_t loadedIndex = _nodeToLoaded[orderIndex];
        auto& entry = _loaded[loadedIndex];
        if (entry.blocked || !entry.entryOk || entry.loaded.descriptor == nullptr ||
            entry.loaded.descriptor->ready == nullptr)
        {
            continue;
        }
        std::printf("\n[host] 装配后自检：%s\n", entry.provider.c_str());

        // 与 entry 同一套兜底。差别是**不把插件标失败**：它已经注册成功、界面已经建好，
        // 这里抛异常只说明"自检自己坏了"，撤销它的注册反而会造成半装配状态。
        // 因此只记 1 项失败（具体几项无从得知，不猜）。
        int failed = 0;
        try
        {
            failed = entry.loaded.descriptor->ready(&entry.api, entry.provider.c_str());
        }
        catch (const std::exception& error)
        {
            std::printf("[host] %s 的 ready 抛异常：%s\n", entry.provider.c_str(), error.what());
            failed = 1;
        }
        catch (...)
        {
            std::printf("[host] %s 的 ready 抛了非 std::exception 的 C++ 异常\n", entry.provider.c_str());
            failed = 1;
        }

        if (failed != 0)
        {
            std::printf("[host] %s 的装配后自检报了 %d 项失败\n", entry.provider.c_str(), failed);
        }
        failures += failed;
    }
    return failures;
}

const std::string& HostApp::providerOfNode(std::size_t nodeIndex) const
{
    static const std::string unknown = "?";
    return nodeIndex < _nodes.size() ? _nodes[nodeIndex].name : unknown;
}

/**
 * @brief 故意不做任何卸载。
 *
 * 装载成功的 dll 句柄归进程所有，退出时由 OS 一并回收。在这里 `FreeLibrary`
 * 反而更危险：它会执行插件的静态析构，而五张表仍持有指向 dll 数据段的
 * `const TDecl*`（`SlotTable::Row::decl`），谁先谁后没有一处代码能保证。
 *
 * 所以本类没有"卸载"这条路——重载等于重启进程（`PluginLoader.h` 的类注释）。
 */
HostApp::~HostApp() = default;

bool HostApp::discover()
{
    PathFinder finder;
    _plugins = finder.plugins();
    std::printf("[host] plugin 目录：%s（来源 %zu 个）\n", finder.pluginDir().string().c_str(),
                _plugins.sources.size());
    return _plugins.status != PluginsStatus::Error;
}

bool HostApp::loadEntry(LoadedEntry& entry)
{
    if (entry.scanned.status != ManifestStatus::Pass || entry.scanned.manifest.dllPath.empty())
    {
        return false;
    }
    entry.loaded = PluginLoader::open(PathFinder::fromUtf8(entry.scanned.manifest.dllPath),
                                      GOLDING_PLUGIN_ABI_VERSION);
    if (entry.loaded.status != LoadStatus::Pass)
    {
        // 打的是 **PluginState** 的编号（和档案表同一套词），不是 LoadStatus 的下标：
        // 两套枚举顺序不一样，混着看会让人拿着 status=3 去查别的。
        PluginState state = PluginState::LibraryError;
        if (entry.loaded.status == LoadStatus::AbiMismatch)
        {
            state = PluginState::AbiMismatch;
        }
        else if (entry.loaded.status == LoadStatus::SdkMismatch)
        {
            state = PluginState::SdkMismatch;
        }
        std::printf("[host] 加载失败（status=%d）：%s\n", toInt(state), entry.scanned.manifest.name.c_str());
        return false;
    }
    // ABI v11：dll 不再自称名字，`provider` 保持 `prepare` 里设好的 `plugin.json` 名字。
    return true;
}

void HostApp::validateNeeds()
{
    for (std::size_t nodeIndex = 0; nodeIndex < _nodes.size(); ++nodeIndex)
    {
        const std::size_t loadedIndex = _nodeToLoaded[nodeIndex];
        const int libraryIndex = _libraryIndex[loadedIndex];
        if (libraryIndex < 0)
        {
            continue;
        }
        LoadedEntry& entry = _loaded[loadedIndex];
        for (const ManifestCapability& need : PluginLibrary::needsOf(libraryIndex))
        {
            if (!CapabilityRegistry::isDeclared(need.name.c_str()))
            {
                if (CapabilityRegistry::strict())
                {
                    // 只降级它自己：硬失败会让一个能力缺席带崩整条链，而用户要的是
                    // "一个插件坏了只影响它自己"。
                    entry.blocked = true;
                    PluginLibrary::setState(libraryIndex, PluginState::Unresolved);
                    std::printf("[host] %s 缺能力 %s（没有任何插件声明提供它）→ 本插件不装配\n",
                                entry.provider.c_str(), need.name.c_str());
                }
                else
                {
                    std::printf("[host] %s 缺能力 %s（放任模式：只留痕，照常装配）\n", entry.provider.c_str(),
                                need.name.c_str());
                }
                continue;
            }

            // 有人声称提供，再比版本。提供方的版本来自 `plugin.json` 的 `provides`，
            // 0 表示"没写版本"——不能当成"第 0 版"放行，也不能悄悄略过：
            // 需求方明确要了版本，就该说清拿不到。
            const unsigned int offered = CapabilityRegistry::versionOf(need.name.c_str());
            if (need.version == 0 || offered >= need.version)
            {
                continue;
            }

            const char* declarer = CapabilityRegistry::declarerOf(need.name.c_str());
            const char* who = declarer != nullptr ? declarer : "?";
            if (CapabilityRegistry::strict())
            {
                entry.blocked = true;
                PluginLibrary::setState(libraryIndex, PluginState::Unresolved);
                std::printf("[host] %s 需要能力 %s ≥ v%u，但 %s 只提供 v%u（0 = 未声明版本）→ 本插件不装配\n",
                            entry.provider.c_str(), need.name.c_str(), need.version, who, offered);
            }
            else
            {
                std::printf("[host] %s 需要能力 %s ≥ v%u，但 %s 只提供 v%u（0 = 未声明版本；放任模式：只留痕）\n",
                            entry.provider.c_str(), need.name.c_str(), need.version, who, offered);
            }
        }
    }
}

void HostApp::validateProvides()
{
    // ---- 正向：声明了却没人注册 = 这个插件坏了 ----
    for (const std::size_t orderIndex : _resolved.order)
    {
        const std::size_t loadedIndex = _nodeToLoaded[orderIndex];
        LoadedEntry& entry = _loaded[loadedIndex];
        const int libraryIndex = _libraryIndex[loadedIndex];
        if (entry.blocked || !entry.entryOk || libraryIndex < 0)
        {
            continue;
        }
        for (const ManifestCapability& offer : PluginLibrary::providesOf(libraryIndex))
        {
            const char* provider = CapabilityRegistry::providerOf(offer.name.c_str());
            if (provider != nullptr && std::strcmp(provider, entry.provider.c_str()) == 0)
            {
                continue;
            }
            entry.entryOk = false; // 让 selectPanel / notifyReady 也跳过它
            PluginLibrary::setState(libraryIndex, PluginState::EntryFailed);
            std::printf("[host] %s 声明了能力 %s 但未注册 → 本插件判失败\n", entry.provider.c_str(),
                        offer.name.c_str());
        }
    }

    // ---- 反向：注册了却没声明 = 允许（内部能力），只提示 ----
    for (const std::string& name : CapabilityRegistry::allNames())
    {
        if (CapabilityRegistry::isDeclared(name.c_str()))
        {
            continue;
        }
        const char* provider = CapabilityRegistry::providerOf(name.c_str());
        std::printf("[host] 提示：%s 注册了未声明的能力 %s（内部能力，允许）\n",
                    provider != nullptr ? provider : "?", name.c_str());
    }
}

bool HostApp::prepare()
{
    // 只允许一次。重复调用不会崩溃，但会**静默地**留下半个旧世界：
    // `_loaded` / `_nodes` / `_nodeToLoaded` 都是追加的，第二轮会把同一批插件
    // 再装一遍（dll 引用计数 +1、entry 再调一次、注册全被驳回判 EntryFailed），
    // 而 `_libraryIndex` 按新长度重置后只有后一半被填。
    //
    // 与其清空三个容器假装支持重入，不如直接拒绝：重载 = 重启进程，进程内
    // 没有"重新扫描"这条路（界面树还是旧的，清了也白清）。
    if (_prepared)
    {
        std::printf("[host] prepare 只能调用一次：重载 = 重启进程，不支持进程内重扫\n");
        return false;
    }
    _prepared = true;

    const auto scanned = PluginScanner::scan(_plugins.sources);

    // deque：hostContext 指向 &entry.provider，vector 扩容会把指针搬走。
    for (const auto& plugin : scanned)
    {
        if (plugin.shadowed)
        {
            std::printf("[host] 忽略 %-12s %s：同名的内置插件优先\n", plugin.manifest.name.c_str(),
                        plugin.directory.string().c_str());
            continue;
        }

        std::printf("[host] 扫描到 %-12s %s status=%d role=%s\n", plugin.manifest.name.c_str(),
                    plugin.tier == PluginTier::Bundled ? "内置" : "外部", static_cast<int>(plugin.status),
                    plugin.manifest.role.empty() ? "-" : plugin.manifest.role.c_str());

        // 读不出来的插件名可能为空（名字正是读不出来的那一段之一），所以原因行按目录名点人。
        if (plugin.status != ManifestStatus::Pass && !plugin.detail.empty())
        {
            std::printf("[host]   ↳ %s：%s\n", PathFinder::toUtf8(plugin.directory.filename()).c_str(),
                        plugin.detail.c_str());
        }

        LoadedEntry entry;
        entry.scanned = plugin;
        entry.provider = plugin.manifest.name;

        // apiVersion 不满足：根本不去 open dll。
        if (plugin.status == ManifestStatus::Pass &&
            plugin.manifest.apiVersion > static_cast<int>(GOLDING_CORE_API_VERSION))
        {
            std::printf("[host] 拒绝 %-12s：apiVersion=%d 高于宿主支持（=%d）\n", plugin.manifest.name.c_str(),
                        plugin.manifest.apiVersion, static_cast<int>(GOLDING_CORE_API_VERSION));
            _loaded.push_back(std::move(entry));
            continue;
        }

        loadEntry(entry);
        _loaded.push_back(std::move(entry));
    }

    // ---- 建档 + 依赖解析 ----
    PluginLibrary::reset();
    // 能力目录的两层一起清：声明面是「这次加载里谁声称提供什么」，运行时面是
    // 「谁真的注册了」，两者必须成对重置，否则重加载之后会留着上一轮的幽灵提供方。
    CapabilityRegistry::reset();
    _libraryIndex.assign(_loaded.size(), -1);

    for (std::size_t index = 0; index < _loaded.size(); ++index)
    {
        const auto& entry = _loaded[index];

        PluginState state = PluginState::Ready;
        if (entry.scanned.status != ManifestStatus::Pass)
        {
            state = PluginState::ManifestInvalid;
        }
        else if (entry.scanned.manifest.apiVersion > static_cast<int>(GOLDING_CORE_API_VERSION))
        {
            state = PluginState::ApiTooNew;
        }
        else if (entry.loaded.status == LoadStatus::AbiMismatch)
        {
            state = PluginState::AbiMismatch;
        }
        else if (entry.loaded.status == LoadStatus::SdkMismatch)
        {
            state = PluginState::SdkMismatch;
        }
        else if (entry.loaded.status != LoadStatus::Pass)
        {
            state = PluginState::LibraryError;
        }

        // 依赖来自 `plugin.json`（ABI v11）：扫描阶段就有，拓扑不必等 dll。
        // 这份数据的唯一权威就是它——descriptor 里那个 `depends` 已随 v11 删除。
        const std::vector<std::string>& depends = entry.scanned.manifest.dependencies;

        ManifestFacts facts;
        if (entry.loaded.descriptor != nullptr)
        {
            facts.sdkId = entry.loaded.descriptor->sdkId;
            facts.sdkVersion = entry.loaded.descriptor->sdkVersion;
        }
        // 能力声明同样只认 `plugin.json`：版本校验发生在拓扑整理时，那时 dll 还没 entry。
        facts.provides = &entry.scanned.manifest.provides;
        facts.needs = &entry.scanned.manifest.needs;

        const int descriptorAbi =
            entry.loaded.descriptor != nullptr ? static_cast<int>(entry.loaded.descriptor->abiVersion) : 0;

        PluginLibrary::add(entry.scanned.manifest.name.c_str(), entry.scanned.manifest.title.c_str(),
                           entry.scanned.manifest.version.c_str(), entry.scanned.manifest.role.c_str(),
                           descriptorAbi, state, depends, facts);
        _libraryIndex[index] = PluginLibrary::count() - 1;

        // 目录由扫描结果决定（内置/外部分层、同名 shadowed 都在这一步之前已经定完），
        // 所以这里记下的就是"权威目录"。UTF-8：路径里中文不能按 ANSI 传。
        PluginLibrary::setDirectory(_libraryIndex[index],
                                    PathFinder::toUtf8(entry.scanned.directory).c_str());

        if (state != PluginState::Ready)
        {
            continue;
        }

        // 声明面：名字 + 版本，没有 vtable——扫描完就能读，所以"要的能力有没有人
        // **声称**提供、提供到第几版"可以在 entry 之前就判出来。
        for (const ManifestCapability& offer : entry.scanned.manifest.provides)
        {
            CapabilityRegistry::declare(offer.name.c_str(), offer.version, entry.scanned.manifest.name.c_str());
        }

        PluginNode node;
        node.name = entry.scanned.manifest.name;
        node.depends = depends;
        _nodes.push_back(std::move(node));
        _nodeToLoaded.push_back(index);
    }

    _resolved = Resolver::resolve(_nodes);

    // needs 的静态校验**必须**在 entry 之前：放到 entry 之后就只能看到"已经注册成功的"，
    // 而那正是它要防的静默失败（插件拿着 nullptr 自己降级，谁也不报错）。
    validateNeeds();

    // 依赖解析的结论回灌：装不上的插件在档案里要如实反映（此前记着 Ready）。
    // 三种原因分开打——"缺依赖""在环上""被连累"的处置一样，但责任人不同，
    // 排查时第一条要看的正是这个。
    for (const auto& item : _resolved.unresolved)
    {
        const std::size_t loadedIndex = _nodeToLoaded[item.index];
        if (_libraryIndex[loadedIndex] >= 0)
        {
            PluginLibrary::setState(_libraryIndex[loadedIndex], PluginState::Unresolved);
        }

        const std::string& name = _nodes[item.index].name;
        switch (item.reason)
        {
        case ResolveReason::MissingDependency:
        {
            std::string missing;
            for (const std::string& dependency : item.missing)
            {
                if (!missing.empty())
                {
                    missing += "、";
                }
                missing += dependency;
            }
            std::printf("[host] %s 缺依赖：%s（没有任何插件用这个名字）→ 不装配\n", name.c_str(),
                        missing.c_str());
            break;
        }
        case ResolveReason::InCycle:
            std::printf("[host] %s 在依赖环上 → 不装配（先拆环，别查它的依赖）\n", name.c_str());
            break;
        case ResolveReason::DependencyUnavailable:
            std::printf("[host] %s 依赖的插件装不上（连带）→ 不装配（往上找根因）\n", name.c_str());
            break;
        }
    }

    return true;
}

void HostApp::registerAll()
{
    // provider 名字 → hostContext 的映射逐次重建（`registerAll` 可能被调多次）。
    providerContexts().clear();

    for (const std::size_t orderIndex : _resolved.order)
    {
        const std::size_t loadedIndex = _nodeToLoaded[orderIndex];
        auto& entry = _loaded[loadedIndex];
        if (entry.loaded.status != LoadStatus::Pass || entry.blocked)
        {
            continue;
        }
        fillHostApi(entry.api, &entry.provider);
        // 记下这个插件自己的 context，供 `mountPage` 反查提供方（见 contextOfProvider）。
        providerContexts().emplace_back(entry.provider, entry.api.hostContext);

        // 兜底：插件抛出的 C++ 异常不许带走宿主。
        //
        // **这只接得住标准 C++ 异常**。两件事它做不到，必须写在这里而不是藏在文档里：
        //   1. 结构化异常（SEH：空指针写、栈溢出、除零）在 MinGW 下不经过 catch(...)；
        //   2. 异常要穿过 dll 边界，两侧必须用同一个运行库。插件静态链 libstdc++ 时，
        //      抛出点直接 terminate，宿主的 catch 根本看不到。
        // 所以这是"尽力兜底"，不是"插件隔离"（`docs/Options.md`）。
        bool threw = false;
        std::string reason;
        try
        {
            entry.entryOk = PluginLoader::callEntry(entry.loaded.library, &entry.api, entry.provider.c_str());
        }
        catch (const std::exception& error)
        {
            threw = true;
            reason = error.what();
        }
        catch (...)
        {
            threw = true;
            reason = "非 std::exception 的 C++ 异常";
        }

        if (threw)
        {
            entry.entryOk = false;
            std::printf("[host] entry 抛异常：%s · %s\n", entry.provider.c_str(), reason.c_str());
        }

        if (!entry.entryOk)
        {
            std::printf("[host] entry 失败：%s\n", entry.provider.c_str());
            if (_libraryIndex[loadedIndex] >= 0)
            {
                PluginLibrary::setState(_libraryIndex[loadedIndex], PluginState::EntryFailed);
            }
        }
    }

    // 必须在 selectPanel **之前**：否则一个声明错的面板插件会被选上、然后在装配时
    // 失败，报错点跑到几百行之外去。
    validateProvides();

    selectPanel();
}

void HostApp::selectPanel()
{
    int panelCount = 0;
    for (const auto& entry : _loaded)
    {
        if (entry.scanned.manifest.role != "panel" || entry.loaded.status != LoadStatus::Pass || !entry.entryOk)
        {
            continue;
        }
        ++panelCount;
        if (_panel == nullptr)
        {
            _panel = &entry;
        }
    }
    if (panelCount > 1)
    {
        std::printf("[host] 警告：发现 %d 个面板插件，只用首个（%s）——面板插件应当唯一\n", panelCount,
                    _panel->provider.c_str());
    }
}

} // namespace golding
