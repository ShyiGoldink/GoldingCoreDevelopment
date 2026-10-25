// ---------------------------------------------------------------------------
// 命令面板本体：浮层 + 搜索框 + 过滤列表 + 执行。
//
// 几何（设计坐标，全部**相对浮层左上角**）：
//
//   ┌─────────────────── 520 ───────────────────┐
//   │ [ 搜索框 488×40 @ (16,16) ]               │
//   │ ─────────────────────────────────────────  │
//   │ 结果项 488×32，从 y=72 起，最多 8 条       │ 400
//   │ （0 条时这一行显示"无匹配"）                │
//   └───────────────────────────────────────────┘
//
// ---------------------------------------------------------------------------
// 三条容易踩的
//
//   1. **过滤在 `onTick` 里做**：`InkingTextInput` 只抛 `submitted`（Enter），
//      没有"每次变化"的事件，所以只能每帧比对文本。比对过才重滤，不是每帧重建。
//   2. **结果项每次过滤后重建，但不在点击栈里重建**：点击回调里 `RemoveChild`
//      会把正在派发的按钮删掉（shell 的菜单层栽过）。所以执行那条路只 `Close`。
//   3. **焦点给输入框**：焦点只能给一个节点，给了面板，输入框就收不到键、
//      IME / 中文输入失效。所以不做上下键选择——Enter 执行第一条，其余靠鼠标点。
// ---------------------------------------------------------------------------

#include "CommandPalette.h"

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <dataStruct/InkingShapeSpec.h>
#include <panel/InkingStaticPanel.h>
#include <panel/PanelData.h>
#include <prefab/PrefabTheme.h>
#include <scene/InkingScene.h>
#include <text/InkingStaticLabel.h>
#include <text/InkingTextInput.h>
#include <text/LabelData.h>
#include <widget/InkingWidget.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

namespace theme = inking::prefab::theme;

constexpr int kWidth = 520;
constexpr int kHeight = 400;

/**内边距：搜索框与结果区共用，左右上下一致。 */
constexpr int kPad = 16;

constexpr int kInputHeight = 40;
/**文字离搜索框左边再留一点（框本身有底色，贴边会挤）。 */
constexpr int kInputTextLeft = 12;

constexpr int kRowHeight = 32;
/**结果区顶边：搜索框 16 + 40，再留 16。 */
constexpr int kRowTop = 72;
/**最多显示几条。8 条 × 32 = 256，加上 72 还在 400 里。 */
constexpr int kMaxRows = 8;

constexpr int kRowWidth = kWidth - kPad * 2;

/**压过顶栏（100/101）与窗口按钮（200），低于菜单（260）与弹窗遮罩（300）。 */
constexpr int kPaletteZ = 240;

/**ASCII 小写化。中文不受影响——它本来就没有大小写。 */
std::string Lower(const char* text)
{
    std::string out;
    if (text == nullptr)
    {
        return out;
    }
    out.reserve(std::string(text).size());
    for (const char* p = text; *p != '\0'; ++p)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
    }
    return out;
}

/**
 * @brief 一条命令配不配得上这段查询。
 *
 * 规则两条，**或**关系：
 *   - `title` 包含查询串；
 *   - `keywords` 里**任一个词**包含查询串（空格分词）。
 *
 * 第二条是给拼音 / 英文别名留的：用户输入 `tu`，关键词 `tupu` 就能命中，
 * 不必把 `tu` 也写进去。
 *
 * 空查询 = 全部命中（按注册顺序）。
 */
bool Matches(const golding::CommandDecl& decl, const std::string& query)
{
    if (query.empty())
    {
        return true;
    }
    if (decl.title != nullptr && Lower(decl.title).find(query) != std::string::npos)
    {
        return true;
    }
    if (decl.keywords == nullptr)
    {
        return false;
    }
    const std::string lowered = Lower(decl.keywords);
    std::size_t begin = 0;
    while (begin < lowered.size())
    {
        const std::size_t space = lowered.find(' ', begin);
        const std::size_t end = (space == std::string::npos) ? lowered.size() : space;
        // **词包含查询串**（不是反过来）：`tupu` 命中 `tu`，而不是 `tu` 命中 `tupu`。
        const std::string token = lowered.substr(begin, end - begin);
        if (token.find(query) != std::string::npos)
        {
            return true;
        }
        if (space == std::string::npos)
        {
            break;
        }
        begin = space + 1;
    }
    return false;
}

inking::PanelData MakePaletteData()
{
    inking::PanelData data;
    data.name = "commands.palette";
    data.width = kWidth;
    data.height = kHeight;
    data.closed.color = theme::kSurface;
    data.closed.themeColor = "panelSurface";
    // 与菜单浮窗同一条：白卡片在白内容区上只能靠 1px 描边 + 一层阴影站住。
    data.closed.stroke.width = 1.0f;
    data.closed.stroke.color = theme::kOutline;
    data.closed.shadow = inking::ShadowSpec{0.0f, 8.0f, 0x4D000000u};
    data.open = data.closed;
    data.shape = inking::ShapeSpec::RoundedRect(12.0f);
    data.clipChildren = true;
    data.zIndex = kPaletteZ;
    return data;
}

/**搜索框的**底色**（输入框本体是标签，没有背景）。 */
inking::PanelData MakeInputBoxData()
{
    inking::PanelData data;
    data.name = "commands.query.box";
    data.width = kRowWidth;
    data.height = kInputHeight;
    data.offsetX = static_cast<float>(kPad);
    data.offsetY = static_cast<float>(kPad);
    data.closed.color = theme::kSurfaceSunken;
    data.open = data.closed;
    data.shape = inking::ShapeSpec::RoundedRect(8.0f);
    return data;
}

inking::TextInputData MakeInputData()
{
    inking::TextInputData data;
    data.name = "commands.query";
    data.width = kRowWidth - kInputTextLeft * 2;
    data.height = kInputHeight;
    data.offsetX = static_cast<float>(kPad + kInputTextLeft);
    data.offsetY = static_cast<float>(kPad);
    data.text.clear();
    data.color = theme::kTextPrimary;
    data.colorToken = "textPrimary";
    data.style.fontSize = 15.0f;
    data.style.alignV = inking::TextAlignV::Middle;
    data.maxLines = 1;
    data.overflow = inking::TextOverflow::Clip;
    return data;
}

inking::ButtonData MakeRowData(int index)
{
    inking::ButtonData data;
    data.name = "commands.item." + std::to_string(index);
    data.width = kRowWidth;
    data.height = kRowHeight;
    data.offsetX = static_cast<float>(kPad);
    data.offsetY = static_cast<float>(kRowTop + index * kRowHeight);
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
    data.text.fontSize = 14.0f;
    data.text.align = inking::TextAlignH::Left;
    data.text.alignV = inking::TextAlignV::Middle;
    data.text.leftSpace = 10.0f;
    data.shape = inking::ShapeSpec::RoundedRect(8.0f);
    return data;
}

inking::LabelData MakeEmptyData()
{
    inking::LabelData data;
    data.name = "commands.empty";
    data.width = kRowWidth;
    data.height = kRowHeight;
    data.offsetX = static_cast<float>(kPad);
    data.offsetY = static_cast<float>(kRowTop);
    data.text = "无匹配";
    data.color = theme::kTextMuted;
    data.colorToken = "textMuted";
    data.style.fontSize = 14.0f;
    data.style.align = inking::TextAlignH::Left;
    data.style.alignV = inking::TextAlignV::Middle;
    // 标签没有按钮那个 `leftSpace`，左缩走 `padding`（`LayoutSpacing` 是左/上/右/下）。
    data.style.padding = inking::LayoutSpacing{10.0f, 0.0f, 0.0f, 0.0f};
    data.maxLines = 1;
    data.overflow = inking::TextOverflow::Ellipsis;
    return data;
}

}  // namespace

namespace golding
{

// ---- 命令表 ----

int CommandTable::Add(const CommandDecl* decl)
{
    if (decl == nullptr || decl->id == nullptr || decl->id[0] == '\0')
    {
        return 1;
    }
    for (const auto& item : _items)
    {
        if (item != nullptr && item->id == decl->id)
        {
            return 2;  // 已被占用：不覆盖
        }
    }

    auto owned = std::make_unique<OwnedCommand>();
    owned->id = decl->id;
    owned->title = decl->title != nullptr ? decl->title : "";
    owned->keywords = decl->keywords != nullptr ? decl->keywords : "";
    owned->decl.id = owned->id.c_str();
    owned->decl.title = owned->title.c_str();
    owned->decl.keywords = owned->keywords.c_str();
    owned->decl.onRun = decl->onRun;
    _items.push_back(std::move(owned));
    return 0;
}

// ---- 面板 ----

CommandPalette::CommandPalette(inking::InkingWidget* parent, const CommandTable& table)
    : inking::InkingFixedPopup(parent, MakePaletteData()), _table(table)
{
    // 子件的 parent 传 nullptr：`AddItem` 会把它们挂到自己身上（与 shell 的
    // `MenuLayer::AddRow` 同一条写法——构造期就给定父级会重复一次挂载）。
    //
    // 搜索框：底（一块静态面板）+ 输入框（标签的派生类，自身没有背景）。
    AddItem(std::make_unique<inking::InkingStaticPanel>(nullptr, MakeInputBoxData()));

    auto input = std::make_unique<inking::InkingTextInput>(nullptr, MakeInputData());
    _input = input.get();
    if (AddItem(std::move(input)) == nullptr)
    {
        _input = nullptr;
    }
    if (_input != nullptr)
    {
        // Enter：执行**第一条**匹配（焦点只能给一个节点，所以不做上下键选择）。
        _input->submitted.Subscribe([this](const std::string&) { RunAt(0); });
    }

    auto empty = std::make_unique<inking::InkingStaticLabel>(nullptr, MakeEmptyData());
    _empty = empty.get();
    if (AddItem(std::move(empty)) == nullptr)
    {
        _empty = nullptr;
    }

    Refresh();
}

void CommandPalette::Open()
{
    // 定点取"父级宽度的一半"会让浮层偏右半个身位——`OpenAt` 的定点是**左上角**
    // （自锚点 (0,0)，向右下展开）。所以再减掉自身宽度的一半，落位后才是居中的。
    // 翻转 / 夹紧仍由基类负责。
    const float parentWidth = GetParent() != nullptr ? static_cast<float>(GetParent()->GetWidth()) : 0.0f;
    const float x = parentWidth > 0.0f ? (parentWidth - static_cast<float>(kWidth)) * 0.5f : 0.0f;
    OpenAt(x, 120.0f);
}

void CommandPalette::SetQuery(const char* text)
{
    if (_input == nullptr)
    {
        return;
    }
    _input->SetEditText(text != nullptr ? text : "");
}

void CommandPalette::onPopupOpened()
{
    if (_input != nullptr)
    {
        _input->SetEditText("");
        // 焦点给输入框（IME / 中文输入才正常）。基类只是把焦点取到**自己**身上，
        // 这里再交下去——焦点在本子树内时基类不会来回抢。
        if (inking::InkingScene* scene = GetScene(); scene != nullptr)
        {
            scene->SetKeyboardFocus(_input);
        }
    }
    _lastQuery.clear();
    Refresh();
}

void CommandPalette::onTick(float deltaSeconds)
{
    inking::InkingFixedPopup::onTick(deltaSeconds);
    if (_input == nullptr)
    {
        return;
    }
    const std::string text = _input->GetEditText();
    if (text == _lastQuery)
    {
        return;  // 没变就不重建：每次输入都重建全部按钮，输入快时会很明显
    }
    _lastQuery = text;
    Refresh();
}

/**
 * @brief 重算匹配、重建结果行。
 *
 * 只在 `onTick` 与"刚打开"这两处调，**不在点击回调里调**——`RemoveChild` 会把
 * 正在派发的按钮删掉（shell 的菜单层栽过这一条）。
 */
void CommandPalette::Refresh()
{
    const std::string query = Lower(_input != nullptr ? _input->GetEditText().c_str() : "");

    _results.clear();
    const int total = _table.Count();
    for (int index = 0; index < total && static_cast<int>(_results.size()) < kMaxRows; ++index)
    {
        const CommandDecl* decl = _table.At(index);
        if (decl == nullptr || decl->title == nullptr || decl->title[0] == '\0')
        {
            continue;  // 空标题不画一个空行
        }
        if (Matches(*decl, query))
        {
            _results.push_back(index);
        }
    }

    for (inking::InkingWidget* row : _rows)
    {
        RemoveChild(row);
    }
    _rows.clear();

    for (int slot = 0; slot < static_cast<int>(_results.size()); ++slot)
    {
        const CommandDecl* decl = _table.At(_results[static_cast<std::size_t>(slot)]);
        if (decl == nullptr)
        {
            continue;
        }
        inking::ButtonData look = MakeRowData(slot);
        // **直接 new 按钮时文字必须自己写进 `label`**（`MenuBar::AddButton` 才代填）。
        look.label = decl->title;

        auto row = std::make_unique<inking::InkingStaticButton>(nullptr, look);
        inking::InkingStaticButton* raw = row.get();
        if (AddItem(std::move(row)) == nullptr)
        {
            continue;
        }
        raw->SetOnClicked([this, slot] { RunAt(slot); });
        _rows.push_back(raw);
    }

    if (_empty != nullptr)
    {
        _empty->SetVisible(_results.empty());
    }
}

/**
 * @brief 执行第 `index` 条**过滤后**的结果。
 *
 * 顺序不能反：**先关面板再执行**。动作里可能又打开另一个面板 / 弹窗，
 * 顺序反了就会打架（与菜单"叶子先关整串再执行"同一条原因）。
 */
void CommandPalette::RunAt(int index)
{
    if (index < 0 || index >= static_cast<int>(_results.size()))
    {
        return;
    }
    const CommandDecl* decl = _table.At(_results[static_cast<std::size_t>(index)]);
    if (decl == nullptr)
    {
        return;
    }
    void (*run)(void*) = decl->onRun;
    Close();
    if (run != nullptr)
    {
        run(nullptr);
    }
}

}  // namespace golding
