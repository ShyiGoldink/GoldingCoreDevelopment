#pragma once

// 设置插件的**服务契约**（`golding.settings`）。
//
// 为什么要有它：设置内容是"一个插件组装、别的插件也能用"的东西。例如一个主题
// 插件想在自己的按钮旁边给出「打开设置」——它不该重新实现一遍"列区块 → 弹窗"，
// 也不该知道设置区块长什么样。所以设置插件把**入口**暴露成服务，内容仍然只有
// 一份（设置表 + settings 插件的渲染）。
//
// 与 `GoldingMenu.h` / `GoldingServices.h` 同一条规矩：契约在提供方，规格在头文件。
//
// 依赖声明（`plugin.json`）里需要一个 `needs`，否则装配顺序与"能不能用"都要靠
// 运行时才发现：
//
//   "requires": { "plugins": ["settings"] },
//   "needs":    { "golding.settings": 1 }

#include <sdk/golding_plugin.h>

namespace golding
{

/** 服务名（唯一键）。 */
inline constexpr const char* kSettingsServiceName = "golding.settings";

struct SettingsService
{
    unsigned int abiVersion;

    /**打开完整设置弹窗（模态，列出全部区块）。0 = 成功。 */
    int (*openAll)(void* self);

    /**打开某一个区块的弹窗（只显示它）。未知 id 返回 1。*/
    int (*openSection)(void* self, const char* sectionId);

    /**
     * @brief 在**你自己的顶栏按钮**下面打开设置菜单（列区块，点一项才弹内容）。
     *
     * 参数是区域项 id（`GoldingRegionItemDecl::id`），与 `golding.menu` 的锚点
     * 同一条约定——面板知道那个按钮在哪儿，插件只交身份。
     *
     * @return 0 = 已打开；非 0 = 菜单服务不在 / 面板不在 / 锚点找不到。
     */
    int (*openMenuAt)(void* self, const char* regionItemId);
};

} // namespace golding
