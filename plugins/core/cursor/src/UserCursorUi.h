#pragma once

// 用户指针的**管理界面**：一份弹窗内容（列表 + 导入 + 选热点）。
//
// 与 `UserCursors.h` 的分工：那边是"格式与仓库"（契约），这边是"怎么摆出来"。
// 界面不认识清单格式——它只调仓库的四个动作（扫 / 导入 / 改热点 / 删）。
//
// 它是**自绘**的（不是系统原生对话框）：InkingUIKit 的 `prefab::FilePicker` 就是
// 组装自绘的（见它的头文件里那条"路线已由用户裁定"），而原生对话框在无头环境里
// 没法验（弹窗要人点），本项目的纪律是"能变成数值判据的自己验"。

#include "UserCursors.h"

#include "GoldingServices.h"

namespace inking
{
class InkingWidget;
}

namespace cursor
{

/**
 * @brief 插件提供的回调：把某个用户指针设为当前指针。
 *
 * @return false = 没设成（例如这一层还没挂上）。
 */
using ApplyUserCursorFn = bool (*)(const UserCursor&);

/**
 * @brief 插件提供的回调：用户指针列表变了（导入 / 删除 / 改热点）。
 *
 * 服务里的样式列表是从这个列表推出来的，所以改完要通知插件刷新一次。
 */
using UserCursorChangedFn = void (*)(void* context);

/**
 * @brief 造一份"用户指针"弹窗的内容。
 *
 * @param parent  弹窗给的内容容器（尺寸由弹窗声明决定）。
 * @param store   用户指针仓库。
 * @param apply   "把这条设为当前指针"。
 * @param changed 列表变了之后回调（可为 nullptr）。
 * @param context 回调的上下文。
 * @param popup   弹窗服务（给「关闭」那颗按钮用；可为 nullptr）。
 *
 * @return 面板根节点（由容器拥有）。
 */
inking::InkingWidget* CreateUserCursorPanel(inking::InkingWidget* parent, UserCursorStore* store,
                                             ApplyUserCursorFn apply, UserCursorChangedFn changed, void* context,
                                             const golding::PopupService* popup);

/**
 * @brief 演示 / 自检用：让当前的用户指针面板直接进入"选热点"那一步（第一条）。
 *
 * 界面上点「热点」按钮走的是同一条路；这个钩子只是给**没有人能点按钮**的环境
 * （无头自检、截图）留一个入口。**当前没有面板** → 什么都不做。
 *
 * 它不收根节点指针：弹窗一关，根节点就没了，那个指针会悬空。
 */
void DemoOpenHotspotEditor();

/**
 * @brief 演示 / 自检用：让当前的用户指针面板直接切到"选文件"那一步（导入界面）。
 *
 * 与上一条同理：文件选择器是**给人点**的，无头环境里没人能点。
 */
void DemoShowFilePicker();

} // namespace cursor
