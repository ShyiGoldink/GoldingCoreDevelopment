#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "Application/CapabilityRegistry.h"
#include "Application/ManifestLoader.h"
#include "Application/PageRegistry.h"
#include "Application/PluginLibrary.h"
#include "Application/PluginLoader.h"
#include "Application/PluginRegistry.h"
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
 * ABI v10：查某个插件的目录。
 *
 * **按名字查，不按 hostContext 查**——`hostContext` 现在是 `&entry.provider`
 * （一个字符串指针），拿它反查目录会需要一张"指针 → 目录"的旁表；名字本来就是
 * 档案表的唯一键，直接查它更短也更稳（`Options.md` §5 那条 `hostContext` 欠账
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

int hostRegisterWidgetClass(void* context, const GoldingWidgetClass* widgetClass)
{
    const std::string provider = context != nullptr ? *static_cast<std::string*>(context) : std::string("?");
    switch (PluginRegistry::registerClass(widgetClass, provider))
    {
    case PluginRegistry::RegisterResult::Ok:
        std::printf("[host] 注册组件 %s（来自 %s）\n", widgetClass->name, provider.c_str());
        return GOLDING_REGISTER_OK;
    case PluginRegistry::RegisterResult::NameTaken:
        std::printf("[host] 组件名冲突，驳回：%s\n", widgetClass->name);
        return GOLDING_REGISTER_NAME_TAKEN;
    case PluginRegistry::RegisterResult::Invalid:
    default:
        std::printf("[host] 组件描述不合法，驳回\n");
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
    return page->create(context, parent);
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
    return section->create(context, parent);
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
            failed = entry.loaded.descriptor->ready(&entry.api);
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
    if (entry.scanned.status != ManifestStatus::Pass || !entry.scanned.manifest.hasDll)
    {
        return false;
    }
    entry.loaded = PluginLoader::open(PathFinder::fromUtf8(entry.scanned.manifest.dllPath),
                                      GOLDING_PLUGIN_ABI_VERSION);
    if (entry.loaded.status != LoadStatus::Pass)
    {
        // 打的是 **PluginState** 的编号（和档案表同一套词），不是 LoadStatus 的下标：
        // 两套枚举顺序不一样，混着看会让人拿着 status=4 去查"没有 dll"。
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
    if (entry.loaded.descriptor != nullptr && entry.loaded.descriptor->name != nullptr)
    {
        entry.provider = entry.loaded.descriptor->name;
    }
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
        for (const CapabilityDecl& need : PluginLibrary::needsOf(libraryIndex))
        {
            if (CapabilityRegistry::isDeclared(need.name.c_str()))
            {
                continue;
            }
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
        for (const std::string& name : PluginLibrary::providesOf(libraryIndex))
        {
            const char* provider = CapabilityRegistry::providerOf(name.c_str());
            if (provider != nullptr && std::strcmp(provider, entry.provider.c_str()) == 0)
            {
                continue;
            }
            entry.entryOk = false; // 让 selectPanel / notifyReady 也跳过它
            PluginLibrary::setState(libraryIndex, PluginState::EntryFailed);
            std::printf("[host] %s 声明了能力 %s 但未注册 → 本插件判失败\n", entry.provider.c_str(), name.c_str());
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

        std::printf("[host] 扫描到 %-12s %s status=%d role=%s dll=%d\n", plugin.manifest.name.c_str(),
                    plugin.tier == PluginTier::Bundled ? "内置" : "外部", static_cast<int>(plugin.status),
                    plugin.manifest.role.empty() ? "-" : plugin.manifest.role.c_str(), plugin.manifest.hasDll ? 1 : 0);

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
        else if (!entry.scanned.manifest.hasDll)
        {
            state = PluginState::NoDll;
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

        const char* const* depends = nullptr;
        std::size_t dependCount = 0;
        if (entry.loaded.descriptor != nullptr && entry.loaded.descriptor->dependCount > 0)
        {
            depends = entry.loaded.descriptor->depends;
            dependCount = entry.loaded.descriptor->dependCount;
        }

        // ---- ABI v9：静态能力宣言 ----
        //
        // `manifest()` 指向的存储由插件持有、活到进程结束，宿主**原样读、不拷贝**；
        // 要留下的字符串都由 PluginLibrary 自己复制一份。
        const GoldingPluginManifest* declared =
            (entry.loaded.descriptor != nullptr && entry.loaded.descriptor->manifest != nullptr)
                ? entry.loaded.descriptor->manifest()
                : nullptr;

        ManifestFacts facts;
        if (declared != nullptr)
        {
            facts.kind = declared->kind;
            facts.provides = declared->provides;
            facts.provideCount = declared->provideCount;
            facts.needs = declared->needs;
            facts.needCount = declared->needCount;
        }
        if (entry.loaded.descriptor != nullptr)
        {
            facts.sdkId = entry.loaded.descriptor->sdkId;
            facts.sdkVersion = entry.loaded.descriptor->sdkVersion;
        }

        const char* descriptorName = entry.loaded.descriptor != nullptr ? entry.loaded.descriptor->name : nullptr;
        const char* descriptorVersion = entry.loaded.descriptor != nullptr ? entry.loaded.descriptor->version : nullptr;
        const int descriptorAbi =
            entry.loaded.descriptor != nullptr ? static_cast<int>(entry.loaded.descriptor->abiVersion) : 0;

        PluginLibrary::add(descriptorName != nullptr ? descriptorName : entry.scanned.manifest.name.c_str(),
                           entry.scanned.manifest.title.c_str(),
                           descriptorVersion != nullptr ? descriptorVersion : entry.scanned.manifest.version.c_str(),
                           entry.scanned.manifest.role.c_str(), descriptorAbi, state, depends, dependCount, facts);
        _libraryIndex[index] = PluginLibrary::count() - 1;

        // 目录由扫描结果决定（内置/外部分层、同名 shadowed 都在这一步之前已经定完），
        // 所以这里记下的就是"权威目录"。UTF-8：路径里中文不能按 ANSI 传。
        PluginLibrary::setDirectory(_libraryIndex[index],
                                    PathFinder::toUtf8(entry.scanned.directory).c_str());

        if (state != PluginState::Ready)
        {
            continue;
        }

        // 声明面：只有名字，没有 vtable——装配前能读到，所以"要的能力有没有人**声称**
        // 提供"可以在 entry 之前就判出来。
        for (std::size_t provideIndex = 0; provideIndex < facts.provideCount; ++provideIndex)
        {
            if (facts.provides != nullptr)
            {
                CapabilityRegistry::declare(facts.provides[provideIndex],
                                            descriptorName != nullptr ? descriptorName
                                                                      : entry.scanned.manifest.name.c_str());
            }
        }

        PluginNode node;
        node.name = descriptorName != nullptr ? descriptorName : entry.scanned.manifest.name;
        for (std::size_t dependIndex = 0; dependIndex < dependCount; ++dependIndex)
        {
            node.depends.emplace_back(depends[dependIndex]);
        }
        _nodes.push_back(std::move(node));
        _nodeToLoaded.push_back(index);
    }

    _resolved = Resolver::resolve(_nodes);

    // needs 的静态校验**必须**在 entry 之前：放到 entry 之后就只能看到"已经注册成功的"，
    // 而那正是它要防的静默失败（插件拿着 nullptr 自己降级，谁也不报错）。
    validateNeeds();

    // 依赖解析的结论回灌：装不上的插件在档案里要如实反映（此前记着 Ready）。
    for (const auto& item : _resolved.unresolved)
    {
        const std::size_t loadedIndex = _nodeToLoaded[item.index];
        if (_libraryIndex[loadedIndex] >= 0)
        {
            PluginLibrary::setState(_libraryIndex[loadedIndex], PluginState::Unresolved);
        }
    }

    return true;
}

void HostApp::registerAll()
{
    for (const std::size_t orderIndex : _resolved.order)
    {
        const std::size_t loadedIndex = _nodeToLoaded[orderIndex];
        auto& entry = _loaded[loadedIndex];
        if (entry.loaded.status != LoadStatus::Pass || entry.blocked)
        {
            continue;
        }
        fillHostApi(entry.api, &entry.provider);

        // 兜底：插件抛出的 C++ 异常不许带走宿主。
        //
        // **这只接得住标准 C++ 异常**。两件事它做不到，必须写在这里而不是藏在文档里：
        //   1. 结构化异常（SEH：空指针写、栈溢出、除零）在 MinGW 下不经过 catch(...)；
        //   2. 异常要穿过 dll 边界，两侧必须用同一个运行库。插件静态链 libstdc++ 时，
        //      抛出点直接 terminate，宿主的 catch 根本看不到。
        // 所以这是"尽力兜底"，不是"插件隔离"（`docs/Options.md` §11）。
        bool threw = false;
        std::string reason;
        try
        {
            entry.entryOk = PluginLoader::callEntry(entry.loaded.library, &entry.api);
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
