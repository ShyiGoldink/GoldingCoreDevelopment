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
 * "可以随意放置内容"落在这两个字段上：`create` 拿到的 `parent` 是一块已经按
 * `width × height` 建好的空面板，里面画什么、怎么排，本插件一概不管。
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
     * 建内容；`parent` 是弹窗里那块空面板。
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
 */
inline constexpr const char* kPopupServiceName = "golding.popup";

struct PopupService
{
    unsigned int abiVersion;

    /**弹出；0 = 成功，非 0 = 声明不合法或面板不在。*/
    int (*push)(void* self, const GoldingPopupDecl* decl);

    /**收起当前弹窗（没弹着时什么也不做）。*/
    void (*pop)(void* self);
};

} // namespace golding
