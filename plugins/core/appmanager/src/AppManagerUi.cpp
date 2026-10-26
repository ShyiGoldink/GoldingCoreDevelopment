#include "AppManagerUi.h"

#include "GoldingServices.h"

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <dataStruct/InkingImageFit.h>
#include <dataStruct/InkingTextLayout.h>
#include <image/ImageData.h>
#include <image/InkingStaticImage.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/FilePicker.h>
#include <prefab/PrefabTheme.h>
#include <text/InkingStaticLabel.h>
#include <video/InkingVideo.h>
#include <video/VideoData.h>
#include <widget/InkingWidget.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace appmanager
{

namespace
{

namespace theme = inking::prefab::theme;
using inking::ButtonData;
using inking::ImageData;
using inking::ImageFit;
using inking::InkingChangeAnchor;
using inking::InkingStaticButton;
using inking::InkingStaticImage;
using inking::InkingStaticLabel;
using inking::InkingStaticPanel;
using inking::InkingStaticVideo;
using inking::LabelData;
using inking::PanelData;
using inking::VideoData;

/** 侧栏一行的高度 / 行间距 / 宽（侧栏宽 220 减两侧 padding 10）。 */
constexpr int kRowHeight = 48;
constexpr int kRowGap = 6;
constexpr int kRowWidth = 200;
/** 行图标尺寸与左右留白。 */
constexpr int kIconSize = 26;
constexpr int kIconLeft = 16;
/** 盒子高度：够放二十多行，超出的被侧栏裁掉（侧栏不开滚动）。 */
constexpr int kSidebarBoxHeight = 1400;
/** 页面底部那一排按钮：启动 / 设置，以及最左边那只小眼睛。 */
constexpr int kStartWidth = 240;
constexpr int kStartHeight = 64;
constexpr int kGearSize = 64;
/** 按钮组内部的间距 / 距内容区右边 / 距底边（都按**内容区**量，不按舞台）。 */
constexpr int kGroupGap = 12;
constexpr int kRightInset = 48;
constexpr int kBottomInset = 32;
/**
 * 背景纯享那只眼睛：**排在「启动」左边**，跟着下面那排走。
 *
 * 早先贴在右上角，结果紧挨着自绘的窗口按钮（- / ×，z=200）——那一块是"标题栏"的
 * 位置，眼睛挤在那儿既是视觉噪声，也容易被窗口按钮压住。放进这一排之后它左右都是
 * 自己人，纯享态下又正好是唯一留下的那个按钮。
 */
constexpr int kEyeSize = 40;
/** 眼睛按钮与其图标的节点名（换图标时要按名字找回那个图片节点）。 */
constexpr const char* kEyeNodeName = "appmanager.eye";
constexpr const char* kEyeIconNodeName = "appmanager.eye.icon";

UiState g_state;
/**
 * 点侧栏那一行时，要不要"它已经在跑就直接进去"。
 *
 * 只在**自检**期间临时置 false：自检会语义点击侧栏行（验"行回调 → 选中 → 切页"整条链），
 * 那时如果那一项恰好在跑，`SetForegroundWindow` 会把用户的桌面抢走——
 * 自检不许有这样的副作用（`docs/APPMANAGER.md` §8 的两条纪律）。
 */
bool g_enterFromSidebar = true;
/** 行 → 图标节点（换图标时直接 `LoadAsset`，不必重建行）。 */
std::map<std::string, inking::InkingStaticImage*> g_rowIcons;
/** 行 → 按钮（改选中色用）。 */
std::map<std::string, InkingStaticButton*> g_rows;

/** 这次"选择文件"是给图标还是背景。 */
enum class PickKind
{
    Icon,
    Background,
};
PickKind g_pickKind = PickKind::Icon;
std::string g_pickAppId;
std::string g_settingsAppId;
/** 两个弹窗各被建过几次（`PopupService` 没有"开着吗"的查询口，用它当观测口）。 */
int g_settingsBuilds = 0;
int g_importBuilds = 0;

PanelData makePanel(const char* name, int width, int height, std::uint32_t color)
{
    PanelData data;
    data.name = name;
    data.width = width;
    data.height = height;
    data.startOpen = true;
    data.closed.color = color;
    data.closed.themeColor = nullptr;
    data.open = data.closed;
    data.shape = inking::ShapeSpec::Rect();
    return data;
}

ButtonData makeRowButton(const std::string& name, int width, float textLeft)
{
    ButtonData data;
    data.name = name;
    data.width = width;
    data.height = kRowHeight;
    data.normal.type = inking::AppearanceType::Color;
    data.normal.color = theme::kSurfaceSunken; // 与侧栏同色：视觉上是一条条"隔空"
    data.hover.type = inking::AppearanceType::Color;
    data.hover.color = theme::kHoverNeutral;
    data.onclicked.type = inking::AppearanceType::Color;
    data.onclicked.color = theme::kAccentSoftPressed;
    data.hoverInheritsNormal = false;
    data.onclickedInheritsNormal = false;
    data.textColor = theme::kTextPrimary;
    data.text.fontSize = 15.0f;
    data.text.align = inking::TextAlignH::Left;
    data.text.alignV = inking::TextAlignV::Middle;
    data.text.leftSpace = textLeft;
    data.shape = inking::ShapeSpec::RoundedRect(10.0f);
    return data;
}

void warn(const std::string& message) { std::printf("[appmanager] %s\n", message.c_str()); }

/** 建一行（挂到盒子末尾）。 */
InkingStaticButton* createRow(const AppEntry& entry)
{
    inking::InkingWidget* box = g_state.sidebarBox;
    if (box == nullptr)
    {
        return nullptr;
    }
    const std::string rowName = "appmanager.row." + entry.id;
    ButtonData data = makeRowButton(rowName, kRowWidth, static_cast<float>(kIconLeft + kIconSize + 16));
    data.label = entry.title;
    auto* row = static_cast<InkingStaticButton*>(
        box->AddChild(std::make_unique<InkingStaticButton>(box, data)));
    if (row == nullptr)
    {
        return nullptr;
    }
    row->SetName(rowName);
    row->SetOnClicked([id = entry.id] {
        // 先切页面（"选中"的本义：右侧先变成这一项的介绍），再看要不要直接进它的窗口。
        //
        // 顺序不能反：`SelectApp` 会让面板重建页面（摘旧页、挂新页），而"切前台"会把
        // 焦点交给别人的窗口——先切页再跳走，用户看到的是"我选了它，然后它已经在跑"，
        // 而不是"我的窗口被抢走之后才偷偷换了页"。
        SelectApp(id);
        // 观测口：先记一笔"这一行真的走到了进入分支"，再决定要不要动前台。
        // （自检点行时会关掉 `g_enterFromSidebar`，但这一步仍然会走，接线因此可断言。）
        ++g_state.enterAttempts;
        if (!g_enterFromSidebar)
        {
            return;
        }
        if (const AppEntry* selected = FindApp(id); selected != nullptr && !EnterRunning(*selected))
        {
            // 两条分支都要留一行：真机上"点了一行却没反应"时，这行就是唯一的解释
            // （没在运行 / reuse=0 / 抢不到前台都会走到这里）。
            std::printf("[appmanager] 侧栏：%s 没在运行（或不允许进入），只切了页面\n", id.c_str());
        }
    });
    g_rows[entry.id] = row;

    const std::filesystem::path icon = IconPath(entry);
    if (!icon.empty())
    {
        ImageData image;
        image.name = "appmanager.icon." + entry.id;
        image.width = kIconSize;
        image.height = kIconSize;
        image.offsetX = static_cast<float>(kIconLeft);
        image.offsetY = static_cast<float>((kRowHeight - kIconSize) / 2);
        image.normal.type = inking::AppearanceType::Image;
        image.normal.source = ToUtf8(icon);
        image.normal.imageFit = ImageFit::Contain;
        image.runtimeAsset = true;   // 运行期读绝对路径（AssetResolve 对绝对路径原样返回）
        image.hitPassThrough = true; // 点图标要落到下面的按钮上
        auto* node = static_cast<InkingStaticImage*>(
            row->AddChild(std::make_unique<InkingStaticImage>(row, image)));
        if (node != nullptr)
        {
            g_rowIcons[entry.id] = node;
        }
    }
    return row;
}

/** "＋ 添加应用"那一行。 */
void buildAddRow()
{
    inking::InkingWidget* box = g_state.sidebarBox;
    if (box == nullptr)
    {
        return;
    }
    ButtonData data = makeRowButton("appmanager.row.add", kRowWidth, static_cast<float>(kIconLeft));
    data.label = "＋ 添加应用";
    data.textColor = theme::kTextMuted;
    auto* row = static_cast<InkingStaticButton*>(
        box->AddChild(std::make_unique<InkingStaticButton>(box, data)));
    if (row == nullptr)
    {
        return;
    }
    row->SetName("appmanager.row.add");
    row->SetOnClicked([] { OpenImportPopup(); });
    g_state.addRow = row;
}

/** 新行插在"添加"行前面：摘下尾行 → 挂新行 → 尾行挂回去（全程不销毁节点）。 */
void appendRow(const AppEntry& entry)
{
    inking::InkingWidget* box = g_state.sidebarBox;
    if (box == nullptr)
    {
        return;
    }
    if (createRow(entry) == nullptr)
    {
        return;
    }
    if (g_state.addRow != nullptr)
    {
        std::unique_ptr<inking::InkingWidget> tail = box->RemoveChild(g_state.addRow);
        if (tail != nullptr)
        {
            box->AddChild(std::move(tail));
        }
    }
    box->Relayout();
}

/** 选中色：当前应用用强调色，其余用正文色。 */
void refreshRowSelection()
{
    for (const auto& [id, row] : g_rows)
    {
        if (row == nullptr)
        {
            continue;
        }
        row->SetTextColor(id == g_state.selectedId ? theme::kAccentStrong : theme::kTextPrimary);
    }
}

/** 换图标：直接让那个图片节点重读文件（不重建行，所以在任何回调栈上都安全）。 */
void updateRowIcon(const AppEntry& entry)
{
    const auto found = g_rowIcons.find(entry.id);
    if (found == g_rowIcons.end() || found->second == nullptr)
    {
        return;
    }
    const std::filesystem::path icon = IconPath(entry);
    if (!icon.empty())
    {
        found->second->LoadAsset(ToUtf8(icon));
    }
}

// ---- 背景纯享：藏 / 显页面上的界面层 ----
//
// 为什么按**名字**找节点、而且每次都从界面根找一遍：页面是"每次导航重建"的
// （面板不缓存页面），跨页存着某个节点的裸指针，换个应用之后再点眼睛就是
// use-after-free。名字是页面自己定的，重建后照样对得上。

/** 纯享时要隐身的节点：页面根的直接孩子里属于"界面层"的那些（背景与眼睛除外）。 */
bool isOverlayNode(const std::string& name)
{
    return name == "appmanager.page.plate" || name == "appmanager.page.title" ||
           name == "appmanager.page.path" || name == "appmanager.launch" ||
           name == "appmanager.settings";
}

inking::InkingWidget* childByName(inking::InkingWidget* parent, const std::string& name)
{
    if (parent == nullptr)
    {
        return nullptr;
    }
    for (inking::InkingWidget* child : parent->GetChildren())
    {
        if (child != nullptr && child->GetName() == name)
        {
            return child;
        }
    }
    return nullptr;
}

/** 从界面根按名字找当前页面根；没装这一页时返回 nullptr。 */
inking::InkingWidget* findPageRoot()
{
    auto* top = static_cast<inking::InkingWidget*>(g_state.host.UiRoot());
    if (top == nullptr)
    {
        return nullptr;
    }
    // 迭代式深度优先 + 节点数上限：页面根在第 4 层（面板根 → body → 内容区 → 页面），
    // 上限只是防呆——界面树是几百个节点，这个循环几乎零成本。
    std::vector<inking::InkingWidget*> stack{top};
    int visited = 0;
    while (!stack.empty() && visited < 1024)
    {
        inking::InkingWidget* node = stack.back();
        stack.pop_back();
        ++visited;
        if (node == nullptr)
        {
            continue;
        }
        if (node->GetName() == kPageRootNodeName)
        {
            return node;
        }
        for (inking::InkingWidget* child : node->GetChildren())
        {
            if (child != nullptr)
            {
                stack.push_back(child);
            }
        }
    }
    return nullptr;
}

/** 把纯享态落到一棵页面上（装配期用已知的根，运行期用找到的根）。 */
void applyOverlayVisibility(inking::InkingWidget* root, bool immersive)
{
    if (root == nullptr)
    {
        return;
    }
    for (inking::InkingWidget* child : root->GetChildren())
    {
        if (child != nullptr && isOverlayNode(child->GetName()))
        {
            child->SetVisible(!immersive);
        }
    }
    // 眼睛图标跟着换：睁眼 = 界面还在（点它进纯享），闭眼 = 已经在纯享。
    auto* icon = static_cast<InkingStaticImage*>(childByName(childByName(root, kEyeNodeName), kEyeIconNodeName));
    if (icon != nullptr)
    {
        const std::filesystem::path path = BuiltinIconPath(immersive ? "eye-off" : "eye");
        if (!path.empty())
        {
            icon->LoadAsset(ToUtf8(path));
        }
    }
}

// ---- 弹窗：选择文件 ----

void applyPickedFile(const std::filesystem::path& source)
{
    AppEntry* entry = FindApp(g_pickAppId);
    if (entry == nullptr)
    {
        return;
    }
    const char* kind = g_pickKind == PickKind::Icon ? "icon" : "background";
    const std::string relative = CopyAsset(entry->id, source, kind);
    if (relative.empty())
    {
        warn("复制素材失败：" + ToUtf8(source));
        return;
    }
    if (g_pickKind == PickKind::Icon)
    {
        entry->icon = relative;
        SaveEntry(*entry);
        updateRowIcon(*entry);
        warn("图标已更新：" + entry->id + " → " + relative);
    }
    else
    {
        entry->backgroundType = IsVideoFile(source) ? "video" : "image";
        entry->backgroundPath = relative;
        SaveEntry(*entry);
        RefreshPage();
        warn("背景已更新：" + entry->id + " → " + relative + "（" + entry->backgroundType + "）");
    }
    if (g_state.popup != nullptr)
    {
        g_state.popup->pop(nullptr);
    }
    // 回到设置弹窗：用户刚改完，应该看见结果。
    OpenSettingsPopup(entry->id);
}

GoldingWidgetHandle createPickPopup(void* /*hostContext*/, void* parent)
{
    auto* content = static_cast<inking::InkingWidget*>(parent);
    if (content == nullptr)
    {
        return nullptr;
    }
    const int width = content->GetWidth() > 0 ? content->GetWidth() : 900;
    const int height = content->GetHeight() > 0 ? content->GetHeight() : 620;

    LabelData title;
    title.name = "appmanager.pick.title";
    title.width = width - 48;
    title.height = 28;
    title.offsetX = 24.0f;
    title.offsetY = 16.0f;
    title.text = g_pickKind == PickKind::Icon ? "选择图标（png / jpg / bmp / webp / svg）"
                                             : "选择背景（图片或视频：png / jpg / mp4 / webm …）";
    title.color = theme::kTextPrimary;
    title.colorToken = "textPrimary";
    title.style.fontSize = 17.0f;
    title.style.alignV = inking::TextAlignV::Middle;
    content->AddChild(std::make_unique<InkingStaticLabel>(content, title));

    PanelData board = makePanel("appmanager.pick.board", width, height - 56, theme::kSurface);
    board.offsetY = 56.0f;
    auto* boardPanel = static_cast<InkingStaticPanel*>(
        content->AddChild(std::make_unique<InkingStaticPanel>(content, board)));
    if (boardPanel == nullptr)
    {
        return nullptr;
    }

    inking::prefab::FilePickerStyle style = inking::prefab::FilePickerStyle::MakeDefault();
    if (g_pickKind == PickKind::Icon)
    {
        style.extensions = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".svg"};
    }
    else
    {
        style.extensions = {".png", ".jpg", ".jpeg", ".bmp", ".webp", ".mp4", ".webm", ".mov", ".mkv"};
    }
    style.acceptText = "使用这个";

    auto* picker = static_cast<inking::prefab::FilePicker*>(
        boardPanel->AddChild(std::make_unique<inking::prefab::FilePicker>(boardPanel, style)));
    if (picker == nullptr)
    {
        return nullptr;
    }
    const std::filesystem::path home = inking::prefab::FilePicker::HomeDirectory();
    if (!home.empty())
    {
        picker->SetDirectory(home);
    }
    picker->accepted.Subscribe([](const std::vector<std::string>& paths) {
        if (!paths.empty())
        {
            applyPickedFile(FromUtf8(paths.front()));
        }
    });
    picker->canceled.Subscribe([] {
        if (g_state.popup != nullptr)
        {
            g_state.popup->pop(nullptr);
        }
    });
    return boardPanel;
}

void openPickPopup(PickKind kind, const std::string& appId)
{
    g_pickKind = kind;
    g_pickAppId = appId;
    const golding::GoldingPopupDecl decl{"appmanager.pick", 900, 620, 0, 0, 1, &createPickPopup, nullptr};
    if (g_state.popup != nullptr && g_state.popup->push(nullptr, &decl) != 0)
    {
        warn("选择文件弹窗打不开");
    }
}

// ---- 弹窗：应用设置 ----

void clearBackground(const std::string& appId)
{
    AppEntry* entry = FindApp(appId);
    if (entry == nullptr)
    {
        return;
    }
    entry->backgroundType = "none";
    entry->backgroundPath.clear();
    SaveEntry(*entry);
    RefreshPage();
    if (g_state.popup != nullptr)
    {
        g_state.popup->pop(nullptr);
    }
    OpenSettingsPopup(appId);
}

GoldingWidgetHandle createSettingsPopup(void* /*hostContext*/, void* parent)
{
    ++g_settingsBuilds;
    auto* content = static_cast<inking::InkingWidget*>(parent);
    const AppEntry* entry = FindApp(g_settingsAppId);
    if (content == nullptr || entry == nullptr)
    {
        return nullptr;
    }
    const int width = content->GetWidth() > 0 ? content->GetWidth() : 720;
    const int height = content->GetHeight() > 0 ? content->GetHeight() : 520;

    PanelData root = makePanel("appmanager.settings.root", width, height, theme::kSurface);
    root.layout.kind = inking::LayoutKind::Stack;
    root.layout.axis = inking::LayoutAxis::Vertical;
    root.layout.gap = 10.0f;
    root.layout.padding = inking::LayoutSpacing{24.0f, 22.0f, 24.0f, 22.0f};
    auto* panel = static_cast<InkingStaticPanel*>(
        content->AddChild(std::make_unique<InkingStaticPanel>(content, root)));
    if (panel == nullptr)
    {
        return nullptr;
    }

    LabelData title;
    title.name = "appmanager.settings.title";
    title.height = 40;
    title.text = "应用设置 —— " + entry->title;
    title.color = theme::kTextPrimary;
    title.colorToken = "textPrimary";
    title.style.fontSize = 21.0f;
    title.style.alignV = inking::TextAlignV::Middle;
    panel->AddChild(std::make_unique<InkingStaticLabel>(panel, title));

    LabelData target;
    target.name = "appmanager.settings.target";
    target.height = 44;
    target.text = entry->target.empty() ? "（还没配路径：可在 apps.json 里手改，或删掉重加）" : entry->target;
    target.color = theme::kTextMuted;
    target.colorToken = "textMuted";
    target.style.fontSize = 13.0f;
    target.maxLines = 2;
    target.overflow = inking::TextOverflow::Ellipsis;
    panel->AddChild(std::make_unique<InkingStaticLabel>(panel, target));

    auto addAction = [&panel, width](const std::string& name, const std::string& text,
                                     const std::string& detail, std::function<void()> action) {
        ButtonData data;
        data.name = name;
        data.width = width - 48;
        data.height = 56;
        data.normal.type = inking::AppearanceType::Color;
        data.normal.color = theme::kSurfaceAlt;
        data.hover.type = inking::AppearanceType::Color;
        data.hover.color = theme::kAccentSoft;
        data.onclicked.type = inking::AppearanceType::Color;
        data.onclicked.color = theme::kAccentSoftPressed;
        data.hoverInheritsNormal = false;
        data.onclickedInheritsNormal = false;
        data.textColor = theme::kTextPrimary;
        data.label = text + "   " + detail;
        data.text.fontSize = 15.0f;
        data.text.align = inking::TextAlignH::Left;
        data.text.alignV = inking::TextAlignV::Middle;
        data.text.leftSpace = 18.0f;
        data.shape = inking::ShapeSpec::RoundedRect(10.0f);
        auto* button = static_cast<InkingStaticButton*>(
            panel->AddChild(std::make_unique<InkingStaticButton>(panel, data)));
        if (button != nullptr)
        {
            button->SetOnClicked(std::move(action));
        }
    };

    addAction("appmanager.settings.icon", "更换图标…", "替换侧栏里那一格的小图标",
              [id = entry->id] { openPickPopup(PickKind::Icon, id); });
    addAction("appmanager.settings.background", "更换背景…", "图片或视频，铺满右侧",
              [id = entry->id] { openPickPopup(PickKind::Background, id); });

    const bool hasBackground = entry->backgroundType != "none" && !entry->backgroundPath.empty();
    addAction("appmanager.settings.clearbg", "清除背景", hasBackground ? "当前：" + entry->backgroundPath : "当前没有背景",
              [id = entry->id, hasBackground] {
                  if (hasBackground)
                  {
                      clearBackground(id);
                  }
              });

    LabelData note;
    note.name = "appmanager.settings.note";
    note.height = 40;
    note.text = "素材复制进 apps/" + entry->id +
                "/；点「启动」永远新开一个实例，点侧栏这一行则会在它已运行时直接进那个窗口"
                "（app.ini 的 reuse=0 可关掉「进入」这一段）。";
    note.color = theme::kTextMuted;
    note.colorToken = "textMuted";
    note.style.fontSize = 12.0f;
    note.maxLines = 2;
    panel->AddChild(std::make_unique<InkingStaticLabel>(panel, note));

    return panel;
}

// ---- 弹窗：添加应用 ----

GoldingWidgetHandle createImportPopup(void* /*hostContext*/, void* parent)
{
    ++g_importBuilds;
    auto* content = static_cast<inking::InkingWidget*>(parent);
    if (content == nullptr)
    {
        return nullptr;
    }
    const int width = content->GetWidth() > 0 ? content->GetWidth() : 920;
    const int height = content->GetHeight() > 0 ? content->GetHeight() : 640;

    LabelData title;
    title.name = "appmanager.import.title";
    title.width = width - 48;
    title.height = 30;
    title.offsetX = 24.0f;
    title.offsetY = 14.0f;
    title.text = "添加应用：选一个 exe / lnk / bat / cmd";
    title.color = theme::kTextPrimary;
    title.colorToken = "textPrimary";
    title.style.fontSize = 18.0f;
    title.style.alignV = inking::TextAlignV::Middle;
    content->AddChild(std::make_unique<InkingStaticLabel>(content, title));

    PanelData board = makePanel("appmanager.import.board", width, height - 54, theme::kSurface);
    board.offsetY = 54.0f;
    auto* boardPanel = static_cast<InkingStaticPanel*>(
        content->AddChild(std::make_unique<InkingStaticPanel>(content, board)));
    if (boardPanel == nullptr)
    {
        return nullptr;
    }

    inking::prefab::FilePickerStyle style = inking::prefab::FilePickerStyle::MakeDefault();
    style.extensions = {".exe", ".lnk", ".bat", ".cmd"};
    style.acceptText = "添加";
    auto* picker = static_cast<inking::prefab::FilePicker*>(
        boardPanel->AddChild(std::make_unique<inking::prefab::FilePicker>(boardPanel, style)));
    if (picker == nullptr)
    {
        return nullptr;
    }
    const std::filesystem::path home = inking::prefab::FilePicker::HomeDirectory();
    if (!home.empty())
    {
        picker->SetDirectory(home);
    }
    picker->accepted.Subscribe([](const std::vector<std::string>& paths) {
        if (paths.empty())
        {
            return;
        }
        AppEntry created;
        if (!CreateApp(FromUtf8(paths.front()), g_state.apps, &created))
        {
            warn("添加失败（类型不支持或写不进数据目录）：" + paths.front());
            return;
        }
        appendRow(created);
        refreshRowSelection();
        warn("已添加：" + created.id + " → " + created.target);
        if (g_state.popup != nullptr)
        {
            g_state.popup->pop(nullptr);
        }
        SelectApp(created.id);
    });
    picker->canceled.Subscribe([] {
        if (g_state.popup != nullptr)
        {
            g_state.popup->pop(nullptr);
        }
    });
    return boardPanel;
}

// ---- 页面 ----

/** 把背景铺满页面（图片 / 视频）。没有背景时什么都不加。 */
void addBackground(inking::InkingWidget* root, const AppEntry& entry, int width, int height)
{
    const std::filesystem::path background = BackgroundPath(entry);
    if (background.empty())
    {
        return;
    }
    if (entry.backgroundType == "video")
    {
        VideoData video;
        video.name = kBackgroundNodeName;
        video.width = width;
        video.height = height;
        video.source = ToUtf8(background);
        video.fit = inking::VideoFit::Cover;
        video.options.autoplay = true;
        video.options.loop = true;
        video.options.muted = true;
        video.zIndex = inking::InkingZIndex::background;
        root->AddChild(std::make_unique<InkingStaticVideo>(root, video));
        return;
    }

    ImageData image;
    image.name = kBackgroundNodeName;
    image.width = width;
    image.height = height;
    image.normal.type = inking::AppearanceType::Image;
    image.normal.source = ToUtf8(background);
    image.normal.imageFit = ImageFit::Cover;
    image.runtimeAsset = true;
    image.hitPassThrough = true;
    image.zIndex = inking::InkingZIndex::background;
    root->AddChild(std::make_unique<InkingStaticImage>(root, image));
}

} // namespace

UiState& State() { return g_state; }

AppEntry* FindApp(const std::string& id)
{
    const auto found = std::find_if(g_state.apps.begin(), g_state.apps.end(),
                                    [&id](const AppEntry& entry) { return entry.id == id; });
    return found == g_state.apps.end() ? nullptr : &*found;
}

GoldingWidgetHandle createSidebar(void* /*hostContext*/, void* parent)
{
    auto* host = static_cast<inking::InkingWidget*>(parent);
    if (host == nullptr)
    {
        return nullptr;
    }
    g_state.sidebarHost = host;

    // 启动模式 `--immersive`：一进来就是背景纯享（截图 / CI 要可复现的纯享画面）。
    //
    // 为什么在这里读 `BootMode()` 而不是 `Register`：宿主的顺序是
    // `registerAll()` → `setBootMode()` →（装配），所以 `Register` 里读到的一直是空串。
    // 而本函数是装配期跑的（面板 createShell 扫区域项），那时模式已经就位。
    if (std::string(g_state.host.BootMode()) == "--immersive")
    {
        g_state.immersive = true;
    }

    PanelData box = makePanel("appmanager.sidebar.box", kRowWidth, kSidebarBoxHeight, theme::kSurfaceSunken);
    box.layout.kind = inking::LayoutKind::Stack;
    box.layout.axis = inking::LayoutAxis::Vertical;
    box.layout.gap = static_cast<float>(kRowGap);
    auto* boxPanel = static_cast<InkingStaticPanel*>(
        host->AddChild(std::make_unique<InkingStaticPanel>(host, box)));
    if (boxPanel == nullptr)
    {
        return nullptr;
    }
    g_state.sidebarBox = boxPanel;

    for (const AppEntry& entry : g_state.apps)
    {
        createRow(entry);
    }
    buildAddRow();
    refreshRowSelection();
    boxPanel->Relayout();
    std::printf("[appmanager] 侧栏建好：%zu 个应用 + 1 行添加\n", g_state.apps.size());
    return boxPanel;
}

GoldingWidgetHandle createPage(void* /*hostContext*/, void* parent)
{
    auto* container = static_cast<inking::InkingWidget*>(parent);
    if (container == nullptr)
    {
        return nullptr;
    }
    const int width = container->GetWidth() > 0 ? container->GetWidth() : 1920;
    const int height = container->GetHeight() > 0 ? container->GetHeight() : 1080;
    // 舞台尺寸（设计画布）：**背景按它铺**，能看到多少由内容区裁剪决定。
    // 为什么不按内容区量：侧栏收/展会让内容区在 1700↔1920 之间变，按内容区量的
    // 背景一旦铺宽过就再也不会缩回来（静态节点改不了尺寸），于是收起侧栏之后
    // 右边留一条白边。铺满舞台则两头都对——内容区只是"窗口"。
    int stageWidth = width;
    int stageHeight = height;
    for (inking::InkingWidget* node = container; node != nullptr; node = node->GetParent())
    {
        stageWidth = node->GetWidth();
        stageHeight = node->GetHeight();
    }

    PanelData rootData = makePanel(kPageRootNodeName, width, height, theme::kSurface);
    auto* root = static_cast<InkingStaticPanel*>(
        container->AddChild(std::make_unique<InkingStaticPanel>(container, rootData)));
    if (root == nullptr)
    {
        return nullptr;
    }
    g_state.pageRoot = root;
    ++g_state.pageBuildCount;

    const AppEntry* entry = FindApp(g_state.selectedId);
    if (entry == nullptr)
    {
        LabelData tip;
        tip.name = "appmanager.page.empty";
        tip.width = width;
        tip.height = 48;
        tip.offsetY = static_cast<float>(height / 2 - 24);
        tip.text = "左侧还没有应用 —— 点「＋ 添加应用」导入一个";
        tip.color = theme::kTextMuted;
        tip.colorToken = "textMuted";
        tip.style.fontSize = 18.0f;
        tip.style.align = inking::TextAlignH::Center;
        tip.style.alignV = inking::TextAlignV::Middle;
        root->AddChild(std::make_unique<InkingStaticLabel>(root, tip));
        return root;
    }

    addBackground(root, *entry, stageWidth, stageHeight);

    // 标题区垫一块**半透明白**：背景可能是任意深色视频 / 照片，深色标题直接压上去
    // 会看不清。卡片是背景之上、文字之下（zIndex 1 vs 2），不挡底图整体观感。
    PanelData plate =
        makePanel("appmanager.page.plate", std::min(width - 96, 760), 168, 0xE8FFFFFFu);
    plate.offsetX = 28.0f;
    plate.offsetY = 16.0f;
    plate.shape = inking::ShapeSpec::RoundedRect(18.0f);
    plate.zIndex = 1;
    root->AddChild(std::make_unique<InkingStaticPanel>(root, plate));

    LabelData heading;
    heading.name = "appmanager.page.title";
    heading.width = std::min(width - 96, 720);
    heading.height = 56;
    heading.offsetX = 48.0f;
    heading.offsetY = 36.0f;
    heading.zIndex = 2;
    heading.text = entry->title;
    heading.color = theme::kTextPrimary;
    heading.colorToken = "textPrimary";
    heading.style.fontSize = 40.0f;
    heading.style.alignV = inking::TextAlignV::Middle;
    root->AddChild(std::make_unique<InkingStaticLabel>(root, heading));

    LabelData pathText;
    pathText.name = "appmanager.page.path";
    pathText.width = std::min(width - 96, 720);
    pathText.height = 24;
    pathText.offsetX = 48.0f;
    pathText.offsetY = 100.0f;
    pathText.zIndex = 2;
    pathText.text = entry->target.empty() ? "（未设置路径）" : entry->target;
    pathText.color = theme::kTextMuted;
    pathText.colorToken = "textMuted";
    pathText.style.fontSize = 13.0f;
    pathText.maxLines = 1;
    pathText.overflow = inking::TextOverflow::Ellipsis;
    root->AddChild(std::make_unique<InkingStaticLabel>(root, pathText));

    // ---- 底部按钮组：**右下角一排**，从左到右是 眼睛 / 启动 / 设置 ----
    //
    // 与标题卡片（左上）成对角：标题不会压到按钮，按钮也不会挡住壁纸的主要内容。
    //
    // 位置用**锚点**而不是绝对坐标：内容区的宽度会随侧栏收展变（1700 ↔ 1920），
    // 写死的 `width - 364` 只在建页面那一刻对；锚点每次现算（`InkingAnchor::GetX`），
    // 于是"侧栏收进去 → 按钮跟着贴到新的右下角"一行代码都不用写。
    //
    // 自身锚点与上级锚点都取右下角 ⇒ `GetX = 内容区宽 − 自身宽 + offsetX`，
    // 所以 offsetX 是"离右边多远"的负值；三块的横向偏移按右边界往回累加。
    const float gearOffsetX = -static_cast<float>(kRightInset);
    const float launchOffsetX = -(static_cast<float>(kRightInset) + kGearSize + kGroupGap);
    const float eyeOffsetX = launchOffsetX - static_cast<float>(kStartWidth + kGroupGap);
    const float groupOffsetY = -static_cast<float>(kBottomInset);
    // 眼睛比另外两块矮，竖直方向按中线对齐（不以底边对齐，否则看着往下掉）。
    const float eyeOffsetY = -(static_cast<float>(kBottomInset) + (kStartHeight - kEyeSize) / 2.0f);

    // ---- 背景纯享（那只小眼睛）：这一排最左边 ----
    //
    // 纯享态下**它自己不隐身**——否则鼠标就再也点不到恢复入口了
    // （`Ctrl+E` 是另一条路，但只给键盘留出口是耍赖）。所以启动 / 设置隐掉之后，
    // 这一排只剩它一个，位置不变（锚点算的是绝对位置，不依赖兄弟）。
    ButtonData eye;
    eye.name = kEyeNodeName;
    eye.width = kEyeSize;
    eye.height = kEyeSize;
    eye.selfAnchor = InkingChangeAnchor::RightBottom;
    eye.traceAnchor = InkingChangeAnchor::RightBottom;
    eye.offsetX = eyeOffsetX;
    eye.offsetY = eyeOffsetY;
    eye.normal.type = inking::AppearanceType::Color;
    eye.normal.color = theme::kAccentSoft;
    eye.hover.type = inking::AppearanceType::Color;
    eye.hover.color = theme::kAccentSoftPressed;
    eye.onclicked.type = inking::AppearanceType::Color;
    eye.onclicked.color = theme::kAccent;
    eye.hoverInheritsNormal = false;
    eye.onclickedInheritsNormal = false;
    eye.stroke.width = 1.0f;
    eye.stroke.color = theme::kAccent;
    eye.shape = inking::ShapeSpec::RoundedRect(12.0f);
    auto* eyeButton = static_cast<InkingStaticButton*>(
        root->AddChild(std::make_unique<InkingStaticButton>(root, eye)));
    if (eyeButton != nullptr)
    {
        // 与 `Ctrl+E` 同一个落点：`ToggleImmersive` 是唯一入口。
        eyeButton->SetOnClicked([] { ToggleImmersive(); });
        const std::filesystem::path eyeIcon = BuiltinIconPath(g_state.immersive ? "eye-off" : "eye");
        if (!eyeIcon.empty())
        {
            const int iconSize = 24;
            ImageData image;
            image.name = kEyeIconNodeName;
            image.width = iconSize;
            image.height = iconSize;
            image.offsetX = static_cast<float>((kEyeSize - iconSize) / 2);
            image.offsetY = static_cast<float>((kEyeSize - iconSize) / 2);
            image.normal.type = inking::AppearanceType::Image;
            image.normal.source = ToUtf8(eyeIcon);
            image.normal.imageFit = ImageFit::Contain;
            image.runtimeAsset = true;
            image.hitPassThrough = true;
            eyeButton->AddChild(std::make_unique<InkingStaticImage>(eyeButton, image));
        }
    }

    ButtonData launch;
    launch.name = "appmanager.launch";
    launch.width = kStartWidth;
    launch.height = kStartHeight;
    launch.selfAnchor = InkingChangeAnchor::RightBottom;
    launch.traceAnchor = InkingChangeAnchor::RightBottom;
    launch.offsetX = launchOffsetX;
    launch.offsetY = groupOffsetY;
    launch.normal.type = inking::AppearanceType::Color;
    launch.normal.color = theme::kAccentSoft;
    launch.hover.type = inking::AppearanceType::Color;
    launch.hover.color = theme::kAccentSoftPressed;
    launch.onclicked.type = inking::AppearanceType::Color;
    launch.onclicked.color = theme::kAccent;
    launch.hoverInheritsNormal = false;
    launch.onclickedInheritsNormal = false;
    // 注意：这条管线的按钮文字在**浅色**下会画成空心描边（实测：纯白和近白都是），
    // 所以启动按钮走"浅底 + 深字"，不指望白色文字。
    launch.textColor = theme::kAccentStrong;
    launch.stroke.width = 1.0f;
    launch.stroke.color = theme::kAccent;
    launch.label = entry->target.empty() ? "配置路径" : "启动";
    launch.text.fontSize = 20.0f;
    launch.text.align = inking::TextAlignH::Center;
    launch.text.alignV = inking::TextAlignV::Middle;
    launch.shape = inking::ShapeSpec::RoundedRect(14.0f);
    auto* launchButton = static_cast<InkingStaticButton*>(
        root->AddChild(std::make_unique<InkingStaticButton>(root, launch)));
    if (launchButton != nullptr)
    {
        launchButton->SetOnClicked([id = entry->id] {
            AppEntry* target = FindApp(id);
            if (target == nullptr)
            {
                return;
            }
            if (target->target.empty())
            {
                OpenSettingsPopup(id);
                return;
            }
            Launch(*target);
        });
    }

    // 设置：和启动按钮同一族（浅底 + 描边 + 深色图形），图标是**三横线**。
    ButtonData gear;
    gear.name = "appmanager.settings";
    gear.width = kGearSize;
    gear.height = kGearSize;
    gear.selfAnchor = InkingChangeAnchor::RightBottom;
    gear.traceAnchor = InkingChangeAnchor::RightBottom;
    gear.offsetX = gearOffsetX;
    gear.offsetY = groupOffsetY;
    gear.normal.type = inking::AppearanceType::Color;
    gear.normal.color = theme::kAccentSoft;
    gear.hover.type = inking::AppearanceType::Color;
    gear.hover.color = theme::kAccentSoftPressed;
    gear.onclicked.type = inking::AppearanceType::Color;
    gear.onclicked.color = theme::kAccent;
    gear.hoverInheritsNormal = false;
    gear.onclickedInheritsNormal = false;
    gear.stroke.width = 1.0f;
    gear.stroke.color = theme::kAccent;
    gear.shape = inking::ShapeSpec::RoundedRect(14.0f);
    auto* gearButton = static_cast<InkingStaticButton*>(
        root->AddChild(std::make_unique<InkingStaticButton>(root, gear)));
    if (gearButton != nullptr)
    {
        gearButton->SetOnClicked([id = entry->id] { OpenSettingsPopup(id); });
        const std::filesystem::path menuIcon = BuiltinIconPath("menu");
        if (!menuIcon.empty())
        {
            ImageData image;
            image.name = "appmanager.settings.icon";
            image.width = 36;
            image.height = 36;
            image.offsetX = static_cast<float>((kGearSize - 36) / 2);
            image.offsetY = static_cast<float>((kGearSize - 36) / 2);
            image.normal.type = inking::AppearanceType::Image;
            image.normal.source = ToUtf8(menuIcon);
            image.normal.imageFit = ImageFit::Contain;
            image.runtimeAsset = true;
            image.hitPassThrough = true;
            gearButton->AddChild(std::make_unique<InkingStaticImage>(gearButton, image));
        }
    }

    // 装配期的这一份就用**手上这个根**，不去树上找：此刻面板可能还没把旧页面摘掉，
    // 按名字找有可能找到上一个应用的页面。
    applyOverlayVisibility(root, g_state.immersive);

    return root;
}

int SidebarRowCount()
{
    return g_state.sidebarBox != nullptr ? static_cast<int>(g_state.sidebarBox->GetOwnedChildCount()) : 0;
}

void SelectApp(const std::string& id)
{
    g_state.selectedId = id;
    refreshRowSelection();
    RefreshPage();
}

void SuppressEnterFromSidebar(bool suppress) { g_enterFromSidebar = !suppress; }

int EnterAttemptCount() { return g_state.enterAttempts; }

void RefreshPage()
{
    if (g_state.nav != nullptr)
    {
        g_state.nav->go(nullptr, kPageId);
    }
}

void OpenImportPopup()
{
    const golding::GoldingPopupDecl decl{"appmanager.import", 920, 640, 0, 0, 1, &createImportPopup, nullptr};
    if (g_state.popup == nullptr || g_state.popup->push(nullptr, &decl) != 0)
    {
        warn("添加弹窗打不开（没有 golding.popup？）");
    }
}

void OpenSettingsPopup(const std::string& id)
{
    g_settingsAppId = id;
    const golding::GoldingPopupDecl decl{"appmanager.settings", 720, 520, 0, 0, 1, &createSettingsPopup, nullptr};
    if (g_state.popup == nullptr || g_state.popup->push(nullptr, &decl) != 0)
    {
        warn("设置弹窗打不开（没有 golding.popup？）");
    }
}

inking::InkingWidget* BackgroundNode() { return childByName(findPageRoot(), kBackgroundNodeName); }

inking::InkingWidget* EyeButton() { return childByName(findPageRoot(), kEyeNodeName); }

bool IsImmersive() { return g_state.immersive; }

void SetImmersive(bool immersive)
{
    g_state.immersive = immersive;
    applyOverlayVisibility(findPageRoot(), immersive);
}

void ToggleImmersive()
{
    SetImmersive(!g_state.immersive);
    std::printf("[appmanager] 背景纯享：%s\n", g_state.immersive ? "开（只留背景）" : "关（界面回来）");
}

int VisibleOverlayCount()
{
    inking::InkingWidget* root = findPageRoot();
    if (root == nullptr)
    {
        return 0;
    }
    int visible = 0;
    for (inking::InkingWidget* child : root->GetChildren())
    {
        if (child != nullptr && isOverlayNode(child->GetName()) && child->IsVisible())
        {
            ++visible;
        }
    }
    return visible;
}

int SettingsBuildCount() { return g_settingsBuilds; }

int ImportBuildCount() { return g_importBuilds; }

} // namespace appmanager
