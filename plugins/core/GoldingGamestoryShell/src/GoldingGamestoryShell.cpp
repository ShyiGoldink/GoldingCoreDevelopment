#include "GoldingMenu.h"
#include "GoldingServices.h"

#include <sdk/GoldingPlugin.h>

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <config/window_config.h>
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingDecoration.h>
#include <dataStruct/InkingEasing.h>
#include <dataStruct/InkingTextLayout.h>
#include <input/Shortcut.h>
#include <panel/InkingFixedPopup.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/MenuBar.h>
#include <prefab/MenuBarData.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>
#include <scene/InkingScene.h>
#include <text/InkingStaticLabel.h>
#include <widget/InkingDynamicWidget.h>
#include <window/InkingWindow.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// GoldingGamestoryShell —— 面板插件（`role: "panel"`），**无插件依赖**。
//
// 它提供四样东西，全部是"别人来填"的，自己不带任何业务：
//
//   1. `menubar` 区域 —— 顶栏，任意插件往里放一项；
//   2. `sidebar` 区域 —— 左侧栏，任意插件往里放一项；
//   3. 右侧内容区 —— 页面（`GoldingPageDecl`）装在这里，靠 `golding.navigation` 切；
//   4. `golding.popup` 服务 —— 任意插件弹一块自己的内容（对话框）。
//
// **core 不认识 menubar / sidebar**：这两个名字是本插件自己定的约定。换一个面板
// 插件，可注册位置的集合就整个换掉，宿主 exe 一个字节不动。
//
// 侧栏**不自动列页面**：页面是"内容"，侧栏是"入口"。哪个页面需要一个入口，
// 由那个页面插件往 `sidebar` 注册一项（只填 `onActivate` 也行，点了调导航服务）。
// 这条分界是为了守住"一个事实一个来源"——页面表管"有哪些页"，区域表管"哪儿有入口"。
//
// 布局用 0.4.0 的 A6/A4：交叉轴 `Stretch` 是默认值，所以侧栏条目、页面根都会
// 自动拉满所在容器；不再有 1224 / 1080 这类写死的宽度。
// ---------------------------------------------------------------------------

namespace
{

namespace theme = inking::prefab::theme;

using inking::ButtonData;
using inking::InkingStaticButton;
using inking::InkingStaticLabel;
using inking::InkingStaticPanel;
using inking::PanelData;

/** 顶栏高度（设计坐标）。MenuBar 的初始尺寸，也是"可见时占多少"。 */
constexpr int kBarHeight = 44;
/** 侧栏宽度。固定，不做折叠——折叠要先定义"哪些项在折叠态还看得见"，那是产品问题。 */
constexpr int kSidebarWidth = 220;
/** 抽屉滑一次的时长（秒）。160ms：看得出是"滑进去的"，又不至于让人等。 */
constexpr float kSidebarAnimSeconds = 0.16f;
/** 扫页面表的上限，纯粹防呆。 */
constexpr int kPageScanLimit = 64;

/** 宽度取整：`Resize` 只收整数，而动画中间值必须是浮点（按整数累加会走形）。 */
int roundToPixel(float value)
{
    return static_cast<int>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

/**
 * @brief 侧栏那一条：**必须是动态档**。
 *
 * 收拉要改宽度，而 `Resize` 只挂在 `InkingDynamicWidget` 上——静态面板"几何构造即定型"。
 * 也试过更省事的"隐藏侧栏 + 让主体重排"，实测**布局求解器不跳过不可见项**：
 * 侧栏仍然占着 220，内容区不吃满（自检当场报出来）。
 *
 * 两条纪律（老 shell 踩过，写在这里免得下一个人重踩）：
 *  - **不要**声明 `OwnsSubtreeHit()`：声明之后子孙不进命中表，侧栏条目点不到；
 *  - `HitTest` 要自己答"点在不在我这块矩形上"，否则这块容器在命中上是个透明洞。
 *
 * 宽度就是全部状态：展开 = `kSidebarWidth`、收起 = 0，中间值由动画算。**不再**
 * `SetVisible(false)`——宽度 0 已经不画也不命中，而一隐藏它就从绘制列表里消失，
 * 自己再也收不到 Tick，展开动画根本没法从 0 起步。
 */
class SideLayer final : public inking::InkingDynamicWidget
{
public:
    SideLayer(inking::InkingWidget* parent, const inking::AnchorData& data, std::uint32_t color)
        : inking::InkingDynamicWidget(parent, data), _color(color), _width(static_cast<float>(data.width))
    {
        _from = _width;
        _to = _width;
    }

    bool HitTest(float worldX, float worldY) const override
    {
        const float x = GetAbsX();
        const float y = GetAbsY();
        return worldX >= x && worldX < x + static_cast<float>(GetWidth()) && worldY >= y &&
               worldY < y + static_cast<float>(GetHeight());
    }

    /** 抽屉里的一切都剪在自己这一块之内：动画中途条目不许溢到内容区上。 */
    bool HasSubtreeClip() const noexcept override { return true; }

    /**
     * 往 `targetWidth` 滑过去（从**当前**宽度起算）。
     *
     * 目标跟"正在去的目标"相同就什么都不做——连点两下不该把动画重置成"从头滑"。
     * 中途反向（展开途中又点收起）则由新目标重新起一段，起点就是当下宽度。
     */
    void AnimateTo(float targetWidth)
    {
        if (targetWidth < 0.0f)
        {
            targetWidth = 0.0f;
        }
        if (targetWidth == _to)
        {
            return;
        }
        _from = _width;
        _to = targetWidth;
        _elapsed = 0.0f;
        _animating = true;
    }

    /** 不走动画，直接落到 `targetWidth`（装配期定初始形态用）。 */
    void SnapTo(float targetWidth)
    {
        if (targetWidth < 0.0f)
        {
            targetWidth = 0.0f;
        }
        _width = targetWidth;
        _from = targetWidth;
        _to = targetWidth;
        _elapsed = kSidebarAnimSeconds;
        _animating = false;
        Resize(roundToPixel(targetWidth), GetHeight());
    }

    /**
     * 推进一步。@return 宽度这一步真的变了（调用方要据此重排父级）。
     *
     * 时间是从**外部**喂进来的（`BodyLayer::onTick` 的固定逻辑步），本类不自己
     * 记时钟：组件里存一个"动画起点"再按墙钟算，会在暂停 / 切场景后突然跳一大段。
     */
    bool AdvanceAnimation(float deltaSeconds)
    {
        if (!_animating)
        {
            return false;
        }
        if (deltaSeconds > 0.0f)
        {
            _elapsed += deltaSeconds;
        }
        float progress = _elapsed / kSidebarAnimSeconds;
        if (progress >= 1.0f)
        {
            progress = 1.0f;
        }
        // 缓动只改进度：`EaseOut` 让抽屉"窜出来、再稳住"，收起时同样先快后慢。
        const float eased = inking::ApplyEasing(inking::Easing::EaseOut, progress);
        _width = progress >= 1.0f ? _to : _from + (_to - _from) * eased;
        if (progress >= 1.0f)
        {
            _animating = false;
        }
        Resize(roundToPixel(_width), GetHeight());
        return true;
    }

    /** 现在展开到多宽（可能落在动画中间）。自检与观测口。 */
    float CurrentWidth() const noexcept { return _width; }

    /** 还在滑吗。 */
    bool IsAnimating() const noexcept { return _animating; }

protected:
    void onRender(float pixelX, float pixelY) const override
    {
        const float magnification = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
        inking::detail::fillShape(inking::detail::currentRenderer(), inking::ShapeSpec::Rect(), GetWidth(),
                                  GetHeight(), magnification, pixelX, pixelY, inking::TransformSpec{}, _color);
    }

private:
    std::uint32_t _color = 0xFFFFFFFFu;
    /** 当前宽度（浮点）。几何取整交给 `roundToPixel`，这里保留精度。 */
    float _width = 0.0f;
    float _from = 0.0f;
    float _to = 0.0f;
    float _elapsed = 0.0f;
    bool _animating = false;
};

/**
 * @brief 右侧内容区：**由主体按"剩下的宽度"直接摆**，不再是 Stack 里的一格。
 *
 * 为什么不用横向 Stack（`fillMain`）分配宽度：求解器的 `fillMain` 只在**剩余空间为正**
 * 时才生效（`InkingLayoutSolver.md`：内容超出就按 Start 排、**不挤不缩**）。而每个孩子
 * 的"期望主轴尺寸"默认就是它**当前**的宽度——内容区一旦被撑宽过，它的期望值就和
 * 容器一样大，剩余空间算成负数，于是侧栏再展开时**宽度缩不回去**（实测：侧栏 220、
 * 内容区仍是 1920，页面被推到画布外 220px）。
 *
 * 所以宽度归 `BodyLayer::SyncGeometry()` 算：`内容宽 = 主体宽 − 侧栏当前宽`，
 * 两个都是浮点动画的即时值。本类只管"我多大、我剪子孙、我画我的底"。
 *
 * 为什么要 `HasSubtreeClip()`：页面装进来之后是**整块**的（背景铺满舞台、按钮贴右下角），
 * 它会比内容区宽——溢出必须先剪掉，否则"内容区右边界"就只是一句话，
 * 侧栏一展开就能看到页面从内容区底下露出来（这正是"两者不分开"的观感来源）。
 */
class ContentLayer final : public inking::InkingDynamicWidget
{
public:
    ContentLayer(inking::InkingWidget* parent, const inking::AnchorData& data, std::uint32_t color)
        : inking::InkingDynamicWidget(parent, data), _color(color)
    {
    }

    bool HitTest(float worldX, float worldY) const override
    {
        const float x = GetAbsX();
        const float y = GetAbsY();
        return worldX >= x && worldX < x + static_cast<float>(GetWidth()) && worldY >= y &&
               worldY < y + static_cast<float>(GetHeight());
    }

    /** 页面超出内容区的部分一律剪掉（见类注释）。 */
    bool HasSubtreeClip() const noexcept override { return true; }

protected:
    void onRender(float pixelX, float pixelY) const override
    {
        const float magnification = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
        inking::detail::fillShape(inking::detail::currentRenderer(), inking::ShapeSpec::Rect(), GetWidth(),
                                  GetHeight(), magnification, pixelX, pixelY, inking::TransformSpec{}, _color);
    }

private:
    std::uint32_t _color = 0xFFFFFFFFu;
};

/**
 * @brief 顶栏以下的那一条：白底 + **手摆的「侧栏 | 内容区」两块**，动态档。
 *
 * 为什么非得是动态档：顶栏进出全屏时"它占多高"在 `kBarHeight` 与 0 之间跳，
 * **全屏下浏览器条消失那一刻，body 的 y 与高必须一起改**——而 `MenuBar` 是静态
 * 面板（`GetCurrentHeight()` 只回答"现在画面上还剩多少"，自己改不了尺寸），
 * 布局求解器又不跳过不可见项，所以只能让 body 自己按探测到的顶栏高度重摆。
 *
 * 为什么不用横向 Stack 排这两块：`fillMain` 只增不减（详见 `ContentLayer` 的类注释），
 * 侧栏收展时内容区的宽度会卡在"上一次的宽度"上。所以两块几何都由 `SyncGeometry()` 写。
 *
 * 它顺带当侧栏抽屉动画的**节拍器**：本类永远有宽有高，一定在绘制列表里，
 * 而收起后的侧栏宽度是 0，靠它自己收不到动态档的 Tick。
 */
class BodyLayer final : public inking::InkingDynamicWidget
{
public:
    BodyLayer(inking::InkingWidget* parent, const inking::AnchorData& data, std::uint32_t color)
        : inking::InkingDynamicWidget(parent, data), _color(color)
    {
    }

    bool HitTest(float worldX, float worldY) const override
    {
        const float x = GetAbsX();
        const float y = GetAbsY();
        return worldX >= x && worldX < x + static_cast<float>(GetWidth()) && worldY >= y &&
               worldY < y + static_cast<float>(GetHeight());
    }

    void SetSidebar(SideLayer* sidebar) noexcept { _sidebar = sidebar; }

    /** 内容区（右侧那块）。几何由本类算，见 `SyncGeometry`。 */
    void SetContent(ContentLayer* content) noexcept { _content = content; }

    /**
     * 把"侧栏占多宽、剩下全给内容区"落到几何上。**幂等**：没变就一个脏都不标。
     *
     * 三件事一起做，因为它们必须同时成立才叫"分开"：
     *   1. 侧栏高度跟住主体（顶栏进出全屏时会变）；
     *   2. 内容区的左边界 = 侧栏当前宽、宽度 = 主体宽 − 侧栏当前宽；
     *   3. 内容区重排一次 → 页面根按交叉轴拉满，页面里的**锚点**再自己跟到新边界。
     *
     * 第 3 步是必需的：布局求解器只管"它排的直接孩子"，页面根被拉伸之后，
     * 页面**自己**那层绝对坐标/锚点不会重新算（库的规矩：框架不做隐式重排）。
     * 锚点位置是每次现算的（`InkingAnchor::GetX`），所以调一次 `Relayout` 就够。
     *
     * @return 这一步真的改了谁的几何。
     */
    bool SyncGeometry()
    {
        if (_sidebar == nullptr || _content == nullptr)
        {
            return false;
        }
        const int bodyHeight = GetHeight();
        const int sideWidth = _sidebar->GetWidth() > 0 ? _sidebar->GetWidth() : 0;
        const int contentWidth = GetWidth() - sideWidth > 0 ? GetWidth() - sideWidth : 0;

        bool changed = false;
        changed = _sidebar->Resize(sideWidth, bodyHeight) || changed;
        changed = _content->ChangeOffset(static_cast<float>(sideWidth), 0.0f) || changed;
        changed = _content->Resize(contentWidth, bodyHeight) || changed;
        if (changed)
        {
            _content->Relayout();
        }
        return changed;
    }

    /**
     * 顶栏现在占多高（`-1` = 问不出来，这帧别动）。
     *
     * 用回调而不是存 `MenuBar*`：顶栏与主体是同一次装配里造出来的兄弟，谁先建
     * 谁后建不该影响这里；回调由装配方在两边都齐了之后才装上。
     */
    void SetBarHeightProbe(std::function<int()> probe) { _probe = std::move(probe); }

    /** 贴到顶栏下面，高度吃掉剩下的整块，再重排（内容区自动跟着长）。 */
    void FollowBarHeight(int barHeight)
    {
        if (barHeight < 0)
        {
            return;
        }
        inking::InkingWidget* parent = GetParent();
        if (parent == nullptr)
        {
            return;
        }
        ChangeOffset(0.0f, static_cast<float>(barHeight));
        Resize(parent->GetWidth(), parent->GetHeight() - barHeight);
        SyncGeometry();
    }

    /**
     * 每拍开头调一次：清"上一帧摘下来的旧节点"。
     *
     * 由装配方装上（见 `createShell`）。**不用 `RunAfterFrames`**：那条路在
     * `--frames`（CI / 截图）模式下根本不派发，节点会一直躺在 graveyard 里，
     * 直到进程退出时随静态 `g_parts` 一起析构——而那时场景已经销毁，
     * `~InkingWidget` 里的 `RemoveDrawNode` 就是一次 use-after-free
     * （实测：退出码 0xC0000005）。
     */
    void SetDeferredSweep(std::function<void()> sweep) { _sweep = std::move(sweep); }

    /** 上一次摆过的顶栏高度（`-2` = 还没摆过，任何 >= 0 的值都会触发第一次）。 */
    int LastBarHeight() const noexcept { return _lastBarHeight; }

protected:
    void onTick(float deltaSeconds) override
    {
        // 先清上一拍摘下来的旧节点（弹窗 / 重建的条目）。此时一定不在任何点击
        // 回调栈上，销毁它们是安全的。
        if (_sweep)
        {
            _sweep();
        }
        if (_probe)
        {
            const int barHeight = _probe();
            if (barHeight >= 0 && barHeight != _lastBarHeight)
            {
                _lastBarHeight = barHeight;
                FollowBarHeight(barHeight);
            }
        }
        if (_sidebar != nullptr && _sidebar->AdvanceAnimation(deltaSeconds))
        {
            // 侧栏宽度变了 → 内容区的左边界与宽度都跟着变（页面在这一步重排）。
            SyncGeometry();
        }
    }

    void onRender(float pixelX, float pixelY) const override
    {
        const float magnification = GetMagnification() > 0.0f ? GetMagnification() : 1.0f;
        inking::detail::fillShape(inking::detail::currentRenderer(), inking::ShapeSpec::Rect(), GetWidth(),
                                  GetHeight(), magnification, pixelX, pixelY, inking::TransformSpec{}, _color);
    }

private:
    std::uint32_t _color = 0xFFFFFFFFu;
    SideLayer* _sidebar = nullptr;
    ContentLayer* _content = nullptr;
    std::function<int()> _probe{};
    /** 清 graveyard 的钩子（由装配方装上，见 `SetDeferredSweep`）。 */
    std::function<void()> _sweep{};
    int _lastBarHeight = -2;
};

/**
 * 面板的宿主入口。
 *
 * **文件级**而不是插件成员：面板的 C 回调（`create` / 弹窗服务 / `Ready`）都不在
 * `Register` 的调用栈上，拿不到那个参数；区域项更是别人点时才进来。
 * `golding::Host` 可以按值拷贝存着（内部只有两个指针），这是 SDK 明确支持的用法。
 */
golding::Host g_host{nullptr, nullptr};

/** 一次创建出来的各部分。生命周期跟着界面树走（节点由父节点拥有）。 */
struct ShellParts
{
    void* hostContext = nullptr;
    InkingStaticPanel* root = nullptr;
    inking::prefab::MenuBar* bar = nullptr;
    /** 顶栏以下的那一条：「侧栏 + 内容区」由它按自己的宽高摆（动态档，见 `BodyLayer`）。 */
    BodyLayer* body = nullptr;
    SideLayer* sidebar = nullptr;
    /** 右侧内容区：几何由 `body->SyncGeometry()` 摆，不再是 Stack 里的一格。 */
    ContentLayer* content = nullptr;
    /** 侧栏现在是展开的吗。收起 = 宽度归 0（动画走完），内容区自动吃满。 */
    bool sidebarVisible = true;
    /** 内容区当前挂着的那一页；没有页面时是 nullptr。 */
    inking::InkingWidget* currentPage = nullptr;
    /** 弹窗遮罩：常驻，弹的时候设可见。 */
    InkingStaticButton* backdrop = nullptr;
    /** 当前那块弹窗内容；每 push 一次重建一次（静态面板不能改尺寸）。 */
    InkingStaticPanel* popup = nullptr;
    /**
     * 当前弹窗的声明——**值拷贝**，不是调用方给的指针。
     *
     * 契约只保证"声明在 `push` 调用期间有效"（`GoldingPopupDecl` 头文件里那条），
     * 而调用方在自己函数里写 `const GoldingPopupDecl decl{...}` 是最自然的写法。
     * 存指针的话，`pop` 那一刻读到的就是那个栈帧被覆盖之后的内存：关闭弹窗时
     * "随机"闪退，且崩在 `onClose` 上，看起来像面板的错（实测过）。
     */
    std::optional<golding::GoldingPopupDecl> popupDecl{};
    /** `popupDecl->id` 指向的文本：指向调用方栈上的字符串同样会悬空。 */
    std::string popupDeclId{};
    bool popupOpen = false;
    /**
     * 一帧内摘下来、**还没销毁**的旧节点。
     *
     * `push` 可能在某个按钮的点击回调栈上被调用（设置里的「用户指针…」就是这样：
     * 点它要求面板换成另一块弹窗）。当场销毁旧弹窗 = 把那个正在执行回调的按钮
     * 一起删掉，回调返回后就是 use-after-free。所以先摘下来，帧末（输入派发
     * 结束、这一帧也画完之后）再销毁。
     */
    std::vector<std::unique_ptr<inking::InkingWidget>> graveyard{};
    /**
     * 顶栏区域项 id → 它自己那个节点。
     *
     * 菜单服务拿到的锚点是"区域项 id"而不是坐标（见 `GoldingMenu.h`），
     * 所以这里必须留下指针。只收顶栏——侧栏项眼下没有菜单入口。
     */
    std::map<std::string, inking::InkingWidget*> barItems;
};

std::shared_ptr<ShellParts> g_parts;

// ---- 数据工厂 ----

PanelData makePanel(const char* name, int width, int height, std::uint32_t color, const char* themeToken)
{
    PanelData data;
    data.name = name;
    data.width = width;
    data.height = height;
    data.startOpen = true;
    data.closed.color = color;
    data.closed.themeColor = themeToken;
    data.open = data.closed;
    data.shape = inking::ShapeSpec::Rect();
    return data;
}

/** 顶栏按钮：透明底 + 悬停变色，视觉上像"菜单项"而不是"一个方块按钮"。 */
ButtonData makeBarButton(const char* name, int width)
{
    ButtonData data;
    data.name = name;
    data.width = width;
    data.height = kBarHeight - 8;
    data.normal.type = inking::AppearanceType::Color;
    data.normal.color = theme::kSurface;
    data.normal.themeColor = "panelSurface";
    data.hover.type = inking::AppearanceType::Color;
    data.hover.color = theme::kHoverNeutral;
    data.onclicked.type = inking::AppearanceType::Color;
    data.onclicked.color = theme::kAccentSoft;
    data.hoverInheritsNormal = false;
    data.onclickedInheritsNormal = false;
    data.textColor = theme::kTextPrimary;
    data.text.fontSize = 15.0f;
    data.text.align = inking::TextAlignH::Center;
    data.text.alignV = inking::TextAlignV::Middle;
    data.shape = inking::ShapeSpec::RoundedRect(8.0f);
    return data;
}

/** 侧栏条目：吃满容器宽度（交叉轴 Stretch），文字左对齐。 */
ButtonData makeSidebarItem(const char* name)
{
    ButtonData data;
    data.name = name;
    data.width = 0; // 让布局拉伸；写死宽度会在侧栏换宽时留下一条缝
    data.height = 40;
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
    data.shape = inking::ShapeSpec::RoundedRect(10.0f);
    return data;
}

void closePopup();

// ---------------------------------------------------------------------------
// 多层级菜单（`golding.menu`）：一串定点浮窗
// ---------------------------------------------------------------------------
//
// 为什么不用现成的 `prefab::MenuPopup`：它第一版**明确不做子菜单**（头文件原话），
// 而且两个 `InkingFixedPopup` 并存时会互相当作"点外部"——各自判自己的 `HitTest`，
// 父菜单会在点开子菜单的同一帧自我关闭；焦点也会被基类的"取回"逻辑来回抢。
//
// 所以层级、落位、关闭仲裁全部落在本插件里，浮窗只当"画框 + 吃点击"的容器：
// 关掉它的两个自动行为（`SetCloseOnOutsideClick(false)` / `SetTakeFocusOnOpen(false)`），
// 统一由 `MenuStack` 判"这一点落在哪一层"。
//
// 一层的生命周期：懒创建（`EnsureLayer`）→ 重建内容（`BuildLevel`）→ 打开
// （`OpenAt`）。关闭只 `SetVisible(false)`，**不销毁节点**——菜单项的点击回调
// 就在那个按钮的派发栈里，当场 `RemoveChild` 会把正在派发的按钮删掉。

constexpr int kMenuMaxLayers = 4;
constexpr int kMenuItemWidth = 200;
constexpr int kMenuItemHeight = 34;
constexpr int kMenuPadding = 6;
constexpr int kMenuGap = 2;
/** 压过顶栏（100/101）与窗口按钮（200），低于弹窗遮罩（300）。 */
constexpr int kMenuBaseZ = 260;
/** 每深一层抬一档：否则父层容器里的项（z+1）会盖住子层容器。 */
constexpr int kMenuLayerZStep = 10;

/** 深拷贝进本插件自己表里的一项（声明只需要在 `open` 调用期间有效）。 */
struct MenuNode
{
    std::string id;
    std::string label;
    void (*onActivate)(void*, const char*) = nullptr;
    int childBase = -1;
    int childCount = 0;
};

struct MenuStack;

class MenuLayer final : public inking::InkingFixedPopup
{
public:
    MenuLayer(inking::InkingWidget* parent, MenuStack* stack, int level);

    void ClearRows();
    void AddRow(std::unique_ptr<InkingStaticButton> row);
    InkingStaticButton* RowAt(int index) const;

    bool ContainsPoint(float x, float y) const { return HitTest(x, y); }
    int Level() const noexcept { return _level; }

    /**
     * 基类没重写这条（`InkingFixedPopup.cpp` 里的描边实现因此永远没人调），
     * 不补的话浮窗在白底上只剩一条阴影——看起来像"没有背景"。
     */
    bool HasRenderDecoration() const noexcept override { return true; }

protected:
    // **完全接管**基类的指针行为：基类那两条（点外部自动关 / 打开时抢焦点）
    // 在多层级下都会误伤兄弟层（见上面那段注释）。
    void onPointerStateChanged() override;

private:
    MenuStack* _stack = nullptr;
    int _level = 0;
    std::vector<InkingStaticButton*> _rows;
};

/** 菜单栈：所有层归它管，`open` / `close` / 点外仲裁都从这里进。 */
struct MenuStack
{
    inking::InkingStaticPanel* root = nullptr;
    bool open = false;
    std::vector<std::unique_ptr<MenuLayer>> layers;
    /** 每次 `open` 重建；所有层的项都住在这里（按层连续的一段）。 */
    std::vector<MenuNode> nodes;

    int Ingest(const golding::MenuItem* items, int count);
    MenuLayer* EnsureLayer(int level);
    void BuildLevel(int level, int base, int count, float anchorX, float anchorY);
    void Open(int base, int count, float anchorX, float anchorY);
    void CloseAll();
    void CloseAbove(int level);
    void Activate(int level, int nodeIndex, int rowIndex);
    void HandlePointerChanged(MenuLayer* self);
    bool AnyLayerContains(float x, float y) const;
};

MenuStack g_menu;

PanelData makeMenuLayerData(int level)
{
    PanelData data;
    data.name = "GoldingGamestoryShell.menu." + std::to_string(level);
    data.width = kMenuItemWidth + kMenuPadding * 2;
    data.height = kMenuPadding * 2; // 空菜单的最小高度；有项时 BuildLevel 用 Resize 重算
    data.closed.color = theme::kSurface;
    data.closed.themeColor = "panelSurface";
    // 1px 描边 + 一层柔和阴影：**白底上的白卡片只能靠这两样站住**。
    // 阴影比 `MenuPopupStyle` 的默认值更实一点（0x33→0x4D、6→8px）：我们的内容区
    // 是纯白（菜单浮窗惯例是浅紫灰场景底），照抄默认值会淡得看不出浮层。
    data.closed.stroke.width = 1.0f;
    data.closed.stroke.color = theme::kOutline;
    data.closed.shadow = inking::ShadowSpec{0.0f, 8.0f, 0x4D000000u};
    data.open = data.closed;
    data.zIndex = kMenuBaseZ + level * kMenuLayerZStep;
    data.shape = inking::ShapeSpec::RoundedRect(12.0f);
    data.layout.kind = inking::LayoutKind::Stack;
    data.layout.axis = inking::LayoutAxis::Vertical;
    data.layout.gap = static_cast<float>(kMenuGap);
    data.layout.padding = inking::LayoutSpacing{static_cast<float>(kMenuPadding),
                                                static_cast<float>(kMenuPadding),
                                                static_cast<float>(kMenuPadding),
                                                static_cast<float>(kMenuPadding)};
    data.layout.align = inking::LayoutAlign::Stretch;
    return data;
}

ButtonData makeMenuRowButton()
{
    ButtonData data;
    data.width = kMenuItemWidth;
    data.height = kMenuItemHeight;
    data.normal.type = inking::AppearanceType::Color;
    data.normal.color = theme::kSurface;
    data.normal.themeColor = "panelSurface";
    data.hover.type = inking::AppearanceType::Color;
    data.hover.color = theme::kAccentSoft;
    data.onclicked.type = inking::AppearanceType::Color;
    data.onclicked.color = theme::kAccentSoftPressed;
    data.hoverInheritsNormal = false;
    data.onclickedInheritsNormal = false;
    data.textColor = theme::kTextPrimary;
    data.text.fontSize = 15.0f;
    data.text.align = inking::TextAlignH::Left;
    data.text.alignV = inking::TextAlignV::Middle;
    data.text.leftSpace = 12.0f;
    data.shape = inking::ShapeSpec::RoundedRect(8.0f);
    return data;
}

MenuLayer::MenuLayer(inking::InkingWidget* parent, MenuStack* stack, int level)
    : inking::InkingFixedPopup(parent, makeMenuLayerData(level)), _stack(stack), _level(level)
{
    // 多层级的前提：关闭与焦点由 `MenuStack` 统一仲裁，浮窗自己别抢。
    SetCloseOnOutsideClick(false);
    SetTakeFocusOnOpen(false);
}

void MenuLayer::ClearRows()
{
    for (InkingStaticButton* row : _rows)
    {
        if (row != nullptr)
        {
            RemoveChild(row);
        }
    }
    _rows.clear();
}

void MenuLayer::AddRow(std::unique_ptr<InkingStaticButton> row)
{
    if (row == nullptr)
    {
        return;
    }
    InkingStaticButton* raw = row.get();
    if (inking::InkingFixedPopup::AddItem(std::move(row)) != nullptr)
    {
        _rows.push_back(raw);
    }
}

InkingStaticButton* MenuLayer::RowAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(_rows.size()))
    {
        return nullptr;
    }
    return _rows[static_cast<std::size_t>(index)];
}

void MenuLayer::onPointerStateChanged()
{
    if (_stack == nullptr || !IsOpen())
    {
        return;
    }
    _stack->HandlePointerChanged(this);
}

int MenuStack::Ingest(const golding::MenuItem* items, int count)
{
    if (items == nullptr || count <= 0)
    {
        return -1;
    }

    std::vector<MenuNode> built;
    built.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index)
    {
        const golding::MenuItem& item = items[index];
        if (item.label == nullptr || item.label[0] == '\0')
        {
            continue; // 空文字的项不画一个空行；它的子层也跟着没有入口
        }
        MenuNode node;
        node.id = item.id != nullptr ? item.id : "";
        node.label = item.label;
        node.onActivate = item.onActivate;
        if (item.children != nullptr && item.childCount > 0)
        {
            const int childBase = Ingest(item.children, item.childCount);
            if (childBase >= 0)
            {
                node.childBase = childBase;
                node.childCount = static_cast<int>(nodes.size()) - childBase;
            }
        }
        built.push_back(std::move(node));
    }

    if (built.empty())
    {
        return -1;
    }

    // 父层在子层**之后**落盘：这样"这段连续下标"对父子都是准的（子层的项
    // 是递归 append 出来的，只需记录起点与长度）。
    const int base = static_cast<int>(nodes.size());
    for (MenuNode& node : built)
    {
        nodes.push_back(std::move(node));
    }
    return base;
}

MenuLayer* MenuStack::EnsureLayer(int level)
{
    while (static_cast<int>(layers.size()) <= level)
    {
        const int next = static_cast<int>(layers.size());
        layers.push_back(std::make_unique<MenuLayer>(root, this, next));
    }
    return layers[static_cast<std::size_t>(level)].get();
}

void MenuStack::BuildLevel(int level, int base, int count, float anchorX, float anchorY)
{
    MenuLayer* layer = EnsureLayer(level);
    if (layer == nullptr || count <= 0)
    {
        return;
    }

    layer->ClearRows();
    // `Resize` 内部在布局有效时会重排一次，所以这里不用再单独喊 `Relayout`。
    const int height = kMenuPadding * 2 + count * kMenuItemHeight + (count - 1) * kMenuGap;
    layer->Resize(kMenuItemWidth + kMenuPadding * 2, height);

    for (int index = 0; index < count; ++index)
    {
        const MenuNode& node = nodes[static_cast<std::size_t>(base + index)];
        ButtonData look = makeMenuRowButton();
        look.name = node.id.empty() ? ("GoldingGamestoryShell.menu.row." + std::to_string(index)) : node.id;
        // 有子层就在文字后给一个箭头：菜单的惯例，且不占额外宽度预算。
        look.label = node.childCount > 0 ? (node.label + "  ›") : node.label;

        auto row = std::make_unique<InkingStaticButton>(nullptr, look);
        row->SetOnClicked([this, level, nodeIndex = base + index, rowIndex = index] {
            Activate(level, nodeIndex, rowIndex);
        });
        layer->AddRow(std::move(row));
    }

    layer->Relayout();
    layer->OpenAt(anchorX, anchorY);
}

void MenuStack::Open(int base, int count, float anchorX, float anchorY)
{
    CloseAll();
    open = true;
    BuildLevel(0, base, count, anchorX, anchorY);
}

void MenuStack::CloseAll()
{
    for (auto& layer : layers)
    {
        if (layer != nullptr && layer->IsOpen())
        {
            layer->Close();
        }
    }
    open = false;
}

void MenuStack::CloseAbove(int level)
{
    for (int index = level + 1; index < static_cast<int>(layers.size()); ++index)
    {
        MenuLayer* layer = layers[static_cast<std::size_t>(index)].get();
        if (layer != nullptr && layer->IsOpen())
        {
            layer->Close();
        }
    }
}

void MenuStack::Activate(int level, int nodeIndex, int rowIndex)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
    {
        return;
    }
    const MenuNode node = nodes[static_cast<std::size_t>(nodeIndex)];

    if (node.childCount > 0)
    {
        if (level + 1 >= kMenuMaxLayers)
        {
            g_host.Warn("[shell] 菜单层级超过上限，子层没有展开");
            return;
        }
        MenuLayer* layer = (level >= 0 && level < static_cast<int>(layers.size()))
                               ? layers[static_cast<std::size_t>(level)].get()
                               : nullptr;
        InkingStaticButton* row = layer != nullptr ? layer->RowAt(rowIndex) : nullptr;
        if (row == nullptr)
        {
            return;
        }

        // 换分支时先收掉更深的层：否则展开新子层后旧的那串还挂在右边。
        CloseAbove(level);

        // 子层钉在**这一项**的右侧：与这一项的顶边对齐、左右留 2px 缝，
        // 否则两个白卡片会连成一块。越界由浮窗的落位算法翻到左边。
        const float anchorX = layer->GetX() + static_cast<float>(layer->GetWidth()) + 2.0f;
        const float anchorY = layer->GetY() + row->GetY();
        BuildLevel(level + 1, node.childBase, node.childCount, anchorX, anchorY);
        return;
    }

    // 叶子：先关整串再执行动作——动作里可能再弹一个模态框，显隐顺序反了会打架。
    CloseAll();
    if (node.onActivate != nullptr)
    {
        node.onActivate(nullptr, node.id.c_str());
    }
}

void MenuStack::HandlePointerChanged(MenuLayer* self)
{
    if (!open || self == nullptr)
    {
        return;
    }
    inking::InkingScene* scene = self->GetScene();
    if (scene == nullptr)
    {
        return;
    }
    // 只在"按着"时判：菜单打开后鼠标划过别处不该把它关掉（那是 hover，不是点击）。
    if (!scene->GetPointerDown().any())
    {
        return;
    }
    if (AnyLayerContains(scene->GetPointerX(), scene->GetPointerY()))
    {
        return;
    }
    CloseAll();
}

bool MenuStack::AnyLayerContains(float x, float y) const
{
    for (const auto& layer : layers)
    {
        if (layer != nullptr && layer->IsOpen() && layer->ContainsPoint(x, y))
        {
            return true;
        }
    }
    return false;
}

// ---- 菜单服务 ----

int menuOpen(void* /*self*/, const char* regionItemId, const golding::MenuDecl* menu)
{
    if (!g_parts || g_parts->root == nullptr || menu == nullptr || menu->items == nullptr ||
        menu->itemCount <= 0)
    {
        return 1;
    }

    g_menu.CloseAll();
    g_menu.nodes.clear();

    const int base = g_menu.Ingest(menu->items, menu->itemCount);
    if (base < 0)
    {
        g_host.Warn((std::string("菜单没有任何可见项：") + (menu->id != nullptr ? menu->id : "(无名)")).c_str());
        return 1;
    }
    const int count = static_cast<int>(g_menu.nodes.size()) - base;

    // 锚点：贴着那个顶栏按钮的左下角展开。找不到 id 就退到窗口左上角——
    // 菜单仍然能开，不至于因为一个 id 拼错就整个功能消失（诊断有一行 WARN）。
    float anchorX = 8.0f;
    float anchorY = static_cast<float>(kBarHeight) + 4.0f;
    const auto it = regionItemId != nullptr ? g_parts->barItems.find(regionItemId) : g_parts->barItems.end();
    if (it != g_parts->barItems.end() && it->second != nullptr)
    {
        const float rootX = g_parts->root->GetAbsX();
        const float rootY = g_parts->root->GetAbsY();
        anchorX = it->second->GetAbsX() - rootX;
        // 留 4px 缝：菜单紧贴按钮底部会像是顶栏的一部分。
        anchorY = it->second->GetAbsY() - rootY + static_cast<float>(it->second->GetHeight()) + 4.0f;
    }
    else if (regionItemId != nullptr)
    {
        g_host.Warn((std::string("菜单锚点区域项不存在，从左上方展开：") + regionItemId).c_str());
    }

    g_menu.Open(base, count, anchorX, anchorY);
    return 0;
}

void menuClose(void* /*self*/)
{
    g_menu.CloseAll();
}

int menuIsOpen(void* /*self*/)
{
    return g_menu.open ? 1 : 0;
}

const golding::MenuService kMenuService{GOLDING_PLUGIN_ABI_VERSION, &menuOpen, &menuClose, &menuIsOpen};


/** 把一项挂进区域容器：填了 `create` 就交给插件自己画，否则按 `title` 造一个按钮。
 *
 * 返回挂上去的那个节点（菜单服务要按区域项 id 找锚点，所以顶栏这一支必须留指针）。
 */
inking::InkingWidget* placeRegionItem(const GoldingRegionItemDecl* item, inking::InkingWidget* container,
                                      const ButtonData& look, bool useMenuBar)
{
    if (item == nullptr || container == nullptr)
    {
        return nullptr;
    }

    if (item->create != nullptr)
    {
        // 契约：插件把新建子树的根挂到 parent 上。顶栏这一支会排在菜单栏的
        // 弹簧与窗口按钮之后（顺序由 MenuBar 内部维护），要精确位置就用按钮形态。
        auto* created = static_cast<inking::InkingWidget*>(item->create(g_host.Context(), container));
        if (created == nullptr)
        {
            g_host.Warn((std::string("区域项内容创建失败：") + (item->id != nullptr ? item->id : "(无名)")).c_str());
        }
        return created;
    }

    if (item->title == nullptr || item->title[0] == '\0')
    {
        return nullptr; // 既没有界面也没有名字：没什么可画的
    }

    if (useMenuBar)
    {
        auto* bar = static_cast<inking::prefab::MenuBar*>(container);
        // 名字用区域项自己的 id：组件树里能看出"这一项是谁注册的"，
        // 自检 / 自动化也能按名字命中（`InkingScene::Activate("settings.entry")`）。
        ButtonData menuLook = look;
        menuLook.name = item->id != nullptr ? item->id : "menubar.item";
        return bar->AddButton(menuLook, item->title, [item, host = g_host] {
            if (item->onActivate != nullptr)
            {
                item->onActivate(host.Context());
            }
        });
    }

    // 直接 new 一条按钮时，文字要自己填进 `ButtonData::label`——`AddButton` 那条路
    // 是由 MenuBar 代填的，这里没有那层，漏了就得到"白底无字"（实测踩过两次）。
    ButtonData data = look;
    data.label = item->title;

    auto* button = static_cast<InkingStaticButton*>(
        container->AddChild(std::make_unique<InkingStaticButton>(container, data)));
    if (button == nullptr)
    {
        return nullptr;
    }
    button->SetName(item->id != nullptr ? item->id : item->title);
    button->SetOnClicked([item, host = g_host] {
        if (item->onActivate != nullptr)
        {
            item->onActivate(host.Context());
        }
    });
    return button;
}

/**
 * 把侧栏的展开/收起落到几何上：**走抽屉动画**。
 *
 * 只设目标宽度，谁也别在这里 `Resize` 到终值——每帧的推进由 `BodyLayer::onTick`
 * 驱动（它是动态档、永远在绘制列表里）。立刻改宽度会得到"瞬间跳一下"，而
 * 立刻重排父级则会在第一帧就把内容区摆到终态，动画看起来像被截了一半。
 */
void applySidebarState(const std::shared_ptr<ShellParts>& parts)
{
    if (!parts || parts->sidebar == nullptr)
    {
        return;
    }
    parts->sidebar->AnimateTo(parts->sidebarVisible ? static_cast<float>(kSidebarWidth) : 0.0f);
}

/** 装配期用的直落版：不走动画，免得首帧看到"正在划入"的中间态。 */
void snapSidebarState(const std::shared_ptr<ShellParts>& parts)
{
    if (!parts || parts->sidebar == nullptr)
    {
        return;
    }
    parts->sidebar->SnapTo(parts->sidebarVisible ? static_cast<float>(kSidebarWidth) : 0.0f);
    if (parts->body != nullptr)
    {
        // 直落之后内容区要立刻跟上（不然首帧能看到"内容区还压着侧栏"的中间态）。
        parts->body->SyncGeometry();
    }
}

/** 侧栏开合（`≡` 与 Tab 共用一个落点，免得两处各写一遍状态机）。 */
void toggleSidebar(const std::shared_ptr<ShellParts>& parts)
{
    if (!parts)
    {
        return;
    }
    parts->sidebarVisible = !parts->sidebarVisible;
    applySidebarState(parts);
    std::printf("[shell] 侧栏%s\n", parts->sidebarVisible ? "展开" : "收起");
}

/** 把内容区换成 `pageId` 那一页；没有这一页时返回 false 并保持原样。 */
bool mountPage(const std::shared_ptr<ShellParts>& parts, const char* pageId)
{
    if (!parts || parts->content == nullptr || pageId == nullptr || pageId[0] == '\0')
    {
        return false;
    }

    const GoldingWidgetHandle handle = g_host.MountPage(pageId, parts->content);
    if (handle == nullptr)
    {
        return false; // 不拆旧的：换页失败不该把用户正在看的页面弄没
    }

    if (parts->currentPage != nullptr)
    {
        parts->content->RemoveChild(parts->currentPage);
    }
    parts->currentPage = static_cast<inking::InkingWidget*>(handle);
    parts->content->Relayout();
    return true;
}

// ---- 导航服务 ----

int navigationGo(void* /*self*/, const char* pageId)
{
    return mountPage(g_parts, pageId) ? 0 : 1;
}

const golding::NavigationService kNavigationService{GOLDING_PLUGIN_ABI_VERSION, &navigationGo};

// ---- 弹窗服务 ----

/**
 * @brief 把摘下来的旧节点排到**下一拍**销毁（真正的清理在 `BodyLayer::onTick` 开头）。
 *
 * 为什么不是立刻销毁：`push` 可能是在某个按钮的点击回调里被调的（设置里的
 * 「用户指针…」就是），那个按钮就在旧弹窗里。当场删掉它，它的 `TriggerClick`
 * 返回后还会碰自己的成员——use-after-free，实测段错误。
 *
 * 为什么不用 `RunAfterFrames(1)`：那条路在 `--frames`（CI / 截图）路径上不派发，
 * 节点会一直躺在 graveyard 里，直到进程退出时随静态 `g_parts` 一起析构——而那时
 * 场景已经销毁，`~InkingWidget` 里的 `RemoveDrawNode` 就是一次 use-after-free
 * （实测退出码 0xC0000005）。改由逻辑步每拍清，两个模式走同一条路。
 */
void ScheduleGraveyardSweep()
{
    // 语义占位：清理点在 `BodyLayer::onTick`。
}

void closePopup()
{
    if (!g_parts || !g_parts->popupOpen)
    {
        return;
    }
    ShellParts& parts = *g_parts;
    parts.popupOpen = false;
    if (parts.backdrop != nullptr)
    {
        parts.backdrop->SetVisible(false);
    }
    if (parts.popup != nullptr)
    {
        parts.popup->SetVisible(false);
    }

    // 先摘声明再回调：`onClose` 里再 push 一次是合法用法，不该被这里清掉。
    //
    // 从这里往下用的是**自己那份拷贝**：声明只承诺在 `push` 期间有效，调用方
    // 常常在栈上写 `const GoldingPopupDecl decl{...}`——存下来的指针到这一刻
    // 早已失效（关闭弹窗时随机闪退的根因）。
    const bool hadDecl = parts.popupDecl.has_value();
    golding::GoldingPopupDecl decl{};
    std::string idText;
    if (hadDecl)
    {
        decl = *parts.popupDecl;
        if (decl.id != nullptr)
        {
            idText = decl.id;
            decl.id = idText.c_str();
        }
    }
    parts.popupDecl.reset();
    parts.popupDeclId.clear();
    if (hadDecl && decl.onClose != nullptr)
    {
        decl.onClose(nullptr);
    }
}

int popupPush(void* /*self*/, const golding::GoldingPopupDecl* decl)
{
    if (!g_parts || decl == nullptr || decl->create == nullptr)
    {
        return 1;
    }
    ShellParts& parts = *g_parts;

    // 弹模态框之前先收菜单：菜单层（260）会被遮罩（300）盖住，但状态还开着，
    // 关掉弹窗之后它又露出来——那是"点了个菜单项，回来菜单还挂着"的怪相。
    g_menu.CloseAll();

    // 静态面板"几何构造即定型"，改不了尺寸——所以每次 push 重建一块，
    // 而不是留一块常驻再 Resize。push 是低频动作（弹对话框），重建的代价可以忽略。
    if (parts.popup != nullptr)
    {
        // 只摘不删：这一行可能在某个按钮的回调栈上（见 `graveyard` 的注释）。
        if (auto removed = parts.root->RemoveChild(parts.popup))
        {
            parts.graveyard.push_back(std::move(removed));
            ScheduleGraveyardSweep();
        }
        parts.popup = nullptr;
    }

    // 声明**深拷贝**：id 文本一起拷。调用方可以在栈上写声明，push 返回后就能扔。
    parts.popupDeclId = decl->id != nullptr ? decl->id : "";
    golding::GoldingPopupDecl copy = *decl;
    copy.id = parts.popupDeclId.c_str();
    parts.popupDecl = copy;

    // 内容区的矩形要**两级相加**：`content` 是 `body` 的孩子，`GetX/GetY` 是相对父级的。
    const int areaX = static_cast<int>(parts.body->GetX() + parts.content->GetX());
    const int areaY = static_cast<int>(parts.body->GetY() + parts.content->GetY());
    const int areaWidth = parts.content->GetWidth();
    const int areaHeight = parts.content->GetHeight();

    const int width = decl->width > 0 ? decl->width : (areaWidth * 3 / 5);
    const int height = decl->height > 0 ? decl->height : (areaHeight * 3 / 5);
    const int x = decl->centered != 0 ? areaX + (areaWidth - width) / 2 : areaX + decl->offsetX;
    const int y = decl->centered != 0 ? areaY + (areaHeight - height) / 2 : areaY + decl->offsetY;

    PanelData data = makePanel(decl->id != nullptr ? decl->id : "popup", width, height, theme::kSurface,
                               "panelSurface");
    data.offsetX = static_cast<float>(x);
    data.offsetY = static_cast<float>(y);
    data.zIndex = 400;
    data.shape = inking::ShapeSpec::RoundedRect(16.0f);
    data.clipChildren = true; // 内容不许越出弹窗
    data.visible = false;

    parts.popup = static_cast<InkingStaticPanel*>(
        parts.root->AddChild(std::make_unique<InkingStaticPanel>(parts.root, data)));
    if (parts.popup == nullptr)
    {
        parts.popupDecl.reset();
        parts.popupDeclId.clear();
        return 1;
    }

    // ---- 内容容器 ----
    //
    // 插件的所有节点都往它上面挂（`GoldingPopupDecl::create` 收到的 parent 就是它）。
    // 与弹窗同尺寸、同圆角：它是白底，直角铺满会把弹窗的四角顶成方角。
    PanelData contentData = makePanel("popup.content", width, height, theme::kSurface, "panelSurface");
    contentData.shape = inking::ShapeSpec::RoundedRect(16.0f);
    contentData.clipChildren = true;
    auto* content = static_cast<InkingStaticPanel*>(
        parts.popup->AddChild(std::make_unique<InkingStaticPanel>(parts.popup, contentData)));

    // ---- 关闭键（X）----
    //
    // 由面板统一提供：内容与关闭键必须分开，否则插件重建自己的内容时会连它一起删掉。
    // 内容铺满整块，所以 X 要显式压一层（zIndex + 后加）——右上角那一小块归它。
    if (content != nullptr)
    {
        ButtonData closeData = makeBarButton("popup.close", 36);
        closeData.height = 36;
        closeData.label = "×";
        closeData.text.fontSize = 20.0f;
        closeData.shape = inking::ShapeSpec::RoundedRect(8.0f);
        closeData.offsetX = static_cast<float>(width - 36 - 12);
        closeData.offsetY = 12.0f;
        closeData.zIndex = 10;
        auto* closeButton = static_cast<InkingStaticButton*>(
            parts.popup->AddChild(std::make_unique<InkingStaticButton>(parts.popup, closeData)));
        if (closeButton != nullptr)
        {
            closeButton->SetOnClicked([] { closePopup(); });
        }
    }

    // 内容建不出来 = 整个弹窗算失败（半成品不如不给）。
    // hostContext 传 nullptr：调用方是在自己的代码里发起的，需要 HostApi 的话
    // 应当把自己那份 `golding::Host` 存下来（见 `GoldingPopupDecl::create` 的注释）。
    if (content == nullptr || decl->create(nullptr, content) == nullptr)
    {
        parts.root->RemoveChild(parts.popup);
        parts.popup = nullptr;
        parts.popupDecl.reset();
        parts.popupDeclId.clear();
        g_host.Warn((std::string("弹窗内容创建失败：") + (decl->id != nullptr ? decl->id : "(无名)")).c_str());
        return 1;
    }

    parts.popupOpen = true;
    parts.backdrop->SetVisible(true);
    parts.popup->SetVisible(true);
    return 0;
}

void popupPop(void* /*self*/)
{
    closePopup();
}

const golding::PopupService kPopupService{golding::kPopupServiceVersion, &popupPush, &popupPop};

// ---- 骨架 ----

void* createShell(void* hostContext, void* parent, const char* /*configName*/)
{
    auto* hostParent = static_cast<inking::InkingWidget*>(parent);
    if (hostParent == nullptr || !g_host.Valid())
    {
        return nullptr;
    }

    // 设计尺寸的唯一来源是宿主那次 `SetDesignSize`。父节点（场景根）多大就排多大，
    // 这里**不抄** 1920/1080——抄了之后"改设计尺寸"就变成四处对齐的手工活。
    const int width = hostParent->GetWidth() > 0 ? hostParent->GetWidth() : inking::kDefaultDesignWidth;
    const int height = hostParent->GetHeight() > 0 ? hostParent->GetHeight() : inking::kDefaultDesignHeight;

    auto parts = std::make_shared<ShellParts>();
    parts->hostContext = hostContext;

    // ---- 根：整块画布，**不做排布** ----
    //
    // 顶栏全屏时可能整条消失（`hideInFullscreen`），那时它占 0 高度。用 Stack 纵排会把
    // 配置高度固定分给它，于是顶上永远空一条。所以根不排布，三块各自按实测值定位。
    // 底色是**白**（不是主题的浅紫场景底）：这块面板的取向是"白窗口 + 灰紫侧栏"，
    // 页面也是白底，靠分隔线与层次区分，不靠整块换色。
    PanelData rootData = makePanel("GoldingGamestoryShell.root", width, height, theme::kSurface, "panelSurface");
    parts->root = static_cast<InkingStaticPanel*>(
        hostParent->AddChild(std::make_unique<InkingStaticPanel>(hostParent, rootData)));

    // ---- 顶栏 ----
    inking::prefab::MenuBarStyle barStyle = inking::prefab::MenuBarStyle::MakeDefault();
    barStyle.panel.name = "GoldingGamestoryShell.menubar";
    barStyle.panel.width = width;
    barStyle.panel.height = kBarHeight;
    barStyle.panel.startOpen = true;
    barStyle.panel.closed.color = theme::kSurface;
    barStyle.panel.closed.themeColor = "panelSurface";
    // 一条 1px 的边线：白顶栏与白内容区之间没有它就会连成一片。
    // 描边画在整块边界上，左右上三条贴着窗口边看不见，实际只留下下面这一条。
    barStyle.panel.closed.stroke.width = 1.0f;
    barStyle.panel.closed.stroke.color = theme::kOutline;
    barStyle.panel.open = barStyle.panel.closed;
    barStyle.panel.layout.gap = 6.0f;
    barStyle.panel.Normalize();
    barStyle.draggable = true; // 无边框窗口：按住空白处拖窗口
    // **不用 MenuBar 自带的窗口按钮**：`captionButtons` 会插一根 `fillMain` 弹簧，
    // 而弹簧的宽度是在"项还没挂全"的那一次布局里定的——实测右上角两个按钮被顶到
    // x=1920（正好等于顶栏宽，即画布外），点不到。窗口按钮改由本插件放在根上（见下），
    // 位置是绝对的，不受菜单栏内部排版影响。
    barStyle.captionButtons = false;
    barStyle.textColor = theme::kTextPrimary;

    parts->bar = static_cast<inking::prefab::MenuBar*>(
        parts->root->AddChild(std::make_unique<inking::prefab::MenuBar>(parts->root, barStyle)));

    // 顶栏**现在**占多少（全屏隐藏时是 0）。主体三块的 y / 高度都按它算。
    const int barHeight = parts->bar->GetCurrentHeight();

    // ---- 抽屉开关（≡）：顶栏最左 ----
    //
    // 收拉**不改尺寸**（静态面板几何构造即定型），而是"改侧栏宽度 + 让主体重算"：
    // 侧栏宽度归 0 之后不再占位，`SyncGeometry()` 把整块宽度都给内容区。
    // 这样收起/展开都不重建任何节点，页面与输入状态原样保留。
    ButtonData toggleLook = makeBarButton("GoldingGamestoryShell.toggle", 40);
    toggleLook.text.fontSize = 18.0f; // ≡ 小字号下几乎看不清
    parts->bar->AddButton(toggleLook, "≡", [parts] { toggleSidebar(parts); });

    // ---- 侧栏的第二个入口：全局 Tab ----
    //
    // 绑在**窗口的快捷键表**上（`MenuBar::BindShortcut`），不是焦点派发：焦点在
    // 输入框里也照样响应。代价说在明处——全局键会**先消费掉**这一下 Tab，输入框
    // 因此拿不到 Tab（想给输入框留 Tab 就得换成 Ctrl+Tab 之类）。
    {
        bool parsed = false;
        const inking::Shortcut sidebarShortcut = inking::Shortcut::Parse("Tab", &parsed);
        const std::uint64_t bound =
            parsed ? parts->bar->BindShortcut(sidebarShortcut, [parts] { toggleSidebar(parts); }, "侧栏开合")
                   : 0;
        if (bound == 0)
        {
            // 绑定失败会表现成"按 Tab 没反应"，所以不能只在返回值里静默。
            g_host.Warn("侧栏快捷键 Tab 没绑上（组合被占或解析失败）");
        }
    }

    // 面板自己的名字：顶栏左侧。它是这块面板的身份，不是业务内容。
    inking::LabelData titleData;
    titleData.name = "GoldingGamestoryShell.title";
    titleData.width = 180;
    titleData.height = kBarHeight;
    titleData.text = "Golding Gamestory";
    titleData.color = theme::kTextPrimary;
    titleData.colorToken = "textPrimary";
    titleData.style.fontSize = 16.0f;
    titleData.style.alignV = inking::TextAlignV::Middle;
    parts->bar->AddItem(std::make_unique<InkingStaticLabel>(nullptr, titleData));

    // ---- 窗口按钮（自绘标题栏的另一半）----
    //
    // 挂在**根**上、按绝对坐标贴右上角：位置不参与任何布局，就不会出现"被菜单栏的
    // 弹簧挤出画布"那种事。z=200 要**高过顶栏**（MenuBar 自身是 100、它的项 101），
    // 否则会被顶栏的白底盖住——这是实测出来的：几何全对、绘制列表里也有，就是看不见。
    // 同时它低于遮罩（300），所以弹窗一开就被盖住，不会出现"弹着窗还能点关闭"。
    auto addWindowButton = [&](const char* name, const char* text, int x, void (*action)()) {
        ButtonData look = makeBarButton(name, 40);
        look.height = 36;
        look.label = text;
        look.offsetX = static_cast<float>(x);
        look.offsetY = 4.0f;
        look.zIndex = 200;
        auto* button = static_cast<InkingStaticButton*>(
            parts->root->AddChild(std::make_unique<InkingStaticButton>(parts->root, look)));
        button->SetOnClicked(action);
    };
    addWindowButton("GoldingGamestoryShell.window.min", "-", width - 84,
                    [] { inking::InkingWindow::Instance().MinimizeWindow(); });
    addWindowButton("GoldingGamestoryShell.window.close", "×", width - 44,
                    [] { inking::InkingWindow::Instance().RequestClose(0); });

    // ---- 主体：横向排「侧栏 + 内容区」----
    //
    // 为什么要有这一层：侧栏宽度归 0 时"剩下的宽度归内容区"必须有一个横向布局的
    // 父级来算。挂根上做不到——根不排布（见上）。
    //
    // **必须是动态档**（`BodyLayer`）：顶栏进出全屏时它占的高度在 `kBarHeight` 与 0
    // 之间跳，body 的 y / 高都得跟着改。静态面板改不了自己的几何，那正是"全屏后
    // 顶上留一条 44px 空白、内容不往上长"的来源。
    inking::AnchorData bodyAnchor;
    bodyAnchor.width = width;
    bodyAnchor.height = height - barHeight;
    bodyAnchor.offsetY = static_cast<float>(barHeight);
    bodyAnchor.color = theme::kSurface;
    // **不再用横向 Stack 分宽度**：求解器的 `fillMain` 只增不减（见 `ContentLayer` 的
    // 类注释），侧栏一变宽内容区就缩不回去。宽度改由 `SyncGeometry()` 直接算。
    parts->body = static_cast<BodyLayer*>(
        parts->root->AddChild(std::make_unique<BodyLayer>(parts->root, bodyAnchor, theme::kSurface)));
    if (parts->body != nullptr)
    {
        parts->body->SetName("GoldingGamestoryShell.body");
    }

    // ---- 左侧栏（区域 sidebar）----
    //
    // 用承载面（比白深一档）而不是白：白顶栏 + 白内容区 + 白侧栏会连成一整片。
    inking::AnchorData sidebarAnchor;
    sidebarAnchor.width = kSidebarWidth;
    sidebarAnchor.height = height - barHeight;
    sidebarAnchor.color = theme::kSurfaceSunken;
    sidebarAnchor.layout.kind = inking::LayoutKind::Stack;
    sidebarAnchor.layout.axis = inking::LayoutAxis::Vertical;
    sidebarAnchor.layout.gap = 6.0f;
    sidebarAnchor.layout.padding = inking::LayoutSpacing{10.0f, 10.0f, 10.0f, 10.0f};
    parts->sidebar = static_cast<SideLayer*>(
        parts->body->AddChild(std::make_unique<SideLayer>(parts->body, sidebarAnchor, theme::kSurfaceSunken)));
    parts->sidebar->SetName("GoldingGamestoryShell.sidebar");

    // 主体接管两件每帧的活：跟住顶栏当前高度、推进侧栏抽屉动画。
    if (parts->sidebar != nullptr)
    {
        parts->body->SetSidebar(parts->sidebar);
        // 探测用 `weak_ptr` 捕获：body 由 root 拥有、root 由场景拥有，而 `g_parts`
        // 可能比它们活得久。强引用会成环（parts → body → lambda → parts），
        // 裸指针会在场景先析构时悬垂；weak 两头都不占。
        std::weak_ptr<ShellParts> weakParts = parts;
        parts->body->SetBarHeightProbe([weakParts]() -> int {
            const std::shared_ptr<ShellParts> locked = weakParts.lock();
            return locked != nullptr && locked->bar != nullptr ? locked->bar->GetCurrentHeight() : -1;
        });
        // 每拍清一次 graveyard（弹窗 / 重建条目的旧节点）。见 `ScheduleGraveyardSweep`。
        parts->body->SetDeferredSweep([weakParts] {
            const std::shared_ptr<ShellParts> locked = weakParts.lock();
            if (locked != nullptr)
            {
                locked->graveyard.clear();
            }
        });
    }

    // ---- 右侧内容区 ----
    //
    // 几何（左边界 / 宽 / 高）由 `BodyLayer::SyncGeometry()` 每次侧栏一动就算一遍，
    // 这里不给布局规则，也不抄一个宽度。
    //
    // 里面那个**纵向 Stack** 是给页面用的：页面根按交叉轴 `Stretch` 拉满内容区宽度，
    // 于是"侧栏收进去 → 页面跟着变宽"不需要页面自己写一行代码。
    inking::AnchorData contentAnchor;
    contentAnchor.width = width - kSidebarWidth;
    contentAnchor.height = height - barHeight;
    contentAnchor.offsetX = static_cast<float>(kSidebarWidth);
    contentAnchor.color = theme::kSurface;
    contentAnchor.layout.kind = inking::LayoutKind::Stack;
    contentAnchor.layout.axis = inking::LayoutAxis::Vertical;
    contentAnchor.layout.gap = 0.0f;
    parts->content = static_cast<ContentLayer*>(
        parts->body->AddChild(std::make_unique<ContentLayer>(parts->body, contentAnchor, theme::kSurface)));
    parts->content->SetName("GoldingGamestoryShell.content");
    parts->body->SetContent(parts->content);
    // 装配期先落一次：首帧就得是"侧栏 220 | 内容区 1700"，不能等第一次动画。
    parts->body->SyncGeometry();

    // ---- 区域条目：menubar 与 sidebar 各扫一遍 ----
    //
    // 扫的是**注册表快照**：区域项都在各插件的 `Register` 里登记，而装配（本函数）
    // 发生在全部 `Register` 之后，所以这里能看到全部。之后再注册就看不到了——
    // 那是"插件必须在 Register 里注册"的必然结果，不是缺陷。
    const int menuItems = g_host.RegionItemCount("menubar");
    for (int index = 0; index < menuItems; ++index)
    {
        const GoldingRegionItemDecl* item = g_host.RegionItemAt("menubar", index);
        inking::InkingWidget* placed =
            placeRegionItem(item, parts->bar, makeBarButton("menubar.item", 96), true);
        // 菜单服务按区域项 id 找锚点（`GoldingMenuItem` 的 `regionItemId` 那一条），
        // 所以顶栏项必须留一份指针。侧栏项眼下没有菜单入口，不留。
        if (placed != nullptr && item != nullptr && item->id != nullptr && item->id[0] != '\0')
        {
            parts->barItems[item->id] = placed;
        }
    }

    const int sideItems = g_host.RegionItemCount("sidebar");
    for (int index = 0; index < sideItems; ++index)
    {
        placeRegionItem(g_host.RegionItemAt("sidebar", index), parts->sidebar, makeSidebarItem("sidebar.item"),
                        false);
    }

    // 顶栏的项全部挂完之后再定对齐、再排一次版：项的增减会各自触发一次重排，
    // 统一在最后收一次口，免得某个中间态的几何被后面的人读到。
    parts->bar->SetJustify(inking::LayoutJustify::Start);
    parts->bar->Relayout();

    // ---- 弹窗遮罩：常驻、初始不可见 ----
    //
    // 用按钮而不是面板：点击回调只有按钮有（`SetOnClicked`）。整块遮住根，
    // 所以 z 要压过所有常规内容。
    ButtonData backdropData;
    backdropData.name = "GoldingGamestoryShell.backdrop";
    backdropData.width = width;
    backdropData.height = height;
    backdropData.normal.type = inking::AppearanceType::Color;
    backdropData.normal.color = 0x66000000u; // 40% 黑
    backdropData.hoverInheritsNormal = true;
    backdropData.onclickedInheritsNormal = true;
    backdropData.shape = inking::ShapeSpec::Rect();
    backdropData.zIndex = 300;
    backdropData.visible = false;
    parts->backdrop = static_cast<InkingStaticButton*>(
        parts->root->AddChild(std::make_unique<InkingStaticButton>(parts->root, backdropData)));
    // 点遮罩**不关闭**：弹窗只由右上角那个 X 关（`golding.popup` v2 的语义）。
    // 回调留空而不是不设——按钮就是"吃掉这一区域的点击"的那件东西，
    // 少了它，点空白会穿到下面的顶栏 / 内容区上去。
    parts->backdrop->SetOnClicked([] {});

    // 启动模式 `--collapsed`：一进来侧栏就是收起的。
    // 宿主只把命令行原样交出来，"哪种模式是什么意思"是面板自己的知识（`bootMode` 契约）。
    if (std::string(g_host.BootMode()) == "--collapsed")
    {
        parts->sidebarVisible = false;
        snapSidebarState(parts); // 直落：首帧不该是"正在划入"的中间态
    }

    // 先排一次版：内容区才有真实尺寸，页面根按它量。
    parts->root->Relayout();

    // ---- 装配出第一页 ----
    for (int index = 0; index < g_host.PageCount() && index < kPageScanLimit; ++index)
    {
        const GoldingPageDecl* page = g_host.PageAt(index);
        if (page != nullptr && page->id != nullptr && page->id[0] != '\0')
        {
            mountPage(parts, page->id);
            break;
        }
    }

    // 菜单栈要往根上挂层：给它根节点（层是懒创建的，这里只是记下父级）。
    g_menu.root = parts->root;

    g_parts = parts;
    std::printf("[shell] 骨架已建：%dx%d，顶栏 %d，侧栏 %d，menubar 项 %d，sidebar 项 %d，页面 %d\n", width, height,
                barHeight, kSidebarWidth, menuItems, sideItems, g_host.PageCount());
    return parts->root;
}

// ---- 装配后自检 ----
//
// 只断言**这块面板自己的**契约：根铺满父级、三块在、内容区尺寸对得上。
// 页面能不能装、区域项能不能建，属于通用契约，由自检插件管；这里不越界。

int check(const char* label, bool ok, const std::string& detail)
{
    std::printf("[%s] shell.%-26s %s\n", ok ? "ok" : "FAIL", label, detail.c_str());
    return ok ? 0 : 1;
}

int runReady(const golding::Host& host)
{
    (void)host;
    int failures = 0;
    if (!g_parts || g_parts->root == nullptr)
    {
        std::printf("[FAIL] shell.骨架不存在\n");
        return 1;
    }

    const ShellParts& parts = *g_parts;
    inking::InkingWidget* parent = parts.root->GetParent();
    failures += check("根铺满父级",
                      parent != nullptr && parts.root->GetWidth() == parent->GetWidth() &&
                          parts.root->GetHeight() == parent->GetHeight(),
                      "root=" + std::to_string(parts.root->GetWidth()) + "x" +
                          std::to_string(parts.root->GetHeight()) + " parent=" +
                          (parent != nullptr ? std::to_string(parent->GetWidth()) + "x" +
                                                   std::to_string(parent->GetHeight())
                                             : std::string("null")));

    failures += check("顶栏 / 侧栏 / 内容区都在",
                      parts.bar != nullptr && parts.sidebar != nullptr && parts.content != nullptr, "三块");

    const int barHeight = parts.bar != nullptr ? parts.bar->GetCurrentHeight() : 0;
    failures += check("内容区高度 = 画布高 − 顶栏当前高",
                      parts.content != nullptr && parts.content->GetHeight() + barHeight == parts.root->GetHeight(),
                      "content=" + std::to_string(parts.content != nullptr ? parts.content->GetHeight() : -1) +
                          " bar=" + std::to_string(barHeight));

    // 收起的侧栏宽度是 **0**（不是"隐藏但仍占 220"）：布局求解器不跳过不可见项，
    // 所以收起必须落到几何上。这条断言就是"收拉真的生效了"的观测口。
    const int occupiedSideWidth = parts.sidebar != nullptr ? parts.sidebar->GetWidth() : 0;
    failures += check("内容区宽度 = 画布宽 − 侧栏宽",
                      parts.content != nullptr &&
                          parts.content->GetWidth() + occupiedSideWidth == parts.root->GetWidth(),
                      "content=" + std::to_string(parts.content != nullptr ? parts.content->GetWidth() : -1) +
                          " side=" + std::to_string(occupiedSideWidth) +
                          (parts.sidebarVisible ? "（展开）" : "（收起）"));

    failures += check("内容区非空", parts.content != nullptr && parts.content->GetWidth() > 0 &&
                                        parts.content->GetHeight() > 0,
                      std::to_string(parts.content != nullptr ? parts.content->GetWidth() : 0) + "x" +
                          std::to_string(parts.content != nullptr ? parts.content->GetHeight() : 0));

    // 顶栏进出全屏时"占多高"会从 44 跳到 0，主体必须自己跟过去（`BodyLayer::onTick`
    // 每拍核对一次）。能断的是"现在这一拍对得上"——启动全屏由 `--fullscreen` 覆盖，
    // 运行期切全屏靠这条链路，少了它顶上就留一条 44px 的空白。
    failures += check("主体顶边 = 顶栏当前高",
                      parts.body != nullptr && static_cast<int>(parts.body->GetY()) == barHeight &&
                          parts.body->GetHeight() + barHeight == parts.root->GetHeight(),
                      "body.y=" + std::to_string(parts.body != nullptr ? static_cast<int>(parts.body->GetY()) : -1) +
                          " body.h=" + std::to_string(parts.body != nullptr ? parts.body->GetHeight() : -1) +
                          " bar=" + std::to_string(barHeight));

    // Tab 绑在**窗口的全局快捷键表**上：绑不上只会表现成"按了没反应"（没有报错、
    // 没有日志），所以在这里钉一条——查的是表里真有这个**组合**，不是"绑了几条"。
    {
        const inking::Shortcut tabKey = inking::Shortcut::Parse("Tab");
        const std::uint64_t tabId = inking::InkingWindow::Instance().GetShortcuts().Find(tabKey);
        failures += check("侧栏快捷键 Tab 已绑定", tabId != 0,
                          "id=" + std::to_string(tabId) + " key=" + tabKey.ToString());
    }

    // 抽屉动画：手动推两拍，验证它**按时间**走（不是瞬间跳），并且精确落到目标。
    // 放在最后并复原——自检跑过之后画面必须和跑之前一样。
    if (parts.sidebar != nullptr)
    {
        const float before = parts.sidebar->CurrentWidth();
        parts.sidebar->SnapTo(0.0f);
        parts.sidebar->AnimateTo(static_cast<float>(kSidebarWidth));
        parts.sidebar->AdvanceAnimation(kSidebarAnimSeconds * 0.5f);
        const float middle = parts.sidebar->CurrentWidth();
        parts.sidebar->AdvanceAnimation(kSidebarAnimSeconds);
        const float ended = parts.sidebar->CurrentWidth();
        const bool stillAnimating = parts.sidebar->IsAnimating();
        parts.sidebar->SnapTo(before);
        if (parts.body != nullptr)
        {
            parts.body->SyncGeometry();
        }
        failures += check("侧栏抽屉按时间推进",
                          middle > 0.0f && middle < static_cast<float>(kSidebarWidth) && !stillAnimating &&
                              ended == static_cast<float>(kSidebarWidth),
                          "mid=" + std::to_string(middle) + " end=" + std::to_string(ended));
    }

    // 侧栏收进去之后，内容区必须**吃掉整块**、页面也跟着变宽（"两者分开"的观测口）。
    //
    // 这条钉的是一个真实的回归：早先内容区靠横向 Stack 的 `fillMain` 分宽度，而求解器
    // 只在剩余空间为正时才分——内容区被撑宽过一次之后就再也缩不回来（侧栏 220、
    // 内容区仍 1920），页面被推到画布外，右下角那两个按钮点不到。
    {
        const float before = parts.sidebar != nullptr ? parts.sidebar->CurrentWidth() : 0.0f;
        int collapsedWidth = -1;
        int collapsedPageWidth = -1;
        int expandedWidth = -1;
        int expandedPageWidth = -1;
        if (parts.sidebar != nullptr && parts.body != nullptr && parts.content != nullptr)
        {
            parts.sidebar->SnapTo(0.0f);
            parts.body->SyncGeometry();
            collapsedWidth = parts.content->GetWidth();
            collapsedPageWidth = parts.currentPage != nullptr ? parts.currentPage->GetWidth() : -1;

            parts.sidebar->SnapTo(static_cast<float>(kSidebarWidth));
            parts.body->SyncGeometry();
            expandedWidth = parts.content->GetWidth();
            expandedPageWidth = parts.currentPage != nullptr ? parts.currentPage->GetWidth() : -1;

            parts.sidebar->SnapTo(before); // 复原：自检不能让首帧换样
            parts.body->SyncGeometry();
        }
        const bool ok = collapsedWidth == parts.root->GetWidth() &&
                        expandedWidth == parts.root->GetWidth() - kSidebarWidth &&
                        (parts.currentPage == nullptr ||
                         (collapsedPageWidth == collapsedWidth && expandedPageWidth == expandedWidth));
        failures += check("侧栏开合 → 内容区与页面一起重排", ok,
                          "收起 content=" + std::to_string(collapsedWidth) + " page=" +
                              std::to_string(collapsedPageWidth) + " / 展开 content=" +
                              std::to_string(expandedWidth) + " page=" + std::to_string(expandedPageWidth));
    }

    return failures;
}

// ---- 面板根工厂与区域声明 ----

const GoldingWidgetClass kRootClass = {
    "GoldingGamestoryShell.root",
    &createShell,
    nullptr, // 节点由父节点拥有，随场景销毁
};

const GoldingRegionDecl kMenuBarRegion = {
    "menubar", // id：本插件自己定的约定，core 不认识它
    "顶栏",
    10,
    0, // 顶栏里不允许同名入口
};

const GoldingRegionDecl kSidebarRegion = {
    "sidebar",
    "侧栏",
    20,
    0,
};

class GoldingGamestoryShellPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        g_host = host; // 回调不在这个调用栈上，必须存一份（Host 只有两个指针，按值存是标准用法）

        // 四条都用 `ok = ... && ok` 串起来：不要短路，否则一条失败会静默跳过后面三条，
        // 日志里就看不出"到底注册到哪一步"。
        bool ok = host.AddWidgetClass(kRootClass);
        ok = host.AddRegion(kMenuBarRegion) && ok;
        ok = host.AddRegion(kSidebarRegion) && ok;
        ok = host.AddService(golding::kNavigationServiceName, &kNavigationService) && ok;
        ok = host.AddService(golding::kPopupServiceName, &kPopupService) && ok;
        ok = host.AddService(golding::kMenuServiceName, &kMenuService) && ok;
        return ok;
    }

    int Ready(const golding::Host& host) override { return runReady(host); }
};

} // namespace

GOLDING_EXPORT_PLUGIN(GoldingGamestoryShellPlugin)
