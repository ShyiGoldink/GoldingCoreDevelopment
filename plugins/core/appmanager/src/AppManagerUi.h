#pragma once

// ---------------------------------------------------------------------------
// appmanager 的**界面层**：侧栏盒子、应用页面、两个弹窗（添加 / 设置）。
//
// 为什么侧栏不用"一个应用注册一个区域项"：`GoldingRegionItemDecl::create` 收不到
// 自己在为哪一项建界面（只有 hostContext + parent），而模块里的区域项是**装配期
// 一次性扫完**的。于是运行期新导入的应用根本进不去。
//
// 所以这里只用**一个**区域项（`appmanager.sidebar`），它建一个自管的纵向盒子，
// 里面一行一个应用 + 最后一行"＋ 添加应用"。增删行 = 在盒子里挂/摘节点，
// 不依赖面板重扫，也不需要重建盒子。
// ---------------------------------------------------------------------------

#include "AppStore.h"

#include "GoldingServices.h"

#include <sdk/GoldingPlugin.h>

#include <string>
#include <vector>

namespace inking
{
class InkingWidget;
}

namespace appmanager
{

/** 页面 id（面板的导航服务按它切页）。 */
inline constexpr const char* kPageId = "appmanager.page";
/** 侧栏那个区域项的 id。 */
inline constexpr const char* kSidebarRegionItemId = "appmanager.sidebar";
/** 页面里"背景铺满"那一层的节点名（自检用）。 */
inline constexpr const char* kBackgroundNodeName = "appmanager.background";

/** 插件只有一份实例，界面状态用文件级单例最省事（与其它插件的做法一致）。 */
struct UiState
{
    golding::Host host{nullptr, nullptr};
    const golding::PopupService* popup = nullptr;
    const golding::NavigationService* nav = nullptr;

    std::vector<AppEntry> apps;
    std::string selectedId;

    /** 面板给的侧栏容器（区域项挂在它上面）。 */
    inking::InkingWidget* sidebarHost = nullptr;
    /** 自管的纵向盒子。 */
    inking::InkingWidget* sidebarBox = nullptr;
    /** "＋ 添加应用"那一行（新行要插在它前面）。 */
    inking::InkingWidget* addRow = nullptr;
    /** 当前页面根（每次导航重建）。 */
    inking::InkingWidget* pageRoot = nullptr;
    /** 页面被重建过几次（自检 / 观测口）。 */
    int pageBuildCount = 0;
};

UiState& State();

/** 在 `apps` 里按 id 找；没有返回 nullptr。 */
AppEntry* FindApp(const std::string& id);

/** 区域项 `create`：建侧栏盒子。 */
GoldingWidgetHandle createSidebar(void* hostContext, void* parent);

/** 页面 `create`：按当前选中应用建页面。 */
GoldingWidgetHandle createPage(void* hostContext, void* parent);

/** 行数（应用数 + 1 行"添加"）。自检用。 */
int SidebarRowCount();

/** 选中一个应用：更新选中色 + 切/重建页面。 */
void SelectApp(const std::string& id);

/** 让右侧重新装配（导航服务同页重挂；面板不缓存页面，所以 = 重建）。 */
void RefreshPage();

/** 打开"添加应用"（文件选择）。 */
void OpenImportPopup();

/** 打开某个应用的设置弹窗。 */
void OpenSettingsPopup(const std::string& id);

/** 自检用：页面根里的那个背景节点（图片或视频）；没有返回 nullptr。 */
inking::InkingWidget* BackgroundNode();

/** 设置弹窗被建过几次（`PopupService` 没有查询口，用它当观测口）。 */
int SettingsBuildCount();

/** 添加弹窗被建过几次。 */
int ImportBuildCount();

} // namespace appmanager
