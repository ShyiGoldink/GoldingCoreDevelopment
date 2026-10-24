#include "GoldingMenu.h"
#include "GoldingServices.h"
#include "GoldingSettings.h"

#include <sdk/GoldingPlugin.h>

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <dataStruct/InkingTextLayout.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/PrefabTheme.h>
#include <text/InkingStaticLabel.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// settings —— 设置入口（普通插件，不是面板）。
//
// 它只做两件事：
//   1. 往 `menubar` 区域注册一项「设置」；
//   2. 被点时弹出一个对话框，把**别人注册的设置区块**列出来。
//
// 自己不带任何配置项。设置项属于各自的功能插件：它们用 `AddSettingsSection`
// 登记，本插件负责"把它们摆出来"——这样加一个设置项不需要改这里。
//
// 依赖 `GoldingGamestoryShell`：区域项要求区域**已经存在**，而区域由面板插件在
// 自己的 `Register` 里声明。没有 `requires.plugins` 就只能靠扫描顺序碰运气。
// ---------------------------------------------------------------------------

namespace
{

namespace theme = inking::prefab::theme;

golding::Host g_host{nullptr, nullptr};
/** 弹窗服务；`Ready`/点击时用得上，`Register` 里存下来。 */
const golding::PopupService* g_popup = nullptr;
/** 菜单服务（面板插件提供）；拿不到时退回"直接弹模态框"。 */
const golding::MenuService* g_menu = nullptr;

constexpr int kPopupWidth = 720;
constexpr int kPopupHeight = 560;

/**
 * 这次弹窗只显示哪一个区块（空 = 全部）。
 *
 * 为什么要一个文件级静态量：弹窗的 `create` 是 C 函数指针，**没有参数位**
 * 传"用户点了哪一项"。点击发生在菜单项回调里，那里把 id 写进来，`create`
 * 再读它——两次调用在同一条点击栈上，不会串。
 */
std::string g_onlySectionId;

/** 弹窗内容：标题 + 已注册的设置区块 + 关闭按钮。 */
GoldingWidgetHandle createSettingsPanel(void* /*hostContext*/, void* parent)
{
    auto* popup = static_cast<inking::InkingWidget*>(parent);
    if (popup == nullptr)
    {
        return nullptr;
    }

    inking::PanelData root;
    root.name = "settings.panel";
    root.width = popup->GetWidth();
    root.height = popup->GetHeight();
    root.startOpen = true;
    root.closed.color = theme::kSurface;
    root.closed.themeColor = "panelSurface";
    root.open = root.closed;
    root.shape = inking::ShapeSpec::Rect();
    root.layout.kind = inking::LayoutKind::Stack;
    root.layout.axis = inking::LayoutAxis::Vertical;
    root.layout.gap = 14.0f;
    root.layout.padding = inking::LayoutSpacing{24.0f, 24.0f, 24.0f, 24.0f};

    auto* panel = static_cast<inking::InkingStaticPanel*>(
        popup->AddChild(std::make_unique<inking::InkingStaticPanel>(popup, root)));

    // 只显示一个区块时标题用它自己的名字：从菜单点进来的用户已经选过一次了，
    // 还写"设置"会让人以为进错了地方。
    const GoldingSettingsDecl* single = nullptr;
    if (!g_onlySectionId.empty())
    {
        for (int index = 0; index < g_host.SettingsSectionCount(); ++index)
        {
            const GoldingSettingsDecl* section = g_host.SettingsSectionAt(index);
            if (section != nullptr && section->id != nullptr && g_onlySectionId == section->id)
            {
                single = section;
                break;
            }
        }
    }

    inking::LabelData title;
    title.name = "settings.title";
    title.height = 36;
    title.text = single != nullptr ? (single->title != nullptr ? single->title : single->id) : "设置";
    title.color = theme::kTextPrimary;
    title.colorToken = "textPrimary";
    title.style.fontSize = 22.0f;
    title.style.alignV = inking::TextAlignV::Middle;
    panel->AddChild(std::make_unique<inking::InkingStaticLabel>(panel, title));

    // 区块的**唯一来源**是宿主的设置表：谁注册了什么，这里就摆什么。
    // 本插件不认识任何具体设置项的名字，也不该认识。
    const int count = g_host.SettingsSectionCount();
    int mounted = 0;
    if (single != nullptr)
    {
        if (g_host.MountSettingsSection(single->id, panel) != nullptr)
        {
            ++mounted;
        }
        else
        {
            g_host.Warn((std::string("设置区块挂载失败：") + single->id).c_str());
        }
    }
    else
    {
        for (int index = 0; index < count; ++index)
        {
            const GoldingSettingsDecl* section = g_host.SettingsSectionAt(index);
            if (section == nullptr || section->id == nullptr)
            {
                continue;
            }
            if (g_host.MountSettingsSection(section->id, panel) != nullptr)
            {
                ++mounted;
            }
            else
            {
                g_host.Warn((std::string("设置区块挂载失败：") + section->id).c_str());
            }
        }
    }

    if (count == 0)
    {
        inking::LabelData empty;
        empty.name = "settings.empty";
        empty.height = 60;
        empty.text = "暂时没有可配置项（还没有插件注册设置区块）";
        empty.color = theme::kTextMuted;
        empty.colorToken = "textMuted";
        empty.style.fontSize = 15.0f;
        empty.style.alignV = inking::TextAlignV::Middle;
        panel->AddChild(std::make_unique<inking::InkingStaticLabel>(panel, empty));
    }

    // ---- 关闭按钮 ----
    //
    // 挂在**弹窗本身**上（不是那个 Stack 容器里）并按绝对坐标贴右下。
    // 不用"弹簧 + 按钮"那套：实测求解器给 `fillMain` 分剩余空间时**不为它后面的项预留**，
    // 于是按钮会被挤出容器、只剩一条边（和 MenuBar 弹簧把窗口按钮顶出画布是同一个根因）。
    inking::ButtonData close;
    close.name = "settings.close";
    close.width = 120;
    close.height = 36;
    close.normal.type = inking::AppearanceType::Color;
    close.normal.color = theme::kSurfaceSunken;
    close.normal.themeColor = "clipSurface";
    close.hover.type = inking::AppearanceType::Color;
    close.hover.color = theme::kHoverNeutral;
    close.onclicked.type = inking::AppearanceType::Color;
    close.onclicked.color = theme::kAccentSoft;
    close.hoverInheritsNormal = false;
    close.onclickedInheritsNormal = false;
    close.label = "关闭";
    close.textColor = theme::kTextPrimary;
    close.text.fontSize = 15.0f;
    close.text.align = inking::TextAlignH::Center;
    close.text.alignV = inking::TextAlignV::Middle;
    close.shape = inking::ShapeSpec::RoundedRect(8.0f);
    close.offsetX = static_cast<float>(popup->GetWidth() - 120 - 24);
    close.offsetY = static_cast<float>(popup->GetHeight() - 36 - 24);
    close.zIndex = 1; // 后加 + 显式压一层，保证在内容面板之上
    auto* closeButton = static_cast<inking::InkingStaticButton*>(
        popup->AddChild(std::make_unique<inking::InkingStaticButton>(popup, close)));
    closeButton->SetOnClicked([] {
        if (g_popup != nullptr)
        {
            g_popup->pop(nullptr);
        }
    });

    std::printf("[settings] 设置面板：%d 个区块，挂上 %d 个\n", count, mounted);
    return panel;
}

const golding::GoldingPopupDecl kSettingsPopupAll = {
    "settings.popup.all", kPopupWidth, kPopupHeight, 0, 0, 1, &createSettingsPanel, nullptr};
/** 单区块用的小一号：内容只有一块，开 720×560 会空一大片。 */
const golding::GoldingPopupDecl kSettingsPopupOne = {
    "settings.popup.one", 560, 420, 0, 0, 1, &createSettingsPanel, nullptr};

void pushSettingsPopup(const golding::GoldingPopupDecl* decl)
{
    if (g_popup == nullptr)
    {
        g_host.Warn("拿不到 golding.popup：设置弹窗打不开");
        return;
    }
    if (g_popup->push(nullptr, decl) != 0)
    {
        g_host.Warn("设置弹窗弹出失败");
    }
}

/** 菜单项被点：只弹那一个区块。 */
void onSettingsItem(void* /*hostContext*/, const char* itemId)
{
    if (itemId == nullptr || itemId[0] == '\0')
    {
        return;
    }
    g_onlySectionId = itemId;
    pushSettingsPopup(&kSettingsPopupOne);
    g_onlySectionId.clear();
}

/**
 * 在 `anchorRegionItemId` 这个顶栏按钮下面打开设置菜单（列区块）。
 *
 * 为什么锚点是"区域项 id"：面板才知道那个按钮在哪（`GoldingMenu.h` 同一条），
 * 所以别人用自己注册的按钮打开设置内容时，也只需要交自己的区域项 id。
 */
int openSettingsMenu(const char* anchorRegionItemId)
{
    if (g_menu == nullptr)
    {
        // 面板不支持菜单：退回"直接弹完整设置框"。降级而不是报错，功能还在。
        g_onlySectionId.clear();
        pushSettingsPopup(&kSettingsPopupAll);
        return 0;
    }

    // 菜单声明只需要在 `open` 调用期间有效（面板会深拷贝整棵树），所以这里
    // 直接拼一份栈上的 `vector`：区块列表本来就是"装配后才确定"的动态内容。
    std::vector<golding::MenuItem> items;
    const int count = g_host.SettingsSectionCount();
    items.reserve(static_cast<std::size_t>(count > 0 ? count : 1));
    for (int index = 0; index < count; ++index)
    {
        const GoldingSettingsDecl* section = g_host.SettingsSectionAt(index);
        if (section == nullptr || section->id == nullptr)
        {
            continue;
        }
        golding::MenuItem item{};
        item.id = section->id;
        item.label = section->title != nullptr ? section->title : section->id;
        item.onActivate = &onSettingsItem;
        items.push_back(item);
    }
    if (items.empty())
    {
        // 空态也是一项：菜单里至少要有一行，否则 `open` 会判"没有可见项"。
        golding::MenuItem empty{};
        empty.label = "（暂时没有可配置项）";
        items.push_back(empty);
    }

    golding::MenuDecl decl{"settings.menu", items.data(), static_cast<int>(items.size())};
    if (g_menu->open(nullptr, anchorRegionItemId, &decl) != 0)
    {
        g_host.Warn("设置菜单打开失败，退回弹窗");
        g_onlySectionId.clear();
        pushSettingsPopup(&kSettingsPopupAll);
        return 1;
    }
    return 0;
}

/**
 * 顶栏「设置」被点：先把设置区块列成一份**多层级浮窗菜单**（`golding.menu`），
 * 点某一项才弹那块内容。
 *
 * 为什么不是"点一下直接弹模态框"：模态框会盖住整个窗口，而设置经常是
 * "看一眼就走"的动作——菜单浮窗不阻断，点外面就收。
 */
void onMenuActivate(void* /*hostContext*/)
{
    openSettingsMenu("settings.entry");
}

// ---- `golding.settings` 服务 ----

int settingsOpenAll(void* /*self*/)
{
    g_onlySectionId.clear();
    pushSettingsPopup(&kSettingsPopupAll);
    return 0;
}

int settingsOpenSection(void* /*self*/, const char* sectionId)
{
    if (sectionId == nullptr || sectionId[0] == '\0')
    {
        return 1;
    }
    g_onlySectionId = sectionId;
    pushSettingsPopup(&kSettingsPopupOne);
    g_onlySectionId.clear();
    return 0;
}

int settingsOpenMenuAt(void* /*self*/, const char* regionItemId)
{
    return openSettingsMenu(regionItemId);
}

const golding::SettingsService kSettingsService{
    GOLDING_PLUGIN_ABI_VERSION,
    &settingsOpenAll,
    &settingsOpenSection,
    &settingsOpenMenuAt,
};

const GoldingRegionItemDecl kMenuEntry = {
    "menubar",       // 区域：面板插件自己定的约定
    "settings.entry", // 区域内唯一 id
    "设置",           // 显示名
    nullptr,         // 图标：暂不画
    100,             // 排在其他入口之后（仍由面板决定怎么排）
    nullptr,         // 不自己画，交给面板画成按钮
    &onMenuActivate,
};

class SettingsPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        g_host = host;

        // 服务在这里取一次：它由面板插件提供，而 `requires.plugins` 保证了面板先 entry。
        g_popup = host.Use<golding::PopupService>(golding::kPopupServiceName, 1);
        if (g_popup == nullptr)
        {
            // 不算失败：没有面板就没有设置入口，这是"面板缺席"的合法降级。
            // 区域项本身也会被驳回（区域不存在），同样不影响别的插件。
            host.Warn("没有 golding.popup：设置入口将不可用");
        }

        // 菜单是主入口、弹窗是降级路径：拿不到就退回"点一下直接弹框"。
        // 不写进 `plugin.json` 的 `needs`，因为它不是硬前提。
        g_menu = host.Use<golding::MenuService>(golding::kMenuServiceName, 1);
        if (g_menu == nullptr)
        {
            host.Warn("没有 golding.menu：设置改为直接弹窗");
        }

        // 服务：让别的插件也能"打开设置"（它们不该重写一遍列区块 + 弹窗）。
        bool ok = host.AddService(golding::kSettingsServiceName, &kSettingsService);
        if (!ok)
        {
            host.Warn("golding.settings 服务注册失败（同名已被占用？）");
        }
        return host.AddRegionItem(kMenuEntry) && ok;
    }
};

} // namespace

GOLDING_EXPORT_PLUGIN(SettingsPlugin)
