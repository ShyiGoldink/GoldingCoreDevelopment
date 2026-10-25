#pragma once

// 面板插件与其它插件之间的**服务契约**。
//
// 为什么这些结构体不在 SDK 里：SDK 只放"任何面板插件都需要的"（注册面、ABI 身份）。
// `popup` 的字段形态是**本面板插件自己的约定**——换一个面板插件，弹窗可能是抽屉、
// 可能是侧滑、可能根本没有；把它抬进 SDK 就等于让 core 认识"弹窗"这个词，而 core
// 一旦认识，就会长出第二套特化表（`docs/Options.md` 反复记的那笔账）。
//
// 所以：**契约在提供方，规格在头文件，谁用谁 include**。同仓库的插件通过
// `plugins/common` 的 include 目录共享这一份；第三方插件要接，就拷这个头。
//
// 服务名是唯一键。取到的指针由提供方持有、活到进程结束；调用方不得释放。

#include <sdk/golding_plugin.h>

namespace golding
{

/**
 * @brief 导航服务（`golding.navigation`）：让任意插件把内容区的页面切过去。
 *
 * 面板插件提供它，页面插件用它。页面 id 来自宿主页面表（`GoldingPageDecl::id`）。
 */
inline constexpr const char* kNavigationServiceName = "golding.navigation";

struct NavigationService
{
    unsigned int abiVersion;

    /**切到指定页面；0 = 成功，非 0 = 没有这一页（或面板不在）。*/
    int (*go)(void* self, const char* pageId);
};

/**
 * @brief 一份弹窗声明：**内容由调用方决定，位置与尺寸由调用方给**。
 *
 * "可以随意放置内容"落在 `create` 收到的那个容器上：它已经按 `width × height`
 * 建好，里面画什么、怎么排，面板插件一概不管。
 *
 * **生命周期**：面板在 `push` 里把整份声明（含 `id` 文本）**拷贝**一份，所以调用方
 * 可以传栈上的临时量——`push` 返回之后就能扔。反过来说，面板不许存这个指针：
 * 关闭弹窗时那个栈帧早就没了（曾经因此每次关弹窗都闪退，见 `docs/NOTES.md`）。
 *
 * 关闭键**由面板提供**（右上角的 X，节点名 `popup.close`），所以插件不该、也
 * 不必再画一个关闭控件；内容要把它那块地方让出来，或者自己用锚点摆开。
 *
 * 关闭只有一条路：点那个 X（代码里调 `PopupService::pop` 是同一条路）。
 * **点遮罩不关闭**——遮罩只负责挡住底下的界面、吃掉落在它上面的点击。
 *
 * 与 `GoldingOverlayDecl`（底部抽屉）的区别：那个只有高度，位置固定在底部；
 * 这个能定位、能居中、能任意大小，是**对话框**而不是抽屉。
 */
typedef struct GoldingPopupDecl
{
    /**标识，仅用于日志与"是不是同一份"的判断。可空。*/
    const char* id;

    /**内容块尺寸（设计坐标）。0 = 取内容区的 3/5。*/
    int width;
    int height;

    /**不居中时的左上角，相对**内容区**左上角（设计坐标）。*/
    int offsetX;
    int offsetY;

    /**1 = 在内容区里水平垂直居中（忽略 offsetX / offsetY）。*/
    int centered;

    /**
     * 建内容；`parent` 是弹窗里的**内容容器**（一个静态面板，与弹窗同尺寸）。
     *
     * 它由弹窗拥有，插件的所有节点都往它上面 `AddChild`，用 `AnchorData` 的
     * selfAnchor / traceAnchor 把东西摆到弹窗里合适的位置。
     *
     * 为什么不是"弹窗面板本身"：关闭键（X）也挂在弹窗面板上，内容与它分开，
     * 插件重建自己的内容时才不会连关闭键一起 `RemoveChild` 掉。
     *
     * **`hostContext` 传 nullptr**：调用方是在自己的代码里发起的，需要 HostApi 时
     * 应当把自己注册时那份 `golding::Host` 存下来用，而不是指望这个参数。
     */
    GoldingWidgetHandle (*create)(void* hostContext, void* parent);

    /**关闭时回调（点遮罩 / 调 pop 都会走一次）。可空。*/
    void (*onClose)(void* hostContext);
} GoldingPopupDecl;

/**
 * @brief 弹窗服务（`golding.popup`）：同一时刻只保留一层。
 *
 * 重复 push 会**换掉**当前内容，不是叠起来——叠层的层级与焦点规则没人定义过，
 * 与其猜，不如先只支持一层。
 *
 * **关闭由 X 触发，点遮罩不关**（面板把遮罩的点击回调留空）。
 */
inline constexpr const char* kPopupServiceName = "golding.popup";

/**
 * 弹窗契约的版本。
 *
 * v1：`create` 拿到的是**弹窗面板本身**；关闭键谁用谁画，点遮罩会关。
 * v2：`create` 拿到的是**内容容器**；关闭键（X）由面板统一提供，**点遮罩不关**。
 *
 * 用 `Host::Use<PopupService>(kPopupServiceName, kPopupServiceVersion)` 表达
 * "我需要 v2 的语义"，版本不够时 `Use` 会 WARN 并放行（实验期的规矩）。
 */
inline constexpr unsigned int kPopupServiceVersion = 2;

struct PopupService
{
    unsigned int abiVersion;

    /**弹出；0 = 成功，非 0 = 声明不合法或面板不在。*/
    int (*push)(void* self, const GoldingPopupDecl* decl);

    /**收起当前弹窗（没弹着时什么也不做）。点 X 走的就是这条。*/
    void (*pop)(void* self);
};

} // namespace golding
