#pragma once

// 插件 SDK 头：插件作者唯一需要拿的头文件。
//
// 这一层只允许出现 C 的 POD 与函数指针——跨 dll 边界不接受任何 C++ 类型
// （std::string / std::vector / 引用都不行）。宿主侧再包一层 C++ 注册表。
//
// 两个导出符号，**顺序不能反**：
//   ① golding_plugin_descriptor()  —— 纯 C POD，含 abiVersion
//   ② golding_plugin_entry(api)    —— 注册组件、绑动作
// 宿主先读 ① 比对 ABI，不匹配就拒绝，绝不调 ②。不这么做，ABI 不对的表现是崩溃。

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 当前 ABI 版本。改动任何结构体字段都要 +1。
 *
 * v7（2026-10-22，迁移任务书 T-05）：**删掉** `GoldingHostApi` 里那条
 * "请求重建界面"的回调（字段名见 0.4.0 之前的 git 历史）。
 * 界面层不再靠"整树重建"换形态（抽屉 / 覆盖层就地改几何），换肤靠主题版本号
 * 自动重查，那条通道成了死代码。留着它就是给下一个人一个绕路口。
 *
 * v8（2026-10-22，迁移任务书 T-06）：加**装配后钩子**——`GoldingPluginDescriptor`
 * 末尾的 `ready`（骨架建完再回调，自检插件靠它拿界面做断言），以及 HostApi 的
 * `uiRoot`（装配后的界面根）与 `bootMode`（宿主启动模式串）。
 *
 * v9（2026-10-23，`TASK-CORE-UPING.md` T-06）：`GoldingPluginDescriptor` 末尾追加
 * 三样东西——`manifest()`（静态能力宣言）、`sdkId`（这套头文件集合的标识）、
 * `sdkVersion`。**不向后兼容**：v8 插件全部被拒，必须同一次重编（`Options.md` §4）。
 *
 * v10（2026-10-23，`TASK-CORE-PLUGINS.md` T-12）：`GoldingHostApi` 末尾追加
 * `pluginDir`——按插件名查它被扫描到的那个目录。**不向后兼容**，同样必须整批重编。
 */
#define GOLDING_PLUGIN_ABI_VERSION 10u

/**
 * 这套 SDK 的标识与版本。descriptor 的 `sdkId` / `sdkVersion` 由它们填。
 *
 * 真源是 `CMakeLists.txt` 的 `project(GoldingCore VERSION ...)`，经
 * `target_compile_definitions` 传进来——**"我在哪套 SDK 下编译"是客观事实，
 * 不该由作者手写**。下面的 `#ifndef` 只是兜底：直接拿源码树编、没走 CMake 时用得到。
 */
#ifndef GOLDING_SDK_ID
#define GOLDING_SDK_ID "golding"
#endif

#ifndef GOLDING_SDK_VERSION
/**兜底值。单测里专门断言它**不等于**这个值，用来抓"忘了在 CMake 里传定义"。*/
#define GOLDING_SDK_VERSION "0.0.0-dev"
#endif

/**
 * 宿主当前支持到的 core 能力版本（单调递增）。
 *
 * 插件在 plugin.json 里声明的 `apiVersion` 是"我需要的最小能力版本"；
 * 声明高于这个数，宿主就拒绝加载——因为宿主给不了它要的能力。
 * 新能力（画布、动作表、区域等）上线时递增这个值，插件据此声明依赖。
 */
#define GOLDING_CORE_API_VERSION 2u

#define GOLDING_PLUGIN_DESCRIPTOR_SYMBOL "golding_plugin_descriptor"
#define GOLDING_PLUGIN_ENTRY_SYMBOL "golding_plugin_entry"

#ifdef _WIN32
#define GOLDING_PLUGIN_EXPORT __declspec(dllexport)
#else
#define GOLDING_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/**组件实例的不透明句柄。宿主与插件都只当它是 void*。*/
typedef void* GoldingWidgetHandle;

/**日志级别，与宿主侧一一对应。*/
enum GoldingLogLevel
{
    GOLDING_LOG_INFO = 0,
    GOLDING_LOG_WARN = 1,
    GOLDING_LOG_ERROR = 2
};

/**注册组件的返回值。*/
enum GoldingRegisterResult
{
    GOLDING_REGISTER_OK = 0,
    GOLDING_REGISTER_NAME_TAKEN = 1,
    GOLDING_REGISTER_INVALID = 2
};

/**
 * @brief 一个可被宿主按名字创建的组件类型。
 *
 * create 的约定：
 *   - parent 是宿主提供的父节点（可能是场景根）。插件必须把新建子树的根
 *     **挂到 parent 上**（由 parent 拥有），然后返回该子树的指针；
 *   - 返回 nullptr 表示创建失败，宿主不会为它做任何清理；
 *   - configName 可以为空串，表示用类型默认配置。
 *
 * destroy 可留空：节点已被 parent 拥有，宿主随场景销毁统一回收。
 */
typedef struct GoldingWidgetClass
{
    const char* name;
    GoldingWidgetHandle (*create)(void* hostContext, void* parent, const char* configName);
    void (*destroy)(void* hostContext, GoldingWidgetHandle widget);
} GoldingWidgetClass;

/**一个内容页：宿主按 id 装配到界面骨架的内容区里。*/
typedef struct GoldingPageDecl
{
    /**唯一标识，冲突即驳回。*/
    const char* id;
    /**抽屉 / 导航里显示的名字。*/
    const char* title;
    /**排序，小的在前。*/
    int order;
    /**创建页面子树；约定同 GoldingWidgetClass::create。*/
    GoldingWidgetHandle (*create)(void* hostContext, void* parent);
} GoldingPageDecl;

/**设置页里的一个区块：插件往"设置"里塞自己的那一块。*/
typedef struct GoldingSettingsDecl
{
    const char* id;
    const char* title;
    int order;
    GoldingWidgetHandle (*create)(void* hostContext, void* parent);
} GoldingSettingsDecl;

/**宿主持有的一份插件档案，魔法阵这类插件靠它做可视化。*/
typedef struct GoldingPluginInfo
{
    const char* name;
    const char* title;
    const char* version;
    const char* role;
    int abiVersion;
    /**
     * 0 = 可用；1 = manifest 不合法；2 = 打不开 dll；3 = ABI 不匹配；
     * 4 = 没有 dll；5 = entry 失败；6 = 依赖不满足（缺失或成环）；
     * 7 = apiVersion 高于宿主支持；8 = `sdkId` 与宿主不一致（不是给这套 SDK 编的）。
     *
     * 这张表在宿主侧的唯一定义处是 `Application/PluginState.h` 的 `PluginState`；
     * 跨 dll 只能传 int，所以这里保留注释而不引入枚举。
     */
    int state;
} GoldingPluginInfo;

/**
 * 覆盖层：从窗口底部升起的整宽面板。
 *
 * 只有骨架插件（Shell）实现它，但声明放在这里——因为**任何插件**都可能想弹出
 * 一个自己的面板。插件通过服务 "golding.overlay" 拿到 push / pop。
 */
typedef struct GoldingOverlayDecl
{
    const char* id;
    /**抽屉期望高度；骨架可能按自己的上限截断。*/
    int height;
    /**创建抽屉内容；parent 是骨架提供的抽屉面板。*/
    GoldingWidgetHandle (*create)(void* hostContext, void* parent);
    /**关闭时回调，可空。*/
    void (*onClose)(void* hostContext);
} GoldingOverlayDecl;

/**
 * @brief 一个可注册区域：**由面板插件声明**。
 *
 * 这是"换面板插件 = 换一套 UI"的关键契约。core 不认识任何具体区域名，
 * 只负责存表、查表、同名驳回；一个插件能往哪儿放东西，完全取决于当前
 * 面板插件声明了哪些区域。
 *
 * 面板插件声明之后，自己按 `regionItemCount` / `regionItemAt` 取出条目渲染，
 * 常见约定（不强制）：`menubar` 顶栏、`sidebar` 侧栏、`statusbar` 状态栏。
 */
typedef struct GoldingRegionDecl
{
    /**区域标识，全局唯一，冲突即驳回。*/
    const char* id;
    /**给人看的名字，仅用于诊断。*/
    const char* title;
    /**面板插件自己排序用，小的在前。*/
    int order;
    /**
     * 同一区域内是否允许重名条目。
     * 0 = 区域内 id 必须唯一（默认，绝大多数情况）；
     * 1 = 允许重名（比如"最近打开"这类每次都可能重复的槽位）。
     */
    int allowDuplicateIds;
} GoldingRegionDecl;

/**
 * @brief 往某个区域里放的一项。
 *
 * 由**任意插件**注册，由**面板插件**消费。两种形态二选一：
 *   - 画界面：填 `create`，面板插件把它挂进区域容器里；
 *   - 只有动作：填 `onActivate`，不填 `create`（菜单项就是这种）。
 * 两个都填也行——既画界面，被点时也走 onActivate。
 */
typedef struct GoldingRegionItemDecl
{
    /**目标区域 id。该区域必须已存在，否则注册被驳回。*/
    const char* regionId;
    /**区域内唯一标识（`allowDuplicateIds` 为 0 时）。*/
    const char* id;
    /**显示名。*/
    const char* title;
    /**图标资源名，可为空。*/
    const char* icon;
    /**区域内排序，小的在前。*/
    int order;
    /**创建条目界面；parent 由面板插件提供。可空。*/
    GoldingWidgetHandle (*create)(void* hostContext, void* parent);
    /**被激活（点击）时调用。可空。*/
    void (*onActivate)(void* hostContext);
} GoldingRegionItemDecl;

/**宿主提供给插件的服务表。所有条目都是 C 函数指针。*/
typedef struct GoldingHostApi
{
    unsigned int abiVersion;
    void* hostContext;
    void (*log)(void* hostContext, int level, const char* message);

    // ---- 注册：插件往宿主登记自己的东西 ----
    int (*registerWidgetClass)(void* hostContext, const GoldingWidgetClass* widgetClass);
    int (*registerPage)(void* hostContext, const GoldingPageDecl* page);

    // ---- 查询：骨架插件靠这三个把导航做出来 ----
    int (*pageCount)(void* hostContext);
    const GoldingPageDecl* (*pageAt)(void* hostContext, int index);

    /**把某个页面装进 parent，返回页面根节点。换页由骨架插件负责摘掉旧的。*/
    GoldingWidgetHandle (*mountPage)(void* hostContext, const char* pageId, void* parent);

    // ---- 设置页扩展点：插件往"设置"里加自己的区块 ----
    int (*registerSettingsSection)(void* hostContext, const GoldingSettingsDecl* section);
    int (*settingsSectionCount)(void* hostContext);
    const GoldingSettingsDecl* (*settingsSectionAt)(void* hostContext, int index);
    GoldingWidgetHandle (*mountSettingsSection)(void* hostContext, const char* sectionId, void* parent);

    // ---- 只读的插件档案：给魔法阵 / 管理器用 ----
    int (*pluginCount)(void* hostContext);
    const GoldingPluginInfo* (*pluginAt)(void* hostContext, int index);

    // ---- 服务表：插件之间唯一的横向通道 ----
    /**
     * 登记一个服务。`vtable` 由提供方定义，首字段必须是 abiVersion。
     * 同名的后到者被驳回——服务名和组件名一样是唯一键。
     */
    int (*registerService)(void* hostContext, const char* name, const void* vtable);

    /**按名字取服务；没有返回 nullptr。*/
    const void* (*queryService)(void* hostContext, const char* name);

    // ---- 区域表（ABI v6）：面板插件声明"有哪些地方可以注册"，其余插件往里放 ----
    // 前三个只有面板插件会调；后三个两边都可能调。
    // 往不存在的区域注册会被驳回（GOLDING_REGISTER_INVALID），
    // 但**不影响该插件其余注册**——面板插件缺席时，其它插件照常工作。

    /**声明一个区域。id 为空或重复则驳回。通常由面板插件在 entry 里调用。*/
    int (*registerRegion)(void* hostContext, const GoldingRegionDecl* region);

    /**本机共有几个区域（面板插件据此知道自己要渲染哪些）。*/
    int (*regionCount)(void* hostContext);
    /**第 index 个区域，按 order 升序、同 order 按 id 字典序；越界返回 nullptr。*/
    const GoldingRegionDecl* (*regionAt)(void* hostContext, int index);

    /**往区域里放一项。regionId 不存在、或区域内 id 重复则驳回。*/
    int (*registerRegionItem)(void* hostContext, const GoldingRegionItemDecl* item);
    /**某个区域里有几项，按 order 升序；区域不存在返回 0。*/
    int (*regionItemCount)(void* hostContext, const char* regionId);
    /**某个区域里的第 index 项；越界返回 nullptr。*/
    const GoldingRegionItemDecl* (*regionItemAt)(void* hostContext, const char* regionId, int index);

    // ---- 装配后（ABI v8）：给"要看着界面说话"的插件两条只读通道 ----

    /**
     * @brief 装配完成的**界面根**（面板插件 `create` 返回的那个节点）；没装配成时 nullptr。
     *
     * 只在 `ready` 回调里用得上：那时骨架已经建好、页面已经挂上。注意它是**某个
     * 面板插件**的根——自检插件不该假设它的类型或结构，只能断言通用契约
     * （尺寸等于父级、页面能挂上去、条目能建出来）。
     */
    void* (*uiRoot)(void* hostContext);

    /**
     * @brief 宿主的**启动模式**串（命令行参数那个，例如 `--expanded`）；没有时是空串。
     *
     * 存在的理由：自检的"按某个模式跑一遍"是**插件自己的**知识（面板知道
     * `--expanded` 是什么意思，core 不知道），但要跑哪种模式只有宿主知道。
     * 所以宿主把原样字符串交出来，谁认谁解释。
     */
    const char* (*bootMode)(void* hostContext);

    // ---- ABI v10：插件目录 ----

    /**
     * @brief 某个插件被扫描到的那个**目录**（绝对路径，UTF-8）；未知名字返回 nullptr。
     *
     * 为什么是"按名字查"而不是"我自己的目录"：`hostContext` 目前只是一个字符串
     * 指针（见 `Options.md` §5），没有承载目录的位置；而插件名本来就是档案表的
     * 唯一键，`Host::PluginDir()` 会把自己的名字填进第一个参数。
     *
     * 用途只有一个：**读自己带的数据文件**（配置、图标、清单）。相对路径的解析
     * 归 core（`plugin.json` 里声明的文件读完就是绝对路径），这里给的是"我这个
     * 插件住在哪"，不是"文件系统随便读"——返回的指针由宿主持有，活到进程结束，
     * 插件不得释放或改写。
     */
    const char* (*pluginDir)(void* hostContext, const char* pluginName);
} GoldingHostApi;

/**
 * @brief 一条能力需求：要哪个名字、至少哪个版本（ABI v9）。
 *
 * 只有名字与最小版本，**不带 vtable**：静态声明要在装配前就能比对，
 * 而 vtable 是运行时事实。
 */
typedef struct GoldingCapabilityNeed
{
    const char* name;
    unsigned int minVersion;
} GoldingCapabilityNeed;

/**
 * @brief 插件的静态宣言（ABI v9）：身份 + 依赖 + 我提供什么 + 我需要什么。
 *
 * 字段与 `GoldingPlugin.h` 的 `golding::PluginManifest` **一一对应**，后者直接
 * 用这个 C 结构当存储（跨 dll 只传 C POD，C++ 那层只是别名 + 便利写法）。
 *
 * `kind` 是自由字符串（`"panel"` / `"page"` / `"logic"` / `"widgets"` …），
 * **core 不解释它**，只存、只展示。
 */
typedef struct GoldingPluginManifest
{
    const char* name;
    const char* version;
    /**宿主装配界面时创建哪个组件类；空/null 表示不提供界面。*/
    const char* rootWidgetClass;
    const char* kind;

    const char* const* depends;
    size_t dependCount;

    /**我提供的能力**名字**（运行时 vtable 由宿主交叉校验）。*/
    const char* const* provides;
    size_t provideCount;

    const GoldingCapabilityNeed* needs;
    size_t needCount;
} GoldingPluginManifest;

/**插件的自我描述。宿主在任何其他调用之前先读它。*/
typedef struct GoldingPluginDescriptor
{
    unsigned int abiVersion;
    const char* name;
    const char* version;
    /**宿主装配界面时创建哪一个组件类；空串表示本插件不提供界面。*/
    const char* rootWidgetClass;

    /**
     * 依赖的插件名。**由 SDK 的 `PluginManifest::depends` 在编译期填好**（见
     * `GoldingPlugin.h` 的 `GOLDING_EXPORT_PLUGIN`），不再写在 plugin.json 里——
     * 依赖是编译期事实，两处写必然对不上。
     */
    const char* const* depends;
    size_t dependCount;

    /**
     * @brief **装配之后**再回调一次（ABI v8，可空）。返回失败项数，0 = 全绿。
     *
     * 时序：扫描 → 加载 → entry（注册）→ 建窗 → 装配骨架（调面板的 create）→
     * **依次调每个插件的 ready**。所以这里 `uiRoot` 已经能查、页面已经挂上。
     *
     * 它存在就是为了自检：断言"根铺满了吗""每页都装得上吗"这类话必须发生在
     * 装配**之后**，而 `entry` 太早（那时连窗口都没有）。
     *
     * 约定：**返回值只报数，不崩**。失败项由宿主汇总成进程退出码的一部分，
     * 所以一个插件的断言红了不应该影响别的插件跑。
     */
    int (*ready)(const GoldingHostApi* hostApi);

    // ---- ABI v9：能力宣言与 SDK 身份 ----

    /**
     * @brief 静态能力宣言（可空）。指向的存储**由插件持有、活到进程结束**，宿主不拷贝。
     *
     * 装配**之前**就能读到，所以宿主能在 entry 之前判"它要的能力没人提供"。
     * 与运行时 `registerService` 的一致性由宿主交叉校验（声明了没注册 = 该插件坏）。
     */
    const GoldingPluginManifest* (*manifest)(void);

    /**
     * 这套头文件集合的标识（`"golding"`）。**严格相等**，不等即拒。
     *
     * 与 `abiVersion` 分工：那个管二进制布局，这个管"给哪套 SDK 编的"。
     * 平台不塞进来——跨平台由 `plugin.json` 的 `targets` 表达（编译目标是编译目标，
     * SDK 是头文件集合，混成一个字符串以后就没法分别校验）。
     */
    const char* sdkId;

    /**
     * 这套 SDK 的版本（`"0.2.0"`）。实验期**只记不拒**：
     * 编译栈要等正式版发布前才选定，在那之前它无法代表"头文件 + 编译器 ABI"这个组合
     * （解除条件见 `docs/Options.md` §1）。
     */
    const char* sdkVersion;
} GoldingPluginDescriptor;

/**
 * @brief 构件服务（ABI v9）：默认 UI 构件的提供方。
 *
 * 由**核心插件库里的一个独立插件**提供（`plugins/core/widgets`），不放进面板插件、
 * 也不塞进 core。于是"纯逻辑插件要一个按钮"不必 include InkingUIKit，也不必知道
 * 按钮是谁画的。
 *
 * 关键设计：`create` 用"**名字 + 配置串**"，不是"每种构件一个函数"。
 * 每加一种构件都是一个 vtable 变更，而 vtable 已经冻结一次了——
 * 名字 + JSON 让面板插件能自由扩充构件集，SDK 版本不动。
 *
 * **契约在 SDK，规格在插件**：这四个函数的形状属于 SDK；`"button"` 有哪些配置字段
 * 属于 widgets 插件（写进它自己的 `widgets_schema.h`）。换一套核心插件时，构件规格
 * 可以整份换掉而 SDK 一行不动。
 */
/**构件服务的名字。契约的一部分，所以和结构体放一起，两边都读这一个定义。*/
#define GOLDING_WIDGETS_SERVICE_NAME "golding.widgets"

typedef struct GoldingWidgetsService
{
    unsigned int abiVersion;

    /**
     * 建一个构件并挂到 `parent` 上。
     *
     * @param kind       构件名（`"button"` / `"knob"` …），由提供方解释。
     * @param configJson 配置串（JSON object 文本），可为空串 = 全用默认值。
     * @return 新建节点的句柄；失败为 nullptr。
     */
    GoldingWidgetHandle (*create)(void* self, void* parent, const char* kind, const char* configJson);

    /**
     * 绑一个事件回调。目前约定只认 `"click"`。
     * @return 0 = 成功；非 0 = 这个构件不支持该事件。
     */
    int (*bind)(void* self, GoldingWidgetHandle widget, const char* event, void (*callback)(void* user), void* user);

    /**
     * 改一条配置（键名与取值都是字符串，由提供方解释）。
     * @return 0 = 成功；-1 = 不认识的键（**不猜、不静默接受**）。
     */
    int (*set)(void* self, GoldingWidgetHandle widget, const char* key, const char* value);

    /**
     * 读一条配置写进 `buffer`（UTF-8，带结尾 0）。
     * @return 写入的字符数；-1 = 不认识的键。
     */
    int (*get)(void* self, GoldingWidgetHandle widget, const char* key, char* buffer, int bufferSize);
} GoldingWidgetsService;

typedef const GoldingPluginDescriptor* (*GoldingPluginDescriptorFn)(void);
typedef int (*GoldingPluginEntryFn)(const GoldingHostApi* hostApi);

/**
 * 插件必须导出这两个函数。
 *
 * @code
 * extern "C" GOLDING_PLUGIN_EXPORT const GoldingPluginDescriptor* golding_plugin_descriptor(void);
 * extern "C" GOLDING_PLUGIN_EXPORT int golding_plugin_entry(const GoldingHostApi* hostApi);
 * @endcode
 */
GOLDING_PLUGIN_EXPORT const GoldingPluginDescriptor* golding_plugin_descriptor(void);
GOLDING_PLUGIN_EXPORT int golding_plugin_entry(const GoldingHostApi* hostApi);

#ifdef __cplusplus
}
#endif
