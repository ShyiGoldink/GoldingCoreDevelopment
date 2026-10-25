#pragma once

// 命令面板插件的**数据面与界面**（`golding.commands` 的提供方一侧）。
//
// 两个东西，一份文件，因为它们是同一件事的两面：
//
//   - `CommandTable`：**命令住哪**。深拷贝后的表，服务（`golding.commands`）只是
//     它对外的一个只读窗口；
//   - `CommandPalette`：**命令怎么被找到并跑起来**。浮层 + 搜索框 + 结果列表。
//
// ---------------------------------------------------------------------------
// 面板的三条边界（写清楚，别让人猜）
//
//   1. **命令只描述"做什么"**：`CommandDecl` 里没有位置 / 尺寸 / 图标。怎么排、
//      怎么搜、面板长什么样，全是本文件的事。
//   2. **结果 ≤ 8 条、每次输入都重建**：命令的量级是几十条，用按钮比引
//      `prefab::ListView` 直接（那个是"数据行 + 滚动"的通用件）。
//   3. **焦点归输入框，不做上下键选择**：焦点只能给一个节点（`InkingScene`
//      的 `SetKeyboardFocus`），给了面板，输入框就收不到键、IME / 中文输入失效。
//      所以 Enter 走 `submitted` 执行**第一条**匹配，其余靠鼠标点。
//
// 面板挂在哪：由 `CommandsPlugin` 在 `Ready` 里把界面根（`host.UiRoot()`）
// 交进来建一次，之后一直复用（关 = 隐藏，不销毁）。

#include "GoldingCommands.h"

#include <panel/InkingFixedPopup.h>

#include <memory>
#include <string>
#include <vector>

namespace inking
{
class InkingStaticLabel;
class InkingTextInput;
}  // namespace inking

namespace golding
{

/**
 * @brief 一条**深拷贝后**的命令。
 *
 * 为什么存 `unique_ptr` 而不是值：`CommandService::at` 交出去的是 `CommandDecl*`，
 * 而 `vector` 扩容会把元素整块搬到新地址——那时别人手里那批指针就全悬了。
 * 换成"每个元素自己一块"之后，追加命令**不会**让已交出的指针失效。
 */
struct OwnedCommand
{
    std::string id;
    std::string title;
    std::string keywords;
    CommandDecl decl{};  ///< 三个 `char*` 分别指向上面三个串（空串也不留 nullptr）
};

/**命令表。**唯一的权威来源**——服务与面板都只读它，谁都不缓存。 */
class CommandTable
{
public:
    /**
     * @brief 登记一条；0 = 成功。
     *
     * 非 0 的三种：`decl` 为空 / `id` 为空 / `id` 已占用。**不覆盖**——
     * "后来者悄悄改掉前者"是最难查的一类 bug（与 `ShortcutRegistry` 的先到
     * 先得同一条理由）。
     */
    int Add(const CommandDecl* decl);

    int Count() const noexcept { return static_cast<int>(_items.size()); }

    /**第 index 条（下标 = 注册顺序，与过滤无关）；越界返回 nullptr。 */
    const CommandDecl* At(int index) const
    {
        if (index < 0 || index >= static_cast<int>(_items.size()))
        {
            return nullptr;
        }
        return &_items[static_cast<std::size_t>(index)]->decl;
    }

private:
    std::vector<std::unique_ptr<OwnedCommand>> _items;
};

/**
 * @brief 命令面板：`InkingFixedPopup` + 搜索框 + 每次过滤后重建的结果列表。
 *
 * 为什么是 `InkingFixedPopup` 而不是 `golding.popup`（模态遮罩）：命令面板要
 * "点外面就收"，而模态遮罩会把整个界面挡住——和"快速执行一条命令"的语义相反。
 */
class CommandPalette final : public inking::InkingFixedPopup
{
public:
    CommandPalette(inking::InkingWidget* parent, const CommandTable& table);

    /**
     * @brief 打开：定点落在父级**水平居中**、顶部 y=120 的位置。
     *
     * 不出窗口（翻转 / 夹紧）由 `InkingFixedPopup` 负责，这里只给定点。
     */
    void Open();

    /**当前**过滤后**的结果条数（自检用）。 */
    int ResultCount() const noexcept { return static_cast<int>(_results.size()); }

    /**
     * @brief 把搜索框的文本换掉（自检用：等价于"用户打了字"）。
     *
     * **只改文本不重滤**——重滤发生在 `onTick`（也就是真链路那一帧），
     * 这样自检跑的是与真机同一条代码路径，而不是另一条捷径。
     */
    void SetQuery(const char* text);

protected:
    void onTick(float deltaSeconds) override;
    void onPopupOpened() override;

    /**基类没重写这条（描边实现因此永远没人调），不补就只剩一条阴影。 */
    bool HasRenderDecoration() const noexcept override { return true; }

private:
    void Refresh();
    /**执行第 `index` 条**过滤后**的结果。 */
    void RunAt(int index);

    const CommandTable& _table;

    inking::InkingTextInput* _input = nullptr;
    /**"无匹配"那一行：常驻，只切可见性（重建标签要重排，没有必要）。 */
    inking::InkingStaticLabel* _empty = nullptr;
    std::vector<inking::InkingWidget*> _rows;

    /**过滤结果：命令表里的下标，按匹配顺序。 */
    std::vector<int> _results;
    /**上一次重滤时的文本；与当前文本不同才重滤。 */
    std::string _lastQuery;
};

}  // namespace golding
