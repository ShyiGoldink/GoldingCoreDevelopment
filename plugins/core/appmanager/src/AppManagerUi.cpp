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
/** 页面底部那两个按钮。 */
constexpr int kStartWidth = 240;
constexpr int kStartHeight = 64;
constexpr int kGearSize = 64;

UiState g_state;
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
    row->SetOnClicked([id = entry.id] { SelectApp(id); });
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
                "/；启动时它已在运行就切到那个窗口，不重复开（app.ini 的 reuse=0 可关掉这个行为）。";
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

    PanelData rootData = makePanel("appmanager.page.root", width, height, theme::kSurface);
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

    addBackground(root, *entry, width, height);

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

    // ---- 底部按钮组：**右下角**，启动在左、设置在右 ----
    //
    // 与标题卡片（左上）成对角：标题不会压到按钮，按钮也不会挡住壁纸的主要内容。
    // 底边留 32，右边留 48。
    const int baseY = height - 96;
    const int rightMargin = 48;
    const int groupWidth = kStartWidth + 12 + kGearSize;
    const int groupLeft = width - rightMargin - groupWidth;

    ButtonData launch;
    launch.name = "appmanager.launch";
    launch.width = kStartWidth;
    launch.height = kStartHeight;
    launch.offsetX = static_cast<float>(groupLeft);
    launch.offsetY = static_cast<float>(baseY);
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
    gear.offsetX = static_cast<float>(groupLeft + kStartWidth + 12);
    gear.offsetY = static_cast<float>(baseY);
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

inking::InkingWidget* BackgroundNode()
{
    if (g_state.pageRoot == nullptr)
    {
        return nullptr;
    }
    for (inking::InkingWidget* child : g_state.pageRoot->GetChildren())
    {
        if (child != nullptr && child->GetName() == kBackgroundNodeName)
        {
            return child;
        }
    }
    return nullptr;
}

int SettingsBuildCount() { return g_settingsBuilds; }

int ImportBuildCount() { return g_importBuilds; }

} // namespace appmanager
