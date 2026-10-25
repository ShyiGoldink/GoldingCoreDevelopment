#pragma once

// 自定义指针的**服务契约**（`golding.cursor`）。
//
// 为什么它要有服务：指针是"一个插件画、别的插件可能想改"的东西。
// 一个写字插件进了画布会想要"准星"，一个演示插件会想要"圆点"，而它们不该各自
// 再去画一层、也不该知道那层挂在哪儿。所以由 cursor 插件独占那一个图层，别
// 人通过服务说"现在要什么样子"。图层只有一个 ⇒ 屏幕上永远只有一个指针。
//
// 与 `GoldingMenu.h` / `GoldingServices.h` / `GoldingSettings.h` 同一条规矩：
// **契约在提供方，规格在头文件，谁用谁 include**。
//
// ---------------------------------------------------------------------------
// 这个服务回答的问题
//
//   1. 换成哪种**内置图案**（六选一：`setStyle`）；
//   2. 换成一张**自己的图**（`setImage`，绝对路径，PNG / SVG 都行）；
//   3. 还要不要这一层（`setEnabled`：关掉就恢复系统指针）；
//   4. 按下时的反馈要不要（`setEffects`）；
//   5. 想自己在代码里"点一下"（`pulse`：不等真的按键，炸一圈涟漪）。
//
// ---------------------------------------------------------------------------
// 用户指针（用户自己加的那些图）
//
// 用户指针**也在样式列表里**（id 带 `user:` 前缀），所以别的插件遍历样式列表时
// 自然会看见它们，不必知道"用户指针"这个概念。
//
// 它们从哪来：用户指针目录（Windows `%APPDATA%\GoldingCore\cursors\`）里的一张
// 图片（PNG / JPG / SVG），配一条清单记录（热点）。格式细则见 `docs/CURSOR.md`。
//
// ---------------------------------------------------------------------------
// 生命周期
//
// `styleIdAt` / `styleTitleAt` / `style()` 返回的字符串由提供方持有、活到进程
// 结束；调用方不得释放或改写。`absolutePath` 同理会被拷一份（线程 / 时机都不用
// 调用方操心）。
//
// `setImage` 的**热点**是图案左上角到"真正指着的那一点"的偏移（设计坐标）：
// 箭头要把笔尖放在热点上，图片通常给中心或左上角。`width / height` 为 0 时按
// 40×40 兜底，负值当 0 处理。

#include <sdk/golding_plugin.h>

namespace golding
{

/** 服务名（唯一键）。 */
inline constexpr const char* kCursorServiceName = "golding.cursor";

/**
 * 契约版本。
 *
 * v1：`setStyle` / `setImage` / `setEnabled` / `setEffects` / `setScale` /
 * `pulse` 六件，外加三件只读（`styleCount` / `styleIdAt` / `styleTitleAt`）。
 *
 * v2：**样式列表里含用户指针**（用户自己加的那些图）。
 *   - 列表 = 内置图案 + 用户指针，用户指针的 id 形如 `user:pen`；
 *   - `setStyle("user:pen")` 选中用户指针（等价于在界面上点「用」）；
 *   - `style()` 在用户指针生效时返回那个 `user:` id。
 *
 * 结构体没变（v2 只扩了语义），所以按 v1 写的调用方照常能用。
 */
inline constexpr unsigned int kCursorServiceVersion = 2;

/**用户指针的 id 前缀（`user:<文件名去扩展名>`）。 */
inline constexpr const char* kCursorUserIdPrefix = "user:";

struct CursorService
{
    unsigned int abiVersion;

    // ---- 内置图案 ----

    /**
     * @brief 有多少种图案（**内置 + 用户指针**）。
     *
     * v2 起这个数会随用户指针目录里的文件变；调用方应当**每次用之前重新问**，
     * 不要缓存上一次的答案。
     */
    int (*styleCount)(void* self);

    /**
     * 第 index 种图案的 id。
     *
     * 内置的是短名（`"arrow"`）；用户指针是 `user:<文件名去扩展名>`
     * （`"user:pen"`）。越界返回 nullptr。
     */
    const char* (*styleIdAt)(void* self, int index);

    /**第 index 种图案的显示名（内置是 `"箭头（经典）"`，用户指针是文件名）；越界返回 nullptr。*/
    const char* (*styleTitleAt)(void* self, int index);

    /**
     * @brief 换图案（内置或用户指针）。
     *
     * @param styleId 内置短名（`"arrow"`）或用户指针 id（`"user:pen"`）。
     *
     * @return 0 = 已换；非 0 = 没有这个 id（**当前图案保持不变**，"拼错名字就
     *         变成透明的那层影"比报错难查得多）。
     */
    int (*setStyle)(void* self, const char* styleId);

    /**
     * 当前图案 id：内置短名 / `"user:<名>"` / `"custom"`（`setImage` 那条路）。
     */
    const char* (*style)(void* self);

    // ---- 自定义图片 ----

    /**
     * @brief 换成一张自己的图，并同时切到自定义态。
     *
     * @param absolutePath 绝对路径（UTF-8）。PNG / JPG / SVG 由图片层自行识别。
     * @param width,height 显示尺寸（设计坐标）；<= 0 按 40×40。
     * @param hotX,hotY    热点相对图片左上角的偏移（设计坐标）。
     *
     * @return 0 = 已换；非 0 = 路径为空 / 读不出来。失败时**保持原样**。
     */
    int (*setImage)(void* self, const char* absolutePath, int width, int height, int hotX, int hotY);

    // ---- 状态 ----

    /**
     * @brief 启用 / 停用整层。
     *
     * 关掉的语义是"把系统指针还回来"：那一层不再画，宿主窗口上的原生光标重新
     * 出现。它不是"隐藏自己的图案"——那种状态会让屏幕上**一个指针都没有**。
     */
    int (*setEnabled)(void* self, int enabled);

    /**现在启用着吗（1 = 启用，0 = 用系统指针）。*/
    int (*isEnabled)(void* self);

    /**
     * @brief 按下时给不给反馈（涟漪 / 移动拖尾 / 图案形变）。
     *
     * 分开成一档而不是塞进 `setStyle`：这两个开关是正交的——"要准星但别抖"
     * 是常见组合，而写成八种样式就得为每种都答一遍要不要特效。
     */
    int (*setEffects)(void* self, int effects);

    /**现在有按下特效吗（1 = 有）。*/
    int (*isEffects)(void* self);

    /**整体缩放（0.5 ~ 3.0，超出夹取）；1.0 = 图案自己的默认尺寸。*/
    int (*setScale)(void* self, float scale);

    /**当前缩放。*/
    float (*scale)(void* self);

    /**
     * @brief 立刻放一圈涟漪（**和真按键同一份动画**）。
     *
     * 给"某件事发生了，但用户并没有按下鼠标"用：一个拖拽落地的瞬间、一次自动
     * 保存成功。走同一份代码是为了让"手动炸的"和"按出来的"长得一模一样。
     *
     * @return 0 = 已放；非 0 = 这一层还没挂上（进程还在装配）。
     */
    int (*pulse)(void* self);
};

} // namespace golding
