#pragma once

// SDK 的 C++ 面（第一层）：插件作者继承的基类 + 宿主能力入口。
//
// 两条不变量（`docs/Options.md` §8、§3）：
//
//   1. `Plugin` **只有三个钩子 + 虚析构**，发布后只许末尾追加。基类由插件作者继承，
//      中间插一个虚函数会让所有旧插件的虚表错位。
//   2. 所有注册面长在 `Host` 上（宿主侧 C 结构体末尾追加）。加一种新注册不动任何
//      插件的虚表——这跟 `GoldingHostApi` 已经走过的 v5→v8 是同一套做法。
//
// **宿主永不调插件对象的虚函数。** 跨 dll 只有 `golding_plugin_descriptor` 与
// `golding_plugin_entry` 两个 C 符号；虚调用全部发生在插件 dll 内部
// （`detail::EntryThunk<T>` / `detail::ReadyThunk<T>` 里），所以 Debug/Release 混编
// 也不会崩。
//
// 本头零 UI 依赖：只有逻辑、没有界面的插件只 include 它，不碰 InkingUIKit。
//
// 用法：
//
//   class HomePlugin final : public golding::Plugin
//   {
//   public:
//       const golding::PluginManifest& Manifest() const override;
//       bool Register(golding::Host& host) override;
//       int Ready(const golding::Host& host) override;   // 可选
//   };
//
//   GOLDING_EXPORT_PLUGIN(HomePlugin)

#include <sdk/golding_plugin.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <type_traits>

// GOLDING_SDK_ID / GOLDING_SDK_VERSION 定义在 `golding_plugin.h`：它们是 descriptor
// 的 `sdkId` / `sdkVersion` 两个字段的唯一来源，跟结构体放一起。

namespace golding
{

class Host;

/**
 * @brief 一条能力需求：要哪个名字、至少哪个版本。
 *
 * 只有名字与最小版本，**不带 vtable**：静态声明在装配前就要能比对，而 vtable
 * 是运行时事实（见 `docs/TASK-CORE-UPING.md` T-02 的选型修正）。
 */
using CapabilityNeed = GoldingCapabilityNeed;

/**
 * @brief 插件对自己的静态描述。装配**之前**就能读到。
 *
 * 字段与 C 版 `GoldingPluginManifest` 一一对应，**直接用 C 结构当存储**：
 * descriptor 的 `manifest()` 返回的就是它，宿主原样读，不经过任何 C++ 类型。
 *
 * 为什么身份（name / version / rootWidgetClass）也在这里：descriptor 的那三个
 * 字段总得有个来源，而基类只许三个钩子、不能再开一个 `Identity()`。
 *
 * `kind` 是自由字符串，**core 不解释它**，只存、只展示。一旦 core 认识 `"panel"`
 * 以外的 kind，就会长出第二张特化表（`PageRegistry` / `SettingsRegistry` 并存
 * 那笔账的起因）。
 *
 * 它是聚合类型且字段无默认值，所以作者只写关心的几项即可（其余按 nullptr / 0）：
 * @code
 * static const golding::PluginManifest kManifest = {
 *     .name = "home", .version = "0.1.0", .kind = "page",
 *     .depends = kDepends, .dependCount = 1,
 * };
 * @endcode
 */
using PluginManifest = GoldingPluginManifest;

/**
 * @brief 宿主能力的 C++ 入口。**每调用一次都留一行日志。**
 *
 * 留痕不是装饰：插件在进程内、崩了不隔离（`docs/Options.md` §2 用户已明确接受），
 * 日志是唯一排查面。所以每次注册、每次能力查询都必须能回答
 * "谁、往哪张表、什么结果"。
 *
 * 可以按值拷贝存成成员（内部只有两个指针）——点击回调不在 `Register` 的调用栈里，
 * 够不到那个参数，旧办法是每个插件养一个文件级 `g_hostApi` 全局量。
 */
class Host
{
public:
    Host(const GoldingHostApi* api, const char* provider) : _api(api), _provider(provider) {}

    /**本插件的名字（日志与降级判断用）。*/
    const char* provider() const noexcept { return _provider != nullptr ? _provider : "?"; }

    /**宿主给的表在不在。不在时所有方法都是空操作（插件被单独测时就是这种情形）。*/
    bool Valid() const noexcept { return _api != nullptr; }

    /**底层 api。只在 SDK 还没包到的边角用它，正常路径走下面的方法。*/
    const GoldingHostApi* api() const noexcept { return _api; }

    /**
     * 宿主的上下文指针（`api->hostContext`）。
     *
     * 只在"要把宿主上下文原样转交给另一个 C 回调"时用——比如面板把区域条目的
     * `create(hostContext, parent)` 调起来。它不是插件自己的状态，别拿它存东西。
     */
    void* Context() const noexcept { return _api != nullptr ? _api->hostContext : nullptr; }

    // ---- 日志 ----

    void Info(const char* message) const { logf(GOLDING_LOG_INFO, "%s", message != nullptr ? message : ""); }
    void Warn(const char* message) const { logf(GOLDING_LOG_WARN, "%s", message != nullptr ? message : ""); }
    void Error(const char* message) const { logf(GOLDING_LOG_ERROR, "%s", message != nullptr ? message : ""); }

    // ---- 注册面：往宿主登记自己的东西 ----

    bool AddWidgetClass(const GoldingWidgetClass& widgetClass) const
    {
        return report("组件类", widgetClass.name,
                      registerCall(_api != nullptr ? _api->registerWidgetClass : nullptr, &widgetClass));
    }

    bool AddPage(const GoldingPageDecl& page) const
    {
        return report("页面", page.id, registerCall(_api != nullptr ? _api->registerPage : nullptr, &page));
    }

    bool AddSettingsSection(const GoldingSettingsDecl& section) const
    {
        return report("设置区块", section.id,
                      registerCall(_api != nullptr ? _api->registerSettingsSection : nullptr, &section));
    }

    bool AddService(const char* name, const void* vtable) const
    {
        int result = GOLDING_REGISTER_INVALID;
        if (_api != nullptr && _api->registerService != nullptr)
        {
            result = _api->registerService(_api->hostContext, name, vtable);
        }
        return report("服务", name, result);
    }

    bool AddRegion(const GoldingRegionDecl& region) const
    {
        return report("区域", region.id,
                      registerCall(_api != nullptr ? _api->registerRegion : nullptr, &region));
    }

    bool AddRegionItem(const GoldingRegionItemDecl& item) const
    {
        return report("区域项", item.id,
                      registerCall(_api != nullptr ? _api->registerRegionItem : nullptr, &item));
    }

    /**设置区块也是页面级的界面片段，挂载口照旧。*/
    GoldingWidgetHandle MountSettingsSection(const char* sectionId, void* parent) const
    {
        if (_api == nullptr || _api->mountSettingsSection == nullptr)
        {
            return nullptr;
        }
        GoldingWidgetHandle handle = _api->mountSettingsSection(_api->hostContext, sectionId, parent);
        logf(GOLDING_LOG_INFO, "[hostapi] 挂载设置区块 %s → %s", text(sectionId),
             handle != nullptr ? "成功" : "失败");
        return handle;
    }

    // ---- 查询面 ----

    int PageCount() const { return _api != nullptr && _api->pageCount != nullptr ? _api->pageCount(_api->hostContext) : 0; }

    const GoldingPageDecl* PageAt(int index) const
    {
        if (_api == nullptr || _api->pageAt == nullptr)
        {
            return nullptr;
        }
        return _api->pageAt(_api->hostContext, index);
    }

    GoldingWidgetHandle MountPage(const char* pageId, void* parent) const
    {
        if (_api == nullptr || _api->mountPage == nullptr)
        {
            return nullptr;
        }
        GoldingWidgetHandle handle = _api->mountPage(_api->hostContext, pageId, parent);
        logf(GOLDING_LOG_INFO, "[hostapi] 挂载页面 %s → %s", text(pageId), handle != nullptr ? "成功" : "失败");
        return handle;
    }

    int SettingsSectionCount() const
    {
        return _api != nullptr && _api->settingsSectionCount != nullptr ? _api->settingsSectionCount(_api->hostContext)
                                                                       : 0;
    }

    const GoldingSettingsDecl* SettingsSectionAt(int index) const
    {
        if (_api == nullptr || _api->settingsSectionAt == nullptr)
        {
            return nullptr;
        }
        return _api->settingsSectionAt(_api->hostContext, index);
    }

    int RegionCount() const
    {
        return _api != nullptr && _api->regionCount != nullptr ? _api->regionCount(_api->hostContext) : 0;
    }

    const GoldingRegionDecl* RegionAt(int index) const
    {
        if (_api == nullptr || _api->regionAt == nullptr)
        {
            return nullptr;
        }
        return _api->regionAt(_api->hostContext, index);
    }

    int RegionItemCount(const char* regionId) const
    {
        if (_api == nullptr || _api->regionItemCount == nullptr)
        {
            return 0;
        }
        return _api->regionItemCount(_api->hostContext, regionId);
    }

    const GoldingRegionItemDecl* RegionItemAt(const char* regionId, int index) const
    {
        if (_api == nullptr || _api->regionItemAt == nullptr)
        {
            return nullptr;
        }
        return _api->regionItemAt(_api->hostContext, regionId, index);
    }

    int PluginCount() const
    {
        return _api != nullptr && _api->pluginCount != nullptr ? _api->pluginCount(_api->hostContext) : 0;
    }

    /**第 index 份插件档案（名字/版本/状态）；越界返回 nullptr。*/
    const GoldingPluginInfo* PluginAt(int index) const
    {
        if (_api == nullptr || _api->pluginAt == nullptr)
        {
            return nullptr;
        }
        return _api->pluginAt(_api->hostContext, index);
    }

    // ---- 装配后（ABI v8 的两条只读通道）----

    /**
     * 装配完成的**界面根**（面板插件 `create` 返回的那个节点）；没装配成时 nullptr。
     *
     * 只在 `Ready` 里用得上。注意它是**某个面板插件**的根——别的插件不该假设它的
     * 结构，只能断言通用契约。
     */
    void* UiRoot() const { return _api != nullptr && _api->uiRoot != nullptr ? _api->uiRoot(_api->hostContext) : nullptr; }

    /**
     * 宿主的启动模式串（`--expanded` 之类）；没有时是空串。
     *
     * 存在的理由：`--expanded` 是什么意思**只有面板插件知道**，但这一轮跑的是哪种
     * 模式只有宿主知道。所以宿主把原样字符串交出来，谁认谁解释。
     */
    const char* BootMode() const
    {
        return _api != nullptr && _api->bootMode != nullptr ? _api->bootMode(_api->hostContext) : "";
    }

    /**
     * @brief 某个插件的目录（绝对路径，UTF-8）；没有返回空串（ABI v10）。
     *
     * 只用"读自己带的数据文件"这一种场景：`Host().PluginDir()` 就是自己的目录，
     * 拼上文件名即可。**别人声明的相对路径不要用它去拼**——那些已经由 core
     * 在读完 `plugin.json` 时解析成绝对路径了。
     */
    const char* PluginDir(const char* pluginName) const
    {
        return _api != nullptr && _api->pluginDir != nullptr ? _api->pluginDir(_api->hostContext, pluginName) : "";
    }

    /**自己的目录；没拿到时返回空串。*/
    const char* PluginDir() const { return PluginDir(_provider); }

    /**
     * @brief 按名字取一个服务（能力目录的前身：现在就是服务表）。
     *
     * 三种结果在日志里必须可区分（`docs/Options.md` §1 的检查点）：
     *   - `缺能力`：没人提供 → 返回 nullptr，调用方自己降级；
     *   - `版本不足（放行）`：拿到了，但对方 abiVersion 低于 minVersion——
     *     实验期仍返回（解除条件见 Options §1）；
     *   - `取用`：正常。
     *
     * @tparam T 服务结构体，首字段必须是 `abiVersion`。
     */
    template <typename T>
    const T* Use(const char* name, unsigned int minVersion = 0) const
    {
        const void* raw = _api != nullptr && _api->queryService != nullptr
                              ? _api->queryService(_api->hostContext, name)
                              : nullptr;
        if (raw == nullptr)
        {
            logf(GOLDING_LOG_WARN, "[cap] %s 缺能力 %s（没有提供方）", provider(), text(name));
            return nullptr;
        }
        const T* service = static_cast<const T*>(raw);
        const unsigned int actual = service->abiVersion;
        if (actual < minVersion)
        {
            logf(GOLDING_LOG_WARN, "[cap] %s 取用 %s（版本不足，放行）：要 %u，实际 %u", provider(), text(name),
                 minVersion, actual);
            return service;
        }
        logf(GOLDING_LOG_INFO, "[cap] %s 取用 %s（abi=%u）", provider(), text(name), actual);
        return service;
    }

private:
    /**注册口的转发：`register*` 九个函数只有"声明类型"不同，其余一模一样。*/
    template <typename TDecl>
    int registerCall(int (*fn)(void*, const TDecl*), const TDecl* decl) const
    {
        return (_api != nullptr && fn != nullptr) ? fn(_api->hostContext, decl) : GOLDING_REGISTER_INVALID;
    }

    void logf(int level, const char* format, ...) const
    {
        if (_api == nullptr || _api->log == nullptr)
        {
            return;
        }
        char buffer[512] = {};
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        _api->log(_api->hostContext, level, buffer);
    }

    /**一次注册的留痕：谁、往哪张表、什么结果。返回"是否成功"。*/
    bool report(const char* table, const char* name, int result) const
    {
        switch (result)
        {
        case GOLDING_REGISTER_OK:
            logf(GOLDING_LOG_INFO, "[sdk] %s 注册%s %s → 成功", provider(), table, text(name));
            return true;
        case GOLDING_REGISTER_NAME_TAKEN:
            logf(GOLDING_LOG_ERROR, "[sdk] %s 注册%s %s → 驳回（名字已被占用）", provider(), table, text(name));
            return false;
        case GOLDING_REGISTER_INVALID:
        default:
            logf(GOLDING_LOG_ERROR, "[sdk] %s 注册%s %s → 驳回（描述不合法，含宿主不支持该注册口）", provider(),
                 table, text(name));
            return false;
        }
    }

    static const char* text(const char* value) noexcept { return value != nullptr ? value : "(空)"; }

    const GoldingHostApi* _api = nullptr;
    const char* _provider = nullptr;
};

/**
 * @brief 插件基类。**三个钩子 + 虚析构，之后冻结。**
 *
 * 为什么不在这里放 AddPage / Query 之类的便利方法：那样每加一种注册都是 ABI 事件
 * （见 `docs/Options.md` §8）。能力全部长在 `Host` 上。
 */
class Plugin
{
public:
    virtual ~Plugin() = default;

    /**静态描述。装配前读，必须活到进程结束（常量或成员的引用都行）。*/
    virtual const PluginManifest& Manifest() const = 0;

    /**注册。返回 false = 插件自己认为没装成（宿主会把状态记成 `EntryFailed`）。*/
    virtual bool Register(Host& host) = 0;

    /**
     * 装配之后（ABI v8 的 `ready`）：骨架建完、页面挂上之后再调一次。
     * 返回**失败项数**，0 = 全绿。不重写就等于"没什么可断言的"。
     */
    virtual int Ready(const Host& host)
    {
        (void)host;
        return 0;
    }
};

namespace detail
{

/**
 * 本 dll 里那**一个**插件对象。
 *
 * 函数内 static：`entry` 之前就构造好，而 `descriptor()` 也在 `entry` 之前被宿主调用
 * （`PluginLoader::open` 的顺序），两边拿到的是同一个对象。
 *
 * 约束：`T` 的构造函数**不能依赖任何运行时状态**——否则会退化成"在 descriptor 里
 * 构造插件对象"，那时插件连 `Host` 都还没拿到。
 */
template <typename T>
T& SharedInstance()
{
    static T instance;
    return instance;
}

// BuildDescriptor 里的 `ready` / `manifest` 指向下面两个 thunk，必须先声明。
template <typename T>
int EntryThunk(const GoldingHostApi* api);

template <typename T>
int ReadyThunk(const GoldingHostApi* api);

template <typename T>
const GoldingPluginManifest* ManifestThunk();

/**填 descriptor。前七个字段与 v8 一模一样，v9 只在**末尾**追加三项。*/
template <typename T>
const GoldingPluginDescriptor* BuildDescriptor()
{
    static const GoldingPluginDescriptor descriptor = [] {
        const PluginManifest& manifest = SharedInstance<T>().Manifest();
        GoldingPluginDescriptor built{};
        built.abiVersion = GOLDING_PLUGIN_ABI_VERSION;
        built.name = manifest.name != nullptr ? manifest.name : "";
        built.version = manifest.version != nullptr ? manifest.version : "";
        built.rootWidgetClass = manifest.rootWidgetClass != nullptr ? manifest.rootWidgetClass : "";
        built.depends = manifest.depends;
        built.dependCount = manifest.dependCount;
        built.ready = &ReadyThunk<T>;

        // v9：能力宣言与 SDK 身份。sdkId / sdkVersion 由编译期常量填，作者不写——
        // "我在哪套 SDK 下编译"是客观事实，手写必然有人写错。
        built.manifest = &ManifestThunk<T>;
        built.sdkId = GOLDING_SDK_ID;
        built.sdkVersion = GOLDING_SDK_VERSION;
        return built;
    }();
    return &descriptor;
}

/**`descriptor->manifest()` 的实现：把插件自己那份静态宣言原样交出去（不拷贝）。*/
template <typename T>
const GoldingPluginManifest* ManifestThunk()
{
    return &SharedInstance<T>().Manifest();
}

/**`golding_plugin_entry` 的实现：把 C 参数包成 `Host`，再交给插件自己的 `Register`。*/
template <typename T>
int EntryThunk(const GoldingHostApi* api)
{
    if (api == nullptr)
    {
        return 1;
    }
    T& plugin = SharedInstance<T>();
    const PluginManifest& manifest = plugin.Manifest();
    Host host(api, manifest.name);

    // 一行"我声明了什么"。能力目录（T-07）起来之前，这行就是唯一能看的能力面：
    // 装配失败时先看它，能立刻分出"没声明"还是"声明了没注册"。
    char summary[256] = {};
    std::snprintf(summary, sizeof(summary), "%s 能力: kind=%s needs=%zu",
                  manifest.name != nullptr ? manifest.name : "?",
                  manifest.kind != nullptr ? manifest.kind : "", manifest.needCount);
    host.Info(summary);

    return plugin.Register(host) ? 0 : 1;
}

/**descriptor 里那个 `ready` 的实现。返回值原样传出（宿主汇总成退出码的一部分）。*/
template <typename T>
int ReadyThunk(const GoldingHostApi* api)
{
    if (api == nullptr)
    {
        return 0;
    }
    T& plugin = SharedInstance<T>();
    const PluginManifest& manifest = plugin.Manifest();
    Host host(api, manifest.name);
    return plugin.Ready(host);
}

} // namespace detail

} // namespace golding

/**
 * @brief 导出插件：生成 `golding_plugin_descriptor` 与 `golding_plugin_entry` 两个 C 符号。
 *
 * 只导出这两个。`abiVersion` 取编译期常量（就是 ABI 该有的样子），其余字段全部来自
 * `T::Manifest()`。作者不写任何 `extern "C"`。
 *
 * @param TYPE 插件类（必须继承 `golding::Plugin`、有默认构造函数）。
 */
#define GOLDING_EXPORT_PLUGIN(TYPE)                                                                       \
    static_assert(std::is_base_of_v<::golding::Plugin, TYPE>, "插件类型必须继承 golding::Plugin");           \
    extern "C" GOLDING_PLUGIN_EXPORT const GoldingPluginDescriptor* golding_plugin_descriptor(void)        \
    {                                                                                                      \
        return ::golding::detail::BuildDescriptor<TYPE>();                                                  \
    }                                                                                                      \
    extern "C" GOLDING_PLUGIN_EXPORT int golding_plugin_entry(const GoldingHostApi* hostApi)                \
    {                                                                                                      \
        return ::golding::detail::EntryThunk<TYPE>(hostApi);                                                \
    }
