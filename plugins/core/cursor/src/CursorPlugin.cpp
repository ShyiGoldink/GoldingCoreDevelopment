// ---------------------------------------------------------------------------
// cursor —— 自定义鼠标指针。
//
// 三件事：
//   1. **替换系统指针**：把原生光标藏起来，用一块跟着指针走的画布画自己的图案；
//   2. **对按下做出反应**：按下进度驱动图案形变 + 一圈涟漪 + 移动时的一段拖尾；
//   3. **把它做成可调的东西**：一个设置区块 + 一个服务（`golding.cursor`），
//      于是"要哪种图案"不再写死在本插件里。
//
// 入口只有**设置**一处（`设置 → 鼠标指针`），顶栏没有「指针」项：它属于"设一次
// 就不动"的东西，不该占着顶栏；而设置区块本来就要有（开关 / 尺寸 / 用户指针
// 都在那儿）。别的插件要换图案走服务，命令面板里也有 `cursor.next`。
//
// **选择会被记住**：`%APPDATA%\GoldingCore\cursor.ini`（格式见 `CursorPrefs.h`），
// 启动时读回来。没有它，用户每次重启都要重新挑一遍指针。
//
// ---------------------------------------------------------------------------
// 挂在哪儿、为什么这么挂
//
// 指针层挂**场景**上（不是面板根）：它是唯一不能被任何东西盖住的图层，而面板
// 根内部有自己的层级约定（顶栏 100 / 窗口按钮 200 / 菜单 260 / 遮罩 300 /
// 弹窗 400）。挂在场景上只要自己的 z 够高（900）就行，不必知道那些数。
//
// 层本身是一块 **200×200 的动态画布**，热点恒在画布正中心：这样就不需要"每种
// 图案各算一次偏移"——偏移量只有一个值（画布半宽），图案自己决定往哪边长。
//
// **整层不参与命中**（`SetHitPassThrough(true)`）：它是盖在最上面的东西，一旦
// 吃点击，底下所有按钮都会失灵。这条不是优化，是能不能用的问题。
//
// ---------------------------------------------------------------------------
// 为什么用电平 reacting 而不是"事件 + 动画"
//
// 按下这件事有两路人需要它：图案要**持续**变形（按住不放就一直是瘪的），涟漪要
// **一次性**触发。所以状态里存的是"按下电平"（0..1 的进度），形变每帧读它；
// 涟漪在电平的**上升沿**生成一次，之后各自老化。两者共用同一份 `press`，
// 不会出现"图案弹回来了但涟漪才刚出来"。
//
// 与 spellbook 的取舍一样：时钟来自 `onTick`（动态组件每帧一次），画面来自
// `Invalidate()`（静态画布"脏了才重画"）。特性在这个组合下可以互相独立地开关。
// ---------------------------------------------------------------------------

#include "CursorPrefs.h"
#include "GoldingCommands.h"
#include "GoldingCursor.h"
#include "GoldingServices.h"
#include "GoldingSettings.h"

#include "UserCursors.h"
#include "UserCursorUi.h"

#include <sdk/GoldingPlugin.h>

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <canvas/CanvasContext.h>
#include <canvas/CanvasData.h>
#include <canvas/InkingStaticCanvas.h>
#include <config/window_config.h>
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingLayout.h>
#include <dataStruct/InkingShapeSpec.h>
#include <dataStruct/InkingTextLayout.h>
#include <image/ImageData.h>
#include <image/InkingStaticImage.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/PrefabTheme.h>
#include <scene/InkingScene.h>
#include <text/InkingStaticLabel.h>
#include <text/LabelData.h>
#include <widget/InkingDynamicWidget.h>
#include <widget/InkingWidget.h>
#include <window/InkingWindow.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
// `windows.h` 把 `DrawText` 定义成宏（`DrawTextA` / `DrawTextW`），会打断
// `CanvasContext::DrawText`。这里只用它的 `ShowCursor`，用完立刻撤掉。
#undef DrawText
#endif

namespace
{

namespace theme = inking::prefab::theme;

// ---- 几何 ----

/** 舞台（跟着指针走的那块画布）的边长。要装得下一圈涟漪（最大约 60 半径）。 */
constexpr int kStageSize = 200;
/** 热点在舞台上的位置（恒为正中心）。 */
constexpr float kCenter = static_cast<float>(kStageSize) * 0.5f;

/**
 * 指针层的 z：**必须压过面板内部所有图层**（弹窗 400 是已知的最高一处）。
 * 留到 900 而不是 410，是给中间的层留出生长的余地——z 的实际含义只在兄弟之间
 * 比较，这里与 `root` 是兄弟，所以只要比 `root` 大即可（面板内部的数不用管）。
 */
constexpr int kCursorZ = 900;

// ---- 时间常数（单位：秒 / 每秒）----

constexpr float kPressAttack = 14.0f;   ///< 按下时 `press` 逼近 1 的速率
constexpr float kPressRelease = 7.0f;   ///< 松开时回落速率（比按下慢：要有余韵）
constexpr float kRippleLife = 0.55f;    ///< 一圈涟漪活多久
constexpr float kTrailLife = 0.30f;     ///< 一个拖尾点活多久
constexpr int kTrailMax = 16;           ///< 拖尾最多几个点（再多就是一条线，不是尾）
constexpr float kTrailStep = 6.0f;      ///< 指针移动多远才落一个拖尾点
constexpr float kDemoFramesPerSecond = 60.0f;
/** 演示模式：多久换一种图案（帧）。用帧而不是秒，截图才是确定的。 */
constexpr int kDemoStyleFrames = 40;
/**演示模式：按下持续多少帧、多久一次。 */
constexpr int kDemoPressFrames = 12;
constexpr int kDemoCycleFrames = 70;
/**演示模式：第几帧把自己的设置区块翻出来（要在第一轮换图案之前）。 */
constexpr int kDemoOpenSettingsFrame = 30;
/**演示模式：第几帧换成"用户指针"面板。 */
constexpr int kDemoOpenUserPanelFrame = 70;
/**演示模式：第几帧进入"选热点"那一步。 */
constexpr int kDemoOpenHotspotFrame = 80;
/**演示模式：第几帧切到"选文件"那一步。 */
constexpr int kDemoOpenPickerFrame = 90;
/**演示模式：第几帧关掉弹窗（走服务，等价于点弹窗右上角那颗 X）。 */
constexpr int kDemoClosePopupFrame = 100;
/**演示模式：第几帧重开设置区块。 */
constexpr int kDemoReopenSettingsFrame = 110;
/**演示模式：第几帧点设置里的「用户指针…」（从按钮回调里换弹窗）。 */
constexpr int kDemoReopenUserPanelFrame = 120;
/**演示模式：第几帧点用户面板里的「关闭」。 */
constexpr int kDemoCloseUserPanelFrame = 130;

// ---- 配色 ----
//
// 与 spellbook 同一套取向：深紫的墨 + 米白的纸 + 一点金。指针要在**纯白内容区**
// 上站得住，所以每个图案要么给用户纸色的填 + 墨色的描边，要么干脆是深色的实心。

constexpr std::uint32_t kInk = 0xFF2E2A38u;      ///< 主体深色
constexpr std::uint32_t kPaper = 0xFFFCFBFFu;    ///< 填充（比纯白暖一档）
constexpr std::uint32_t kAccent = 0xFF7C63B8u;   ///< 强调紫
constexpr std::uint32_t kAccentSoft = 0xFFC9B8E8u;
constexpr std::uint32_t kGold = 0xFFD9B45Au;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// ---- 内置图案 ----

enum class CursorShape
{
    Arrow,
    Ring,
    Cross,
    Dot,
    Spark,
    Quill,
};

struct StyleDef
{
    const char* id;
    const char* title;
    CursorShape shape;
};

/**
 * 六选一。**新增一种图案只改这张表 + `PaintShape` 的一个 case**。
 *
 * id 走服务出去，所以它是跨 dll 契约的一部分——改它是破坏性变更，和改字段名
 * 同一档待遇。
 */
const StyleDef kStyles[] = {
    {"arrow", "箭头（经典）", CursorShape::Arrow},
    {"ring", "圆环", CursorShape::Ring},
    {"cross", "准星", CursorShape::Cross},
    {"dot", "圆点", CursorShape::Dot},
    {"spark", "星芒", CursorShape::Spark},
    {"quill", "羽毛笔", CursorShape::Quill},
};

constexpr int kStyleCount = static_cast<int>(sizeof(kStyles) / sizeof(kStyles[0]));

/**自定义图片态的伪 id（`CursorService::style()` 用它回答"现在不是内置档"）。*/
constexpr const char* kCustomStyleId = "custom";

// ---- 状态 ----

struct Ripple
{
    float worldX;
    float worldY;
    float age;
    int kind;  ///< 0 = 主键，1 = 次键（右键给金色）
};

struct TrailPoint
{
    float worldX;
    float worldY;
    float age;
};

struct CursorState
{
    /**指针（世界 / 设计坐标）。*/
    float x = 0.0f;
    float y = 0.0f;
    bool inside = false;

    bool primaryDown = false;
    bool secondaryDown = false;

    /**按下电平：0..1。图案形变与涟漪透明度都读它。*/
    float press = 0.0f;

    /**累计秒数。只给"自己会转"的图案用（星芒）。*/
    double seconds = 0.0;

    std::vector<Ripple> ripples;
    std::vector<TrailPoint> trail;

    bool enabled = true;
    bool effects = true;
    float scale = 1.0f;

    /**当前内置图案下标；`userId` 非空时这一项不代表画出来的东西。*/
    int styleIndex = 0;

    /**
     * 当前用的是哪个**用户指针**（`user:<文件名去扩展名>`）；空 = 用内置图案。
     *
     * 它是"用户指针"的唯一权威：服务里的 `style()` 读它，设置面板的高亮读它，
     * 画的时候也读它（内置图案与用户图片是**互斥**的两条路）。
     */
    std::string userId;

    /**
     * 当前用的是不是"服务直接指定的那张图"（`setImage` 走的那条路）。
     *
     * 它与 `userId` 是**互斥**的两条路：前者不进仓库、不持久化，后者是用户
     * 指针目录里的一张图（能被列出来、能被选中）。
     */
    bool customImage = false;

    /**演示模式：自己喂一份假的指针状态，不读真实输入（也不打扰真实输入）。*/
    bool demo = false;
    int demoFrame = 0;
};

std::shared_ptr<CursorState> g_state;

/**自己的 `Host` 副本：所有 C 回调（`create` / `onActivate` / 服务）都够不到参数。*/
golding::Host g_host{nullptr, nullptr};

/**
 * 设置服务（`golding.settings`，可选）。
 *
 * 平时用不上（用户自己点开设置）；**演示模式**靠它把自己的区块翻出来——
 * `openSection` 是"打开某一块设置"的唯一正路，这里借它走同一条路，而不是
 * 自己再弹一块私有的面板。
 */
const golding::SettingsService* g_settings = nullptr;

/**弹窗服务（`golding.popup`，可选）：用户指针管理面板要一个弹窗来装。*/
const golding::PopupService* g_popup = nullptr;

/**
 * 偏好仓库（`cursor.ini`）：记住用户挑的是哪只指针 / 三个开关。
 *
 * 在 `Ready` 里建。**加载完成之前不许写盘**（`g_prefsReady`）：`Ready` 里的
 * 自检会临时切图案、开关，那些不是用户的选择。
 */
cursor::CursorPrefsStore* g_prefsStore = nullptr;
bool g_prefsReady = false;

/**
 * 用户指针仓库（目录 + 清单）。
 *
 * 它在 `Ready` 里建（那时才知道该用哪个目录），而服务在 `Register` 里就注册了
 * ——所以服务里那几条函数必须能容忍"仓库还没准备好"。
 */
cursor::UserCursorStore* g_userStore = nullptr;

/**当前的用户指针列表（扫目录 + 清单的结果）。*/
std::vector<cursor::UserCursor> g_userCursors;

/**
 * @brief 用户指针的 id / 标题：**只增不删**的稳定存储。
 *
 * 服务返回的字符串按契约要"活到进程结束"，所以不能每次刷新就 `clear()` 一遍
 * （那会让别人手里的指针悬空）。这里用一个 `deque` 攒着，刷新时**复用**已有的
 * 条目、只追加新的。
 */
struct UserStyleSlot
{
    std::string id;
    std::string title;
};
std::deque<UserStyleSlot> g_userStylePool;
/**`g_userCursors` 的每一项在池子里的下标（同下标一一对应）。*/
std::vector<std::size_t> g_userStyleIndex;

/**指针层的三个节点。生命周期跟着界面树（由父节点拥有）。*/
inking::InkingWidget* g_carrier = nullptr;
inking::InkingStaticCanvas* g_stage = nullptr;
inking::InkingStaticImage* g_image = nullptr;

/**
 * 场景指针（非拥有）。
 *
 * 演示 / 自检要"按名字点一下某个按钮"：弹窗里的按钮没有对外的引用，而
 * `InkingScene::Activate(name)` 是语义点击的唯一正路（不绑具体控件类型）。
 */
inking::InkingScene* g_scene = nullptr;



// ---------------------------------------------------------------------------
// 系统指针的显隐
// ---------------------------------------------------------------------------
//
// InkingUIKit 没有暴露"藏起原生光标"这件事（0.5.1 的公共头里连 cursor 这个词
// 都没有），所以这里直接落到 Win32 的 `ShowCursor`：它是个**显示计数**，负数时
// 窗口不画光标，而且 `SetCursor`（SDL 换箭头形状走的是这条路）盖不住它——所以
// 只要没有别人调 `ShowCursor(TRUE)`，这一层是稳的。本插件自己是唯一的调用方。
//
// 计数是进程级的，所以这里必须自己记住"现在藏了没有"，重复调用只会把计数推向
// -2 / -3，而恢复时少一次 `ShowCursor(TRUE)` 光标就永远回不来。

void ApplySystemCursor(bool visible)
{
#ifdef _WIN32
    static bool s_hidden = false;
    if (!visible && !s_hidden)
    {
        ShowCursor(FALSE);
        s_hidden = true;
    }
    else if (visible && s_hidden)
    {
        ShowCursor(TRUE);
        s_hidden = false;
    }
#else
    (void)visible;
#endif
}

/**这层指针画不画：由"启用了吗"和"指针在不在窗口里"共同决定。*/
bool ShouldDraw(const CursorState& state)
{
    if (!state.enabled)
    {
        return false;
    }
    return state.demo || state.inside;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

std::uint32_t WithAlpha(std::uint32_t color, float alpha)
{
    const std::uint32_t digit = static_cast<std::uint32_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (digit << 24) | (color & 0x00FFFFFFu);
}

/**以圆心为中心、外径 `diameter`、环宽 `thickness` 的一圈（`ring` 走 Fill 不走 Stroke）。*/
void StrokeRing(inking::CanvasContext& ctx, float cx, float cy, float diameter, float thickness,
                std::uint32_t color)
{
    const float box = diameter + thickness;  // 环宽是一半在外一半在内
    ctx.FillShape(inking::ShapeSpec::Ring(thickness), cx - box * 0.5f, cy - box * 0.5f, box, box, color);
}

/**经典箭头的轮廓（相对笔尖，`×1` 时宽 22 高 36）。*/
const float kArrowOutline[] = {0.0f, 0.0f, 0.0f, 32.0f, 8.0f,  23.0f, 14.0f,
                               36.0f, 19.0f, 34.0f, 12.0f, 21.0f, 22.0f, 21.0f};

/**羽毛笔的笔尖（相对笔尖的一枚叶形）。*/
const float kNibOutline[] = {0.0f,  0.0f,  7.0f,  12.0f, 10.0f, 30.0f, 4.0f,
                             50.0f, -1.0f, 44.0f, -6.0f, 26.0f, -6.0f, 11.0f};

/**
 * @brief 把一份"相对热点的轮廓"搬到舞台上：缩放 + 旋转 + 平移到热点。
 *
 * 旋转绕**热点**转（而不是绕包围盒中心）：笔尖指着哪儿这件事不能因为按住鼠标
 * 就挪位——按下的形变是"笔压下去"，不是"手歪了"。
 *
 * `out` 至少要有 `count * 2` 个元素；`points` 是 `x0,y0,x1,y1,…`。
 */
void PlaceOutline(float* out, const float* points, int count, float centerX, float centerY, float scale,
                  float rotationDeg)
{
    const float angle = rotationDeg * kDegToRad;
    const float cosA = std::cos(angle);
    const float sinA = std::sin(angle);
    for (int index = 0; index < count; ++index)
    {
        const float x = points[index * 2 + 0] * scale;
        const float y = points[index * 2 + 1] * scale;
        out[index * 2 + 0] = centerX + x * cosA - y * sinA;
        out[index * 2 + 1] = centerY + x * sinA + y * cosA;
    }
}

/**
 * @brief 画某一个内置图案。
 *
 * 每个 case 只做两件事：用 `press` 决定这一瞬间的几何，然后画。画布中心就是
 * 热点，所以这边一个 case 里不该出现"热点偏移"的计算。
 */
void PaintShape(inking::CanvasContext& ctx, const CursorState& state, CursorShape shape)
{
    const float cx = kCenter;
    const float cy = kCenter;
    const float press = state.press;
    const float base = state.scale;
    // 所有图案共用的"按下去就压扁一点"：1 → 0.86。它是唯一一个每种都要的形变，
    // 所以放在这里而不是写六遍——但星芒要转得快、圆点要涨得大，那些例外在各自的
    // case 里叠。
    const float squeeze = 1.0f - 0.14f * press;

    switch (shape)
    {
    case CursorShape::Arrow: {
        const float s = base * squeeze;
        const int count = static_cast<int>(sizeof(kArrowOutline) / sizeof(kArrowOutline[0]) / 2);
        float placed[sizeof(kArrowOutline) / sizeof(kArrowOutline[0])] = {};
        PlaceOutline(placed, kArrowOutline, count, cx, cy, s, 0.0f);

        // 底下垫一层淡紫：白 receptors 白的时候至少有个影子告诉用户"指针在这儿"。
        ctx.FillCircle(cx + 6.0f * s, cy + 8.0f * s, 34.0f * s, WithAlpha(kAccent, 0.10f + 0.16f * press));
        ctx.FillShape(inking::ShapeSpec::PolygonPath(placed, count), 0.0f, 0.0f, ctx.Width(), ctx.Height(),
                      kPaper);
        ctx.StrokeShape(inking::ShapeSpec::PolygonPath(placed, count), 0.0f, 0.0f, ctx.Width(), ctx.Height(),
                        2.2f * std::max(s, 0.6f), WithAlpha(kInk, 0.92f));
        break;
    }

    case CursorShape::Ring: {
        const float diameter = 26.0f * base * squeeze;
        const float thickness = 3.0f * base * (1.0f + 0.8f * press);
        StrokeRing(ctx, cx, cy, diameter, thickness, WithAlpha(kInk, 0.92f));
        ctx.FillCircle(cx, cy, 5.0f * base, WithAlpha(kInk, 0.95f));
        if (press > 0.01f)
        {
            // 按住时中心点张开成一小圈：像"捏住了"。
            StrokeRing(ctx, cx, cy, 13.0f * base, 1.6f * base, WithAlpha(kAccent, 0.65f * press));
        }
        break;
    }

    case CursorShape::Cross: {
        const float inner = (7.0f + 3.0f * press) * base;
        const float outer = (18.0f - 2.0f * press) * base;
        for (int arm = 0; arm < 4; ++arm)
        {
            const float angle = kPi * 0.5f * static_cast<float>(arm);
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            ctx.DrawLine(cx + inner * cosA, cy + inner * sinA, cx + outer * cosA, cy + outer * sinA,
                         2.4f * base, WithAlpha(kInk, 0.90f));
        }
        ctx.FillCircle(cx, cy, 3.0f * base, WithAlpha(kInk, 0.95f));
        if (press > 0.01f)
        {
            StrokeRing(ctx, cx, cy, 44.0f * base, 1.6f * base, WithAlpha(kAccent, 0.55f * press));
        }
        break;
    }

    case CursorShape::Dot: {
        // 圆点是唯一"越按越大"的：它的语义是"我在这里"，按住时宜扩散不宜收缩。
        const float halo = (26.0f + 18.0f * press) * base;
        ctx.FillCircle(cx, cy, halo, WithAlpha(kAccent, 0.16f + 0.20f * press));
        ctx.FillCircle(cx, cy, 12.0f * base * squeeze, WithAlpha(kInk, 0.95f));
        break;
    }

    case CursorShape::Spark: {
        const float spinBase = static_cast<float>(state.seconds) * 24.0f;
        const float spin = spinBase + press * 45.0f;  // 按住多转一点：一次"抖"
        const float size = 34.0f * base * squeeze;
        ctx.FillShape(inking::ShapeSpec::Star(4, 0.34f, spin), cx - size * 0.5f, cy - size * 0.5f, size, size,
                      WithAlpha(kAccent, 0.92f));
        const float arcBox = size * 1.8f;
        ctx.FillShape(inking::ShapeSpec::Arc(2.0f * base, spin, 64.0f), cx - arcBox * 0.5f,
                      cy - arcBox * 0.5f, arcBox, arcBox, WithAlpha(kGold, 0.55f + 0.25f * press));
        ctx.FillShape(inking::ShapeSpec::Arc(2.0f * base, spin + 180.0f, 64.0f), cx - arcBox * 0.5f,
                      cy - arcBox * 0.5f, arcBox, arcBox, WithAlpha(kGold, 0.55f + 0.25f * press));
        ctx.FillCircle(cx, cy, 6.0f * base, kPaper);
        ctx.FillCircle(cx, cy, 2.6f * base, kInk);
        break;
    }

    case CursorShape::Quill: {
        const int count = static_cast<int>(sizeof(kNibOutline) / sizeof(kNibOutline[0]) / 2);
        float placed[sizeof(kNibOutline) / sizeof(kNibOutline[0])] = {};
        // 静态 -28° 是握笔的角度，按住再压下去 12°：和"写下去"是同一件事。
        const float tilt = -28.0f - 12.0f * press;
        const float s = base * squeeze;
        PlaceOutline(placed, kNibOutline, count, cx, cy, s, tilt);

        ctx.FillShape(inking::ShapeSpec::PolygonPath(placed, count), 0.0f, 0.0f, ctx.Width(), ctx.Height(),
                      WithAlpha(kAccent, 0.88f));
        ctx.StrokeShape(inking::ShapeSpec::PolygonPath(placed, count), 0.0f, 0.0f, ctx.Width(), ctx.Height(),
                        1.8f * std::max(s, 0.6f), WithAlpha(kInk, 0.85f));

        // 笔脊：从笔尖到笔肚的一条金线。它是"这是一支笔"的唯一提示，缺了就是一个色块。
        const float angle = tilt * kDegToRad;
        const float tipX = 0.0f;
        const float tipY = 0.0f;
        const float bellyX = 46.0f * s;
        const float bellyY = 2.0f * s;
        ctx.DrawLine(cx + tipX * std::cos(angle) - tipY * std::sin(angle),
                     cy + tipX * std::sin(angle) + tipY * std::cos(angle),
                     cx + bellyX * std::cos(angle) - bellyY * std::sin(angle),
                     cy + bellyX * std::sin(angle) + bellyY * std::cos(angle), 1.4f * base,
                     WithAlpha(kGold, 0.9f));
        ctx.FillCircle(cx, cy, 4.2f * base * (1.0f + 0.25f * press), kGold);
        break;
    }
    }
}

/**整块舞台的内容：图案 + 涟漪 + 拖尾。**绘制器每帧最多被调一次**。*/
void PaintCursor(inking::CanvasContext& ctx, const CursorState& state)
{
    // 拖尾在下、图案在上：它是"刚才在哪儿"，不该挡住"现在在哪儿"。
    if (state.effects)
    {
        for (const TrailPoint& point : state.trail)
        {
            const float remain = 1.0f - point.age / kTrailLife;
            if (remain <= 0.0f)
            {
                continue;
            }
            const float localX = point.worldX - state.x + kCenter;
            const float localY = point.worldY - state.y + kCenter;
            ctx.FillCircle(localX, localY, (2.0f + 5.0f * remain) * state.scale,
                           WithAlpha(kAccentSoft, 0.45f * remain));
        }
    }

    for (const Ripple& ripple : state.ripples)
    {
        const float t = std::clamp(ripple.age / kRippleLife, 0.0f, 1.0f);
        if (t >= 1.0f)
        {
            continue;
        }
        const float localX = ripple.worldX - state.x + kCenter;
        const float localY = ripple.worldY - state.y + kCenter;
        const float radius = (8.0f + 52.0f * t) * state.scale;
        const std::uint32_t color = ripple.kind == 1 ? kGold : kAccent;
        StrokeRing(ctx, localX, localY, radius * 2.0f, (3.4f - 2.2f * t) * state.scale,
                   WithAlpha(color, 0.55f * (1.0f - t)));
    }

    // 内置图案与用户图片是**互斥**的两条路：选了用户指针就不画内置图案
    // （图片是另一个节点，见 `ApplyUserCursor`），涟漪与拖尾两边都画。
    if (state.userId.empty() && !state.customImage)
    {
        PaintShape(ctx, state, kStyles[state.styleIndex].shape);
    }
}

// ---------------------------------------------------------------------------
// 指针层：一块自己跟着指针走的动态画布
// ---------------------------------------------------------------------------

/**把"用户指针"管理面板弹出来（定义在文件后半段：它要的东西多）。*/
void PushUserCursorPopup();

/**演示模式：把要弹的面板排到帧末去（定义在文件后半段）。*/
void ScheduleDemoActions();

/**
 * @brief 在指针当前位置炸一圈；`kind` = 0 主键色，1 次键色（右键给金色）。
 *
 * 队列上限 8：连点会堆积，而涟漪是半透明的，堆到二十层就是一团实心的紫，比漏掉
 * 一圈难看得多。
 */
void SpawnRipple(CursorState& state, int kind)
{
    if (state.ripples.size() >= 8u)
    {
        state.ripples.erase(state.ripples.begin());
    }
    state.ripples.push_back(Ripple{state.x, state.y, 0.0f, kind});
}

/**
 * @brief 指针层的载体。
 *
 * 选**动态档**是为了每帧能改自己的位置（静态档"几何构造即定型"）。代价是它不进
 * 命中表——而那正好是我们想要的（指针不许吃点击），所以这里连
 * `OwnsSubtreeHit()` 都不用声明；子树里的静态画布靠 `SetHitPassThrough` 让路。
 *
 * 它自己不画东西（`onRender` 空实现）：默认实现会按锚点数据里的占位色糊一个
 * 200×200 的实心方块出来。
 */
class CursorCarrier final : public inking::InkingDynamicWidget
{
public:
    CursorCarrier(inking::InkingWidget* parent, std::shared_ptr<CursorState> state)
        : inking::InkingDynamicWidget(parent, MakeAnchor()), _state(std::move(state))
    {
        SetName("cursor.carrier");
        SetHitPassThrough(true);

        inking::CanvasData stage;
        stage.name = "cursor.stage";
        stage.width = kStageSize;
        stage.height = kStageSize;
        stage.color = 0u;  // 本体透明：这块布上只有内容
        stage.shape = inking::ShapeSpec::Rect();
        stage.zIndex = 0;

        auto canvas = std::make_unique<inking::InkingStaticCanvas>(this, stage);
        auto* raw = canvas.get();
        if (AddChild(std::move(canvas)) != nullptr)
        {
            raw->SetHitPassThrough(true);
            raw->SetPainter([this](inking::CanvasContext& ctx) { PaintCursor(ctx, *_state); });
            g_stage = raw;
        }
    }

protected:
    void onTick(float deltaSeconds) override
    {
        if (!_state)
        {
            return;
        }
        CursorState& state = *_state;
        state.seconds += static_cast<double>(deltaSeconds);

        if (state.demo)
        {
            DriveDemo(state);
        }
        else
        {
            inking::InkingScene* scene = GetScene();
            if (scene == nullptr)
            {
                return;
            }
            state.x = scene->GetPointerX();
            state.y = scene->GetPointerY();
            state.inside = scene->IsPointerInside();
            state.primaryDown = scene->IsPointerDown();
            state.secondaryDown = scene->IsSecondaryPointerDown();
        }

        Step(state, deltaSeconds);

        const bool visible = ShouldDraw(state);
        ChangeOffset(state.x - kCenter, state.y - kCenter);
        if (visible != _wasVisible)
        {
            SetVisible(visible);
            _wasVisible = visible;
        }
        if (visible && g_stage != nullptr)
        {
            // 每帧一次就够：内容与位置都变了，而"脏了才重画"是画布的规矩。
            g_stage->Invalidate();
        }
    }

    void onRender(float /*pixelX*/, float /*pixelY*/) const override {}

private:
    static inking::AnchorData MakeAnchor()
    {
        inking::AnchorData data;
        data.width = kStageSize;
        data.height = kStageSize;
        data.zIndex = kCursorZ;
        data.color = 0u;
        return data;
    }

    /**
     * 演示模式（`--cursor-demo`）：自己喂一份假的指针状态。
     *
     * **这里只动数据，不动界面树**：加节点 / 弹窗这类"改树"的动作放在 tick 里
     * 会撞上场景正在遍历绘制列表的时刻（实测会段错误）。演示要弹的那些面板
     * 改用 `RunAfterFrames` 排到帧末（见 `ScheduleDemoActions`）。
     *
     * **刻意不写回场景**（不去调 `SetPointerState`）：那份状态一改，整机的 hover /
     * 点击派发就跟着动，跑一次 demo 会顺手把界面点一圈。演示只要"看起来像有人
     * 在用"，不需要真的有输入。
     *
     * 全部按**帧**推进而不是按秒：截图要能复现（同一帧号 = 同一张图）。
     */
    void DriveDemo(CursorState& state)
    {
        // 第一帧：窗口已经开着了，这时候排的帧末任务才会被执行。
        if (state.demoFrame == 0)
        {
            ScheduleDemoActions();
        }

        const float width = static_cast<float>(inking::DesignWidth());
        const float height = static_cast<float>(inking::DesignHeight());
        const float t = static_cast<float>(state.demoFrame) / kDemoFramesPerSecond;

        state.x = width * (0.5f + 0.28f * std::sin(t * 1.25));
        state.y = height * (0.5f + 0.24f * std::sin(t * 1.90 + 0.7));
        state.inside = true;
        state.primaryDown = (state.demoFrame % kDemoCycleFrames) < kDemoPressFrames;
        state.secondaryDown = false;

        // 每 40 帧换一种图案：一次 demo 能把六种都走一遍。
        if (state.demoFrame % kDemoStyleFrames == 0)
        {
            state.styleIndex = state.demoFrame / kDemoStyleFrames % kStyleCount;
            state.userId.clear();
            state.customImage = false;
            if (g_image != nullptr)
            {
                g_image->SetVisible(false);
            }
        }
        ++state.demoFrame;
    }

    /**推进动画：电平、涟漪、拖尾。位置在这里已经是最终值。*/
    void Step(CursorState& state, float deltaSeconds)
    {
        const bool anyDown = state.primaryDown || state.secondaryDown;
        const float target = anyDown ? 1.0f : 0.0f;
        const float rate = anyDown ? kPressAttack : kPressRelease;
        state.press += (target - state.press) * std::min(1.0f, rate * deltaSeconds);
        if (std::fabs(target - state.press) < 0.002f)
        {
            state.press = target;  // 收敛：留一个永远到不了头的尾巴没有意义
        }

        // 上升沿 = 一次涟漪。松开不给（松开是"收"，不是"放")。
        if (state.primaryDown && !_wasPrimary)
        {
            SpawnRipple(state, 0);
        }
        if (state.secondaryDown && !_wasSecondary)
        {
            SpawnRipple(state, 1);
        }
        _wasPrimary = state.primaryDown;
        _wasSecondary = state.secondaryDown;

        const float step = std::max(deltaSeconds, 0.0f);
        std::vector<Ripple> liveRipples;
        liveRipples.reserve(state.ripples.size());
        for (Ripple& ripple : state.ripples)
        {
            ripple.age += step;
            if (ripple.age < kRippleLife)
            {
                liveRipples.push_back(ripple);
            }
        }
        state.ripples.swap(liveRipples);

        std::vector<TrailPoint> liveTrail;
        liveTrail.reserve(state.trail.size() + 1);
        for (TrailPoint& point : state.trail)
        {
            point.age += step;
            if (point.age < kTrailLife)
            {
                liveTrail.push_back(point);
            }
        }
        state.trail.swap(liveTrail);

        if (!state.effects)
        {
            return;
        }
        // 拖尾：走够一段距离才落一个点，否则原地悬停也会堆出一团。
        const bool moved = std::fabs(state.x - _lastTrailX) + std::fabs(state.y - _lastTrailY) >= kTrailStep;
        if (moved)
        {
            state.trail.push_back(TrailPoint{state.x, state.y, 0.0f});
            if (static_cast<int>(state.trail.size()) > kTrailMax)
            {
                state.trail.erase(state.trail.begin());
            }
            _lastTrailX = state.x;
            _lastTrailY = state.y;
        }
    }

    std::shared_ptr<CursorState> _state;
    bool _wasPrimary = false;
    bool _wasSecondary = false;
    bool _wasVisible = true;
    float _lastTrailX = -1.0e9f;
    float _lastTrailY = -1.0e9f;
};

// ---------------------------------------------------------------------------
// 动作：所有入口（设置面板 / 命令 / 服务）最后都落到这里
// ---------------------------------------------------------------------------

/**
 * 把当前选择写进偏好文件。
 *
 * 前置声明：`SelectStyle` / `ApplyUserCursor` / `SetEnabled` 都要在改动之后调它，
 * 而"把状态收成偏好"要读它们改的那几个字段——定义放在这一段的末尾，
 * 顺序就是"先改状态、再记账"。
 */
void SavePrefs();

/**当前指针的名字（内置图案名 / 用户指针显示名 / 自定义图片）。*/
std::string CurrentPointerName()
{
    if (g_state == nullptr)
    {
        return "（无）";
    }
    if (g_state->customImage)
    {
        return "自定义图片";
    }
    if (!g_state->userId.empty())
    {
        for (const cursor::UserCursor& item : g_userCursors)
        {
            if (item.id == g_state->userId)
            {
                return item.displayName;
            }
        }
        return g_state->userId;
    }
    return kStyles[g_state->styleIndex].title;
}

/**换成第 index 种内置图案（同时退出用户指针态）。*/
void SelectStyle(int index)
{
    if (!g_state || index < 0 || index >= kStyleCount)
    {
        return;
    }
    g_state->styleIndex = index;
    g_state->userId.clear();
    g_state->customImage = false;
    if (g_image != nullptr)
    {
        g_image->SetVisible(false);
    }
    g_host.Info((std::string("cursor：指针图案 → ") + kStyles[index].title).c_str());
    SavePrefs(); // 用户挑了它，就得记住（服务调用也算——别的插件换的也是"当前选择"）
}

/**
 * @brief 换成一张**用户指针**（自己的图）。
 *
 * 做法：把图片节点重建成"这张图"（静态图片几何构造即定型，换图只能重建），
 * 并让它的**热点**落在指针位置——偏移 = 舞台中心 − 热点 × 缩放。
 *
 * @return false = 这一层还没挂上（进程还在装配）。
 */
bool ApplyUserCursor(const cursor::UserCursor& item)
{
    if (!g_state || g_carrier == nullptr)
    {
        return false;
    }
    if (g_image != nullptr)
    {
        g_carrier->RemoveChild(g_image);
        g_image = nullptr;
    }

    const float scale = g_state->scale > 0.0f ? g_state->scale : 1.0f;
    const int width = std::max(1, static_cast<int>(std::lround(item.width * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(item.height * scale)));

    inking::ImageData data;
    data.name = "cursor.image";
    data.width = width;
    data.height = height;
    data.runtimeAsset = true;  // `source` 是绝对路径，运行期读
    data.hitPassThrough = true;
    data.normal.type = inking::AppearanceType::Image;
    data.normal.source = item.absolutePath;
    data.shape = inking::ShapeSpec::Rect();
    // 热点对齐：图的左上角要落在（舞台中心 − 热点）处，指针那一点才是热点。
    data.offsetX = kCenter - static_cast<float>(item.hotX) * scale;
    data.offsetY = kCenter - static_cast<float>(item.hotY) * scale;
    data.zIndex = 1;

    auto node = std::make_unique<inking::InkingStaticImage>(g_carrier, data);
    auto* raw = node.get();
    if (g_carrier->AddChild(std::move(node)) == nullptr)
    {
        return false;
    }
    g_image = raw;
    g_state->userId = item.id;
    g_state->customImage = false;
    g_host.Info((std::string("cursor：指针图案 → 用户指针 ") + item.displayName).c_str());
    SavePrefs();
    return true;
}

/**
 * @brief 重扫用户指针目录，刷新服务里的样式列表。
 *
 * 清单里有的、目录里没有的（文件被删了）直接消失；当前用的那只如果没了，
 * 退回内置图案——**不留一个"指向不存在的文件"的当前指针**。
 */
void RefreshUserCursorList()
{
    if (g_userStore == nullptr)
    {
        return;
    }
    g_userCursors = g_userStore->Scan();
    g_userStyleIndex.clear();
    g_userStyleIndex.reserve(g_userCursors.size());
    for (const cursor::UserCursor& item : g_userCursors)
    {
        std::size_t slot = g_userStylePool.size();
        for (std::size_t index = 0; index < g_userStylePool.size(); ++index)
        {
            if (g_userStylePool[index].id == item.id && g_userStylePool[index].title == item.displayName)
            {
                slot = index;
                break;
            }
        }
        if (slot == g_userStylePool.size())
        {
            g_userStylePool.push_back(UserStyleSlot{item.id, item.displayName});
        }
        g_userStyleIndex.push_back(slot);
    }

    if (g_state != nullptr && !g_state->userId.empty())
    {
        bool stillThere = false;
        for (const cursor::UserCursor& item : g_userCursors)
        {
            if (item.id == g_state->userId)
            {
                stillThere = true;
                break;
            }
        }
        if (!stillThere)
        {
            g_state->userId.clear();
            if (g_image != nullptr)
            {
                g_image->SetVisible(false);
            }
            // 那只图被删了：偏好也不能继续指着它，顺手写回默认。
            SavePrefs();
        }
    }
}

/**命令面板那条 `cursor.next`：换下一个内置图案。*/
void CycleStyle()
{
    if (!g_state)
    {
        return;
    }
    SelectStyle((g_state->styleIndex + 1) % kStyleCount);
}

void SetEnabled(bool enabled)
{
    if (!g_state)
    {
        return;
    }
    g_state->enabled = enabled;
    ApplySystemCursor(!enabled);
    SavePrefs();
}

void SetEffects(bool effects)
{
    if (!g_state)
    {
        return;
    }
    g_state->effects = effects;
    if (!effects)
    {
        g_state->trail.clear();
    }
    SavePrefs();
}

// ---- 偏好：记住用户挑的那一只 ----
//
// 定义放在这里（而不是动作区开头）：`CapturePrefs` 要读 `SelectStyle` /
// `SetEnabled` 改的那几个字段，`ApplyPrefs` 又要调它们——两头都在这一段之上，
// 所以只让"写盘"这一个动作走前置声明（见动作区开头）。

/**
 * @brief 把当前状态收成一份偏好。
 *
 * `setImage` 换的那张图（`customImage`）**不进**偏好：它不在用户指针目录里，
 * 契约上就不持久化（`docs/CURSOR.md` §7）——记住一个随时可能被删掉的绝对路径，
 * 下次启动只会得到一个画不出来的指针。
 */
cursor::CursorPrefs CapturePrefs()
{
    cursor::CursorPrefs prefs;
    if (g_state == nullptr)
    {
        return prefs;
    }
    prefs.enabled = g_state->enabled;
    prefs.effects = g_state->effects;
    prefs.scale = g_state->scale;
    if (g_state->customImage)
    {
        prefs.styleId.clear();
    }
    else if (!g_state->userId.empty())
    {
        prefs.styleId = g_state->userId;
    }
    else
    {
        prefs.styleId = kStyles[g_state->styleIndex].id;
    }
    return prefs;
}

/**
 * @brief 写盘（用户真的改了什么之后调）。
 *
 * 三个"不写"：
 *   - 偏好还没加载完（`g_prefsReady` 还假着）——自检会临时切图案，那不是选择；
 *   - 演示模式（`--cursor-demo` 一路换图案），它会覆盖用户的真实配置；
 *   - 没有仓库（`Ready` 之前）。
 */
void SavePrefs()
{
    if (!g_prefsReady || g_prefsStore == nullptr || g_state == nullptr || g_state->demo)
    {
        return;
    }
    std::string error;
    if (!g_prefsStore->Save(CapturePrefs(), &error))
    {
        g_host.Warn(("cursor：记不住这次选择，" + error).c_str());
    }
}

/**
 * @brief 启动时把偏好装上去。
 *
 * 用户指针的 id 要拿**当前扫描结果**去对：清单里那只图可能已经被删了。对不上
 * 就留在出厂默认（打一句说明），而不是留一个"指向不存在的文件"的当前指针。
 */
void ApplyPrefs(const cursor::CursorPrefs& prefs)
{
    if (g_state == nullptr)
    {
        return;
    }
    SetEnabled(prefs.enabled);
    SetEffects(prefs.effects);
    g_state->scale = std::clamp(prefs.scale, 0.5f, 3.0f);

    if (prefs.styleId.empty())
    {
        return;
    }
    for (int index = 0; index < kStyleCount; ++index)
    {
        if (prefs.styleId == kStyles[index].id)
        {
            SelectStyle(index);
            return;
        }
    }
    for (const cursor::UserCursor& item : g_userCursors)
    {
        if (item.id == prefs.styleId)
        {
            if (ApplyUserCursor(item))
            {
                return;
            }
            break;
        }
    }
    g_host.Warn(("cursor：偏好里的指针已经不在（" + prefs.styleId + "），用默认图案").c_str());
}

// ---------------------------------------------------------------------------
// 用户指针管理弹窗
// ---------------------------------------------------------------------------

/**列表变了（导入 / 删除 / 改热点）之后回调：服务里的样式列表要跟着刷新。*/
void OnUserCursorsChanged(void* /*context*/)
{
    RefreshUserCursorList();
}

/**弹窗内容：用户指针列表 + 导入 + 选热点。*/
GoldingWidgetHandle CreateUserCursorPanelThunk(void* /*hostContext*/, void* parent)
{
    if (g_userStore == nullptr)
    {
        return nullptr;
    }
    return cursor::CreateUserCursorPanel(static_cast<inking::InkingWidget*>(parent), g_userStore, &ApplyUserCursor,
                                         &OnUserCursorsChanged, nullptr, g_popup);
}

/**
 * @brief 把"用户指针"管理面板弹出来。
 *
 * 它是**另一个弹窗**，不是设置区块的一部分：那块内容（列表 + 文件选择器 +
 * 预览）比单区块弹窗大得多，而 `golding.popup` 同一时刻只有一层——所以用户
 * 在设置里点「用户指针…」时，这一层会替换掉设置弹窗（设置弹窗的内容也就
 * 看不到了，但设置本身还在，下次打开还在）。
 *
 * 注意这个 push 发生在**设置区块那颗按钮的回调栈**上：面板会先把旧弹窗摘下来、
 * 帧末再销毁（不能当场删掉正在执行回调的按钮）。那是面板侧的责任，这里只负责
 * 把声明交出去——声明放在静态区，双保险（旧版面板会把指针存下来）。
 */
void PushUserCursorPopup()
{
    if (g_popup == nullptr)
    {
        g_host.Warn("cursor：没有 golding.popup，用户指针面板开不了");
        return;
    }
    // 尺寸与 `UserCursorUi.cpp` 的 kPanelWidth / kPanelHeight 一致。
    static const golding::GoldingPopupDecl kUserCursorPopup{"cursor.userpopup", 760, 560, 0, 0, 1,
                                                            &CreateUserCursorPanelThunk, nullptr};
    if (g_popup->push(nullptr, &kUserCursorPopup) != 0)
    {
        g_host.Warn("cursor：用户指针面板弹不出来");
    }
}

// ---------------------------------------------------------------------------
// 设置区块：让用户自己挑
// ---------------------------------------------------------------------------

inking::LabelData MakeLabel(const char* name, const std::string& text, float fontSize, std::uint32_t color,
                            int width, int height, int x = 0, int y = 0)
{
    inking::LabelData label;
    label.name = name;
    label.width = width;
    label.height = height;
    label.offsetX = static_cast<float>(x);
    label.offsetY = static_cast<float>(y);
    label.text = text;
    label.color = color;
    label.style.fontSize = fontSize;
    label.style.alignV = inking::TextAlignV::Middle;
    label.maxLines = 1;
    label.overflow = inking::TextOverflow::Ellipsis;
    return label;
}

inking::ButtonData MakeRow(int width, int height)
{
    inking::ButtonData data;
    data.width = width;
    data.height = height;
    data.normal.type = inking::AppearanceType::Color;
    data.normal.color = theme::kSurfaceAlt;
    data.normal.themeColor = "drawerSurface";
    data.hover.type = inking::AppearanceType::Color;
    data.hover.color = theme::kHoverNeutral;
    data.onclicked.type = inking::AppearanceType::Color;
    data.onclicked.color = theme::kAccentSoft;
    data.hoverInheritsNormal = false;
    data.onclickedInheritsNormal = false;
    data.textColor = theme::kTextPrimary;
    data.text.fontSize = 15.0f;
    data.text.align = inking::TextAlignH::Left;
    data.text.alignV = inking::TextAlignV::Middle;
    data.text.leftSpace = 12.0f;
    data.shape = inking::ShapeSpec::RoundedRect(theme::kRadiusItem);
    return data;
}

/**
 * 设置区块的内容。
 *
 * 一律**单行 + 省略号**：`Stack` 不会因为"文字换行变高了"而重排兄弟（几何没变），
 * 多行文字会把下一段压上来（spellbook 的详情那一段有同一条注释）。
 */
GoldingWidgetHandle CreateCursorSettings(void* /*hostContext*/, void* parent)
{
    auto* surface = static_cast<inking::InkingWidget*>(parent);
    if (surface == nullptr || !g_state)
    {
        return nullptr;
    }

    const int width = surface->GetWidth();
    const int height = surface->GetHeight();
    if (width <= 0 || height <= 0)
    {
        return nullptr;
    }

    inking::PanelData root;
    root.name = "cursor.settings";
    root.width = width;
    root.height = height;
    root.startOpen = true;
    root.closed.color = theme::kSurface;
    root.closed.themeColor = "panelSurface";
    root.open = root.closed;
    root.shape = inking::ShapeSpec::Rect();
    root.layout.kind = inking::LayoutKind::Stack;
    root.layout.axis = inking::LayoutAxis::Vertical;
    root.layout.gap = 6.0f;
    root.layout.padding = inking::LayoutSpacing{20.0f, 20.0f, 20.0f, 20.0f};

    auto* panel = static_cast<inking::InkingStaticPanel*>(
        surface->AddChild(std::make_unique<inking::InkingStaticPanel>(surface, root)));
    if (panel == nullptr)
    {
        return nullptr;
    }

    const int textWidth = width - 40;
    // 一行说明（不再重复区块标题：settings 插件单区块弹窗的标题就是它）。
    panel->AddChild(std::make_unique<inking::InkingStaticLabel>(
        panel, MakeLabel("cursor.settings.hint", "指针画在最上层；按下会有涟漪与形变。", 13.0f,
                         theme::kTextMuted, textWidth, 22)));

    /**
     * 一个横向排布的容器行。图案有六种，竖排一行一个会把单区块弹窗（420 高）
     * 撑爆——settings 插件渲染区块的面板**不滚动**，超出去就是被裁掉。
     */
    auto makeRow = [panel, textWidth](int height) {
        inking::PanelData row;
        row.name = "cursor.settings.row";
        row.width = textWidth;
        row.height = height;
        row.startOpen = true;
        // 行底取**弹窗同色**（白）：行只是排列的容器，可它自己也要画底色——
        // 透明会触发面板的"配错了"兜底（品红），一行两端各露一条。
        row.closed.color = theme::kSurface;
        row.closed.themeColor = "panelSurface";
        row.open = row.closed;
        row.shape = inking::ShapeSpec::Rect();
        row.layout.kind = inking::LayoutKind::Stack;
        row.layout.axis = inking::LayoutAxis::Horizontal;
        row.layout.gap = 6.0f;
        auto* raw = static_cast<inking::InkingStaticPanel*>(
            panel->AddChild(std::make_unique<inking::InkingStaticPanel>(panel, row)));
        return raw;
    };

    // ---- 图案：两列三行 ----
    //
    // 这一段的按钮**归这一段自己管**（不进插件的全局量）：设置区块的内容属于
    // 弹窗，弹窗被替换或关闭时那些按钮会被销毁，而插件还拿着它们的指针——
    // 下一次刷新就会踩到已释放的内存（实测间歇性段错误）。
    auto styleButtons = std::make_shared<std::vector<inking::InkingStaticButton*>>();
    auto currentLabel = std::make_shared<inking::InkingStaticLabel*>(nullptr);

    // 刷新"当前是哪一个"：由**这一段自己的点击**触发（外部改了指针，那一行
    // 下次打开设置时自然是新的——它每次打开都是重新建的）。
    auto refresh = [styleButtons, currentLabel] {
        for (std::size_t index = 0; index < styleButtons->size(); ++index)
        {
            inking::InkingStaticButton* button = (*styleButtons)[index];
            if (button == nullptr || index >= static_cast<std::size_t>(kStyleCount))
            {
                continue;
            }
            const bool current = g_state != nullptr && g_state->userId.empty() && !g_state->customImage &&
                                 g_state->styleIndex == static_cast<int>(index);
            button->SetText((current ? "● " : "○ ") + std::string(kStyles[index].title));
        }
        if (*currentLabel != nullptr)
        {
            (*currentLabel)->SetText("当前：" + CurrentPointerName());
        }
    };

    inking::InkingStaticPanel* styleRow = nullptr;
    for (int index = 0; index < kStyleCount; ++index)
    {
        if (index % 2 == 0)
        {
            styleRow = makeRow(28);
        }
        if (styleRow == nullptr)
        {
            continue;
        }

        inking::ButtonData look = MakeRow((textWidth - 6) / 2, 28);
        look.name = std::string("cursor.settings.style.") + kStyles[index].id;
        look.text.fontSize = 13.0f;
        auto* button = static_cast<inking::InkingStaticButton*>(
            styleRow->AddChild(std::make_unique<inking::InkingStaticButton>(styleRow, look)));
        if (button == nullptr)
        {
            continue;
        }
        styleButtons->push_back(button);
        button->SetOnClicked([index, refresh] {
            SelectStyle(index);
            refresh();
        });
    }
    refresh();

    // ---- 开关 ----
    inking::ButtonData enableLook = MakeRow(textWidth, 30);
    enableLook.name = "cursor.settings.enabled";
    auto* enableButton = static_cast<inking::InkingStaticButton*>(
        panel->AddChild(std::make_unique<inking::InkingStaticButton>(panel, enableLook)));
    if (enableButton != nullptr)
    {
        const auto syncEnable = [enableButton] {
            const bool on = g_state != nullptr && g_state->enabled;
            enableButton->SetText(on ? "启用自定义指针：开" : "启用自定义指针：关（用系统指针）");
        };
        syncEnable();
        enableButton->SetOnClicked([syncEnable] {
            SetEnabled(!(g_state != nullptr && g_state->enabled));
            syncEnable();
        });
    }

    inking::ButtonData effectsLook = MakeRow(textWidth, 30);
    effectsLook.name = "cursor.settings.effects";
    auto* effectsButton = static_cast<inking::InkingStaticButton*>(
        panel->AddChild(std::make_unique<inking::InkingStaticButton>(panel, effectsLook)));
    if (effectsButton != nullptr)
    {
        const auto syncEffects = [effectsButton] {
            const bool on = g_state != nullptr && g_state->effects;
            effectsButton->SetText(on ? "按下特效（涟漪 / 拖尾）：开" : "按下特效（涟漪 / 拖尾）：关");
        };
        syncEffects();
        effectsButton->SetOnClicked([syncEffects] {
            SetEffects(!(g_state != nullptr && g_state->effects));
            syncEffects();
        });
    }

    // ---- 用户指针：入口 + 当前是哪一只 ----
    //
    // 「用户指针…」开的是**另一个弹窗**（列表 + 导入 + 选热点）——那块内容比
    // 这个区块大得多，塞在这里会把单区块弹窗撑爆（settings 插件的面板不滚动）。
    {
        inking::InkingStaticPanel* userRow = makeRow(30);
        if (userRow != nullptr)
        {
            inking::ButtonData userLook = MakeRow(150, 28);
            userLook.name = "cursor.settings.user";
            userLook.label = "用户指针…";
            userLook.text.align = inking::TextAlignH::Center;
            userLook.text.leftSpace = 0.0f;
            auto* userButton = static_cast<inking::InkingStaticButton*>(
                userRow->AddChild(std::make_unique<inking::InkingStaticButton>(userRow, userLook)));
            if (userButton != nullptr)
            {
                userButton->SetOnClicked([] { PushUserCursorPopup(); });
            }

            // 标签的偏移给 0：它是**行内布局**排的（按钮后面那一格），
            // 自己再给一个绝对偏移会被叠加一次（布局排的位置 + 我给的位置）。
            *currentLabel = static_cast<inking::InkingStaticLabel*>(
                userRow->AddChild(std::make_unique<inking::InkingStaticLabel>(
                    userRow, MakeLabel("cursor.settings.current", "当前：", 13.0f, theme::kTextMuted, textWidth - 160,
                                        28, 0, 0))));
        }
    }

    // ---- 尺寸：三档而不是滑块 ----
    //
    // 滑块是"连续"的语义，而指针尺寸只有"小 / 中 / 大"三种需求；真正连续地调，
    // 人会在 0.97 和 1.03 之间来回试半天。
    inking::InkingStaticPanel* sizeRow = makeRow(28);
    inking::ButtonData sizeLook = MakeRow((textWidth - 12) / 3, 28);
    const char* sizeNames[3] = {"小", "中", "大"};
    const float sizeValues[3] = {0.75f, 1.0f, 1.4f};
    for (int step = 0; step < 3 && sizeRow != nullptr; ++step)
    {
        inking::ButtonData look = sizeLook;
        look.name = std::string("cursor.settings.size.") + std::to_string(step);
        look.label = sizeNames[step];
        look.text.align = inking::TextAlignH::Center;
        look.text.leftSpace = 0.0f;
        auto* button = static_cast<inking::InkingStaticButton*>(
            sizeRow->AddChild(std::make_unique<inking::InkingStaticButton>(sizeRow, look)));
        if (button == nullptr)
        {
            continue;
        }
        const float value = sizeValues[step];
        button->SetOnClicked([value] {
            if (g_state != nullptr)
            {
                g_state->scale = value;
                SavePrefs(); // 尺寸也是"选过就记住"的一项
            }
        });
    }

    // 刷新一次"当前指针"那行：它是在上面才建出来的（刷新要等它存在才改得到字）。
    refresh();

    panel->Relayout();
    return panel;
}

const GoldingSettingsDecl kSettingsSection = {
    "cursor",       // id：别的插件可以用 `golding.settings` 的 openSection 开到这一块
    "鼠标指针",      // 设置菜单里显示的名字
    30,             // 排在别人的后面一点
    &CreateCursorSettings,
    nullptr,        // 没有"点一下直接做事"的形态：这块就是要给人挑的
};

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------
//
// **只有设置里一处**：顶栏不再有「指针」。指针样式是"设一次就不动"的东西，
// 顶栏该留给真正高频的动作；而设置区块本来就得有（开关 / 尺寸 / 用户指针都在
// 那儿），再在顶栏放一份入口就是同一件事两处维护。
//
// 仍然保留的非界面入口：`golding.cursor` 服务（别的插件换图案）与命令面板里的
// `cursor.next`（`Ctrl+K` 搜"指针"）。命令面板是可选的，缺席时一切照常。

// ---------------------------------------------------------------------------
// 服务（`golding.cursor`）
// ---------------------------------------------------------------------------
//
// **样式列表 = 内置图案 + 用户指针**（用户指针的 id 带 `user:` 前缀）。
// 这样别的插件不用知道"用户指针"是什么：它照样遍历样式列表、照样按 id 选。

int cursorStyleCount(void* /*self*/)
{
    return kStyleCount + static_cast<int>(g_userCursors.size());
}

const char* cursorStyleIdAt(void* /*self*/, int index)
{
    if (index < 0)
    {
        return nullptr;
    }
    if (index < kStyleCount)
    {
        return kStyles[index].id;
    }
    const std::size_t user = static_cast<std::size_t>(index - kStyleCount);
    if (user >= g_userStyleIndex.size())
    {
        return nullptr;
    }
    return g_userStylePool[g_userStyleIndex[user]].id.c_str();
}

const char* cursorStyleTitleAt(void* /*self*/, int index)
{
    if (index < 0)
    {
        return nullptr;
    }
    if (index < kStyleCount)
    {
        return kStyles[index].title;
    }
    const std::size_t user = static_cast<std::size_t>(index - kStyleCount);
    if (user >= g_userStyleIndex.size())
    {
        return nullptr;
    }
    return g_userStylePool[g_userStyleIndex[user]].title.c_str();
}

int cursorSetStyle(void* /*self*/, const char* styleId)
{
    if (styleId == nullptr)
    {
        return 1;
    }
    const std::string wanted(styleId);
    for (int index = 0; index < kStyleCount; ++index)
    {
        if (std::string(kStyles[index].id) == wanted)
        {
            SelectStyle(index);
            return 0;
        }
    }
    // 用户指针：id 形如 `user:pen`。
    for (const cursor::UserCursor& item : g_userCursors)
    {
        if (item.id == wanted)
        {
            return ApplyUserCursor(item) ? 0 : 1;
        }
    }
    return 1;  // 没有这个名字：保持原样，别把屏幕上的指针变没
}

const char* cursorStyle(void* /*self*/)
{
    if (g_state == nullptr)
    {
        return kStyles[0].id;
    }
    if (!g_state->userId.empty())
    {
        return g_state->userId.c_str();
    }
    if (g_state->customImage)
    {
        return kCustomStyleId;
    }
    return kStyles[g_state->styleIndex].id;
}

/**
 * @brief 换成一张自己的图（**不进仓库**的那条路）。
 *
 * 与用户指针目录里的图的区别：这张图**不登记、不持久化**，只是这一次换上去。
 * 想让它出现在"用户指针"列表里，走导入流程（设置面板 / `--cursor-import=`）。
 *
 * 图片节点**按需创建**：默认的六档都是画出来的，为一个八成用不上的功能在建树时
 * 就去读文件（还可能读不着）不划算。换图不需要保留上一张，直接摘掉重建——
 * 静态图片"几何构造即定型"，改尺寸也要重建，两条路殊途同归。
 */
int cursorSetImage(void* /*self*/, const char* absolutePath, int width, int height, int hotX, int hotY)
{
    if (!g_state || g_carrier == nullptr || absolutePath == nullptr || absolutePath[0] == '\0')
    {
        return 1;
    }

    const int imageWidth = width > 0 ? width : 40;
    const int imageHeight = height > 0 ? height : 40;

    if (g_image != nullptr)
    {
        g_carrier->RemoveChild(g_image);
        g_image = nullptr;
    }

    inking::ImageData data;
    data.name = "cursor.image";
    data.width = imageWidth;
    data.height = imageHeight;
    data.runtimeAsset = true;  // `source` 是绝对路径，运行期读
    data.hitPassThrough = true;
    data.normal.type = inking::AppearanceType::Image;
    data.normal.source = absolutePath;
    data.shape = inking::ShapeSpec::Rect();
    data.offsetX = kCenter - static_cast<float>(hotX);
    data.offsetY = kCenter - static_cast<float>(hotY);
    data.zIndex = 1;

    auto node = std::make_unique<inking::InkingStaticImage>(g_carrier, data);
    auto* raw = node.get();
    if (g_carrier->AddChild(std::move(node)) == nullptr)
    {
        return 1;
    }

    g_image = raw;
    g_state->customImage = true;
    g_state->userId.clear();
    g_host.Info((std::string("cursor：指针图案 → 自定义图片 ") + absolutePath).c_str());
    // 这张图不持久化（见 `CapturePrefs`），但"当前选择"变了：把偏好写回默认，
    // 否则文件里还留着上一个内置图案，重启后突然换成它——那不是用户刚才选的。
    SavePrefs();
    return 0;
}

int cursorSetEnabled(void* /*self*/, int enabled)
{
    SetEnabled(enabled != 0);
    return 0;
}

int cursorIsEnabled(void* /*self*/) { return (g_state != nullptr && g_state->enabled) ? 1 : 0; }

int cursorSetEffects(void* /*self*/, int effects)
{
    SetEffects(effects != 0);
    return 0;
}

int cursorIsEffects(void* /*self*/) { return (g_state != nullptr && g_state->effects) ? 1 : 0; }

int cursorSetScale(void* /*self*/, float scale)
{
    if (g_state == nullptr)
    {
        return 1;
    }
    g_state->scale = std::clamp(scale, 0.5f, 3.0f);
    SavePrefs();
    return 0;
}

float cursorScale(void* /*self*/) { return g_state != nullptr ? g_state->scale : 1.0f; }

int cursorPulse(void* /*self*/)
{
    if (!g_state || g_carrier == nullptr)
    {
        return 1;
    }
    SpawnRipple(*g_state, 0);
    return 0;
}

const golding::CursorService kCursorService{
    golding::kCursorServiceVersion,
    &cursorStyleCount,
    &cursorStyleIdAt,
    &cursorStyleTitleAt,
    &cursorSetStyle,
    &cursorStyle,
    &cursorSetImage,
    &cursorSetEnabled,
    &cursorIsEnabled,
    &cursorSetEffects,
    &cursorIsEffects,
    &cursorSetScale,
    &cursorScale,
    &cursorPulse,
};

// ---- 命令面板（`golding.commands`，可选）----

void OnCommandNextStyle(void* /*hostContext*/) { CycleStyle(); }

const golding::CommandDecl kCommandNext{
    "cursor.next",
    "切换鼠标指针图案",
    "cursor shubiao zhizhen pointer",
    &OnCommandNextStyle,
};

void RegisterCommand(const golding::Host& host)
{
    // 与 spellbook / settings 同一条：**不写 `needs`**，命令面板不在时照常工作。
    const golding::CommandService* commands =
        host.Use<golding::CommandService>(golding::kCommandsServiceName, golding::kCommandsServiceVersion);
    if (commands == nullptr)
    {
        host.Warn("没有 golding.commands（命令面板缺席），cursor.next 不注册");
        return;
    }
    if (commands->add(nullptr, &kCommandNext) != 0)
    {
        host.Warn("cursor.next 命令注册被驳回（id 已占用？）");
    }
}

// ---------------------------------------------------------------------------
// 装配后：挂层 + 自检
// ---------------------------------------------------------------------------

int PrintCheck(const char* label, bool ok, const std::string& detail)
{
    std::printf("[%s] cursor.%-22s %s\n", ok ? "ok" : "FAIL", label, detail.c_str());
    return ok ? 0 : 1;
}

/**
 * 把指针层挂到**场景**上。
 *
 * 为什么是场景而不是 `uiRoot`：那一层本插件之外的任何人都管不着，而面板根是面板
 * 插件自己的地盘（它随时可能重排 / 裁剪孩子）。挂在它的**父级**（场景）上，两边
 * 的几何承诺互不相干。
 */
int AttachLayer(const golding::Host& host)
{
    void* uiRootRaw = host.UiRoot();
    auto* uiRoot = static_cast<inking::InkingWidget*>(uiRootRaw);
    if (uiRoot == nullptr)
    {
        host.Warn("cursor：宿主还没有界面根，指针层挂不上去");
        return 1;
    }
    inking::InkingScene* scene = uiRoot->GetScene();
    if (scene == nullptr)
    {
        host.Warn("cursor：界面根不在任何场景里，指针层挂不上去");
        return 1;
    }

    g_state = std::make_shared<CursorState>();
    // 演示模式的意思只有本插件知道（`bootMode` 是宿主原样交出来的串）。
    g_state->demo = std::string(host.BootMode()) == "--cursor-demo";

    g_scene = scene;
    auto carrier = std::make_unique<CursorCarrier>(scene, g_state);
    auto* raw = carrier.get();
    if (scene->AddChild(std::move(carrier)) == nullptr)
    {
        g_state.reset();
        return 1;
    }
    g_carrier = raw;

    SetEnabled(g_state->enabled);
    return 0;
}

/**
 * @brief 命令行导入：`--cursor-import=<路径>`。
 *
 * 为什么要这条：界面上的导入要人点（弹窗 + 选文件 + 点热点），**无头环境里
 * 点不了**——没有它，导入这条路径就没有可自动验证的入口。而"能不能被验证"
 * 是本项目的一条纪律。
 *
 * 走的是**同一条**导入实现（`UserCursorStore::Import`），不是另写一份。
 */
void MaybeImportFromBootMode(const golding::Host& host)
{
    const std::string mode = host.BootMode();
    const std::string prefix = "--cursor-import=";
    if (mode.rfind(prefix, 0) != 0)
    {
        return;
    }
    const std::string path = mode.substr(prefix.size());
    if (path.empty())
    {
        host.Warn("cursor：--cursor-import 后面没有路径");
        return;
    }
    if (g_userStore == nullptr)
    {
        host.Warn("cursor：用户指针仓库还没准备好");
        return;
    }
    cursor::UserCursor imported;
    std::string error;
    if (!g_userStore->Import(cursor::FromUtf8(path), &imported, &error))
    {
        host.Warn(("cursor：导入失败 " + error).c_str());
        return;
    }
    host.Info(("cursor：已导入用户指针 " + imported.fileName + "（" + std::to_string(imported.width) + "×" +
               std::to_string(imported.height) + "）").c_str());
    RefreshUserCursorList();
    ApplyUserCursor(imported);
}

/**
 * @brief 演示模式：把要弹的面板排到**帧末**去。
 *
 * 为什么不在 tick 里弹：tick 正在遍历场景的绘制列表，这时往树上加节点会让
 * 那次遍历拿到失效的指针（实测段错误）。`RunAfterFrames` 的回调落在"帧末"
 * ——场景脏消费之后、提交之前——那时改树是安全的。
 *
 * 为什么是"第一帧"才排，而不是在 `Ready` 里排：`Open()` 会清掉待办的任务队列，
 * 而 `Ready` 跑在 `Open()` **之前**——排在那儿的任务一个都不会执行（实测：
 * 排了四个动作，跑一百帧一个也没弹出来）。
 */
void ScheduleDemoActions()
{
    inking::InkingWindow& window = inking::InkingWindow::Instance();
    // 设置区块：走服务那条路（`openSection`），而不是再弹一块私有面板。
    window.RunAfterFrames(kDemoOpenSettingsFrame,
                          [] {
                              if (g_settings != nullptr)
                              {
                                  g_settings->openSection(nullptr, "cursor");
                              }
                          });
    // 用户指针面板（同一时刻只有一层弹窗，所以它会替换掉设置弹窗）。
    window.RunAfterFrames(kDemoOpenUserPanelFrame, [] { PushUserCursorPopup(); });
    // 选热点那一步：界面上点「热点」按钮走的是同一条路。
    window.RunAfterFrames(kDemoOpenHotspotFrame, [] { cursor::DemoOpenHotspotEditor(); });
    // 选文件那一步：界面上点「导入图片…」走的是同一条路。
    window.RunAfterFrames(kDemoOpenPickerFrame, [] { cursor::DemoShowFilePicker(); });

    // ---- 关闭 / 重开 ----
    //
    // 这一段覆盖的是"弹窗被替换 / 关闭"这条路：用户报的"关掉用户指针面板就闪退"
    // 就发生在这里。两次关闭分别点两条不同的路：
    //   - 直接调 `pop`（设置面板里「关闭」那颗按钮走的就是它）；
    //   - 按名字点「关闭」/「用户指针…」按钮（真实点击，会走进按钮自己的回调栈）。
    window.RunAfterFrames(kDemoClosePopupFrame, [] {
        if (g_popup != nullptr)
        {
            g_popup->pop(nullptr);
        }
    });
    window.RunAfterFrames(kDemoReopenSettingsFrame, [] {
        if (g_settings != nullptr)
        {
            g_settings->openSection(nullptr, "cursor");
        }
    });
    // 从**设置区块的按钮**里 push：旧弹窗（连同这颗按钮）在回调栈上被替换。
    window.RunAfterFrames(kDemoReopenUserPanelFrame, [] {
        if (g_scene != nullptr)
        {
            g_scene->Activate("cursor.settings.user");
        }
    });
    window.RunAfterFrames(kDemoCloseUserPanelFrame, [] {
        if (g_scene != nullptr)
        {
            g_scene->Activate("cursor.user.close");
        }
    });
}

/**
 * @brief 偏好的往返自检（写 → 读 → 比）。
 *
 * 用**进程临时目录**，绝不碰用户真实的 `cursor.ini`：自检会写文件，写到用户目录
 * 就成了"跑一次测试改一次用户的指针"。
 *
 * 除了往返，还钉两条解析纪律：未知键 / 坏行跳过（`futureKey`、没有分隔符那行），
 * 越界值钳住（`scale 99` → 3）。
 */
int CheckPrefsRoundTrip()
{
    int failures = 0;
    std::error_code code;
    // 临时目录拿不到（极少数受限环境）就退到工作目录：自检宁可写在当前目录，
    // 也不该用一个会抛异常的 `temp_directory_path()` 把整个 `Ready` 带走。
    std::filesystem::path directory = std::filesystem::temp_directory_path(code);
    if (code || directory.empty())
    {
        directory = std::filesystem::current_path(code);
    }
    directory /= "golding-cursor-prefs-test";
    std::filesystem::create_directories(directory, code);
    cursor::CursorPrefsStore store(directory / cursor::FromUtf8(cursor::kPrefsFileName));

    cursor::CursorPrefs wanted;
    wanted.styleId = "user:pen";
    wanted.enabled = false;
    wanted.effects = false;
    wanted.scale = 0.75f;

    std::string error;
    const bool saved = store.Save(wanted, &error);
    cursor::CursorPrefs readBack;
    const bool loaded = saved && store.Load(&readBack, &error);
    const bool same = loaded && readBack.styleId == wanted.styleId && readBack.enabled == wanted.enabled &&
                      readBack.effects == wanted.effects && std::fabs(readBack.scale - wanted.scale) < 0.001f;
    failures += PrintCheck("偏好文件可写可读", saved && loaded && same,
                           saved && loaded ? (wanted.styleId + " / 关 / 关 / 0.75") : error);

    const cursor::CursorPrefs parsed = cursor::ParsePrefs(
        "# 注释\nstyle\tcross\nfutureKey\t1\n坏行没有分隔符\nenabled\tmaybe\neffects\t0\nscale\t99\n");
    failures += PrintCheck("偏好解析：坏行跳过、越界钳住",
                           parsed.styleId == "cross" && parsed.enabled && !parsed.effects &&
                               std::fabs(parsed.scale - 3.0f) < 0.001f,
                           "style=" + parsed.styleId + " enabled=" + std::to_string(parsed.enabled ? 1 : 0) +
                               " effects=" + std::to_string(parsed.effects ? 1 : 0) + " scale=" +
                               std::to_string(parsed.scale));

    // 加载链路（`ApplyPrefs`）也验一次：改状态 → 应用 → 断言 → 还原。
    // 这时 `g_prefsReady` 还是假的，所以它不会写用户文件。
    const cursor::CursorPrefs backup = CapturePrefs();
    cursor::CursorPrefs applied;
    applied.styleId = "cross";
    applied.enabled = false;
    applied.effects = false;
    applied.scale = 0.75f;
    ApplyPrefs(applied);
    const bool appliedOk = g_state != nullptr && std::string(cursorStyle(nullptr)) == "cross" &&
                           !g_state->enabled && !g_state->effects && std::fabs(g_state->scale - 0.75f) < 0.001f;
    failures += PrintCheck("偏好能被应用", appliedOk,
                           std::string("当前=") + (cursorStyle(nullptr) != nullptr ? cursorStyle(nullptr) : "?") +
                               " enabled=" + std::to_string(g_state != nullptr && g_state->enabled ? 1 : 0));
    ApplyPrefs(backup);

    std::filesystem::remove_all(directory, code);
    return failures;
}

int RunReady(const golding::Host& host)
{
    int failures = 0;

    // 演示模式要借它把自己的设置区块翻出来；拿不到就少这一段（其余照常）。
    g_settings = host.Use<golding::SettingsService>(golding::kSettingsServiceName, 1);

    // 用户指针仓库：目录在 `Ready` 里才建（那时才知道用哪个目录）。
    g_userStore = new cursor::UserCursorStore(cursor::UserCursorStore::DefaultDirectory());
    RefreshUserCursorList();

    // 偏好仓库的位置现在就定下来，但**读取放在自检之后**：自检会临时换图案 / 开关，
    // 那些不是"上次的选择"，不能拿来当启动状态。
    g_prefsStore = new cursor::CursorPrefsStore(cursor::CursorPrefsStore::DefaultFile());

    failures += AttachLayer(host);
    failures += PrintCheck("指针层已挂到场景", g_carrier != nullptr,
                           g_carrier != nullptr ? "cursor.carrier" : std::string("挂不上"));

    failures += PrintCheck("舞台尺寸 = 200×200", g_stage != nullptr && g_stage->GetWidth() == kStageSize &&
                                                    g_stage->GetHeight() == kStageSize,
                           g_stage != nullptr ? std::to_string(g_stage->GetWidth()) + "x" +
                                                    std::to_string(g_stage->GetHeight())
                                              : std::string("没有舞台"));

    failures += PrintCheck("整层不参与命中",
                           g_carrier != nullptr && g_carrier->IsHitPassThrough() &&
                               (g_stage == nullptr || g_stage->IsHitPassThrough()),
                           "carrier + stage");

    failures += PrintCheck("样式列表（内置 + 用户）", cursorStyleCount(nullptr) >= kStyleCount,
                           std::to_string(cursorStyleCount(nullptr)) + " 种（内置 " +
                               std::to_string(kStyleCount) + "）");

    failures += PrintCheck("用户指针目录可用",
                           g_userStore != nullptr && g_userStore->EnsureDirectory(nullptr),
                           g_userStore != nullptr ? g_userStore->directoryText() : std::string("没有仓库"));

    // 服务自举：换一次再换回来，验证"按名字换"这条通路是通的（这是其它插件唯一
    // 能用的入口，崩在它自己手里最容易被当成"别人的错"）。
    const char* before = cursorStyle(nullptr);
    const int swapped = cursorSetStyle(nullptr, "cross");
    const int restored = cursorSetStyle(nullptr, before);
    failures += PrintCheck("服务可切换图案", swapped == 0 && restored == 0,
                           std::string(before != nullptr ? before : "?") + " → cross → 还原");

    failures += CheckPrefsRoundTrip();

    // ---- 把上次的选择装回来 ----
    //
    // 放在自检之后、`g_prefsReady = true` 之前：从这里往后用户在界面上做的选择
    // 才写盘（自检里的临时改动不算）。
    if (g_prefsStore != nullptr)
    {
        cursor::CursorPrefs prefs;
        std::string error;
        if (g_prefsStore->Load(&prefs, &error))
        {
            ApplyPrefs(prefs);
        }
        else
        {
            host.Warn(("cursor：读不了偏好，" + error).c_str());
        }
    }
    g_prefsReady = true;
    failures += PrintCheck("偏好文件可用", g_prefsStore != nullptr,
                           g_prefsStore != nullptr ? g_prefsStore->pathText() : std::string("没有仓库"));

    MaybeImportFromBootMode(host);
    return failures;
}

class CursorPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        g_host = host;  // 回调不在这个调用栈上，必须存一份

        // 两条注册不要短路：少注册一条会静默丢一个入口，日志里看不出走到哪一步。
        //
        // 这里**没有** `AddRegionItem`：顶栏不再有「指针」。入口只在设置里
        // （`AddSettingsSection`）；服务是给别的插件用的，不是界面入口。
        bool ok = host.AddSettingsSection(kSettingsSection);
        ok = host.AddService(golding::kCursorServiceName, &kCursorService) && ok;

        // 弹窗服务：用户指针管理面板要一个弹窗来装。没有它，设置里的
        // 「用户指针…」就点不开（其余功能不受影响）。
        g_popup = host.Use<golding::PopupService>(golding::kPopupServiceName, golding::kPopupServiceVersion);
        if (g_popup == nullptr)
        {
            host.Warn("没有 golding.popup：用户指针管理面板开不了");
        }

        RegisterCommand(host);
        return ok;
    }

    int Ready(const golding::Host& host) override { return RunReady(host); }
};

} // namespace

GOLDING_EXPORT_PLUGIN(CursorPlugin)
