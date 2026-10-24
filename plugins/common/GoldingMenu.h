#pragma once

// 面板插件与其它插件之间的**菜单契约**（`golding.menu`）。
//
// 为什么不在 SDK 里：菜单是"面板插件自己的约定"——换一个面板插件，同一个
// 菜单可能是抽屉、可能是侧滑、也可能根本没有。把它抬进 SDK 就等于让 core
// 认识"菜单"这个词，而 core 一旦认识，就会长出第二套特化表（`docs/Options.md`
// 反复记的那笔账）。所以与 `GoldingServices.h` 同一条规矩：
// **契约在提供方，规格在头文件，谁用谁 include**。
//
// ---------------------------------------------------------------------------
// 这个服务回答的问题
//
// "我注册了一个顶栏按钮，点它要弹出一串层级菜单"——按钮是区域项，菜单的
// 层级、定位、关闭、互斥全部由**面板插件**负责，插件只交一份纯数据。
//
// 三层结构（`MenuItem::children` 再指向一组项即多一级）：
//
//   menu.open("settings.entry", &decl)
//        │
//        ├─ 一级项 ──点击──▶ 子层（钉在这一项的右侧）──▶ 再一级 …
//        │
//        └─ 叶子项 ──点击──▶ onActivate() + 整串关掉
//
// ---------------------------------------------------------------------------
// 生命周期（这条最容易踩）
//
// **声明只需要在 `open` 调用期间有效**：面板在打开那一刻把整棵树深拷贝进
// 自己的表。所以调用方可以现场拼一份栈上的 `std::vector<MenuItem>`（比如
// "设置区块是装配后才有的"那种动态内容），open 返回后就能扔。
//
// `onActivate` 的函数指针会被**长期持有**（它指向调用方的代码，不是数据），
// 所以必须是静态函数的地址；`hostContext` 由面板在点击时传 nullptr——
// 需要 HostApi 的插件应当把自己那份 `golding::Host` 存成静态量再用。
//
// ---------------------------------------------------------------------------
// 与 `golding.popup` 的分工
//
// - `golding.menu`：**非模态**的一串浮窗。点外面关、点叶子关，不遮挡内容区；
// - `golding.popup`：**模态**对话框（遮罩 + 阻断）。要"填一堆控件再确认"用它。
//
// 同一个面板插件同时提供两者，互不替代。

#include <sdk/golding_plugin.h>

namespace golding
{

/** 服务名（唯一键）。 */
inline constexpr const char* kMenuServiceName = "golding.menu";

/**
 * @brief 菜单里的一项。
 *
 * **两种形态二选一**（与区域项同一套取向）：
 *   - `children` 非空：这一项是个**入口**，点击展开子层，`onActivate` 不会被调；
 *   - `children` 为空：叶子，点击执行 `onActivate` 并把整串菜单关掉。
 *
 * 两个都填时按"有子层"走——子层的存在本身就是更强的意图。
 */
struct MenuItem
{
    /**标识，仅用于日志与组件命名。可空。*/
    const char* id;

    /**项文字。空串的项会被跳过（不画一个空行）。*/
    const char* label;

    /**子项数组；空 = 叶子。*/
    const MenuItem* children;

    /**`children` 的项数（`children` 非空时才有意义）。*/
    int childCount;

    /**
     * 叶子被点击时调。可空（空 = 点了只关菜单）。
     *
     * `hostContext` 恒传 nullptr——调用方是在自己的代码里发起的，需要 HostApi
     * 时应把自己那份 `golding::Host` 存成静态量。`itemId` 就是这一项的 `id`
     * （深拷贝之后的那份，调用期间有效），所以**一个静态回调能服务所有项**：
     * 按 id 分派即可，不必为每一项造一个函数指针。
     */
    void (*onActivate)(void* hostContext, const char* itemId);
};

/**一份菜单。`id` 只用于日志。 */
struct MenuDecl
{
    const char* id;

    /**一级项数组（按给定顺序排，面板不重排）。*/
    const MenuItem* items;

    /**`items` 的项数。<= 0 时 `open` 返回失败，不弹一个空菜单。*/
    int itemCount;
};

/**菜单服务（`golding.menu`）。 */
struct MenuService
{
    unsigned int abiVersion;

    /**
     * @brief 在区域项 `regionItemId` 的**下方**打开一份菜单。
     *
     * 为什么锚点是一个"区域项 id"而不是坐标：面板才知道那个按钮在哪儿，
     * 插件手里只有 `regionItemId` 这层身份（`GoldingRegionItemDecl::onActivate`
     * 不带坐标）。让插件自己算坐标，就得让它认识 InkingUIKit 的节点树。
     *
     * 找不到该区域项时退化为"从窗口左上角展开"并打一条 WARN——菜单仍然能开，
     * 不至于因为一个 id 拼错就整个功能消失。
     *
     * @return 0 = 已打开；非 0 = 面板不在 / 菜单为空 / 已经开着。
     */
    int (*open)(void* self, const char* regionItemId, const MenuDecl* menu);

    /**收起整串菜单（幂等）。 */
    void (*close)(void* self);

    /**现在有菜单开着吗（1 = 开着）。 */
    int (*isOpen)(void* self);
};

} // namespace golding
