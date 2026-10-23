# 任务书：core 与插件开发包升级（SDK 接口化 + 能力宣言）

> 状态：**已完成**（10 个任务全部落地，2026-10-23；细目见 §3~§6 的压缩行与 `git log --grep=T-0`）
> 日期：2026-10-23
> 来源：用户对插件形态的两项要求（能力宣言 / 插件抽象与开发包）+ 三轮设计评审；核实结论见 §2
> 目标版本：第一批 `dev000-sdk`（零 ABI 变更）/ 第二批 `dev000-sdk`（接口落地）/ 第三批 `dev001`（ABI v9 能力目录）/ 第四批 `dev001`（构件能力）
> 涉及层：`include/sdk/`、`host/`、`src/Application/`、`plugins/`、`CMakeLists.txt`

---

## 0. 结论（TL;DR）

两项要求拆成 **10 个任务、4 批**。核心判断：**"插件抽象"可以做到零 ABI 变更、宿主零改动**（第一批纯新增，删文件即回退）；**"能力宣言"必须动 ABI**，排在第三批，且版本校验按用户决议**暂时恒通过**（记录在 `docs/Options.md`）。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 把 10 个自检模式收成 ctest | 一 | 护栏欠账 | `CMakeLists.txt`、`tools/selftest.bat`（新增） | 中 |
| T-02 | 新增 GoldingPlugin.h（三钩子基类 + Host） | 一 | 要求 2 | `include/sdk/GoldingPlugin.h`（新增）、`tests/`（新增） | 中 |
| T-03 | 新增 GoldingWidgets.h 并把 home 迁成样板 | 二 | 要求 2 | `include/sdk/GoldingWidgets.h`、`plugins/core/home/src/HomePlugin.cpp` | 中 |
| T-04 | 把 shell 迁到 Plugin 接口 | 二 | 要求 2 | `plugins/core/shell/src/ShellPlugin.cpp` | 高 |
| T-05 | 其余 6 个实验插件迁移并删旧宏 | 二 | 要求 2 | 6 个插件 cpp、`GoldingPluginBase.h` | 中 |
| T-06 | descriptor 增 manifest() 与 SDK 标识并升 ABI v9 | 三 | 要求 1 + 回答 2 | `include/sdk/golding_plugin.h`、`host/HostApp.cpp` | 中 |
| T-07 | 建能力目录与 needs 交叉校验 | 三 | 要求 1 | 新增 `Application/CapabilityRegistry.*`、`HostApp.cpp` | 高 |
| T-08 | 校验 provides 与运行时注册一致 | 三 | 要求 1 | `HostApp::registerAll` | 低 |
| T-09 | 新增 widgets 插件与 golding.widgets 契约 | 四 | 要求 1 + 修正 ② | `plugins/core/widgets/`（新增）、`GoldingWidgets.h` | 高 |
| T-10 | home 的按钮改走构件服务 | 四 | 修正 ② | `plugins/core/home/src/HomePlugin.cpp` | 低 |

设计取向两条不变量：

1. **基类只放生命周期三钩子并冻结，扩展点全走 Host（宿主侧末尾追加）**——这样以后加能力不动任何插件的虚表，与 `GoldingHostApi` 已走过的 v5→v8 是同一套做法。
2. **能力由宿主持有目录，插件之间不直连**；每次注册与每次能力查询都留痕——故障只能靠日志排查，日志就是唯一的排查面。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么；**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、**哪一步不能反**。不贴大段代码 |
| **与改动前对比** | 表格；至少一行写"改动后仍不变"的边界；优先给数字 |

另附 **落点**、**验收**（可断言）、**风险与回退**（至少各一条）。

### 1.2 完成即压缩（核心规则）

任务完成后，把该任务整段压缩成一行：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并把 §7 进度表状态改为 `已完成`。**两处必须同改。** 细目不留在本文件，回看用 `git log --grep=T-01`。

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 <标题>-已完成（日期，结论）` |
| 放弃 | `- T-01 <标题>-已放弃（原因；替代方案）`，不删行 |
| 拆分 | `- T-01 <标题>-已拆分为 T-01a / T-01b`，各自走完整三段 |
| 阻塞 | 状态改 `阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

四条全中才算完成：编译通过 / 验收断言在自检或 ctest 里跑绿（不是临时探针）/ 涉及文档同步改 / 「与改动前对比」每行能对上实测。

---

## 2. 基线事实（核实结论）

证据优先给"文件:行号"或实测输出。本轮核实方式：静态阅读 + 一次无头运行（默认模式，`exit=0`，`失败项 0`，9 个插件全 `abi=8`）。

| 条目 | 判定 | 证据 |
| - | - | - |
| 1. 插件的全部注册入口只有 6 种 + 2 个钩子 | 成立 | `golding_plugin.h:206-254` 六个 register* 与对应查询；钩子为 `entry` 与 `ready` |
| 2. 插件的属性散在三处，没有一份装配前可读的完整档案 | 成立 | descriptor（`golding_plugin.h:278-306`）、`plugin.json`（`ManifestLoader.cpp:126-213`）、运行时表项 |
| 3. 服务 vtable 的 `abiVersion` 首字段定义了但**零处校验** | 成立 | `plugins/common/GoldingServices.h:16/28/44` 三处定义；`grep abiVersion plugins/` 无任何读取点 |
| 4. `consumes`（我要用哪个能力）在任何地方都不存在 | 成立 | `golding_plugin.h` 只有 `depends`（插件名），无"需要的能力名"概念；`MagicPlugin.cpp:330` 查不到导航服务即静默继续 |
| 5. SDK 的 C++ 面只支持单 widget / 单 page / 单区块 | 成立 | `GoldingPluginBase.h:174-194` 三个成员各一个指针，无追加口 |
| 6. descriptor 无条件填 `ready`，未声明 `onReady` 的插件也被回调 | 成立 | `GoldingPluginBase.h:140`；实测默认模式打出 7 行"装配后自检"，其中 5 行后面无断言 |
| 7. 没有任何 C++ 接口基类 | 成立 | `grep "virtual ~" include plugins` 零命中（`third_party/` 除外） |
| 8. 没有默认 UI 构件的提供方 | 成立 | `GoldingServices.h` 只有 overlay / navigation / apps 三个服务，无构件工厂 |
| 9. 没有自动化自检入口 | 成立 | `enable_testing` / `add_test` 各 0 处；10 个模式一直是手敲 |
| 10. 退出码不是失败项数 | 成立 | `host/main.cpp:273` `return g_failures == 0 ? 0 : 2;` |
| 11. `requires.plugins` 被强制要求存在但不参与解析 | 成立 | `ManifestLoader.cpp:157-165` 写入无人读取；Resolver 只吃 `descriptor->depends` |
| 12. 纯数据插件（无 dll）不装配 | 成立 | `HostApp.cpp:442-445` 标 `NoDll`；不进 `_nodes`，故依赖它的插件被判 `MissingDependency` |
| 13. 进程内插件崩溃不隔离，只能靠日志排查 | 部分成立 | 用户已明确接受；现状确无隔离手段，但注册与查询的日志覆盖尚可（`HostApp.cpp:66-283` 每个注册都打一行） |
| 14. 升 ABI 版本会让所有旧插件被拒 | 成立 | `PluginLoader.cpp:65-70` 严格相等比较；无"向后兼容"分支 |
| 15. descriptor 里没有任何 SDK 身份标识 | 成立 | `golding_plugin.h:278-306` 只有 `abiVersion` / `name` / `version` / `rootWidgetClass` / `depends` / `ready`；"这个插件是给哪套 SDK 编的"无法表达 |
| 16. "换一套核心插件 = 换一个应用"不需要新机制 | 成立 | `PathFinder.cpp:155-156` 已把 `plugin/core/` 整目录当作一套；换掉目录内容即换应用，SDK 不必分裂（用户 2026-10-23 明确） |

---

## 3. 第一批：开发包立起来（零 ABI 变更）

- T-01 把 10 个自检模式收成 ctest-已完成（2026-10-23，`ctest --test-dir build` 10/10；`tools/selftest.bat` 拼错 `--overlay` 返回 1 并点名该模式）

> 落地时补上的一条：宿主对**拼错的模式**静默放行（退出码 0、日志与默认模式逐字相同），
> 只比退出码的 bat 等于橡皮章。故 `selftest.bat` 除了退出码，还要求非默认模式的
> `[ok]`/`[probe]` 行数**与默认模式当场标定的基准不同**——参数拼错就会被这一条抓住。

---

- T-02 新增 GoldingPlugin.h（三钩子基类 + Host）-已完成（2026-10-23，`ctest -R sdk_host` 23 条断言全绿；OFF 分支同样能编能跑）

> 落地时的两处补正：① `PluginManifest` 多了 `name` / `version` / `rootWidgetClass`——
> descriptor 那三个字段总得有来源，而基类只许三个钩子、不能再开一个 `Identity()`；
> ② `Ready` 默认实现返回 0 而 descriptor 的 `ready` 仍**无条件填**，行为与今天一致
> （T-05 那句"只对声明了 Ready 的插件回调"没有可用的重写探测手段，且会让自检标题行数变化）。

## 4. 第二批：接口落地（零 ABI 变更）

- T-03 新增 GoldingWidgets.h 并把 home 迁成样板-已完成（2026-10-23，ctest 11/11；`[plugin] home 能力: kind=page needs=0`；home 的页面几何与迁移前逐字相同）

> 落地顺序照任务书执行：先只换类型、跑一遍 10 模式，输出与基线**逐字相同**；再换声明形态。
> 新增的那行能力留痕放在 `EntryThunk` 里，所以**每个**插件都会打一行，不只 home。

- T-04 把 shell 迁到 Plugin 接口-已完成（2026-10-23，11 条几何断言与迁移前逐条相同；日志只多 `[sdk]`/`[hostapi]` 留痕行）

> 两处与任务书不一致，以代码为准：① shell 原本**没有** `depends("theme")`（任务书那句"不能顺手去掉"前提不成立）；
> ② 面板的 C 回调不在 `Register` 栈上，落的是**文件级** `golding::Host` 而不是插件成员。

- T-05 其余实验插件迁移并删旧宏-已完成（2026-10-23，`grep GoldingPluginBase` 在 plugins/host/src 零命中；装配后自检 9 行与迁移前同名同序）

> 任务书说"实测 7 行"，实际迁移前是 **9 行**（9 个插件各一行）。迁移后仍是同样 9 行、同样顺序，
> 所以"行数不变"这条成立。另外 `settings` / `magic` 的 C 回调够不到 `Register` 的参数，
> 与 shell 一样落文件级 `Host`；其余插件没有需要跨回调的宿主入口。

## 5. 第三批：能力目录（ABI v9）

- T-06 descriptor 增 manifest() 与 SDK 标识并升 ABI v9-已完成（2026-10-23，`abiVersion=9`；abi 改 8 的插件被拒 status=3、sdkId="other" 被拒 status=8、sdkVersion 不同只放行，三者都不崩）

> 验收测试顺手照出一个既有缺陷：`main.cpp` 把 `resolved.order` 里的**节点下标**当成档案下标
> 去取名字，只要有一个插件被拒，后面所有插件名整体错位，"每个插件都有去处"也会误判。已修：
> 新增 `providerOfNode`，并把去处分成 **可装配 / 装不上 / 加载阶段否掉** 三种。

- T-07 建能力目录与 needs 交叉校验-已完成（2026-10-23，home 的 needs 指一个不存在的名字 → `[host] home 缺能力 ...`、`state=6`、其余 8 个照常、退出码 0）

> 验收里"ctest 里除 home 页面装得上外的模式仍绿"这一条，实际是 `selftest.page3` 变红：
> home 不装配就不再注册页面，页面表从 4 页变 3 页，索引 3 越界。护栏确实会响，只是响在另一个 test 上。

- T-08 校验 provides 与运行时注册一致-已完成（2026-10-23，shell 声明了 navigation 却不注册 → 点名日志 + `state=5` + 不被选为面板）

> 与任务书不一致的一处：ctest 不会因此变红，因为"没有面板插件"是**设计上的合法状态**
> （README 验收清单第一条：删掉 core 插件程序仍启动）。护栏证据是那行点名日志与 `state=5`，
> 不是退出码——把"面板缺席"改成红色等于推翻那条既有约定。

## 6. 第四批：构件能力

- T-09 新增 widgets 插件与 golding.widgets 契约-已完成（2026-10-23，`[host] 注册服务 golding.widgets（来自 widgets）`；档案多一行 `widgets state=0 abi=9 kind=widgets`）

> 两处落地细节：① `knob` 只做到「最小可用」（面板 + 侧表记值）——InkingUIKit 没有现成旋钮，
> 自绘要新写组件类，等真有人用再说；② 配置串解析用的是 core 的 JSON 读取器，为此把它从
> `src/Application/` 挪到 `include/Application/`；widgets 是**唯一**额外链接 `golding_core` 的插件。

- T-10 home 的按钮改走构件服务-已完成（2026-10-23，按钮几何仍 200x48@72,268；`grep InkingStaticButton` 在 HomePlugin.cpp 零命中；探针 (200,330) 仍返回 `block / home.launch`）

> 按要求把几何断言写进了 home 的 `Ready()`（否则配置串漏一个字段就是「能画但位置不对」的静默错）。
> 它只在首页挂着的那一轮断言：`--page=1..3` 会把首页换走，那时找不到节点不算失败。

## 7. 批次门槛与进度

### 7.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| 一（开发包地基） | `ctest --test-dir build` 10/10；`ctest -R sdk_host` 通过；`GOLDING_WITH_INKING=OFF` 下能 configure 并跑 SDK 单测 |
| 二（接口落地） | 10/10 仍绿；`grep -rn "GoldingPluginBase" plugins/ host/ src/` 零命中；`[host] 装配后自检：` 行数与迁移前一致 |
| 三（能力目录） | `abiVersion == 9`；临时改成 8 的插件被拒且不崩；`sdkId` 不同的插件被拒且不崩、其余照常；`sdkVersion` 不同只警告仍加载；缺能力插件标 `state=6` 而其余照常；10/10 |
| 四（构件能力） | `golding.widgets` 服务可查；home 的按钮经服务创建且几何不变；10/10 |

### 7.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 把 10 个自检模式收成 ctest | 一 | 已完成 |
| T-02 | 新增 GoldingPlugin.h（三钩子基类 + Host） | 一 | 已完成 |
| T-03 | 新增 GoldingWidgets.h 并把 home 迁成样板 | 二 | 已完成 |
| T-04 | 把 shell 迁到 Plugin 接口 | 二 | 已完成 |
| T-05 | 其余实验插件迁移并删旧宏 | 二 | 已完成 |
| T-06 | descriptor 增 manifest() 并升 ABI v9 | 三 | 已完成 |
| T-07 | 建能力目录与 needs 交叉校验 | 三 | 已完成 |
| T-08 | 校验 provides 与运行时注册一致 | 三 | 已完成 |
| T-09 | 新增 widgets 插件与 golding.widgets 契约 | 四 | 已完成 |
| T-10 | home 的按钮改走构件服务 | 四 | 已完成 |

完成任务后，本节状态改 `已完成`，§3~§6 对应段落按 **§1.2** 压缩成一行——两处必须同时改。

---

## 8. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 插件崩溃隔离 / 异常边界（SEH、进程外插件） | 用户明确接受"允许瑕疵，靠日志排查"；成本极高且与"一个 dll 一个插件"的轻量模型冲突 |
| C++ 接口跨 dll 多态（宿主 `dynamic_cast<Plugin*>`） | 要求编译器与 STL ABI 完全一致，Debug/Release 混编必崩；跨插件调用只走能力目录 |
| 插件继承插件（`extends` 具体插件） | 与"依赖在前"的单向拓扑冲突，要新造"父插件不可用时子插件状态"等一整套语义；`extends` 的位置语义已由 `regionId` + `depends` 覆盖 |
| `kind` 做强类型枚举 | 一旦 core 认识 `"panel"` 以外的 kind，就会长出第二张特化表 |
| 宿主同时兼容 v8 与 v9 | 实验期不背兼容包袱（见 T-06）；`Options.md` 记录解除条件 |
| 把 `PageRegistry` / `SettingsRegistry` 归并进 Region | 与本轮正交，是 `dev001` 站稳之后的事（沿用 `hardening-task-book` §7 的决定） |
| 纯数据插件（无 dll）的装配路径 | manifest 的 `widgets{}` 仍无消费者，本轮不动 |
| `magic.json` 读取与 `magicSchemaVersion` | 与本轮正交，`HANDOVER.md` §4 挂着 |
| 一次性迁移全部插件到新接口再验证 | 用户明确"具体插件是实验品，慢慢做"；本文件按 T-03→T-04→T-05 逐个迁、逐个跑 ctest |
| 多套"核心插件库"目录 / 多份 SDK 并存 | 用户 2026-10-23 明确：换一套核心插件就是换一个应用，把 `plugins/core/` 整个换掉即可（甚至整套写成不同的 shell）；`include/sdk/` 只有一份，核心插件的扩展头留在它自己目录里 |
| 把平台塞进 `sdkId` | 跨平台由 manifest 的 `targets` 表达；平台是编译目标、SDK 是头文件集合，混在一起以后没法分别校验（见 T-06） |
