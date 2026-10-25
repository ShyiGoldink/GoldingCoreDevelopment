# 任务书：命令面板插件（`commands`）（自身规划）

> 状态：**待执行**（本文件按任务逐条跟踪，完成规则见 §1）  
> 日期：2026-10-24  
> 来源：用户提问「下一个插件做啥」→ 选定"命令面板"；核实结论见 §2  
> 目标版本：`0.3.0`（三批）  
> 涉及层：`plugins/common`（新契约）、`plugins/core/commands`（新插件）、`plugins/core/spellbook`、`plugins/core/settings`、`docs/`

---

## 0. 结论（TL;DR）

**1 个新插件、1 份新契约、2 处既有插件各加几行注册**。不碰 core、不碰 ABI——因为快捷键那条路库已经铺好了（`InkingWindow::Instance()` 是公开单例）。

| 编号 | 任务 | 批次 | 落点 | 代价 |
| - | - | - | - | - |
| T-01 | 立 `golding.commands` 契约与 `commands` 插件骨架 | 0.3.0-a | `plugins/common/GoldingCommands.h`、`plugins/core/commands/` | 低 |
| T-02 | 做面板本体：浮层 + 搜索框 + 过滤列表 + 执行 | 0.3.0-b | `plugins/core/commands/src/CommandPalette.cpp/.h` | 中 |
| T-03 | 接快捷键与顶栏入口，并让 spellbook / settings 把动作注册进来 | 0.3.0-c | `commands` 的 `Register`、两个插件的 `Register`、`plugins/CMakeLists.txt` | 中 |

设计取向，两条不变量：

1. **命令只描述"做什么"，不认识界面**：`CommandDecl` 里没有任何位置/尺寸/图标字段。谁想加动作就交一个结构体，面板怎么排是面板的事。
2. **可选依赖，缺了就降级**：spellbook / settings 注册命令**不写 `needs`**——命令面板不在时它们照常工作，只是没那条命令（`Use` 取不到就打一行 WARN 跳过）。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、哪一步不能反。不贴大段代码 |
| **与改动前对比** | 表格；**至少一行"改动后仍不变"的边界** |

另附 **落点**、**验收**（可断言）、**风险与回退**。

### 1.2 完成即压缩（核心规则）

任务完成后，把该任务整段压缩成一行：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并把 §6.2 状态改 `已完成`。**两处必须同改。** 细目交给 `git log --grep=T-01`。

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 <标题>-已完成（日期，结论）` |
| 放弃 | `- T-01 <标题>-已放弃（原因；替代方案）` |
| 拆分 | `- T-01 <标题>-已拆分为 T-01a / T-01b`，各自走完整三段 |
| 阻塞 | `- **状态**：阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

四条全中才算完成：编译通过 / 验收断言在自检里跑绿（不是临时探针）/ 文档同步 / 对比表每行能对上实测。

---

## 2. 基线事实（核实结论）

| 条目 | 判定 | 证据 |
| - | - | - |
| 库里有进程级快捷键表 | 成立 | `include/input/ShortcutRegistry.h:41`（`Bind/Unbind/IsTaken/Dispatch`）；`include/window/InkingWindow.h:473`（`GetShortcuts()`） |
| 插件拿不到窗口实例 | **部分成立** | 插件只有 `InkingWidget*` / `InkingScene*`，而 `include/scene/InkingScene.h` 没有 `GetWindow`；但 `InkingWindow::Instance()` 是 **public static**（`InkingWindow.h:37`），`src/prefab/MenuBar.cpp:228` 就是这么用的 → 插件可以走单例，**不需要改 ABI** |
| 场景能把键盘派给指定节点 | 成立 | `InkingScene.h:522 SetKeyboardFocus`、`:525 GetKeyboardFocus`、`:538 DispatchKeyboard` |
| 输入框可直接用（含 Enter 提交） | 成立 | `include/text/InkingTextInput.h:65`（`class InkingTextInput : public InkingStaticLabel`）、`:69`（`submitted` 事件）、`:73`（`WantsKeyboardFocus() == true`） |
| 有"点外关闭 + 定点打开"的浮层 | 成立 | `include/panel/InkingFixedPopup.h:86 OpenAt`、`:92 Close`、`:112 SetCloseOnOutsideClick`、`:116 SetTakeFocusOnOpen`、`:107 AddItem`；shell 的菜单层正在用它 |
| 有列表预制件 | 成立（**本任务不用**） | `include/prefab/ListView.h`、`ListRow.h`。理由见 T-02 的候选 A |
| 顶栏区域项可注册 | 成立 | `GoldingRegionItemDecl` + shell 的 `menubar` 区域（spellbook 已在用，`plugins/core/spellbook/src/SpellbookPlugin.cpp` 的 `kEntry`） |
| 面板需要新的服务契约 | 成立 | 现有契约只有 `golding.popup`/`menu`/`settings`/`navigation`；没有任何"注册动作"的口 |
| 命令面板必须改 ABI / core | **不成立** | 三件事（快捷键单例、浮层、输入框）全在库的公开面上；契约放 `plugins/common` 与现有四个契约同级 |

据此，**本任务书不含 SDK、core、宿主改动**。若开工后发现 `InkingWindow::Instance()` 在插件 dll 里链接不到（未导出），才升级为 ABI 任务（见 §7）。

---

## 3. 第一批：0.3.0-a（契约与骨架）

- T-01 立 `golding.commands` 契约与 `commands` 插件骨架-已完成（2026-10-25，`plugins/common/GoldingCommands.h` + `plugins/core/commands/`，服务 `golding.commands` 在日志可查；`ctest` 6/6；内建两条 `commands.open` / `commands.close`，`add` 深拷贝、同 id 驳回）

---

## 4. 第二批：0.3.0-b（面板本体）

- T-02 做面板本体：浮层 + 搜索框 + 过滤列表 + 执行-已完成（2026-10-25，`CommandPalette` 520×400 挂在 `host.UiRoot()`，过滤在 `onTick`；Ready 自检四条全绿——打开、汉字过滤 2 条、拼音过滤 1 条、`Activate("commands.item.0")` 执行后关面板；截图确认不遮顶栏）

---

## 5. 第三批：0.3.0-c（快捷键、入口、接入现有动作）

- T-03 接快捷键与顶栏入口，并让 spellbook / settings 注册命令-已完成（2026-10-25，顶栏「命令」order 40 生效、`Ctrl+K` 绑上（id=1）；spellbook 2 条 + settings 1 条注册成功（count=5）；删掉 commands 插件的构建下 spellbook / settings 仍 0 失败项。**唯一偏离计划**：`Bind` 从 `Register` 挪到 `Ready`——`InkingWindow::Instance()` 在 `registerAll()` 阶段碰不得（见 `docs/NOTES.md` 坑））

---

## 6. 批次门槛与进度

### 6.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| 0.3.0-a | 编译零警告；`ctest` 6/6；`plugin/core/commands/` 目录里有 `plugin.json` + `bin/commands.dll`；服务名在日志里可查（`[host] 注册服务 golding.commands`） |
| 0.3.0-b | `commands` 的 Ready 自检三条断言全绿（项数 2 → 1、执行计数 1）；面板能在 1920×1080 下完整显示且不遮住顶栏；中文输入可用 |
| 0.3.0-c | `Ctrl+K` 能开；`Activate("commands.entry")` 能开；spellbook / settings 在**删掉 commands 插件**的构建下仍零失败项（验证可选依赖的降级） |

### 6.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 立 `golding.commands` 契约与 `commands` 插件骨架 | 0.3.0-a | 已完成 |
| T-02 | 做面板本体：浮层 + 搜索框 + 过滤列表 + 执行 | 0.3.0-b | 已完成 |
| T-03 | 接快捷键与顶栏入口，并让 spellbook / settings 注册命令 | 0.3.0-c | 已完成 |

三个批次均已落地（§3~§5 已按 §1.2 压缩，细目见 `git log --grep=T-0x`）。

---

## 7. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 键盘上下键选择结果 | 焦点只能给一个节点。给面板 → 输入框拿不到焦点、IME/中文输入失效；给输入框 → 方向键被输入框吃掉。要做必须先解决"焦点转发 + IME 候选窗位置"，属于独立任务（`InkingTextInput` 的 `TextEditState` 是现成起点） |
| 命令带参数 / 子命令（"打开 xxx 的 yyy"） | v1 只做无参动作。带参数会引出"如何编辑参数"的第二套 UI，先把无参跑通 |
| 命令的快捷键（每条命令各自绑一个） | 与 T-03 的全局表冲突（`ShortcutRegistry` 先到先得），且菜单栏已在用同一张表。等"命令自带快捷键"真的被需要再说 |
| 命令分组 / 图标 / 最近使用 | 列表是本任务唯一 UI；分组会把"输入即过滤"变复杂。留到有 20+ 条命令时再谈 |
| 用 `prefab::ListView` | 见 T-02 候选 A：结果 ≤ 8 条、每次输入都重建，按钮更直接 |
| 改 `HostApi` 加快捷键字段 | 见 §2：`InkingWindow::Instance()` 已够用；只有它链接失败才升级（那时另开任务，不塞进本文件） |
