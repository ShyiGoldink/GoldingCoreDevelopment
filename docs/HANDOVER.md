# 交接单（2026-10-22 13:15）

> 给下一个对话用的。只写"事实 + 半成品 + 坑"，不写过程。
> 状态：分支 `feature/core-layout-plugin`。
>
> **先读 `core-review.md` §6**——10-23 定了 `dev001` 只承载 core + 插件开发包（`Options.md` §13）。
> 构建入口已拆成根（只有 core + 单测）/ `host/` / `plugins/` + `cmakes/GoldingPlugin.cmake`，
> 只含 `include/ src/ tests/ cmakes/` 的检出实测 2/2。**唯一还没定的是"零 UI 插件样例"（M-3）**。
> **先读第 8 节**——13:15 之后又做了一批，第 1~7 节的"未提交""半成品"部分已部分过时。
> **再读第 13 节**——三张表收成一个 `SlotTable`（页 / 设置 / 区域条目共用一套规则）。
> **然后读第 12 节**——10-23 夜最新一轮：登记表计数、`entry`/`ready` 兜底（含做不到的边界），
> 以及查出来的"magic 断言依赖 selftest 挂载"这条跨插件时序问题。
> **然后读第 11 节**——T-10b / T-11：`widgets{}` 已删，`magic.json` 已通，ABI 仍 v10。
> **再读第 10 节**——那一节是 ABI v10 与路径解析，仍是现行事实。

---

## 13. 三张表收成一个 `SlotTable`（2026-10-23 夜，第二轮）

**做了什么**：新增 `include/Application/SlotTable.h`（header-only 模板），页表、设置表、
区域里的条目表都改成它的实例。删掉了三份重复的排序 / 去重 / 提供方 / 清空代码：

| | 改前 | 改后 |
| - | - | - |
| 页表 | `PageRegistry.cpp` 自己维护 `vector<Entry>` + 排序 | `SlotTable<GoldingPageDecl>` 实例 |
| 设置表 | 同上，几乎逐行复制 | `SlotTable<GoldingSettingsDecl>` 实例 |
| 区域条目 | 自己维护 `vector<ItemEntry>` + `upper_bound` 插入 | 每个区域持一个 `SlotTable<GoldingRegionItemDecl>` |
| 驳回原因 | 三处各写一套文案 | 一处生成，区域用 `Rules::scope` 加前缀（`区域 menubar 内同 id 已存在：file`） |

**没有做的**：没有把页 / 设置变成"两个保留区域 id"。原因写在 `Options.md` §12：那样
`allRegions()` 会凭空多两项，面板遍历时会把页面当条目摆——拿插件可见的语义换一个类名，
不划算。

**两条必须记住的规则差异**（`SlotTieBreak`）：
- 页 / 设置：同 order 按 **id 字典序** → 与加载先后无关；
- 区域条目：同 order **先到先得** → 面板按注册顺序摆，重排会让用户眼前跳。

单测钉住这两条 + `allowDuplicateIds=1` 放行，共 49 项（`ctest` 12/12，OFF 分支 2/2）。

**踩到的坑（模板类里别用函数内静态存储）**：第一版把行存储写成
`std::vector<Row>& rows() { static std::vector<Row> storage; return storage; }`，
结果**同一个 `TDecl` 的所有实例共享一份存储**——每个区域的条目表都是
`SlotTable<GoldingRegionItemDecl>`，于是全部区域共用一个 vector。改成普通成员
`std::vector<Row> _rows`，表本体由各注册表用函数内静态量持有。

---

## 12. core 第二轮：登记表计数 + entry/ready 兜底（2026-10-23 夜）

**登记表计数（已落地）**

- 新增 `include/Application/RegistryStats.h`：`accepted` / `rejected` / `lastReject` / `reset()`。
- 接入五张表：`PluginRegistry`、`PageRegistry`、`SettingsRegistry`、`RegionRegistry`、
  `ServiceRegistry`，各加 `static const RegistryStats& stats()`，`clear()` 连计数一起复位。
- 两处**故意不接**：`CapabilityRegistry` 的运行时面就是 `ServiceRegistry`，再记一份等于双计数；
  `RegionRegistry` 的区域与条目**共用一份**（同一张表的两种写入，拆开逼调用方问两次）。
- 单测 27 → 45 项。断言的是决策（驳回几次、原因点名哪个 id），不是日志文案。
  `ctest` 12/12；`GOLDING_WITH_INKING=OFF` 分支 2/2。

**entry / ready 兜底（已落地，边界见 `Options.md` §11）**

- `registerAll`：`callEntry` 包 `try/catch`，抛了就把 `entryOk=false` + `EntryFailed`，继续下一个。
- `notifyReady`：`ready` 包 `try/catch`，抛了**只记 1 项失败**，不撤销注册（已经装好了，撤销
  会留半装配状态）。
- 接不住的两类必须记住：SEH（空指针写等）在 MinGW 下不经过 `catch(...)`；插件静态链
  libstdc++ 时异常在 dll 边界直接 `terminate`。**这是兜底，不是隔离。**
- 实测方式：临时把 selftest 的 `Register` 改成 `throw std::runtime_error("probe")` →
  日志出现 `[host] entry 抛异常：selftest · probe`，随后 10 个插件照常解析、正常收尾；
  验完已还原（`git diff` 里不应有 selftest 的这处改动）。

**顺手查出来的一个真问题（未修，属于插件侧）**

`magic` 的 `ready` 断言"内环直径 = 2×半径"，而那个直径是**别人**在挂载设置区块时算出来的：
`selftest` 的 `ready` 会挨个 `mountSettingsSection` 做"页面可装配"断言，轮到 `magic` 时
才把 `g_innerDiameter` 填上。于是出现一条**跨插件时序依赖**——`selftest` 一旦 entry 失败
（或抛异常），`magic` 的断言就报 `实际=-1`。对比实验确认：让 selftest `return false`
同样复现，与本次兜底无关。

正解二选一：① `magic` 在自己的 `Ready` 里自己建一次区块再断言（不看别人）；② 该断言在
未挂载时显式跳过并打印"未挂载，跳过"。倾向 ①——自己产出的值自己验。

---

## 11. T-10b / T-11：magic.json 通了，widgets{} 删了（2026-10-23 深夜）

`TASK-CORE-PLUGINS.md` 第三批收尾。**ABI 仍是 v10**，没有升版本事件。

**T-10b 读 `magic.json`**

- `magic` 的 `Register` 里 `loadLook(host.PluginDir())`，用 core 的 JSON 读取器解析
  `<插件目录>/magic.json`；键只有五个：`magicSchemaVersion` / `ringRadius` / `starPoints` /
  `ringColor` / `coreColor`。**缺键按默认补齐、坏值整份忽略**——后者只打一行 WARN，每份文件只一句。
- 半径上限**只有一个来源**：`maxRingRadius() = (画布高 290 − 20) / 2 = 135`。配置校验与 `Ready`
  共用它，否则会出现"配置允许 200、自检只认 135"两套基准（这种双真源是本项目栽过两次的坑）。
- `Ready` 新增 4 条断言，最后一条"内环直径 = 2×半径"是**画出来的**那个值，不是算式：
  `createMagicSection` 建完立刻把 `innerRing->GetWidth()` 存进 `int g_innerDiameter`。
- **踩到的坑（重要）**：一开始存的是 `InkingWidget* g_innerRing`，`Ready` 里读它 ⇒ 约 20% 概率
  SIGSEGV（gdb 栈：`runReady → magic.dll`）。设置区块会随页面切换销毁重建，缓存节点指针就是野指针。
  **规则**：跨阶段只传值（尺寸/名字/计数），别传别人拥有的节点指针。
- `magic` 现在也 `target_link_libraries(... golding_core)`（和 `widgets` 一样只为 JSON 解析器）。
  静态库链进 dll = 那个 dll 里有一份自己的 core，**只能当纯函数用**，注册一律走 `Host`。
- `magic.json` 随插件目录分发（`plugins/CMakeLists.txt` 里 magic 的 POST_BUILD 拷一份）；出厂值就是原来的
  写死常量，所以**外观不变**。

**T-11 删 `widgets{}`（选 B）**

- `Manifest::widgets` / `WidgetDecl` 删除，`ManifestLoader` 对应解析段删除；`PathFinder::resolve`
  的注释、README §3、`Options.md` §9、`open-questions.md` 第 9 条都已标注。
- **顺带做的一条（可独立回退）**：`plugin.json` 顶层键改成**白名单**
  （`name/title/version/apiVersion/role/requires/targets`），未知键判 `Invalid`。没有白名单的话，
  删掉字段后写 `widgets` 只会被静默忽略——那正是 `abiVersion` 写在 json 里那种死法的重演。
- 为此新增 `ManifestResult::detail`（人看的一句话，`Pass` 时为空）→ `ScannedPlugin::detail`
  → 宿主扫描日志多一行 `[host]   ↳ theme：未知字段 widgets`。`Invalid` 不再只是一个 `status=1`。

**实测**：`ctest` 连续 8 轮 **12/12**；无头 `失败项 0`，10 个插件 `abi=10`；`OFF` 分支 **2/2**
（`core` 27 项 + `sdk_host`）；`magic.json` 五种情况逐条验过（出厂 95 / 改 130 与段数 8 / 版本 99 忽略 /
半径 300 忽略 / 删文件回默认）。

**留给下一轮**：`magic` 改 `ICanvasPainter`（`Handover` §4 #3）、左侧启动栏（T-09）、1080p 布局（T-08）、
`Ready` 标题行的空壳问题（T-03）。第三批的门槛三条已满足，但**第一批与第二批的 T-01~T-09 在进度表上仍是未开始**
（进度表没随实作同步过，以代码为准：knob 仍不可交互、store 卡片仍无点击、页面仍是 1224×680）。

---

## 10. ABI v10：路径解析 + 插件目录（2026-10-23 晚）

`TASK-CORE-PLUGINS.md` 的 T-10 拆成 T-10a（通道，**已完成**）/ T-10b（读 `magic.json`，未开始）。
这一轮只做 T-10a，外加路径解析——两者都落在 core，且都是"插件不必自己拼路径"的前提。

- **`PathFinder::resolve(base, relative)`**：绝对路径、`C:foo` 这种带盘符无根的、`..` 逃出 base 的一律
  返回空 path。逃逸判定是**逐段比**，不拼字符串（否则 `plugin/home2` 会被当成在 `plugin/home` 之下）。
  **不查存在性**——存在性是调用方的事（dll 可以缺，配置缺了要报错，两者判定不同）。
- **`PathFinder::toUtf8 / fromUtf8`**：别用 `path.string()`（MinGW 下按 "C" locale 解码，中文路径抛
  `Illegal byte sequence`），也别用 `path(std::string)`（那是 ANSI）。
- **`ManifestLoader::read` 当场解析** `targets.win64.lib` ⇒ `Manifest` 里是绝对路径（同一段曾处理 `widgets{}.path`，那个字段已在第 11 节的 T-11 删除）；
  写成绝对路径或用 `..` 逃出去 ⇒ 整份 `Invalid`（这是声明错误，不是"没配"）。
  `HostApp::loadEntry` 因此不再 `directory / dllPath`，改 `PathFinder::fromUtf8(...)`。
- **HostApi 末尾追加 `pluginDir(hostContext, pluginName)`**，`GOLDING_PLUGIN_ABI_VERSION` → **10**，
  10 个插件同批重编。签名带名字而不是只吃 `hostContext`：后者现在只是 `&entry.provider`
  （字符串指针），拿它反查目录要另维护"指针 → 目录"旁表；名字本来就是档案表的唯一键。
  `Options.md` §5 那条 `hostContext` 欠账因此**仍然不用动**。
- **目录字符串存在 `PluginLibrary`**（`setDirectory` / `indexOf` / `directoryOf`），不在 `LoadedEntry`：
  `hostPluginDir` 是 `fillHostApi` 的静态回调，拿不到 `HostApp` 实例，而其它回调本来就都读档案表。
  未知名字返回 **nullptr**（不是空串）——"没这个插件"与"有但没登记目录"要能分开。
- **core 第一次有单测**：`tests/core_tests.cpp`（当时 26 项；第 11 节删了 3 条 widgets 断言、加了 2 条白名单断言，现 27 项）+ ctest 里的 `core`。
  `GOLDING_WITH_INKING=OFF` 下 ctest 从 1 个变 2 个——"core 可无头自检"不再依赖 InkingUIKit 装得上。
- **自检新增 12 项**：每个 `state=0` 的插件都要能问到目录、且该目录里真有 `plugin.json`；
  另加一条"未知名字返回 nullptr"。断言在 `plugins/dev/selftest` 的 `Ready` 里。

**实测**：`ctest` **12/12**；无头 `失败项 0`；10 个插件 `abi=10`；`OFF` 分支 **2/2**。
**坑**：`std::filesystem::u8path` 在 C++20 已弃用，GCC 16 直接报 `-Wdeprecated-declarations`——
插件侧要 UTF-8 → path 就手写 `std::filesystem::path{std::u8string(s.begin(), s.end())}`。

## 9. 迁移任务书执行完毕（2026-10-22 晚，覆盖第 3/4/5 节的过时结论）

`docs/migration-task-book.md` 的 6 个任务（三批）**全部完成**，10 模式自检失败项 0。
下面几条是**别按旧认知写代码**的部分：

- **没有"请求整树重建"这条通道了**（ABI v7 删掉）。抽屉展开 / 收起是 `Resize`，
  覆盖层弹出是 `SetVisible` + 改尺寸，换肤是 `ThemeVersion` 自动重查。第 3 节最后
  那条"全部走置状态 + 请求重建"已作废。
- **动态容器可以放静态按钮了**，但有条件：**不声明 `OwnsSubtreeHit()`**。声明之后
  子孙不进命中表，而场景的深挖只走"静态表命中且是容器"那一支 —— 按钮会点不到。
  第 5 节第 1 条（"动态组件子树里的按钮永远收不到点击"）在 0.4.0 之后**只对声明了
  `OwnsSubtreeHit()` 的容器成立**。
- 第 5 节第 2/3 条（事后 `AddChild` 不进渲染列表、隐藏过再显示不渲染）在 0.4.0 里
  **已修**（A2/A3），所以覆盖层改成了常驻节点。
- 第 4 节 #1（全屏留白）**已修**：主体高度 = `窗口高 − MenuBar::GetCurrentHeight()`，
  `--fullscreen` 模式正面断言。
- **自检插件化**（ABI v8）：descriptor 加 `ready`（装配后回调，返回失败项数），
  HostApi 加 `uiRoot` / `bootMode`。自检归属：宿主冒烟检查 → `selftest` 插件
  （通用契约）→ 各插件 `ready`（自己的几何、自己的模式）。宿主 `main.cpp` 里
  不再有任何面板节点名。
- **设计画布改成 1080p**（任务书 T-05）：`host/main.cpp` 的 `kDesignWidth` / `kDesignHeight` = 1920×1080。
  插件从父节点量尺寸（T-04），所以改那一处就够；但**页面里的绝对坐标仍按 1224×680 写着**，
  1080p 下内容挤在左上、右侧空 ~900px——那是 `docs/hardening-task-book.md` 的 T-06（未做）。
- 新增插件 `selftest`（源码 `plugins/dev/selftest/`，产物落 `plugin/core/selftest`），
  所以插件数从 8 变 9。

第 6 节的"建议下一步"里，第 2 条（全屏留白）已完成；其余（magic.json、画布、
左侧启动栏、`widgets{}`、Resolver 降级）仍然没动。

---

## 8. 20:00 之后的进展（覆盖前文的过时部分）

已提交：`8b26550`（框架落地，53 文件）· `c044523`（InkingUIKit 需求单）· `44f3e14`（core 正确性修补 + 分层 + 面板插件）。

**运行布局变了**：插件产物现在装在 `plugin/core/<name>/`，不再是 `plugin/<name>/`。
`PathFinder` 先扫 `plugin/core/`（Bundled）再扫 `plugin/`（External），顺序即优先级。
第三方插件丢进 `plugin/<name>/` 即可；与内置同名的会被标 `shadowed` 让位。

**改掉的东西（别再按旧认知写代码）**：

- `plugin.json` **不再有 `abiVersion`**，ABI 唯一来源是 dll 的 descriptor。
- `role` 的值从 `core` 改成 **`panel`**；面板插件按"装成功 + 首个命中"选，即内置优先，多个会告警。
- `Resolver::ResolveResult` 结构换了：`order` **只含能装的**，装不上的在 `unresolved` 里带原因（`MissingDependency` / `InCycle` / `DependencyUnavailable`）。`status` 只剩 `Pass` / `Partial`。
- `PluginLibrary::add` 的 state 参数现在是 `PluginState` 枚举；新增 `setState` 供依赖结论回灌。
- `PathFinder::plugins()` 返回 `sources`（`PluginSource{directory, tier}`），不再叫 `directories`。
- `PluginScanner::scan` 吃 `sources`，输出带 `tier` 与 `shadowed`。

**顺手修的两个真 bug**（此前没人发现，因为档案表没有逻辑依赖）：

1. `state` 在 entry 调用之前计算，`!entry.entryOk` 恒为真 → 所有加载成功的插件都被标成 EntryFailed(5)。
2. `hostContext = &entry.provider` 而 `loaded` 是 `std::vector` → 一旦中途再 `push_back`，所有已注册插件的指针一起悬空。已改 `std::deque`。

**新增的验收结果**：

- 9 种模式全部 `失败项 0`、退出码 0。
- 删掉 `plugin/core/shell` → 程序正常启动、7 个档案完整（6 个标 `state=6`）、界面为空、跳过几何自检、**退出码 0**。
- 往 `plugin/home/` 放一份同名插件 → 打印"忽略 home：同名的内置插件优先"，仍是 8 个可装配。

**下一步的顺序（用户已定）**：先等 InkingUIKit 补完（见 `docs/inking-requirements.md`），
再拆 `HostApp` + 把 SelfTest 做成插件；`Region` 契约已随 ABI v6 落地（见下）。

**ABI v6：区域表**（21:50 加）

- `GoldingRegionDecl`（面板插件声明可注册位置）+ `GoldingRegionItemDecl`（任意插件往里放一项）
  + 6 个 HostApi 函数：`registerRegion` / `regionCount` / `regionAt` /
  `registerRegionItem` / `regionItemCount` / `regionItemAt`。
- core 侧 `Application/RegionRegistry`（第五张表），静态类 + 同名驳回，与其它四张表同套路。
- **core 不认识 `menubar`**：那是 shell 自己定的约定。换面板插件 = 换一套可注册位置。
- shell 声明 `menubar`，并从 `regionItemAt` 生成顶栏按钮；`home` 往 `menubar` 注册"首页"
  （`create` 为空 + `onActivate` 走导航服务）。顶栏因此从两个写死按钮变成三个。
- 往不存在的区域注册 → 驳回但不中断该插件其余注册；面板缺席 → 区域表空、程序照常。

**仍未归并**：`PageRegistry` / `SettingsRegistry` 是区域机制之前的特化表，暂与 Region 并存
（分别相当于 `content` / `settings` 区域）。先让机制存在，归并留到 dev001 站稳之后。

**HostApp 拆分完成**（22:20）

- `host/HostApp.{h,cpp}`：产品编排（扫描/加载/建档/解析/entry/选面板），**无 InkingUIKit 依赖**，可无头自检。
- `host/main.cpp`：只剩命令行 + 建窗口装配 + 自检（仍硬编码 shell 节点名，P0-4 遗留）。
- `requestRebuild` 是界面层能力，main 经 `HostApp::setRebuildHandler` 注入。
- SelfTest 仍**未**插件化：InkingUIKit 的 `RequestClose(code)` / 装配后钩子 / 点击基类化还没到位。
  当前 `RequestClose()` 仍是无参的。

---

## 1. 一句话现状

骨架（三栏）+ 10 个插件 + 服务表 / 能力目录 + 依赖解析 + 主题切换 + 魔法阵 + 底部覆盖层 + 构件服务，**都能跑**；
但下面第 4 节列的六件事，每件都是"做了一半"。

## 2. 怎么跑

```bash
cd D:/GoldingCore
cmake -S . -B build -G Ninja -DGOLDING_WITH_INKING=ON \
      -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 交互（双击也行）
run.bat

# 无头自检：10 个启动模式各一个 ctest，一条命令全跑
ctest --test-dir build --output-on-failure

# 同一套模式的 bat 版（自己找 SDL3；任一模式红了返回非零）
tools\selftest.bat
```

模式清单：默认 / `--expanded` / `--page=0..3` / `--theme=golding-light` / `--overlay` /
`--probe` / `--fullscreen`。断言本身没有变，只是不再用手敲。

自检当前：10 个模式全部通过（`ctest` 10/10）。

## 3. 已完成

| 层 | 内容 |
| --- | --- |
| SDK | `include/sdk/golding_plugin.h`（**C ABI v10**，含 `manifest()` / `sdkId` / 能力目录契约 / `pluginDir`）、`GoldingPlugin.h`（`Plugin` 三钩子基类 + `Host`）、`GoldingWidgets.h`（构件包装 + `golding.widgets` 客户端） |
| 宿主 core | `PathFinder` `ManifestLoader` `PluginRegistry` `PageRegistry` `SettingsRegistry` `ServiceRegistry` `PluginScanner` `PluginLoader` `PluginLibrary` `Resolver` |
| 宿主 exe | `host/main.cpp`：扫描 → 拓扑排序 → 按序调 entry → 装配骨架 → 自检 + 截图 |
| 插件（8 个） | `shell`（骨架）· `home` · `store` · `settings` · `magic`（页+区块）· `theme` · `addapp` · `appdrawer` |
| 服务 | `golding.overlay`（shell）· `golding.navigation`（shell）· `golding.apps`（addapp） |
| 文档 | `docs/architecture.md`（结构定稿，9 节）· `docs/open-questions.md`（早期决议） |

已定的关键决策（别推翻）：

- 依赖**只认 dll 里的 descriptor**，`plugin.json` 的 `requires` 只作展示，不参与解析。
- 插件之间**只能**通过服务名 + 函数指针表通信，不传 C++ 类型。
- 菜单层级**已删除**（曾经设计过 `GoldingMenuItem` + parentId，用户要求删）。
- 换肤、抽屉展开、覆盖层弹出——**全部走"置状态 + 请求重建"**，不做事后挂节点。

## 4. 半成品清单（重要）

| # | 事 | 现在到哪 | 还差 |
| --- | --- | --- | --- |
| 1 | **全屏留白** | 只写进 `architecture.md` §8 的分析 | 代码没动。`kTopBarHeight = 40` 仍写死在 shell 布局常量里，`MenuBar` 全屏隐藏后会留一条空白。要么关掉 `hideInFullscreen`，要么把顶栏高当运行时量 |
| 2 | ~~**magic.json**~~ | **已完成**（第 11 节 T-10b）：插件自己读、自己解释，缺文件退回默认 | 只剩「改 `ICanvasPainter`」（#3）没做 |
| 3 | **magic 改成 `ICanvasPainter`** | 画布 API 已确认在包里可用（`InkingStaticCanvas` + `CanvasContext` + `PolygonPath`） | 现在还是用 `Panel` 的 `Ring`/`Star` 拼的 |
| 4 | **左侧应用启动栏** | `addapp` 有清单服务、`appdrawer` 是覆盖层版 | 侧栏没做。要动就得给 Shell 加"抽屉条目来源"的服务，否则 core 认业务 |
| 5 | ~~**纯数据插件 `widgets{}`**~~ | **已处置**（第 11 节 T-11）：字段已删，`plugin.json` 顶层键改为白名单 | 不再是半成品；「纯数据插件」这个概念仍未定（`Options.md` §9） |
| 6 | **Resolver 的降级** | 拓扑 / 环 / 缺失都能算出来 | 只 `printf`，插件缺失时没有实际降级行为 |

顺手可做的小洞：

- `store` 页的 6 张卡片 `SetOnClicked` 数为 **0**，纯展示，点了没反应。
- README §3 仍把 `requires.plugins` 写成必需字段，与"依赖只认 dll"不一致。
- 顶栏只有"菜单/设置"两个硬编码按钮，没有"插件注册菜单项"的能力。
- 没有动作表（`setParam / playSound / show / hide / emit`），插件之间只能切页。
- **全部文件未提交**（`git log` 只有 2 个提交）。`git checkout` 一次就全没了。

## 5. 宿主库（InkingUIKit 0.1.0-test）的硬约束

这些是拿真实崩溃和空画面换来的，新对话务必先看：

1. **`InkingDynamicWidget` 不进命中表** —— 它子树里的静态按钮**永远收不到点击**。要响应输入的子树，容器必须是 `InkingStaticPanel`。
2. **事后 `AddChild` 的子树不进渲染列表** —— 连一块纯红面板都不画。节点在、可见性为真、就是不渲染。所以覆盖层只能用"重建"实现。
3. **创建时 `SetVisible(false)`，之后再 `SetVisible(true)` 也不渲染。** 不能用可见性开关做弹层。
4. **`0x00000000` 面板色渲染成黑块**，不是透明。要"无背景"就填成页面底色。
5. **布局只在交叉轴不拉伸** —— `width=0` 的按钮解出来就是 0 宽，不画也不报错。
6. **`fillMain` 只在首次收敛** —— 改尺寸不能靠重排，只能重建。
7. **命中探针必须在帧内** —— `Show()` 之前 `QueryHit` 一律返回 null（命中表帧首才建）。
8. **`GoldingHostApi` 必须活到进程结束** —— 插件会长期持有指针；建成栈上局部量会段错误，且没有任何输出。
9. **页面根节点要在骨架排过一次版之后创建**，否则 `parent->GetHeight()` 还是 0。
10. **换页判定不能比指针** —— 旧页析构后新页常落在同一地址，按内容判。
11. `GetX/GetY` 是**父级坐标系**，不是屏幕坐标。
12. 字体没有 `☰`（渲染成方框），用 `≡`。
13. `.bat` **不能存 UTF-8 中文**（cmd 按 GBK 读，会吞换行）。

## 6. 建议的下一步顺序

1. **先提交**，把当前全部成果保住（含 `.gitignore` 补 `docs/` 排除策略）。
2. **全屏留白**（第 4 节 #1）—— 它牵扯骨架布局常量，越晚改越贵。
3. **magic.json + `magicSchemaVersion`**（#2）—— 数据化的地基。
4. **magic 改 `ICanvasPainter`**（#3）—— 有了 2 才有意义。
5. **左侧启动栏**（#4）—— 需要一个"抽屉条目来源"服务，属新机制。
6. ~~**`widgets{}` 装配路径**（#5）~~——已放弃该方案（字段已删，见第 11 节）。
7. **Resolver 降级**（#6）—— 最后收尾。

## 7. 给新对话的一句话开场

> 读 `docs/HANDOVER.md` + `docs/architecture.md`，按第 6 节的顺序继续。
> 第 5 节的 13 条是宿主库硬约束，写代码前先遵守，不要试图绕过。
