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
/** 页面根的节点名（从界面树上找它，避免跨页持有裸指针）。 */
inline constexpr const char* kPageRootNodeName = "appmanager.page.root";

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
    /**
     * 侧栏点击走到"进入已在运行的窗口"那一段的次数（观测口）。
     *
     * 为什么需要它：真机点行会抢前台，自检不能这么干；而"行回调真的接上了这一段"
     * 是本次改动的核心接线。于是用一个计数器把"走到了"记下来，跳转本身由
     * `CanEnterRunning` / `ActivateRunning` 的断言覆盖（见 `AppManagerPlugin.cpp`）。
     */
    int enterAttempts = 0;

    /**
     * 背景纯享：页面上的按钮 / 文字全部隐身，只剩背景（眼睛按钮自己留着，
     * 否则鼠标就没法把它叫回来了）。
     *
     * 状态放在这里而不是页面里：页面每次切应用都会被重建，而这个开关是**用户偏好**，
     * 换个应用应该还是纯享态。
     */
    bool immersive = false;
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

/**
 * 自检用：临时关掉"点侧栏就进入已有窗口"。
 *
 * 为什么需要这个口子：自检会**语义点击**侧栏行（验"行回调 → 选中 → 切页"整条链），
 * 而点行现在会顺带把已经在跑的那个程序的窗口切到前台——自检不该动用户的桌面
 * （`docs/APPMANAGER.md` §8 的两条纪律：不启动外部程序、不碰用户真实状态）。
 * 传 `false` 恢复默认行为。
 */
void SuppressEnterFromSidebar(bool suppress);

/** 自检用：侧栏点击走到"进入已在运行的窗口"那一段的次数。 */
int EnterAttemptCount();

/** 让右侧重新装配（导航服务同页重挂；面板不缓存页面，所以 = 重建）。 */
void RefreshPage();

/** 打开"添加应用"（文件选择）。 */
void OpenImportPopup();

/** 打开某个应用的设置弹窗。 */
void OpenSettingsPopup(const std::string& id);

/** 自检用：页面根里的那个背景节点（图片或视频）；没有返回 nullptr。 */
inking::InkingWidget* BackgroundNode();

/** 自检用：页面根里的那只眼睛按钮；没有返回 nullptr。 */
inking::InkingWidget* EyeButton();

/** 自检用：页面上"纯享时会隐身"的节点里，现在还有几个是可见的。 */
int VisibleOverlayCount();

/** 背景纯享开关（`Ctrl+E` 与那只眼睛按钮共用的唯一落点）。 */
void SetImmersive(bool immersive);

/** 当前是纯享态吗。 */
bool IsImmersive();

/** 取反。 */
void ToggleImmersive();

/** 设置弹窗被建过几次（`PopupService` 没有查询口，用它当观测口）。 */
int SettingsBuildCount();

/** 添加弹窗被建过几次。 */
int ImportBuildCount();

} // namespace appmanager
