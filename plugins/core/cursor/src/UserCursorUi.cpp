// 用户指针管理面板的实现：列表 + 导入（自绘文件选择器）+ 选热点。
//
// 见 `UserCursorUi.h` 的分工说明。这份文件只管"怎么摆出来"，不碰清单格式。
//
// ---------------------------------------------------------------------------
// 两条实现上的硬约束（都是踩过的）
//
// 1. **静态组件没有 `SetOffset`**：几何写入口只有动态档有。所以位置一律写进
//    数据（`LabelData.offsetX` / `ButtonData.offsetX`），构造即定型。
// 2. **面板是"根拥有管理器"**：`UserCursorRoot` 持一份 `unique_ptr<UserCursorPanel>`，
//    按钮回调里才可以放心用 `this`（按钮是根的孩子，比管理器先死）。

#include "UserCursorUi.h"

#include "GoldingServices.h"

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <canvas/CanvasContext.h>
#include <canvas/CanvasData.h>
#include <canvas/InkingStaticCanvas.h>
#include <dataStruct/InkingAnchorData.h>
#include <dataStruct/InkingShapeSpec.h>
#include <image/ImageData.h>
#include <image/InkingStaticImage.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/FilePicker.h>
#include <prefab/FilePickerData.h>
#include <prefab/PrefabTheme.h>
#include <scene/InkingScene.h>
#include <text/InkingStaticLabel.h>
#include <text/LabelData.h>
#include <widget/InkingWidget.h>
#include <window/InkingWindow.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#undef DrawText
#endif

namespace cursor
{

namespace
{

namespace theme = inking::prefab::theme;

/**
 * @brief 当前活着的用户指针面板（**至多一个**：弹窗同一时刻只有一层）。
 *
 * 演示钩子靠它找到面板，而不是让调用方传一个根节点指针过来——那个指针在
 * 弹窗被替换时就已经悬空了（弹窗一关，根节点连同它托管的管理器一起没了）。
 *
 * 管理器析构时清空它：管理器是根节点的成员，根节点销毁时它先死。
 */
class UserCursorPanel;
UserCursorPanel* g_currentPanel = nullptr;

/**弹窗内容尺寸（与插件那边的弹窗声明一致）。*/
constexpr int kPanelWidth = 760;
constexpr int kPanelHeight = 560;

constexpr int kMargin = 24;
constexpr int kListTop = 144;
constexpr int kListHeight = 330;
constexpr int kRowHeight = 30;
constexpr int kRowGap = 8;

/**预览区里那张图最多画多大（像素）。*/
constexpr int kPreviewMax = 280;

/**
 * @brief 把容器里**拥有的**孩子摘下来、排到帧末销毁（不是当场删）。
 *
 * 为什么不能直接 `ClearChildren()`：列表行里的「删」按钮，它的点击回调**就是**
 * 重建这次列表的入口。当场销毁 = 回调返回后踩到已经释放的按钮（实测闪退）。
 *
 * 摘下来的节点由 `shared_ptr` 持有，帧末任务因此不依赖面板还活着（弹窗可能在
 * 这中间被关掉）。借用型孩子（构造期传 parent 的成员）`RemoveChild` 返回
 * `nullptr`，原样留在容器里——那不是这里该管的东西。
 */
void DetachChildrenDeferred(inking::InkingWidget* container)
{
    if (container == nullptr)
    {
        return;
    }
    const std::vector<inking::InkingWidget*> children = container->GetChildren();
    auto dropped = std::make_shared<std::vector<std::unique_ptr<inking::InkingWidget>>>();
    for (inking::InkingWidget* child : children)
    {
        if (child == nullptr)
        {
            continue;
        }
        if (std::unique_ptr<inking::InkingWidget> owned = container->RemoveChild(child))
        {
            dropped->push_back(std::move(owned));
        }
    }
    if (!dropped->empty())
    {
        inking::InkingWindow::Instance().RunAfterFrames(1, [dropped] { dropped->clear(); });
    }
}

inking::LabelData MakeLabel(const char* name, const std::string& text, float fontSize, std::uint32_t color, int width,
                            int height, int x, int y)
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

inking::ButtonData MakeButton(const char* name, const std::string& text, int width, int height, int x, int y,
                              bool primary = false)
{
    inking::ButtonData data;
    data.name = name;
    data.width = width;
    data.height = height;
    data.offsetX = static_cast<float>(x);
    data.offsetY = static_cast<float>(y);
    data.label = text;
    data.normal.type = inking::AppearanceType::Color;
    data.normal.color = primary ? theme::kAccent : theme::kSurfaceAlt;
    data.hover.type = inking::AppearanceType::Color;
    data.hover.color = primary ? theme::kAccentStrong : theme::kHoverNeutral;
    data.onclicked.type = inking::AppearanceType::Color;
    data.onclicked.color = primary ? theme::kAccentStrong : theme::kAccentSoft;
    data.hoverInheritsNormal = false;
    data.onclickedInheritsNormal = false;
    data.textColor = primary ? theme::kSurface : theme::kTextPrimary;
    data.text.fontSize = 14.0f;
    data.text.align = inking::TextAlignH::Center;
    data.text.alignV = inking::TextAlignV::Middle;
    data.shape = inking::ShapeSpec::RoundedRect(theme::kRadiusItem);
    return data;
}

/**
 * @brief 面板的实现。
 *
 * 它**不拥有根节点**（根节点拥有它）。所以构造时还没有 parent，所有子节点都在
 * `Build()` 里造——那时根已经在了。
 */
class UserCursorPanel
{
public:
    UserCursorPanel(UserCursorStore* store, ApplyUserCursorFn apply, UserCursorChangedFn changed, void* context,
                    const golding::PopupService* popup)
        : _store(store), _apply(apply), _changed(changed), _context(context), _popup(popup)
    {
    }

    /**接上根节点，然后造出全部子节点。*/
    ~UserCursorPanel() { g_currentPanel = nullptr; }

    void Attach(inking::InkingWidget* root)
    {
        _root = root;
        g_currentPanel = this;
        BuildChrome();
        BuildPicker();
        BuildEditor();
        Refresh();
    }

    /**
     * @brief 演示 / 自检用：直接进入"选热点"那一步（第一条）。
     *
     * 界面上点「热点」按钮走的是同一条路（`ShowEditor`），但无头环境里没人
     * 能点按钮——没有这个钩子，"预览图 + 热点十字"这一段就没有可验证的入口。
     */
    /**演示 / 自检用：直接切到"选文件"那一步（导入界面）。*/
    void DemoShowPicker()
    {
        ShowPicker();
    }

    void DemoOpenEditorForFirst()
    {
        if (_items.empty())
        {
            return;
        }
        // 取**最大**的那张：演示与截图里，小图（十几像素）在缩放后的屏幕上几乎
        // 看不见，而"预览图有没有画出来"正是这一步要验的东西。
        std::size_t best = 0;
        for (std::size_t index = 1; index < _items.size(); ++index)
        {
            if (_items[index].width * _items[index].height > _items[best].width * _items[best].height)
            {
                best = index;
            }
        }
        ShowEditor(_items[best]);
    }

private:
    // ---- 搭骨架 ----

    void BuildChrome()
    {
        _root->AddChild(std::make_unique<inking::InkingStaticLabel>(
            _root, MakeLabel("cursor.user.title", "用户指针", 18.0f, theme::kTextPrimary, 400, 28, kMargin, 14)));

        _root->AddChild(std::make_unique<inking::InkingStaticLabel>(
            _root, MakeLabel("cursor.user.hint", "把 PNG / JPG / SVG 放进下面这个目录，或用「导入图片…」挑一个。",
                             13.0f, theme::kTextMuted, kPanelWidth - 2 * kMargin, 22, kMargin, 48)));

        _root->AddChild(std::make_unique<inking::InkingStaticLabel>(
            _root, MakeLabel("cursor.user.dir", _store->directoryText(), 12.0f, theme::kTextMuted,
                             kPanelWidth - 2 * kMargin, 20, kMargin, 74)));

        int x = kMargin;
        x = AddButton("cursor.user.open", "打开目录", 118, x, [this] { OpenDirectory(); }) + 8;
        x = AddButton("cursor.user.refresh", "刷新", 88, x, [this] { Refresh(); }) + 8;
        x = AddButton("cursor.user.import", "导入图片…", 132, x, [this] { ShowPicker(); }, true) + 8;
        AddButton("cursor.user.close", "关闭", 88, x, [this] { ClosePopup(); });

        // 列表容器：一块浅底的区域，行在里面**绝对定位**排。
        // 不用 Stack 布局：行数会变，而位置本来就是算出来的（索引 × 行高），
        // 直接摆更省事也更可控。
        inking::PanelData list;
        list.name = "cursor.user.list";
        list.width = kPanelWidth - 2 * kMargin;
        list.height = kListHeight;
        list.offsetX = static_cast<float>(kMargin);
        list.offsetY = static_cast<float>(kListTop);
        list.startOpen = true;
        list.closed.color = theme::kSurfaceAlt;
        list.closed.themeColor = "drawerSurface";
        list.open = list.closed;
        list.shape = inking::ShapeSpec::RoundedRect(theme::kRadiusCard);
        _list = static_cast<inking::InkingStaticPanel*>(
            _root->AddChild(std::make_unique<inking::InkingStaticPanel>(_root, list)));
    }

    /**加一颗按钮，返回它右边缘的 x（方便排下一颗）。*/
    int AddButton(const char* name, const std::string& text, int width, int x, std::function<void()> action,
                  bool primary = false)
    {
        inking::ButtonData data = MakeButton(name, text, width, 32, x, 104, primary);
        auto* button = static_cast<inking::InkingStaticButton*>(
            _root->AddChild(std::make_unique<inking::InkingStaticButton>(_root, data)));
        if (button != nullptr)
        {
            button->SetOnClicked(std::move(action));
        }
        return x + width;
    }

    void BuildPicker()
    {
        inking::prefab::FilePickerStyle style = inking::prefab::FilePickerStyle::MakeDefault();
        style.extensions = {".png", ".jpg", ".jpeg", ".svg"};
        style.mode = inking::prefab::FilePickMode::OpenFile;
        style.multiSelect = false;
        style.panel.width = kPanelWidth - 40;
        style.panel.height = kPanelHeight - 40;
        // 底色给白：选择器是**盖在别的东西上面**的一层，透底会跟下面的内容叠着看。
        style.panel.closed.color = theme::kSurface;
        style.panel.closed.themeColor = "panelSurface";
        style.panel.open = style.panel.closed;
        style.panel.shape = inking::ShapeSpec::RoundedRect(theme::kRadiusPanel);
        style.listHeight = 300.0f;

        auto picker = std::make_unique<inking::prefab::FilePicker>(_root, style);
        auto* raw = picker.get();
        if (_root->AddChild(std::move(picker)) == nullptr)
        {
            return;
        }
        _picker = raw;
        _picker->SetVisible(false);
        _pickerToken = _picker->accepted.Subscribe(
            [this](const std::vector<std::string>& paths) { OnPickerAccepted(paths); });
    }

    void BuildEditor()
    {
        inking::PanelData editor;
        editor.name = "cursor.user.editor";
        editor.width = kPanelWidth - 2 * kMargin;
        editor.height = kListHeight;
        editor.offsetX = static_cast<float>(kMargin);
        editor.offsetY = static_cast<float>(kListTop);
        editor.startOpen = true;
        editor.closed.color = theme::kSurfaceAlt;
        editor.closed.themeColor = "drawerSurface";
        editor.open = editor.closed;
        editor.shape = inking::ShapeSpec::RoundedRect(theme::kRadiusCard);
        _editor = static_cast<inking::InkingStaticPanel*>(
            _root->AddChild(std::make_unique<inking::InkingStaticPanel>(_root, editor)));
        _editor->SetVisible(false);

        _editor->AddChild(std::make_unique<inking::InkingStaticLabel>(
            _editor, MakeLabel("cursor.user.editor.hint", "在图上点一下 = 把热点定在那儿。", 14.0f, theme::kTextPrimary,
                                460, 26, 16, 12)));

        _editorInfo = static_cast<inking::InkingStaticLabel*>(
            _editor->AddChild(std::make_unique<inking::InkingStaticLabel>(
                _editor, MakeLabel("cursor.user.editor.info", "", 13.0f, theme::kTextMuted, 460, 26, 16, 44))));

        // 预览区：图 + 热点十字 + 一层透明按钮（接点击）。
        inking::PanelData preview;
        preview.name = "cursor.user.preview";
        preview.width = kPreviewMax + 32;
        preview.height = kPreviewMax + 32;
        preview.offsetX = 16.0f;
        preview.offsetY = 78.0f;
        preview.startOpen = true;
        preview.closed.color = theme::kSurface;
        preview.closed.themeColor = "panelSurface";
        preview.open = preview.closed;
        preview.shape = inking::ShapeSpec::RoundedRect(theme::kRadiusItem);
        _previewFrame = static_cast<inking::InkingStaticPanel*>(
            _editor->AddChild(std::make_unique<inking::InkingStaticPanel>(_editor, preview)));

        // 热点标记：一块小画布，画一个十字。尺寸固定（预览区最大就是这么大），
        // 十字的位置按比例算——画布原点与图原点重合。
        inking::CanvasData marker;
        marker.name = "cursor.user.hotmark";
        marker.width = kPreviewMax;
        marker.height = kPreviewMax;
        marker.color = 0u;
        marker.offsetX = 16.0f;
        marker.offsetY = 16.0f;
        marker.shape = inking::ShapeSpec::Rect();
        _hotMarker = static_cast<inking::InkingStaticCanvas*>(
            _previewFrame->AddChild(std::make_unique<inking::InkingStaticCanvas>(_previewFrame, marker)));
        if (_hotMarker != nullptr)
        {
            _hotMarker->SetHitPassThrough(true);
            _hotMarker->SetVisible(false);
        }

        inking::ButtonData done = MakeButton("cursor.user.editor.done", "完成", 88, 32, 16, kListHeight - 48, true);
        auto* doneButton = static_cast<inking::InkingStaticButton*>(
            _editor->AddChild(std::make_unique<inking::InkingStaticButton>(_editor, done)));
        if (doneButton != nullptr)
        {
            doneButton->SetOnClicked([this] { HideEditor(); });
        }
    }

    // ---- 列表 ----

    void Refresh()
    {
        _items = _store->Scan();
        RebuildList();
        if (_changed != nullptr)
        {
            _changed(_context);
        }
    }

    void RebuildList()
    {
        if (_list == nullptr)
        {
            return;
        }
        // 摘下来、帧末再销毁：这个函数可能正跑在某个行按钮（「删」）的回调里，
        // 当场删掉那颗按钮就是 use-after-free（见 `DetachChildrenDeferred`）。
        DetachChildrenDeferred(_list);

        if (_items.empty())
        {
            _list->AddChild(std::make_unique<inking::InkingStaticLabel>(
                _list, MakeLabel("cursor.user.empty", "还没有用户指针。导入一张，或者把图片直接放进上面的目录。",
                                 14.0f, theme::kTextMuted, kPanelWidth - 2 * kMargin - 32, 28, 16, 16)));
            return;
        }

        for (std::size_t index = 0; index < _items.size(); ++index)
        {
            const UserCursor& item = _items[index];
            const int y = static_cast<int>(index) * (kRowHeight + kRowGap);
            const std::string fileName = item.fileName;

            _list->AddChild(std::make_unique<inking::InkingStaticLabel>(
                _list, MakeLabel("cursor.user.name", item.displayName, 14.0f, theme::kTextPrimary, 260, kRowHeight, 16,
                                 y)));

            const std::string hotText =
                "热点 (" + std::to_string(item.hotX) + ", " + std::to_string(item.hotY) + ")";
            _list->AddChild(std::make_unique<inking::InkingStaticLabel>(
                _list, MakeLabel("cursor.user.hot", hotText, 12.0f, theme::kTextMuted, 170, kRowHeight, 284, y)));

            const std::string sizeText = std::to_string(item.width) + "×" + std::to_string(item.height);
            _list->AddChild(std::make_unique<inking::InkingStaticLabel>(
                _list, MakeLabel("cursor.user.size", sizeText, 12.0f, theme::kTextMuted, 90, kRowHeight, 430, y)));

            AddRowButton("cursor.user.use", "用", 520, y, 56, [this, item] { ApplyCursor(item); });
            AddRowButton("cursor.user.hotbtn", "热点", 582, y, 66, [this, item] { ShowEditor(item); });
            AddRowButton("cursor.user.del", "删", 654, y, 50, [this, fileName] { RemoveCursor(fileName); });
        }
    }

    void AddRowButton(const char* name, const std::string& text, int x, int y, int width,
                      std::function<void()> action)
    {
        inking::ButtonData data = MakeButton(name, text, width, 28, x, y + 1);
        auto* button = static_cast<inking::InkingStaticButton*>(
            _list->AddChild(std::make_unique<inking::InkingStaticButton>(_list, data)));
        if (button != nullptr)
        {
            button->SetOnClicked(std::move(action));
        }
    }

    // ---- 动作 ----

    void ApplyCursor(const UserCursor& item)
    {
        if (_apply != nullptr)
        {
            _apply(item);
        }
    }

    void RemoveCursor(const std::string& fileName)
    {
        std::string error;
        if (!_store->Remove(fileName, &error))
        {
            Log("删不掉用户指针：" + error);
            return;
        }
        Refresh();
    }

    void OpenDirectory()
    {
        std::string error;
        if (!_store->EnsureDirectory(&error))
        {
            Log(error);
            return;
        }
#ifdef _WIN32
        const std::wstring wide = _store->directory().wstring();
        const HINSTANCE result = ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<std::intptr_t>(result) <= 32)
        {
            Log("打开用户指针目录失败");
        }
#else
        Log("只有 Windows 上实现了打开目录");
#endif
    }

    void ClosePopup()
    {
        if (_popup != nullptr)
        {
            _popup->pop(nullptr);
        }
    }

    // ---- 导入 ----

    void ShowPicker()
    {
        std::string error;
        if (!_store->EnsureDirectory(&error))
        {
            Log(error);
            return;
        }
        if (_picker == nullptr)
        {
            return;
        }
        // 起点是用户指针目录（那就是目的地）；用户可以往上一级去找自己的图。
        if (!_picker->SetDirectory(_store->directory()))
        {
            const std::filesystem::path home = inking::prefab::FilePicker::HomeDirectory();
            if (!home.empty())
            {
                _picker->SetDirectory(home);
            }
        }
        _picker->SetVisible(true);
        // 列表与"选热点"那一页都要让开：它们和选择器是**互斥**的三块内容，
        // 同时可见会叠在一起看。
        if (_list != nullptr)
        {
            _list->SetVisible(false);
        }
        if (_editor != nullptr)
        {
            _editor->SetVisible(false);
        }
    }

    void HidePicker()
    {
        if (_picker != nullptr)
        {
            _picker->SetVisible(false);
        }
        if (_list != nullptr)
        {
            _list->SetVisible(true);
        }
    }

    void OnPickerAccepted(const std::vector<std::string>& paths)
    {
        if (paths.empty())
        {
            HidePicker();
            return;
        }
        UserCursor imported;
        std::string error;
        if (!_store->Import(FromUtf8(paths.front()), &imported, &error))
        {
            Log("导入失败：" + error);
            HidePicker();
            return;
        }
        Log("已导入用户指针：" + imported.fileName + "（" + std::to_string(imported.width) + "×" +
            std::to_string(imported.height) + "）");
        HidePicker();
        Refresh();
        ShowEditor(imported);
    }

    // ---- 选热点 ----

    void ShowEditor(const UserCursor& item)
    {
        _editing = item;
        if (_editor == nullptr || _previewFrame == nullptr)
        {
            return;
        }
        // 预览：按原图 1:1 画；图太大就等比缩小（热点按比例折算回去）。
        const float scale = std::min(
            1.0f, std::min(static_cast<float>(kPreviewMax) / static_cast<float>(std::max(1, item.width)),
                           static_cast<float>(kPreviewMax) / static_cast<float>(std::max(1, item.height))));
        _previewScale = scale;

        const int displayWidth = std::max(1, static_cast<int>(item.width * scale));
        const int displayHeight = std::max(1, static_cast<int>(item.height * scale));

        // 旧节点先摘掉（静态图片几何构造即定型，换图只能重建）。
        if (_previewImage != nullptr)
        {
            _previewFrame->RemoveChild(_previewImage);
            _previewImage = nullptr;
        }
        if (_previewHit != nullptr)
        {
            _previewFrame->RemoveChild(_previewHit);
            _previewHit = nullptr;
        }

        inking::ImageData image;
        image.name = "cursor.user.preview.image";
        image.width = displayWidth;
        image.height = displayHeight;
        image.runtimeAsset = true;
        image.hitPassThrough = true;  // 它自己不吃点击：点击交给下面那层透明按钮
        image.normal.type = inking::AppearanceType::Image;
        image.normal.source = item.absolutePath;
        image.shape = inking::ShapeSpec::Rect();
        image.offsetX = 16.0f;
        image.offsetY = 16.0f;
        image.zIndex = 1;
        auto node = std::make_unique<inking::InkingStaticImage>(_previewFrame, image);
        auto* raw = node.get();
        if (_previewFrame->AddChild(std::move(node)) == nullptr)
        {
            return;
        }
        _previewImage = raw;

        // 透明按钮：铺在图上，接住点击。
        //
        // 三态里 `onclicked` 必须**不是全透明**：按钮的归一化规则是"三态全透明
        // 就兜底成品红"（那是在说"这里配错了"）。给一个几乎看不见的按下色就绕开了。
        inking::ButtonData hit;
        hit.name = "cursor.user.preview.hit";
        hit.width = displayWidth;
        hit.height = displayHeight;
        hit.offsetX = 16.0f;
        hit.offsetY = 16.0f;
        hit.zIndex = 3;
        hit.normal.type = inking::AppearanceType::Color;
        hit.normal.color = 0x00FFFFFFu;
        hit.hover.type = inking::AppearanceType::Color;
        hit.hover.color = 0x0A000000u;
        hit.onclicked.type = inking::AppearanceType::Color;
        hit.onclicked.color = 0x1A000000u;
        hit.hoverInheritsNormal = false;
        hit.onclickedInheritsNormal = false;
        hit.shape = inking::ShapeSpec::Rect();
        auto hitNode = std::make_unique<inking::InkingStaticButton>(_previewFrame, hit);
        auto* hitRaw = hitNode.get();
        if (_previewFrame->AddChild(std::move(hitNode)) == nullptr)
        {
            return;
        }
        _previewHit = hitRaw;
        _previewHit->SetOnClicked([this] { OnPreviewClicked(); });

        if (_editorInfo != nullptr)
        {
            _editorInfo->SetText("当前热点 (" + std::to_string(item.hotX) + ", " + std::to_string(item.hotY) +
                                 ")　图片 " + std::to_string(item.width) + "×" + std::to_string(item.height));
        }
        DrawHotMarker();

        _editor->SetVisible(true);
        if (_list != nullptr)
        {
            _list->SetVisible(false);
        }
    }

    void HideEditor()
    {
        if (_editor != nullptr)
        {
            _editor->SetVisible(false);
        }
        if (_list != nullptr)
        {
            _list->SetVisible(true);
        }
    }

    /**预览图上点了一下：把落点换算成图片像素，存进清单。*/
    void OnPreviewClicked()
    {
        if (_previewImage == nullptr || _previewFrame == nullptr)
        {
            return;
        }
        inking::InkingScene* scene = _previewFrame->GetScene();
        if (scene == nullptr)
        {
            return;
        }
        // 按下点：场景记着"这一下是按在哪儿的"（世界 / 设计坐标）。
        const inking::MousePoint press = scene->GetPressOrigin(inking::PointerButton::Primary);
        const float localX = press.x - _previewImage->GetAbsX();
        const float localY = press.y - _previewImage->GetAbsY();
        const float scale = _previewScale > 0.0f ? _previewScale : 1.0f;
        const int hotX = std::clamp(static_cast<int>(std::lround(localX / scale)), 0, std::max(0, _editing.width - 1));
        const int hotY =
            std::clamp(static_cast<int>(std::lround(localY / scale)), 0, std::max(0, _editing.height - 1));

        std::string error;
        if (!_store->SetHotspot(_editing.fileName, hotX, hotY, &error))
        {
            Log("写热点失败：" + error);
            return;
        }
        _editing.hotX = hotX;
        _editing.hotY = hotY;
        if (_editorInfo != nullptr)
        {
            _editorInfo->SetText("当前热点 (" + std::to_string(hotX) + ", " + std::to_string(hotY) + ")");
        }
        DrawHotMarker();
        if (_changed != nullptr)
        {
            _changed(_context);
        }
    }

    /**在预览图上画一个十字，标出当前热点。*/
    void DrawHotMarker()
    {
        if (_hotMarker == nullptr)
        {
            return;
        }
        const float scale = _previewScale > 0.0f ? _previewScale : 1.0f;
        const float x = static_cast<float>(_editing.hotX) * scale;
        const float y = static_cast<float>(_editing.hotY) * scale;
        _hotMarker->SetPainter([x, y](inking::CanvasContext& ctx) {
            ctx.DrawLine(x - 9.0f, y, x + 9.0f, y, 1.6f, 0xD933303Cu);
            ctx.DrawLine(x, y - 9.0f, x, y + 9.0f, 1.6f, 0xD933303Cu);
            ctx.FillCircle(x, y, 4.0f, 0xD933303Cu);
        });
        _hotMarker->SetVisible(true);
    }

    static void Log(const std::string& message)
    {
        std::printf("[cursor] %s\n", message.c_str());
    }

    UserCursorStore* _store = nullptr;
    ApplyUserCursorFn _apply = nullptr;
    UserCursorChangedFn _changed = nullptr;
    void* _context = nullptr;
    const golding::PopupService* _popup = nullptr;

    inking::InkingWidget* _root = nullptr;
    inking::InkingStaticPanel* _list = nullptr;
    inking::prefab::FilePicker* _picker = nullptr;
    std::uint64_t _pickerToken = 0;

    inking::InkingStaticPanel* _editor = nullptr;
    inking::InkingStaticLabel* _editorInfo = nullptr;
    inking::InkingStaticPanel* _previewFrame = nullptr;
    inking::InkingStaticImage* _previewImage = nullptr;
    inking::InkingStaticButton* _previewHit = nullptr;
    inking::InkingStaticCanvas* _hotMarker = nullptr;

    std::vector<UserCursor> _items;
    UserCursor _editing;
    float _previewScale = 1.0f;
};

/**
 * @brief 弹窗内容的根节点：**它拥有那个管理器**。
 *
 * 顺序：根的析构先跑（成员 `_manager` 被销毁），**然后**基类才销毁孩子。所以
 * 管理器比按钮先死——按钮的回调里持有的 `this` 会悬空。
 *
 * 但按钮的回调只在**用户点它**的时候跑，而那时界面还在、弹窗还没关；关弹窗
 * 之后按钮立刻被销毁，不会再有人点它。所以这个顺序是安全的。
 */
class UserCursorRoot final : public inking::InkingStaticPanel
{
public:
    UserCursorRoot(inking::InkingWidget* parent, const inking::PanelData& data,
                   std::unique_ptr<UserCursorPanel> manager)
        : inking::InkingStaticPanel(parent, data), _manager(std::move(manager))
    {
    }

    UserCursorPanel* Manager() const noexcept { return _manager.get(); }

private:
    std::unique_ptr<UserCursorPanel> _manager;
};

} // namespace

void DemoOpenHotspotEditor()
{
    if (g_currentPanel != nullptr)
    {
        g_currentPanel->DemoOpenEditorForFirst();
    }
}

void DemoShowFilePicker()
{
    if (g_currentPanel != nullptr)
    {
        g_currentPanel->DemoShowPicker();
    }
}

inking::InkingWidget* CreateUserCursorPanel(inking::InkingWidget* parent, UserCursorStore* store,
                                             ApplyUserCursorFn apply, UserCursorChangedFn changed, void* context,
                                             const golding::PopupService* popup)
{
    if (parent == nullptr || store == nullptr)
    {
        return nullptr;
    }

    inking::PanelData data;
    data.name = "cursor.userpanel";
    data.width = kPanelWidth;
    data.height = kPanelHeight;
    data.startOpen = true;
    data.closed.color = theme::kSurface;
    data.closed.themeColor = "panelSurface";
    data.open = data.closed;
    data.shape = inking::ShapeSpec::Rect();

    auto manager = std::make_unique<UserCursorPanel>(store, apply, changed, context, popup);
    UserCursorPanel* raw = manager.get();
    auto* root = new UserCursorRoot(parent, data, std::move(manager));
    raw->Attach(root);
    return root;
}

} // namespace cursor
