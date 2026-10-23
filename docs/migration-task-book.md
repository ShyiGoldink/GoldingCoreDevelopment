# 任务书：删绕路代码——InkingUIKit 0.4.0 迁移（GoldingCore 自身规划）

> 状态：**已完成**（2026-10-22 三批 6 个任务全部落地，完成规则见 §1；细目见 git 历史）
> 日期：2026-10-22
> 来源：`D:/InkingUIKit/docs/zh/plans/2026-10-22-goldingcore-requirements.md` §6（InkingUIKit 0.4.0 落地后给的迁移提示）
> 目标版本：第一批（0.4.0 迁移 · 低风险项）/ 第二批（0.4.0 迁移 · 面板动态化）/ 第三批（SelfTest 插件化）
> 涉及层：`plugins/core/shell/`、`host/`、`include/sdk/`、`plugins/dev/selftest/`（新增）

---

## 0. 结论（TL;DR）

InkingUIKit 0.4.0 落地了 18 条需求的 15 条（A/B/D 组），GoldingCore 侧与之对应的**绕路代码现在可以删了**。共切 6 个任务、分三批：第一批 2 个低风险独立项（顶栏高度动态化、自检去 `dynamic_cast`），第二批 3 个面板动态化重构（这是「删 requestRebuild」的真正落点，高风险单独一批），第三批 1 个 SelfTest 插件化（最大、依赖 D 组能力）。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 顶栏高度改运行时量 | 一 | §6-2 | `plugins/core/shell/src/ShellPlugin.cpp` | 低 |
| T-02 | 自检点击改 `Activate` | 一 | §6-4 | `host/main.cpp` | 低 |
| T-03 | 抽屉改动态面板 + 就地 Resize | 二 | §6-1 | `ShellPlugin.cpp` | 高 |
| T-04 | 覆盖层改就地挂/显隐 | 二 | §6-1 | `ShellPlugin.cpp` | 中 |
| T-05 | 删 requestRebuild 通道 | 二 | §6-1 | `HostApp.*` / SDK | 中 |
| T-06 | SelfTest 做成插件 | 三 | 需求单 §5 | `plugins/dev/selftest/`（新增） | 高 |

设计取向两条（贯穿全部任务）：

1. **行为等价优先**：迁移后自检的几何断言（56/200/1224/1080/680）必须仍然全绿——删绕路代码不能以破坏既有验收为代价。
2. **先低风险后高风险**：第一批两个独立项先落地并验证，再动面板类型；面板动态化是「删 requestRebuild」的前提，不先换容器就谈不上删。

**结果**：6 个任务全部完成，10 模式失败项 0。执行中发现并修正的、任务书没料到的三件事（细节在 §3~§5 的完成行里）：
动态容器**不能**声明 `OwnsSubtreeHit()`、`fillMain` 的"意愿"必须一直是 0（否则第二次排版就锁死在旧宽度）、
`ButtonData::textColor` 没有 token 通道（换肤要靠一次补刷）。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么；**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、哪一步不能反。不贴大段代码 |
| **与改动前对比** | 表格；**至少一行"改动后仍不变"的边界**，否则是过度承诺 |

另附 **落点**、**验收**（可断言）、**风险与回退**（至少各一条）。

### 1.2 完成即压缩（核心规则）

任务完成后，把该任务整段压缩成一行：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并把 §6 进度表的状态改为 `已完成`。**两处必须同改。**

细目不留在本文件里——它在 git 历史中，回看用 `git log --grep=T-01`。

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 <标题>-已完成（日期，结论）` |
| 放弃 | `- T-01 <标题>-已放弃（原因；替代方案）` |
| 拆分 | `- T-01 <标题>-已拆分为 T-01a / T-01b`，各自走完整三段 |
| 阻塞 | `状态：阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

四条全中才算完成，缺一条保持 `未开始` / `进行中`：

1. 编译通过；
2. 验收断言在自检里跑绿（不是临时探针）；
3. 涉及文档同步改；
4. 「与改动前对比」每行都能对上实测。

---

## 2. 基线事实（核实结论）

| 条目 | 判定 | 证据 |
| - | - | - |
| §6-1 删 requestRebuild（抽屉/覆盖层） | **部分成立** | `ShellPlugin.cpp:97/114/214` 三处 `requestRebuild()`。但对照文档说「改 `Resize`」漏了前提：`Resize` 只挂在 `InkingDynamicWidget`（`InkingWidget.h:844`），shell 全用静态面板（`ShellPlugin.cpp:27` 注释明说「宽度变化靠整树重建」），所以**必须先换动态面板，才能就地 Resize** |
| §6-1 换页本就无需重建 | **成立** | `mountPage`（`ShellPlugin.cpp:185`）已是就地 `RemoveChild` + `AddChild`，不请求重建；只有抽屉展开、覆盖层显隐才走重建 |
| §6-1 动态容器子树能命中 | **成立** | `InkingWidget.h:430` `HitTestNode` 默认实现已是「容器语义」，声明 `OwnsSubtreeHit()` 的动态组件不用抄递归；静态按钮在动态容器里能收点击 |
| §6-2 顶栏高度写死 | **成立** | `ShellPlugin.cpp:48` `kTopBarHeight=40`；`MenuBar::GetCurrentHeight()` 已可用（`MenuBar.h:109`，隐藏时给 0） |
| §6-3 宽度魔数 | **成立** | `ShellPlugin.cpp:49-50`（56/200）、`main.cpp:402/433/435`（1224/1080）。依赖 T-03（面板动态化后才谈 Stretch） |
| §6-4 自检 dynamic_cast | **成立** | `main.cpp:360/382/442/460` 四处 `dynamic_cast<InkingStaticButton*>`；`InkingScene::Activate(name)` 已可用（`InkingScene.h:253`） |
| §5 SelfTest 插件化前置 | **成立** | D1/D3/D5/D7 已齐（`PumpOnce`/`PostFrameTask`/`RequestClose(int)`/`Activate`）；缺的是 core 的「装配后钩子」与「两阶段装配暴露界面根」，属 GoldingCore 侧 |

---

## 3. 第一批：顶栏动态化 + 自检去类型（低风险独立项）

- T-01 顶栏高度改运行时量-已完成（2026-10-22，主体位置/高度由 `MenuBar::GetCurrentHeight()` 决定，root 不再用 Stack 纵排；新增 `--fullscreen` 模式正面断言「顶栏整条隐藏时主体 y=0 h=720、内容区跟着 720」，10 模式失败项 0）
- T-02 自检点击改 `Activate`-已完成（2026-10-22，四处「转型成 `InkingStaticButton*` + `TriggerClick`」全换 `scene.Activate(名字)`；`grep -c dynamic_cast host/main.cpp` = 0，10 模式失败项 0）

## 4. 第二批：面板动态化（删 requestRebuild 的真正落点）

- T-03 抽屉改动态面板 + 就地 Resize-已完成（2026-10-22，新 `ShellLayer`（`InkingDynamicWidget` + 自己答 `HitTest` + 每帧现查主题色）承载抽屉 / 内容区；`toggleSidebar` 改 `side->Resize(56↔200)` + `body->Relayout()`，条目始终在树里靠 `SetVisible` 收起。一处与任务书不同：**不能**给动态容器声明 `OwnsSubtreeHit()`——声明后子孙不进命中表，而场景的深挖只走"静态表命中且是容器"那一支，按钮就点不到；0.4.0 的动态容器子孙本来就各自进表。验收：56/200 / 1224/1080 / 页面跟着变窄三个断言全绿，且新增"动态容器里的静态按钮可命中"断言）
- T-04 覆盖层改就地挂/显隐-已完成（2026-10-22，遮罩（z=90）与底部抽屉（z=95）在 `createShell` 里一次性建出、常驻挂在**根**上（内容区已是纵向布局容器，挂进去会被当成布局项）；`overlayPush` 只改尺寸 + `SetVisible(true)`，`closeOverlay` 只 `SetVisible(false)`，内容只在换声明时重建。`--overlay` 模式"应用柜抽屉已弹出 / 遮罩已显示"全绿）
- T-05 删 requestRebuild 通道-已完成（2026-10-22，SDK 删字段、ABI 6→7；HostApp 删 `setRebuildHandler`/注入；main 删 `hostRequestRebuild`/`g_rebuildRequested`/`g_rebuildAction`；theme 删调用。换肤改走主题版本号自动重查 + 一次"按钮文字色"补刷（`ButtonData::textColor` 没有 token 通道，是宿主库那边还没开的 C2），深/浅两套截图整屏一致。全仓 `grep -c requestRebuild` = 0，10 模式失败项 0）

## 5. 第三批：SelfTest 插件化

- T-06 SelfTest 做成插件-已完成（2026-10-22，SDK 加 `ready` 装配后钩子 + `uiRoot` + `bootMode`（ABI v7→v8，`GOLDING_CORE_API_VERSION` 1→2）；新插件 `plugins/dev/selftest/`（源码在 `plugins/dev/`，产物落 `plugin/core/selftest`，CMake 函数加了可选的源码目录参数）只断言**通用契约**：根铺满、每页可装配、每个能画界面的区域条目可创建、装过又摘干净。**与任务书不同的三处**：①"面板几何归 shell"做到底——40/680/56/1224/1080 与 `--expanded` / `--page=N` / `--probe` 场景都在 shell 的 `ready` 里，`--theme=` 归 theme、`--overlay` 归 appdrawer（用各自的节点名，不跨插件点名）；②selftest **不写** `depends("shell")`——它断言的是对任何面板都成立的契约，绑死面板反而会出现"换掉 shell 之后自检根本不加载"；③没有界面时 selftest 跳过并返回 0（删掉面板插件仍是合法状态、退出码 0）。验收：`grep -c shell. host/main.cpp` = 0，10 模式失败项 0，删掉 `plugin/core/shell` 后 8 个档案里 6 个标"装不上"、界面为空、退出码 0）

> 本轮三批 6 个任务**全部完成**。此后自检的归属是：宿主冒烟检查（`host/main.cpp`）→
> `selftest`（通用契约）→ 各插件 `ready`（自己的几何、自己的模式）。
>
> **下一轮**（回归护栏 + 迁移留下的结构账）另立一份：`docs/hardening-task-book.md`。

---

## 6. 批次门槛与进度

### 6.1 门槛

> 自检模式数：原 9 个，第一批加了 `--fullscreen`（T-01 的正面证据）→ 现在 **10 个**。
> 下文写的「9 模式」= 原来那 9 个；「10 模式」= 含 `--fullscreen`。
> 第三批之后模式的**跑法**也搬进了插件：宿主只认 `--stay` / `--fullscreen`，
> 其余原样经 `bootMode` 交给认领它的插件。

| 批次 | 发布前必须满足 |
| - | - |
| 第一批 | 10 模式失败项 0，且 `--expanded` 的 56/200 断言仍绿（T-01 不改变非全屏几何） |
| 第二批 | 10 模式失败项 0，且 `grep -c requestRebuild`（含 SDK/HostApp/main/shell/theme）归零 |
| 第三批 | 10 模式失败项 0，且 `grep -c shell. host/main.cpp` 归零（自检不再硬编码面板节点名） |

### 6.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 顶栏高度改运行时量 | 一 | 已完成 |
| T-02 | 自检点击改 `Activate` | 一 | 已完成 |
| T-03 | 抽屉改动态面板 + 就地 Resize | 二 | 已完成 |
| T-04 | 覆盖层改就地挂/显隐 | 二 | 已完成 |
| T-05 | 删 requestRebuild 通道 | 二 | 已完成 |
| T-06 | SelfTest 做成插件 | 三 | 已完成 |

---

## 7. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 保持静态面板、只把 requestRebuild 换名 | 没删绕路，等于没做（用户已裁定「彻底删」） |
| 把 C 组 3 条（透明背景/按钮对齐/字体）纳入本轮 | InkingUIKit 侧已明确暂缓，前置在 inkgen 白名单与字体资产，不在 GoldingCore 侧 |
| 宽度魔数（1224/1080/200/56）单独一批提前做 | 依赖 T-03（面板动态化后才谈 Stretch），提前做是空中楼阁 |
| 把 PageRegistry/SettingsRegistry 归并进 Region | 归并是「dev001 站稳后」的事，与删绕路代码正交，混进来会扩大回归面 |
