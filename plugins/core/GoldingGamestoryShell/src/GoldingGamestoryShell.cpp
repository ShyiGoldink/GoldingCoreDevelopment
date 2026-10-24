#include "GoldingServices.h"

#include <sdk/GoldingPlugin.h>

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <config/window_config.h>
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingTextLayout.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/MenuBar.h>
#include <prefab/MenuBarData.h>
#include <prefab/PrefabTheme.h>
#include <render/InkingDraw.h>
#include <text/InkingStaticLabel.h>
#include <widget/InkingDynamicWidget.h>
#include <window/InkingWindow.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

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
/** 扫页面表的上限，纯粹防呆。 */
constexpr int kPageScanLimit = 64;

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
 */
class SideLayer final : public inking::InkingDynamicWidget
{
public:
    SideLayer(inking::InkingWidget* parent, const inking::AnchorData& data, std::uint32_t color)
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
    /** 顶栏以下的那一条：横向排「侧栏 + 内容区」。 */
    InkingStaticPanel* body = nullptr;
    SideLayer* sidebar = nullptr;
    InkingStaticPanel* content = nullptr;
    /** 侧栏现在是展开的吗。收起 = 隐藏 + 让主体重排（内容区自动吃满）。 */
    bool sidebarVisible = true;
    /** 内容区当前挂着的那一页；没有页面时是 nullptr。 */
    inking::InkingWidget* currentPage = nullptr;
    /** 弹窗遮罩：常驻，弹的时候设可见。 */
    InkingStaticButton* backdrop = nullptr;
    /** 当前那块弹窗内容；每 push 一次重建一次（静态面板不能改尺寸）。 */
    InkingStaticPanel* popup = nullptr;
    const golding::GoldingPopupDecl* popupDecl = nullptr;
    bool popupOpen = false;
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

/** 把一项挂进区域容器：填了 `create` 就交给插件自己画，否则按 `title` 造一个按钮。 */
void placeRegionItem(const GoldingRegionItemDecl* item, inking::InkingWidget* container,
                     const ButtonData& look, bool useMenuBar)
{
    if (item == nullptr || container == nullptr)
    {
        return;
    }

    if (item->create != nullptr)
    {
        // 契约：插件把新建子树的根挂到 parent 上。顶栏这一支会排在菜单栏的
        // 弹簧与窗口按钮之后（顺序由 MenuBar 内部维护），要精确位置就用按钮形态。
        if (item->create(g_host.Context(), container) == nullptr)
        {
            g_host.Warn((std::string("区域项内容创建失败：") + (item->id != nullptr ? item->id : "(无名)")).c_str());
        }
        return;
    }

    if (item->title == nullptr || item->title[0] == '\0')
    {
        return; // 既没有界面也没有名字：没什么可画的
    }

    if (useMenuBar)
    {
        auto* bar = static_cast<inking::prefab::MenuBar*>(container);
        // 名字用区域项自己的 id：组件树里能看出"这一项是谁注册的"，
        // 自检 / 自动化也能按名字命中（`InkingScene::Activate("settings.entry")`）。
        ButtonData menuLook = look;
        menuLook.name = item->id != nullptr ? item->id : "menubar.item";
        bar->AddButton(menuLook, item->title, [item, host = g_host] {
            if (item->onActivate != nullptr)
            {
                item->onActivate(host.Context());
            }
        });
        return;
    }

    // 直接 new 一条按钮时，文字要自己填进 `ButtonData::label`——`AddButton` 那条路
    // 是由 MenuBar 代填的，这里没有那层，漏了就得到"白底无字"（实测踩过两次）。
    ButtonData data = look;
    data.label = item->title;

    auto* button = static_cast<InkingStaticButton*>(
        container->AddChild(std::make_unique<InkingStaticButton>(container, data)));
    if (button == nullptr)
    {
        return;
    }
    button->SetName(item->id != nullptr ? item->id : item->title);
    button->SetOnClicked([item, host = g_host] {
        if (item->onActivate != nullptr)
        {
            item->onActivate(host.Context());
        }
    });
}

/** 把侧栏的展开/收起落到几何上：改宽度 → 主体重排（内容区的 `fillMain` 自动跟着变）。 */
void applySidebarState(const std::shared_ptr<ShellParts>& parts)
{
    if (!parts || parts->sidebar == nullptr)
    {
        return;
    }
    parts->sidebar->Resize(parts->sidebarVisible ? kSidebarWidth : 0, parts->sidebar->GetHeight());
    parts->sidebar->SetVisible(parts->sidebarVisible);
    if (parts->body != nullptr)
    {
        parts->body->Relayout();
    }
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
    const golding::GoldingPopupDecl* decl = parts.popupDecl;
    parts.popupDecl = nullptr;
    if (decl != nullptr && decl->onClose != nullptr)
    {
        decl->onClose(nullptr);
    }
}

int popupPush(void* /*self*/, const golding::GoldingPopupDecl* decl)
{
    if (!g_parts || decl == nullptr || decl->create == nullptr)
    {
        return 1;
    }
    ShellParts& parts = *g_parts;

    // 静态面板"几何构造即定型"，改不了尺寸——所以每次 push 重建一块，
    // 而不是留一块常驻再 Resize。push 是低频动作（弹对话框），重建的代价可以忽略。
    if (parts.popup != nullptr)
    {
        parts.root->RemoveChild(parts.popup);
        parts.popup = nullptr;
    }
    parts.popupDecl = decl;

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

    // hostContext 传 nullptr：调用方是在自己的代码里发起的，需要 HostApi 的话
    // 应当把自己那份 `golding::Host` 存下来（见 `GoldingPopupDecl::create` 的注释）。
    if (decl->create(nullptr, parts.popup) == nullptr)
    {
        parts.root->RemoveChild(parts.popup);
        parts.popup = nullptr;
        parts.popupDecl = nullptr;
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

const golding::PopupService kPopupService{GOLDING_PLUGIN_ABI_VERSION, &popupPush, &popupPop};

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
    // 收拉**不改尺寸**（静态面板几何构造即定型），而是"隐藏侧栏 + 让主体重排"：
    // 侧栏在横向 Stack 里，隐藏之后不再占位，内容区的 `fillMain` 自动吃满。
    // 这样收起/展开都不重建任何节点，页面与输入状态原样保留。
    ButtonData toggleLook = makeBarButton("GoldingGamestoryShell.toggle", 40);
    toggleLook.text.fontSize = 18.0f; // ≡ 小字号下几乎看不清
    parts->bar->AddButton(toggleLook, "≡", [parts] {
        parts->sidebarVisible = !parts->sidebarVisible;
        applySidebarState(parts);
        std::printf("[shell] 侧栏%s\n", parts->sidebarVisible ? "展开" : "收起");
    });

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
    // 为什么要有这一层：收拉侧栏靠"隐藏 + 重排"，而"剩下的宽度归内容区"必须有一个
    // 横向布局的父级来算。挂根上做不到——根不排布（见上）。
    PanelData bodyData = makePanel("GoldingGamestoryShell.body", width, height - barHeight, theme::kSurface,
                                   "panelSurface");
    bodyData.offsetY = static_cast<float>(barHeight);
    bodyData.layout.kind = inking::LayoutKind::Stack;
    bodyData.layout.axis = inking::LayoutAxis::Horizontal;
    bodyData.layout.gap = 0.0f;
    parts->body = static_cast<InkingStaticPanel*>(
        parts->root->AddChild(std::make_unique<InkingStaticPanel>(parts->root, bodyData)));

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

    // ---- 右侧内容区 ----
    //
    // 宽度写 0 + `fillMain`：宽度由主体算（侧栏收/展都会变），不在这里抄一个数。
    // 纵向 Stack：页面根按交叉轴 `Stretch` 拉满宽度（0.4.0 的默认对齐）。
    PanelData contentData =
        makePanel("GoldingGamestoryShell.content", 0, height - barHeight, theme::kSurface, "panelSurface");
    contentData.layout.kind = inking::LayoutKind::Stack;
    contentData.layout.axis = inking::LayoutAxis::Vertical;
    contentData.layout.gap = 0.0f;
    contentData.layoutItem.fillMain = true;
    parts->content = static_cast<InkingStaticPanel*>(
        parts->body->AddChild(std::make_unique<InkingStaticPanel>(parts->body, contentData)));

    // ---- 区域条目：menubar 与 sidebar 各扫一遍 ----
    //
    // 扫的是**注册表快照**：区域项都在各插件的 `Register` 里登记，而装配（本函数）
    // 发生在全部 `Register` 之后，所以这里能看到全部。之后再注册就看不到了——
    // 那是"插件必须在 Register 里注册"的必然结果，不是缺陷。
    const int menuItems = g_host.RegionItemCount("menubar");
    for (int index = 0; index < menuItems; ++index)
    {
        placeRegionItem(g_host.RegionItemAt("menubar", index), parts->bar, makeBarButton("menubar.item", 96), true);
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
    parts->backdrop->SetOnClicked([] { closePopup(); });

    // 启动模式 `--collapsed`：一进来侧栏就是收起的。
    // 宿主只把命令行原样交出来，"哪种模式是什么意思"是面板自己的知识（`bootMode` 契约）。
    if (std::string(g_host.BootMode()) == "--collapsed")
    {
        parts->sidebarVisible = false;
        applySidebarState(parts);
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
        return ok;
    }

    int Ready(const golding::Host& host) override { return runReady(host); }
};

} // namespace

GOLDING_EXPORT_PLUGIN(GoldingGamestoryShellPlugin)
