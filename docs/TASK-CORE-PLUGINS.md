# 任务书：核心插件库完善 + 上轮验收收尾

> 状态：**待执行**（本文件按任务逐条跟踪）
> 日期：2026-10-23
> 来源：`docs/TASK-CORE-UPING.md` 十任务落地后的实际验收（发现 4 处收尾）+ 核心插件库（`plugins/core/`）的已知半成品；核实结论见 §2
> 目标版本：第一批 `0.2.1`（收尾）/ 第二批 `0.3.0`（核心插件库）/ 第三批 `0.3.1`（数据化）
> 涉及层：`docs/`、`host/`、`include/sdk/`、`plugins/core/`、`CMakeLists.txt`、`.gitignore`

---

## 0. 结论（TL;DR）

上轮验收**实质通过**（`ctest` 11/11、`OFF` 分支可编可跑、无头 `失败项 0`、10 个插件 `abi=9`），但留下了 4 处收尾；核心插件库还有 4 项明确的半成品。共 **11 个任务、3 批**。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 对齐三份文档里的自检数字 | 一 | 验收① | `TASK-CORE-UPING.md`、`HANDOVER.md`、`hardening-task-book.md` | 低 |
| T-02 | 退出码改成失败项数 | 一 | 验收补充（接管 hardening T-02） | `host/main.cpp` 末尾 | 低 |
| T-03 | 让未实现 `Ready` 的插件不出标题行 | 一 | 验收② | `GoldingPlugin.h`、`HostApp.cpp` | 中 |
| T-04 | page 自检改成遍历页面表 | 一 | 验收③ | `plugins/dev/selftest/`、`CMakeLists.txt` | 中 |
| T-05 | `.gitignore` 支持任意 build 目录 | 一 | 验收④ | `.gitignore` | 低 |
| T-06 | 把 knob 做成可见可拖的旋钮 | 二 | 核心插件库 | `plugins/core/widgets/` | 中 |
| T-07 | store 的六张卡片接上点击 | 二 | 核心插件库 | `plugins/core/store/` | 低 |
| T-08 | 页面布局适配 1080p | 二 | 核心插件库（接管 hardening T-06） | 4 个页面插件 + `appdrawer` + `home` 的构件配置串 | 高 |
| T-09 | 左侧应用启动栏（走 Region，不新增机制） | 二 | 核心插件库 | `shell` 声明 `sidebar` 区域、`addapp` 注册条目 | 中 |
| T-10 | 读 `magic.json`（含"插件目录"通道；T-10a / T-10b 均已完成） | 三 | 核心插件库 | `GoldingPlugin.h` + `magic` 插件 | 高 |
| T-11 | 给 `widgets{}` 定生死（已完成，选 B：删字段） | 三 | 核心插件库 | `Manifest` 或装配器 | 低 |

设计取向两条不变量：

1. **收尾批先做**。T-01~T-05 是护栏与文档，后面六项改插件时全靠它们判断"改坏了没有"。
2. **核心插件库的东西一律落在插件里**。T-09 明确用 Region 而不是新服务——如果这里能靠 Region 做成，说明区域机制不只对 `menubar` 有效；做不成才说明机制缺东西，那时再加不迟。

---

## 1. 维护规则（简版，完整版见 `docs/TASK-CORE-UPING.md` §1）

- 每个任务写三段：**准备怎么改**（含放弃的候选）、**大致改动逻辑**（含"哪一步不能反"）、**与改动前对比**（至少一行"不变/仍"边界）；另附**落点 / 验收 / 风险与回退**。
- 完成后整段压成一行 `- T-xx 标题-已完成（YYYY-MM-DD，一句可验证的结论）`，删掉五个细目，**并把 §6 进度表同步改**。细目进 git，回看用 `git log --grep=T-xx`。
- DoD 四条：编译通过 / 断言在 `ctest` 或自检里跑绿 / 文档同步 / 对比表每行对得上实测。
- 状态只用四种：`未开始` / `进行中` / `已完成` / `已放弃` / `阻塞（原因）`。

---

## 2. 基线事实（核实结论）

核实方式：本轮实跑（`ctest` 11/11、`GOLDING_WITH_INKING=OFF` 配置+编译+`sdk_host` 1/1、无头运行 `exit=0` 且 `失败项 0`）+ 源码 grep。数字都来自实测输出。

| 条目 | 判定 | 证据 |
| - | - | - |
| 1. `ctest` 实际是 11 个 test，文档写 10/10 | 成立 | 实跑 11/11（10 个模式 + `sdk_host`）；`TASK-CORE-UPING.md:190,193`、`HANDOVER.md:122`、`hardening-task-book.md:227,235` 仍写 10/10 |
| 2. T-05 标题写"其余 6 个"，实际迁了 7 个 | 成立 | `plugins/` 下除 home / shell 外还有 addapp / appdrawer / magic / settings / store / theme / selftest 七个 cpp 都已是新接口 |
| 3. 退出码仍是两档 | 成立 | `host/main.cpp:283` `return g_failures == 0 ? 0 : 2;`（hardening T-02 未做） |
| 4. 装配后自检 10 行标题，5 行真无断言 | 成立 | 无头输出实测：10 行，`addapp` / `widgets` / `magic` / `settings` / `store` 后面无 `[ok]`；`appdrawer` / `theme` 在默认模式下也无断言（它们只在 `--overlay` / `--theme=` 有） |
| 5. `Plugin::Ready` 默认返回 0，而 descriptor 的 `ready` 无条件填 | 成立 | `GoldingPlugin.h:427`（`built.ready = &ReadyThunk<T>`）、`GoldingPlugin.h:378-382`（默认 `return 0`）；虚函数无法探测"是否重写"，所以只能靠返回值约定 |
| 6. page 自检硬编码索引 0..3 | 成立 | `host/CMakeLists.txt` 末尾四个 `golding_add_selftest(pageN --page=N)`；页面数一变就索引越界（T-07 注记已观察到 `selftest.page3` 因此变红） |
| 7. `.gitignore` 只忽略 `build/` | 成立 | 全文只有一行 `build/`；本轮验证 `OFF` 分支建出 `build-noking/` 直接变成未跟踪目录 |
| 8. knob 可见但不可交互 | 成立 | `WidgetsPlugin.cpp:149-184`（圆 + `KnobState` 侧表；`kKeyVisible=0` 时才隐藏）、`:226` `bind` 对 knob 返回 `-1`；无刻度、无拖动 |
| 9. store 六张卡片无点击 | 成立 | `grep SetOnClicked plugins/core/store/src/StorePlugin.cpp` = **0** |
| 10. 页面仍是 1224×680 绝对坐标 | 成立 | 内容区实测 1864×1040（`[ok] 内容区铺满剩余宽度 x=56 y=0 w=1864 h=1040`），而 hardening §5 记录 settings 行面板固定 900、appdrawer 5×224 |
| 11. 1080p 改造的新耦合点：home 的按钮坐标在 widgets 配置串里 | 成立 | `HomePlugin.cpp:90` `widgets->create(nullptr, root.get(), "button", kLaunchConfig)`，几何写在 `kLaunchConfig` 的 JSON 里 |
| 12. 左侧启动栏不存在，但 Region 机制已够用 | 成立 | `plugins/core/shell` 只声明 `menubar`；Region Item 已支持"只有动作"（`GoldingRegionItemDecl::onActivate`），`home` 就是这种用法 |
| 13. `magic.json` 一行读取代码都没有，且**插件拿不到自己的目录** | 成立 | 全仓库 `magic.json`/`magicSchemaVersion` 命中 0；`GoldingHostApi` 没有任何"插件目录"通道，`PathFinder` 只在宿主内部使用 |
| 14. `manifest.widgets` 读了但无消费者 | 成立 | `ManifestLoader.cpp:183` 是唯一写入点；`grep WidgetDecl` 无其它命中 |

---

## 3. 第一批：收尾（`0.2.1`）

### T-01 对齐三份文档里的自检数字

- **状态**：未开始
- **落点**：`docs/TASK-CORE-UPING.md` §7.1（两处）、`docs/HANDOVER.md` §2/§3、`docs/hardening-task-book.md` §6.1（两处）、`TASK-CORE-UPING.md` 的 T-05 标题与 §0 表格

**准备怎么改**

"10/10"这个说法已经漂移成三种含义：`ctest` 的 test 数（11）、自检模式数（10）、以及"全绿"这件事。只把数字改成 11 会让读者以为有 11 个模式。两个候选：

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 全改成 11/11 | 三份文档统一替换 | 读者会去数"第 11 个模式是什么" | 不做 |
| B 明确写"10 个模式 + 1 个单测 = 11 个 test，全绿" | 一句话的事 | 略长，但含义精确 | **推荐** |

另外 T-05 标题的"其余 6 个"改成"其余 7 个"（6 个 core 插件 + `selftest`）。§0 表格同改。

**大致改动逻辑**

1. 先 `grep -rn "10/10" docs/` 列出全部位置，再逐处改——凭记忆改必漏。
2. 每个位置按上下文选措辞：门槛表写"11 个 test 全绿"，叙述句写"10 个模式 + `sdk_host`"。
3. T-05 标题与 §0 表格的"6 个"改"7 个"，并在压缩行旁注明"6 个 core + selftest"。
4. **不能反的一步**：先 grep 收口，再改；改完再 grep 一次确认零命中。这条任务本身没有技术风险，唯一的失败模式是"漏一处，反而新增一个矛盾"。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 文档里的 test 数 | 10/10（与实际 11 不符） | 11（10 个模式 + 1 个单测） |
| 代码与 ctest 定义 | 11 个 test | **不变**（纯文档任务） |
| T-05 的迁移范围 | "其余 6 个" | 7 个（6 core + selftest） |
| 其它文档结论 | — | **不变**（只改数字与标题，不动结论） |

**验收**：`grep -rn "10/10" docs/` 零命中；`grep -rn "其余 6 个" docs/` 零命中；`ctest --test-dir build` 实跑仍是 11/11（证明文档对齐的是真的）。

**风险与回退**：风险 = 把"10 个模式"也一起替换成 11，导致模式清单与 `CMakeLists.txt` 对不上；回退 = `git checkout docs/`（纯文档，零代码影响）。

---

### T-02 退出码改成失败项数

- **状态**：未开始
- **落点**：`host/main.cpp:283`（`main` 末尾的 return）

**准备怎么改**

沿用 `hardening-task-book.md` §3 已定稿的方案 A：`return g_failures > 125 ? 125 : g_failures;`。125 是 cmd / shell 的退出码上限口径（与技能自带的校验脚本一致）。放弃的候选：保持 0/2 两档（CI 只能知道"红了"，不知道红几条）、写 `selftest_report.json`（多一份产物格式要维护）。

现在这个任务的价值比当初更高：`ctest` 已经接上了，`ctest` 的失败信息里能直接看到每个 test 的退出码——失败项数因此从"日志里数"变成"一眼看见"。

**大致改动逻辑**

1. 把 `return g_failures == 0 ? 0 : 2;` 改成按失败项数返回并夹到 125。
2. 同处补一句注释说明为什么是 125 而不是 255。
3. **不能反的一步**：`g_failures` 的累加必须已经包含插件 `ready` 的返回值（`g_failures += app.notifyReady()` 在它之前一行）——否则插件里的失败不进退出码，`ctest` 会绿灯放过。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 全绿 | 0 | 0（**不变**） |
| 红 1 条 | 2 | 1 |
| 红 7 条 | 2 | 7 |
| 红 300 条 | 2 | 125（夹住） |
| `run.bat` 的 `if errorlevel 1` | 生效 | 仍生效（**不变**，非零即失败） |

**验收**：临时把 `ShellPlugin` 里"顶栏高 40"断言的 `40` 改成 `41`，`ctest` 报 1 个 test 失败且该 test 退出码为 `1`；改回恢复 11/11。

**风险与回退**：风险 = 有外部脚本把"退出码 2"当判据（当前仓库内没有）；回退 = 单行改回 `? 0 : 2`。

---

### T-03 让未实现 `Ready` 的插件不出标题行

- **状态**：未开始
- **落点**：`include/sdk/GoldingPlugin.h`（`Plugin::Ready` 默认实现 + 新增 `BeginSelfCheck`）、`host/HostApp.cpp:342-364`（`notifyReady`）、5 个插件的 `Ready` 首行

**准备怎么改**

问题不是"标题难看"，而是**噪音养出"这行不重要"的阅读习惯**，正好盖住真在断言的那个插件。实测 10 行标题里 5 行后面什么都没有。

难点：虚函数**无法探测是否被重写**（C++ 没有这个能力），所以只能靠返回值约定。三个候选：

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A `PluginManifest` 加 `bool hasSelfCheck` | 字段清晰 | 要动 `GoldingPluginManifest` 结构体 → 再升一次 ABI（v10），为一个日志格式升 ABI 不值 | 不做 |
| B 宿主先调 `ready`，`-1` 就不打标题 | 零 ABI | 标题在断言**之后**才打，输出顺序倒过来（先一堆 `[ok]`，再一个标题） | 不做 |
| C `Ready` 默认返回 `-1`（= 未实现），并把**打标题**下放到 SDK 的 `BeginSelfCheck()`，插件在自己 `Ready` 的第一行调 | 零 ABI；标题顺序天然正确；未实现者完全静默 | 5 个已实现的插件各加一行 | **推荐** |

约定三条：`-1` = 未实现（宿主静默跳过、不计失败）；`>= 0` = 失败项数；`BeginSelfCheck(host)` 打印的字符串与现在**逐字相同**（`"\n[host] 装配后自检：<name>\n"`），保证既有输出可对比。

`appdrawer` 与 `theme` 还要多做一步：在**不属于自己的模式**下返回 `-1`（它们只在 `--overlay` / `--theme=` 有断言）。"谁认谁跑"这条约定本来就该这样落地。

**大致改动逻辑**

1. `Plugin::Ready` 默认 `return -1;`。
2. 新增 `inline int BeginSelfCheck(const Host& host)`：`printf` 那行标题，返回 0 便于 `return BeginSelfCheck(host), checker.failures();` 这种写法（或让插件自己分两行调用）。
3. `HostApp::notifyReady`：`const int failed = ...; if (failed < 0) { continue; }`——**不打标题、不计失败**；否则照旧汇总。
4. 5 个插件：`selftest` / `shell` / `home` 的无条件自检首行调 `BeginSelfCheck`；`appdrawer` / `theme` 在模式不符时提前 `return -1`。
5. **不能反的一步**：宿主侧 `-1` 判断必须**只看返回值、不看有没有 ready 指针**——`descriptor->ready` 永远非空（宏无条件填），拿它判断等于没判断。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 默认模式的标题行 | 10 行（5 行后面无断言） | **3 行**（selftest / shell / home），每行后都有断言 |
| `--overlay` 的标题行 | 10 行 | 4 行（多 `appdrawer`） |
| `--theme=golding-light` 的标题行 | 10 行 | 4 行（多 `theme`） |
| 断言的条数与结论 | 25 条 `[ok]`、0 条 `[FAIL]` | **不变**（只改标题的打印时机） |
| ABI | v9 | **不变**（只改返回值约定与一个 inline 函数） |

**验收**：无头默认模式输出里 `装配后自检：` 恰好 3 行，且每行下面紧接 `[ok]`/`[FAIL]`；`--overlay` 4 行、`--theme=golding-light` 4 行；`ctest` 仍 11/11；`失败项：0` 不变。

**风险与回退**：风险 = 某个插件的 `Ready` 原本依赖"宿主会打标题"来分隔日志，改成自己打之后格式错位（验收里逐字对比标题行即可发现）；回退 = 默认实现改回 `return 0` 并删掉 `BeginSelfCheck` 调用（宿主侧 `-1` 判断留着无害）。

---

### T-04 page 自检改成遍历页面表

- **状态**：未开始
- **落点**：`plugins/dev/selftest/src/SelfTestPlugin.cpp`（新增 `--pages=all` 模式）、`host/CMakeLists.txt` 末尾（四个 pageN test 改成一个）

**准备怎么改**

现状是 `--page=0..3` 四个 test，索引写死在 CMake 里。页面数一变就两个方向都错：页面变少 → 索引越界变红（T-07 注记已观察到）；页面变多 → 新页面永远不被测。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 加 `--page=last` | 只测最后一页 | 中间页仍漏 | 不做 |
| B CMake 里按页面数生成 | configure 期拿不到运行时页面数 | 做不到 | 不做 |
| C 越界当"合法空页" | 改一行判定 | 页面变少时 test 变绿，护栏失效 | 不做 |
| D 新增 `--pages=all`，`selftest` 遍历 `pageCount` 逐页挂载 | 一次覆盖全部页面，与页面数无关；加页面自动被测 | 少 3 个 test（11→8） | **推荐** |

**大致改动逻辑**

1. `selftest` 认 `--pages=all`：读 `bootMode`，`for (i = 0; i < host.PageCount(); ++i)` 逐页 `MountPage`，每页一条断言（"页面可装配 <title>"）。
2. 保持 `--page=N` 的语义不变（它是页面插件自己的调试模式，不在 ctest 里用）。
3. `host/CMakeLists.txt`：删 `page0..page3` 四个 `golding_add_selftest`，加 `golding_add_selftest(pages-all --pages=all)`。
4. **不能反的一步**：先让 `--pages=all` 在现有 4 页下跑通、且输出 4 条"页面可装配"，再删旧 test。反过来就失去了对比基线。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| ctest 里的页面覆盖 | 4 个 test 各测一页（索引写死） | 1 个 test 测全部页（数量无关） |
| test 总数 | 11 | 8（10 模式 −4 +1 = 8，含 `sdk_host`） |
| 加一个页面插件 | 要手动加一个 `--page=4` | **不变**（自动被 `--pages=all` 覆盖） |
| 页面被摘掉 | 索引越界，红在无关的 test 上 | **不变**（仍绿；遍历到几页就断言几页） |
| 逐页几何断言 | 各插件自己的 `Ready` 里 | **不变**（本任务只改"装得上"这一层） |

**验收**：`ctest` 8/8 全绿；`--pages=all` 输出恰好 4 条"页面可装配"（home/store/settings/magic）；临时把 `store` 的 `plugin.json` 的 dll 改成不存在的名字，`--pages=all` 输出 3 条且**不越界、不变红**。

**风险与回退**：风险 = `MountPage` 有副作用（挂载后再摘），逐页挂载可能留下残留节点——断言里要带"装过又摘干净"（`selftest` 已有这条）；回退 = 恢复四个 `pageN`（纯新增/删除 test 定义）。

---

### T-05 `.gitignore` 支持任意 build 目录

- **状态**：未开始
- **落点**：`.gitignore` 第一行

**准备怎么改**

`build/` 一条不够用：`hardening-task-book.md` 的门槛要求验证 `GOLDING_WITH_INKING=OFF`，那必须另建目录；本轮我就建出 `build-noking/` 并立刻变成未跟踪目录（已删）。两个候选：

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A `build*/` | 一行改完 | 会连带忽略任何以 `build` 开头的目录（本项目没有这种源码目录） | **推荐** |
| B 逐个列举 `build/`、`build-noking/`、`build-debug/` | 精确 | 下次又忘一个 | 不做 |

**大致改动逻辑**

1. `build/` → `build*/`。
2. 顺手检查有没有别的构建产物约定（当前只有 `build/`）。
3. **不能反的一步**：确认仓库里没有以 `build` 开头的**源码**目录（`git ls-files | grep "^build"` 为空），否则会把源码一起忽略。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| `build/` | 忽略 | **不变** |
| `build-noking/` | 未跟踪，污染 `git status` | 忽略 |
| 源码目录 | 无影响 | **不变**（不存在以 build 开头的源码目录） |

**验收**：建一个空的 `build-x/` 目录，`git status --short` 不出现它；`git ls-files | Select-String "^build"` 为空。

**风险与回退**：风险 = 误忽略未来的 `builder/` 之类目录；回退 = 改回 `build/` 一行。

---

## 4. 第二批：核心插件库（`0.3.0`）

### T-06 把 knob 做成可见可拖的旋钮

- **状态**：未开始
- **落点**：`plugins/core/widgets/src/WidgetsPlugin.cpp`（`createKnob` / `widgetsBind` / `widgetsSet`）、`plugins/core/widgets/include/widgets_schema.h`（新增键）

**准备怎么改**

现状准确描述（实测）：knob 画出来是**一个圆**（圆角矩形取半径一半），值记在侧表 `Knobs()` 里，`bind("click")` 对它返回 `-1`，**没有刻度、没有拖动、没有视觉反馈**。也就是说它现在是个"记住数字的圆"。

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 继承 `InkingWidget` 自绘一个组件类 | 旋转、拖拽、命中全套自己做 | 要跟 InkingUIKit 的构造与命中约定对齐，成本最高 | 不做（现在不值得） |
| B 圆 + 指示线（旋转角度反映取值），拖动走指针事件 | 用现有组件 + 侧表存值 + 拖动时改角度 | 中；但 `bind` 的接口是"点击"语义，要扩事件名 | 退路 |
| C 圆 + 指示线 + **点击循环步进**（每点一次 +1 档），`bind("change")` 回调 | 不改事件模型（复用点击），视觉上用指示线表达取值 | 低；"不是真拖动"是明确的妥协 | **推荐**（先做） |
| D 删掉 knob | 省事 | schema 里已有 `kKindKnob`，删了以后再加要改接口 | 不做 |

选 C 的理由：先把"看得见、能改、能通知"三件事做实，拖动留给真有人用的时候（`Options.md` 的判据：问过两次才值得加）。

**大致改动逻辑**

1. `widgets_schema.h` 加 `kKeySteps`（档数，默认 10）、`kKeyIndicator`（指示线宽度）。
2. `createKnob`：在圆里加一个细长的 `InkingStaticPanel` 当指示线（宽 2、高 `size/2`、锚在中心、按 `value/min/max` 算旋转角）；把指示线指针存进 `KnobState`。
3. `widgetsSet` 改值后同步重算指示线角度（这是"值变了看得见"的唯一证据）。
4. `widgetsBind`：knob 支持 `"change"`——点击时按 `kKeySteps` 步进并回调；`"click"` 仍返回 `-1`（保持"这个构件没有点击概念"的语义）。
5. **不能反的一步**：`KnobState` 的侧表键是 `const void*`（组件指针）。指示线是子节点，**不能**拿它当键，否则 `set` 找不到 knob。所有查找仍以 knob 本体指针为键。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| knob 的外观 | 一个纯色圆 | 圆 + 指示线（角度 = 取值） |
| 改变取值 | 只能通过服务 `set()`，无交互 | 点击步进，且 `set()` 与点击都让指示线动 |
| `bind("change")` | 不支持（返回 -1） | 支持，回调带新值 |
| `bind("click")` 对 knob | 返回 -1 | **不变**（仍返回 -1，语义没被撑开） |
| button 构件 | 完整实现 | **不变** |

**验收**：home 或 selftest 里建一个 knob（`{"steps":10,"value":0.5}`），点击 3 次后 `get("value")` 等于 0.8；指示线角度随取值变化（断言角度差 > 0）；`ctest` 仍全绿。

**风险与回退**：风险 = 指示线作为子节点会进命中表，点击命中到指示线而不是 knob（`QueryHit` 返回的 target 不是 knob 本体，步进逻辑拿不到）——缓解：`bind` 时按父链向上找到 `widgets.knob` 名字的节点；回退 = 删掉指示线与 `"change"` 分支，回到"记住数字的圆"。

---

### T-07 store 的六张卡片接上点击

- **状态**：未开始
- **落点**：`plugins/core/store/src/StorePlugin.cpp`

**准备怎么改**

`SetOnClicked` 计数为 **0**，六张卡片纯展示。三个候选：

| 方案 | 做后能看到什么 | 代价 | 结论 |
| - | - | - | - |
| A 点击弹覆盖层显示详情 | 跨插件协作（store 用 `golding.overlay`） | 要写一份详情内容 + 依赖 appdrawer 之外的声明 | 推荐（第二步） |
| B 点击只打日志 + 切换选中态 | 最小闭环，可断言 | 用户看不到"有反应"之外的收益 | **推荐（先做）** |
| C 点击新开一个详情页 | 要注册新页面 | 页面表变化会牵动 T-04 的遍历数 | 不做 |

先 B 后 A：B 先把"点击通路"接上并留下断言，A 再换成真内容。

**大致改动逻辑**

1. 六张卡片循环里，`card->SetOnClicked([...] { INK_LOG_INFO("Store", "卡片 clicked: <name>"); })`。
2. 同时切换该卡片的描边/底色（选中态），让"点了"在画面上也成立。
3. 在 `Ready` 里加一条断言：点击后日志出现、且选中态生效（用一个可读的开关量记"最近点击的卡片名"）。
4. **不能反的一步**：点击回调里**不要**直接改组件外观又同时触发重排（会与 `fillMain` 粘旧尺寸冲突）。只改颜色/描边，不改尺寸。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 点卡片 | 无任何反应 | 日志一行 + 卡片高亮 |
| `SetOnClicked` 数量 | 0 | 6 |
| 页面布局 | 固定网格 | **不变**（只改颜色，不动尺寸） |
| 卡片数量 | 6 | **不变** |

**验收**：`--probe` 模式在卡片位置返回该卡片名；点击后 `Ready` 的断言（"最近点击 = 卡片名"）为真；`ctest` 全绿。

**风险与回退**：风险 = 卡片是动态容器里的静态按钮，命中依赖"不声明 `OwnsSubtreeHit`"（HANDOVER §5 硬约束）；回退 = 删掉 `SetOnClicked` 六行（回到纯展示）。

---

### T-08 页面布局适配 1080p

- **状态**：未开始
- **落点**：`plugins/core/home/src/HomePlugin.cpp`（含 `kLaunchConfig`）、`store/`、`settings/`、`magic/`、`appdrawer/src/AppDrawerPlugin.cpp`

**准备怎么改**

接管 `hardening-task-book.md` 的 T-06（方案与候选表已在其 §5 定稿：页面根改布局容器，不按比例放大、不做场景级缩放）。本任务只补**两条上一轮新增的约束**：

1. **home 的按钮几何现在在 widgets 配置串里**（`kLaunchConfig` 的 JSON），不在 C++ 常量里——改布局要连它一起改，否则按钮会留在旧坐标上。
2. **widgets 只接受配置串里的绝对数值**（`offsetX` / `offsetY` / 尺寸）。布局容器化之后，按钮如果还需要"相对排布"，要么由调用方算出数值再传、要么给 widgets 的 button 加 `anchor` 键。**推荐前者**：widgets 不应该知道布局。

顺序照 hardening §5：一页一页来，每页改完立刻 `ctest`。

**大致改动逻辑**

1. `home`：页面根改纵向 `Stack`，标题/副标题一组、按钮一组、`art` 用 `selfAlign = End` 贴右；`kLaunchConfig` 里的绝对坐标改成布局算出来的位置（或删掉 offset，交给容器）。
2. `settings`：行面板吃满内容区宽（1864），表头与值贴两端。
3. `store`：网格列数按可用宽度算，末列右边缘贴内容区。
4. `appdrawer`：网格同样按宽度算列数。
5. `magic`：画布尺寸不变（900×290 是它自己的设计尺寸），只把它在页面里居中。
6. 每页在 `Ready` 里加一条断言：**最右侧可见子节点右边缘 ≥ 内容区右边缘 − 64**。
7. **不能反的一步**：一页一页来。页面根一旦被拉伸，里面按绝对坐标摆的子节点不会跟着动——5 页一起改，红了分不清是布局错还是断言错。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| settings 行面板 | 固定 900 宽贴左（右侧空 ~964px） | 吃满 1864，表头与值贴两端 |
| home 的按钮 | `kLaunchConfig` 里 72,268 钉死 | 由布局决定 |
| home 的 `art` | x=732 钉死 | 贴右（`selfAlign = End`） |
| appdrawer / store 网格 | 固定 5 列 ×224（右侧空 ~744px） | 列数按宽度算 |
| magic 画布 | 900×290 贴左 | 尺寸**不变**，居中 |
| 1280×720 下的观感 | 现在的样子 | **不变**（相对布局在两档分辨率下结果一致） |

**验收**：`--page=0..3`（或 T-04 之后的 `--pages=all`）里每页有"最右元素右边缘 ≥ 内容区右边缘 − 64"的断言且全绿；四张截图右侧留白 ≤ 64px。

**风险与回退**：风险 = 页面根容器化会改掉"页面根尺寸 = create 时刻父级尺寸"这条隐含假设，而 4 个页面都在用它——**改一半就会散**；回退 = 每页独立（各自一个 `PanelData` 分支），哪页没改好就退回绝对坐标，不影响别页。

---

### T-09 左侧应用启动栏（走 Region，不新增机制）

- **状态**：未开始
- **落点**：`plugins/core/shell/src/ShellPlugin.cpp`（声明 `sidebar` 区域 + 渲染）、`plugins/core/addapp/src/AddAppPlugin.cpp`（注册条目）

**准备怎么改**

`HANDOVER.md` §4 #4 记的是"要动就得给 Shell 加'抽屉条目来源'的服务"。本任务换一个更省的落点：**用 Region**。

理由：`sidebar` 就是"一个可以往里放东西的位置"，而 Region 机制的契约正是这个——`GoldingRegionItemDecl` 已经支持"只有动作、不画界面"（`home` 往 `menubar` 放"首页"就是这种）。所以：

| 方案 | 做法 | 代价 | 结论 |
| - | - | - | - |
| A 新增 `golding.drawer` 服务（addItem / removeItem） | 灵活，可动态增删 | 多一个服务契约要维护；且它做的事与 Region 高度重叠 | 不做（除非需要动态增删） |
| B shell 声明 `sidebar` 区域 + 渲染条目，addapp 注册 | 零新机制；顺带验证"区域机制不只对 menubar 有效" | 条目是静态的（entry 期注册，不能运行期增删） | **推荐** |
| C 在 shell 里硬编码应用列表 | 最快 | 违反"core / 面板不认识业务" | 不做 |

选 B 还有一层价值：如果 `sidebar` 能用 Region 做成，就证明这套机制是通用的；做不成（比如需要动态增删），才说明缺的是"可变的区域"，那时再加服务。**这个判断要写进任务结论**。

**大致改动逻辑**

1. `shell`：`Register` 里 `host.AddRegion(kSidebarRegion{"sidebar", "侧栏", 20, allowDuplicateIds=false})`；`createShell` 里按 `regionItemCount("sidebar")` 渲染成侧栏按钮列（左侧启动栏），位置在 `shell.side` 与 `shell.content` 之间。
2. `addapp`：`Register` 里遍历自己的应用清单，逐个 `AddRegionItem({"sidebar", <app name>, <title>, nullptr, order, nullptr, &launchApp})`；`launchApp` 需要用 `Host`（文件级成员，与 shell / magic 同样的做法）。
3. `addapp` 的 `Manifest().needs` 加 `golding.apps`？不需要——它就是提供方。它要 `depends = {"shell"}`（已有）。
4. **不能反的一步**：`sidebar` 必须在 **shell 的 `Register` 里先声明**，addapp 才能往里放——靠 `depends` 拓扑保证，所以 addapp 的 `depends` 里必须有 `shell`（现状已有）。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 左侧启动栏 | 不存在（`shell.side` 只有 56 宽的空条） | 按 `sidebar` 条目渲染的按钮列 |
| 新增一个应用入口 | 要改 shell 代码 | 装一个插件 / 加一个清单项即可 |
| `menubar` 的行为 | home 的"首页"入口 | **不变** |
| Region 表 | 1 个区域（`menubar`） | 2 个（`menubar` + `sidebar`） |
| core 代码 | — | **不变**（不加任何特化） |

**验收**：启动输出出现 `[host] 声明区域 sidebar（来自 shell）` 与 `[host] 区域项 sidebar/<app>（来自 addapp）`；侧栏按钮出现在 `shell.side` 右侧且点击后走启动逻辑（日志可见）；`ctest` 全绿（含 T-03 之后"标题行数"不变）。

**风险与回退**：风险 = 侧栏条目多时撑破 `shell.body` 高度（`sidebar` 没有滚动能力）；缓解 = 先限定最多 N 项，超出打警告；回退 = shell 不渲染 `sidebar`（区域留着，条目被忽略，符合"往不存在的位置注册会被驳回但不影响其余"的既有规则）。

---

## 5. 第三批：数据化（`0.3.1`）

### T-10 读 `magic.json`（含"插件目录"通道）-已拆分为 T-10a / T-10b

- T-10a 插件目录通道（ABI v10）-已完成（2026-10-23，`abi=10` 覆盖 10 个插件；`core_tests` 26 项 / 自检新增 12 项，全绿）

**拆分的理由**：方案 A 原本就要求"先做通道、再做读取"（见下），而通道是 ABI 事件、读取是解析逻辑，混在一轮里红了分不清责任。所以通道先落，`magic.json` 留成 T-10b。

**落地与计划的偏差（两处，都记下来免得下次重推）**：

1. 签名不是 `pluginDir(hostContext)` 而是 `pluginDir(hostContext, pluginName)`。原因：`hostContext` 现在是 `&entry.provider`（一个字符串指针），拿它反查目录要额外维护一张"指针 → 目录"的旁表；插件名本来就是档案表的唯一键，按名字查更短，也顺带把 `Options.md` §5 那条 `hostContext` 欠账留到真需要时再动。
2. 目录字符串存在 `PluginLibrary`（core 的档案表）而不是 `LoadedEntry`：`hostPluginDir` 是 `fillHostApi` 的静态回调，拿不到 `HostApp` 实例，而其它回调本来就都读档案表。存放在 `deque<std::string>` 之外另存 `std::string`，`c_str()` 生命周期到 `reset()` 为止。

另有一处不在这条任务里、但同批落地的：`PathFinder::resolve` + `toUtf8/fromUtf8`。`plugin.json` 的 `targets.win64.lib` 与 `widgets{}.path` 现在**读完就是绝对路径**（绝对路径或 `..` 逃逸判 `Invalid`），所以消费者拿到的路径不需要再拼基准——这也是"插件不必自己拼路径"的依据。

---

### T-10b 读 `magic.json`-已完成（2026-10-23）

- 一条可验证的结论：`magic/plugin.json` 旁边放 `magic.json`（`magicSchemaVersion` / `ringRadius` / `starPoints` / `ringColor` / `coreColor`）即生效——半径 130 实测 `[magic] magic.json 已生效：半径=130`，`Ready` 断言"内环直径 = 2×半径"报 `实际=260 期望=260`；删文件退回默认 95 并打一行；`magicSchemaVersion=99` 与 `ringRadius=300` 各自只打一行 WARN 后忽略整份文件。
- 落地与计划的偏差（三处）：
  1. **默认值是 95 不是 120**（原文写错；`kRingRadius` 一直是 95）。
  2. **半径上限 135 而不是 200**：`maxRingRadius()` = (画布高 290 − 20) / 2，只此一处，配置校验与自检共用——否则会出现"配置文件允许 200、自检只认 135"两套基准。文档记的是上限的**由来**，不是常量。
  3. **`Ready` 断言的是"建的时候从节点上读到的直径"，不是节点指针**：设置区块会随页面切换销毁重建，缓存节点指针后 `Ready` 里读它随机 SIGSEGV（实测抓到的栈是 `runReady → magic.dll`）；改存 `int g_innerDiameter` 后连跑 8 轮 `ctest` 12/12。
- 与改动前对比：`magic.json` 从"一行读取代码都没有"变成"插件自己读、自己解释"；`abiVersion` 仍 10；没有该文件时外观与之前**完全一致**（默认值就是原来写死的常量）。
- 风险仍在：`magic.json` 的键名从此是本插件的**公开契约**，改名等于让旧文件静默退回默认（会打一行 WARN，不会崩）。

---

### T-11 给 `widgets{}` 定生死-已完成（2026-10-23，选 B：删字段）

- 一条可验证的结论：`grep -rn "WidgetDecl\|manifest.widgets" src include host tests plugins` 零命中；临时给 `theme/plugin.json` 加 `"widgets": {...}` 后启动打 `[host]   ↳ theme：未知字段 widgets`、`theme` 判 `state=1`，其余 9 个插件照常（`可装配 9 / 加载阶段否掉 1`）。
- **顺带补上的一条**：`plugin.json` 顶层键现在**按白名单校验**（`name/title/version/apiVersion/role/requires/targets`），未知键判 `Invalid` 并把键名写进 `ManifestResult::detail`（新增字段），宿主扫描日志多一行 `↳ <目录名>：<原因>`。没有这句原因，"未知键驳回"的表现就只是 `status=1`，作者得逐个字段猜——那和不校验没差多少。
- 与改动前对比：`widgets{}` 从"读进 `Manifest` 无人使用"变成"不认识且明确驳回"；现有 10 个插件的 json **不变**；manifest 必填项 **不变**；纯数据插件仍**不支持**（`Options.md` §9）。
- 回退：恢复字段 + 白名单里加回 `widgets`。代价是回到"悬空字段"，不建议。

---

## 6. 批次门槛与进度

### 6.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| 一（收尾） | `ctest` 全绿且**文档数字与实际一致**（`grep -rn "10/10" docs/` 零命中）；默认模式"装配后自检"标题 3 行且每行下有断言；`--pages=all` 一次覆盖全部页面；`git status` 无构建目录残留 |
| 二（核心插件库） | knob 点击能改值且指示线随动；store 六张卡片有 `SetOnClicked`；每页"最右元素右边缘 ≥ 内容区右边缘 − 64"断言全绿；`sidebar` 出现在区域表里且 core 代码零改动 |
| 三（数据化） | `abiVersion == 10`；`magic.json` 存在时生效、缺失时退回默认；`widgets{}` 已删或已落地（二选一，不留悬空）——**三条均已满足**（2026-10-23） |

### 6.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 对齐三份文档里的自检数字 | 一 | 未开始 |
| T-02 | 退出码改成失败项数 | 一 | 未开始 |
| T-03 | 让未实现 Ready 的插件不出标题行 | 一 | 未开始 |
| T-04 | page 自检改成遍历页面表 | 一 | 未开始 |
| T-05 | .gitignore 支持任意 build 目录 | 一 | 未开始 |
| T-06 | 把 knob 做成可见可拖的旋钮 | 二 | 未开始 |
| T-07 | store 的六张卡片接上点击 | 二 | 未开始 |
| T-08 | 页面布局适配 1080p | 二 | 未开始 |
| T-09 | 左侧应用启动栏（走 Region） | 二 | 未开始 |
| T-10 | 读 magic.json（含插件目录通道） | 三 | 已拆分为 T-10a / T-10b |
| T-10a | 插件目录通道（ABI v10） | 三 | 已完成 |
| T-10b | 读 magic.json | 三 | 已完成 |
| T-11 | 给 widgets{} 定生死 | 三 | 已完成（选 B：删字段） |

完成任务后，本节状态改 `已完成`，§3~§5 对应段落按 §1 压缩成一行——两处必须同时改。

---

## 7. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 为 `Ready` 加 `hasSelfCheck` 字段 | 要为一行日志升 ABI；用返回值 `-1` 约定即可（T-03） |
| 为左侧启动栏加 `golding.drawer` 服务 | 先用 Region 试（T-09）；真需要运行期增删再加 |
| 让 widgets 知道布局（`anchor` 键） | 布局归页面插件；widgets 只接受算好的数值（T-08） |
| 给 knob 做真拖动 | 先做"点击步进 + 指示线"，问过两次再上拖动（`Options.md` 判据） |
| 让 core 读 `magic.json` | core 一旦认识 magic 就多一张特化表；配置由插件自己解释（T-10） |
| 保留 `widgets{}` 但继续不实现 | 挂着会让人以为它该工作（T-11 选 B） |
| 自检加"只在开发构建跑"的开关 | 沿用 `hardening-task-book.md` §7 的决定：现在没有发布构建这个概念 |
| `PageRegistry` / `SettingsRegistry` 归并进 Region | 与 T-09 无关（T-09 用的是 Region 新增，不是归并旧表）；归并仍等 `dev001` |
