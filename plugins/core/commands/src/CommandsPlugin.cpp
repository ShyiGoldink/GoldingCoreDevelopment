// ---------------------------------------------------------------------------
// commands —— 命令面板（`golding.commands` 的提供方）。
//
// 三件事：
//   1. 提供 `golding.commands` 服务：别的插件交一个 `CommandDecl` 就把动作挂进来；
//   2. 面板本体（`CommandPalette`）：浮层 + 搜索框 + 过滤列表 + 执行；
//   3. 入口：顶栏「命令」一项 + `Ctrl+K` 快捷键。
//
// ---------------------------------------------------------------------------
// 两条不变量
//
//   1. **命令只描述"做什么"，不认识界面**：本插件不认识任何一条具体命令的名字
//      （除自己内建的那条"关闭面板"），也不认识任何插件名 / 节点名 / 区域名。
//   2. **依赖是可选的**：注册命令的插件**不该**在 `plugin.json` 写 `needs`——
//      本插件不在时它们照常工作，只是少一条命令（`Use` 取不到 → WARN → 跳过）。
//
// ---------------------------------------------------------------------------
// 快捷键：走库的进程级表，但**只能在 `Ready` 里绑**
//
// `InkingWindow::Instance()` 是库的公开单例，进程级一张表（`input/ShortcutRegistry.h`）。
// 插件 dll 与宿主链的是同一个 `inking` 库，所以直接 Bind 即可，不必往 `HostApi`
// 追加字段——那才是 ABI 事件。
//
// **但它在 `Register` 里碰不得**：`Instance()` 第一次被调用时就把设计尺寸烘进
// 根锚点，而宿主是在 `registerAll()` **之后**才调 `SetDesignSize(1920,1080)`——
// 那时会被拒（"已有几何按旧尺寸定型"），宿主直接启动期硬失败（实测踩到）。
// 所以 `Bind` 落在 `Ready`：装配之后、跑帧之前，窗口单例已经存在。
//
// ---------------------------------------------------------------------------
// 面板挂在哪
//
// `Ready` 里拿 `host.UiRoot()`（面板插件建的那棵界面树的根）。它是**某个面板
// 插件**的节点，但"能挂子节点"是所有 `InkingWidget` 的通用契约，所以这里不需要
// 认识它的结构。给不出（没有面板插件）就不建面板，服务仍然可用。
// ---------------------------------------------------------------------------

#include "CommandPalette.h"

#include <sdk/GoldingPlugin.h>

#include <input/Shortcut.h>
#include <input/ShortcutRegistry.h>
#include <scene/InkingScene.h>
#include <widget/InkingWidget.h>
#include <window/InkingWindow.h>

#include <cstdio>
#include <memory>
#include <string>

namespace
{

/**自己的 `Host` 副本：点击 / 快捷键回调都不在 `Register` 的调用栈上。 */
golding::Host g_host{nullptr, nullptr};

/**命令表：服务与面板都只读这一份（**唯一的权威来源**）。 */
golding::CommandTable g_table;

/**
 * 面板；界面根给不出时是 nullptr（面板插件缺席的合法降级）。
 *
 * **裸指针，不是 `unique_ptr`**：它已经挂在界面树上（`AddItem` / 父节点持有），
 * 树析构时会删掉它。再包一层智能指针就是进程退出时**析构两次**——实测约 10% 的
 * 启动在退出路径上段错误。与"插件 create 出来的节点归父节点所有"是同一条规则。
 */
golding::CommandPalette* g_palette = nullptr;

/**快捷键 id；0 = 没绑上（被别人占了，或还没到 `Ready`）。 */
inking::ShortcutRegistry::Id g_shortcutId = 0;

// ---- `golding.commands` 服务（对外只有只读那半边）----

int commandsAdd(void* /*self*/, const golding::CommandDecl* decl) { return g_table.Add(decl); }

int commandsCount(void* /*self*/) { return g_table.Count(); }

const golding::CommandDecl* commandsAt(void* /*self*/, int index) { return g_table.At(index); }

const golding::CommandService kCommandService{
    GOLDING_PLUGIN_ABI_VERSION,
    &commandsAdd,
    &commandsCount,
    &commandsAt,
};

// ---- 入口 ----

/**
 * @brief 打开面板。快捷键与顶栏项走**同一条路**，所以两者的行为不会分叉。
 */
void OpenPalette()
{
    if (g_palette == nullptr)
    {
        g_host.Warn("commands：面板没建起来（界面根给不出），打不开");
        return;
    }
    g_palette->Open();
}

// ---- 内建命令 ----

/**
 * 关面板。
 *
 * 面板没建（界面根给不出）时是空操作——"关一个没开着的面板"本来就该什么也不做，
 * 所以那条命令在那种情形下只是**没有可见效果**，不是错误。
 */
void onClosePalette(void* /*hostContext*/)
{
    if (g_palette != nullptr)
    {
        g_palette->Close();
    }
}

/**与顶栏项 / `Ctrl+K` 同一条路：面板自己也可以被"搜"出来执行。 */
void onOpenPalette(void* /*hostContext*/) { OpenPalette(); }

/**
 * 两条内建命令。它们不是占位：面板自己就该能被搜到、能被关掉——
 * 而且**自检要用它们当材料**（见 `CheckPalette`），所以不必再造两条假命令。
 *
 * `commands.open` 的 keywords 里带 `palette`，`commands.close` 带 `guanbi`：
 * 拼音别名就是给 `keywords` 这个字段准备的（中文标题搜不到英文/拼音输入）。
 */
const golding::CommandDecl kOpenCommand{
    "commands.open",
    "打开命令面板",
    "open dakai dk palette",
    &onOpenPalette,
};

const golding::CommandDecl kCloseCommand{
    "commands.close",
    "关闭命令面板",
    "close guanbi gb",
    &onClosePalette,
};

void onEntryActivate(void* /*hostContext*/) { OpenPalette(); }

/**
 * @brief 绑 `Ctrl+K`。**只能在 `Ready` 里调**（见文件头：窗口单例碰早了会毁掉
 * 设计尺寸）。
 *
 * 绑不上**不算失败**：`ShortcutRegistry` 先到先得，被别人占了就退回"只靠顶栏
 * 入口"，功能不丢，只留一条 WARN。
 */
void BindShortcut()
{
    if (g_shortcutId != 0)
    {
        return;
    }
    g_shortcutId = inking::InkingWindow::Instance().GetShortcuts().Bind(
        inking::Shortcut::Parse("Ctrl+K"), [] { OpenPalette(); }, "commands.open");
    if (g_shortcutId == 0)
    {
        g_host.Warn("commands：Ctrl+K 没绑上（已被占用？），只剩顶栏入口");
    }
}

/**顶栏那一项：面板画成按钮，文字就是 title。order 40 = 排在「插件」(60) 左边。 */
const GoldingRegionItemDecl kEntry = {
    "menubar", "commands.entry", "命令", nullptr, 40, nullptr, &onEntryActivate,
};

// ---- 自检 ----

/**一行断言。`Ready` 的返回值就是失败项数，宿主把它算进退出码。 */
int check(const char* label, bool ok, const std::string& detail)
{
    std::printf("[%s] commands.%-24s %s\n", ok ? "ok" : "FAIL", label, detail.c_str());
    return ok ? 0 : 1;
}

/**
 * @brief 面板本体的自检。**不是临时探针**——它是 `Ready` 的一部分，每次启动都跑。
 *
 * 五条断言全部走**与真机同一条代码路径**，所以"自检过了、真机不过"不会发生在
 * 这几条上：
 *
 *   1. `Open()` → `IsOpen()`；
 *   2. 改搜索框文本 → **跑一帧**（`Tick`，场景每帧调的也是它）→ 过滤生效；
 *   3. 换个更窄的关键字 → 项数从 2 掉到 1；
 *   4. `Activate("commands.item.0")` 执行第一条：那条是 `commands.close`，
 *      所以可观测的结果就是"面板关掉了"——比一个静态计数器实在。
 *   5. `Activate("commands.entry")`（顶栏那一项）→ 面板又开了：这一条把
 *      "区域项 → 面板"整条链一起验了。
 *
 * 材料用两条**内建命令**而不是另造两条假命令：面板里不该出现"probe.xxx"这种
 * 只有自检才认识的东西。
 *
 * 自检完把面板关掉：它是真实界面，留在画面上会改变首帧的样子。
 */
int CheckPalette(inking::InkingScene* scene)
{
    if (g_palette == nullptr)
    {
        return 1;
    }
    int failures = 0;

    g_palette->Open();
    failures += check("面板能打开", g_palette->IsOpen(), "commands.palette");

    // 「命令」两个字正好只出现在两条内建标题里。哪天有插件的命令标题也带它，
    // 这条会报出来——届时换一个更窄的查询即可，不是代码坏了。
    g_palette->SetQuery("命令");
    g_palette->Tick(0.016f);
    const int all = g_palette->ResultCount();
    failures += check("汉字过滤：两条内建都在", all == 2, "resultCount=" + std::to_string(all));

    g_palette->SetQuery("guanbi");
    g_palette->Tick(0.016f);
    const int one = g_palette->ResultCount();
    failures += check("拼音过滤：只剩关闭那条", one == 1, "resultCount=" + std::to_string(one));

    if (one > 0 && scene != nullptr)
    {
        scene->Activate("commands.item.0");
        failures += check("执行第一条后面板关掉", !g_palette->IsOpen(),
                          "isOpen=" + std::to_string(g_palette->IsOpen()));
    }

    // 顶栏入口：`commands.entry` 是面板插件按区域项 id 命名的按钮，
    // `Activate`（语义点击）走的就是"用户点顶栏那一项"的同一条链。
    if (scene != nullptr)
    {
        const bool entryOpened = scene->Activate("commands.entry") && g_palette->IsOpen();
        failures += check("顶栏「命令」能打开", entryOpened, "commands.entry");
        g_palette->Close();
    }

    // 自检完把面板关掉：它是真实界面，留在画面上会改变首帧的样子。
    g_palette->Close();
    return failures;
}

class CommandsPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        g_host = host;

        // 顺序不能反：内建命令要在 `AddService` **之前**进表。
        // 反过来会出现"服务已对外可见、但列表还是空"的那一瞬——下一帧才补上，
        // 行为不确定。
        g_table.Add(&kOpenCommand);
        g_table.Add(&kCloseCommand);

        bool ok = host.AddService(golding::kCommandsServiceName, &kCommandService);
        ok = host.AddRegionItem(kEntry) && ok;
        return ok;
    }

    int Ready(const golding::Host& host) override
    {
        int failures = 0;

        // 插件表不为空 = 装配真的跑过（不是被单独加载做探测）。
        failures += check("插件表非空", host.PluginCount() > 0,
                          "pluginCount=" + std::to_string(host.PluginCount()));

        // 走**服务那条路**数一遍，而不是直接问表：这样连"服务有没有注册成功"
        // 一起验了（别人拿到的就是这一条路径）。
        const int count = commandsCount(nullptr);
        failures += check("命令表有内建那两条", count >= 2, "count=" + std::to_string(count));
        // 别人真的把命令注册进来了吗（可选依赖有没有接上）。Spellbook 两条 +
        // settings 一条，用 `>= 1` 是为了"只装了其中一个"也能过。
        failures += check("有别的插件注册了命令", count >= 3, "count=" + std::to_string(count));

        // 快捷键只能在这里绑（窗口单例在 `Register` 阶段还碰不得，见文件头）。
        BindShortcut();

        // 面板要挂在界面根上；给不出就是没有面板插件，属合法降级（不算失败项）。
        auto* root = static_cast<inking::InkingWidget*>(host.UiRoot());
        if (root == nullptr)
        {
            host.Warn("commands：没有界面根，面板不建（服务照常可用）");
            return failures;
        }

        if (g_palette == nullptr)
        {
            g_palette = new golding::CommandPalette(root, g_table);
        }
        failures += check("面板建起来了", g_palette != nullptr, "commands.palette");
        failures += check("快捷键或顶栏入口至少一个", g_shortcutId != 0 || host.RegionItemCount("menubar") > 0,
                          "shortcut=" + std::to_string(g_shortcutId) +
                              " menubar=" + std::to_string(host.RegionItemCount("menubar")));

        failures += CheckPalette(root->GetScene());
        return failures;
    }
};

}  // namespace

GOLDING_EXPORT_PLUGIN(CommandsPlugin)
