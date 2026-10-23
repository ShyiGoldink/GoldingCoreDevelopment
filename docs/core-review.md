# core 层现状报告（2026-10-22）

> 范围：`host/` + `include/` + `src/`（**不含** `plugins/`，含 `plugins/core/` 的讨论只作为参照）。
> 分支 `feature/core-layout-plugin`，全部改动未提交。

---

## 0. 结论：三句话

1. **机制层可用，编排层不可搬。** 扫描 / manifest / dll 三段加载 / 四张注册表 / 依赖解析 —— 这 10 个模块是纯函数或静态类，无 UI 依赖、无全局状态，可以直接进 `dev001`。但 `host/main.cpp`（757 行）把启动编排、HostApi 回调、界面装配、几何自检、截图脚手架全塞在一个 `main()` 里，还夹带 3 个文件级全局变量，这一层必须先拆。

2. **你提的"面板插件决定注册区域"这件事，core 里目前没有对应物。** 现在只有两张硬编码的特例表 —— 页面表（内容区）和设置区块表（设置页），它们是"内容区"和"设置页"两个区域的**特化实现**，不是通用机制。菜单栏是 shell 里写死的两个按钮，**任何插件都无法往里加一项**。这是 core 最核心的缺口。

3. **"core 优先找 `plugins/core` 下的面板插件"目前在运行时无据可依。** 源码里的 `plugins/core/` 只是源码分组，构建时被拍平到 `build/plugin/<name>/`（`CMakeLists.txt:60`），`PathFinder::plugins()` 只扫一层（`PathFinder.cpp:99-127`）。骨架插件实际是按 `role == "core"` 在**全部插件里取最后一个**（`main.cpp:501-508`），没有"优先"，也没有冲突报告。

---

## 1. core 层模块清单

| # | 模块 | 文件 | 行数 | 职责 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 1 | `PathFinder` | `include/Tool/` `src/Tool/` | 40 + 134 | exe 目录 / plugin 目录 / 扫一层子目录 | 可用，缺分层 |
| 2 | `Manifest` + `ManifestLoader` | `include/DataStruct/` `src/Application/` | 53 + 218 | `plugin.json` → 结构；BOM 剥离、必需字段、`widgets{}` 外链校验 | 可用，字段有冗余 |
| 3 | `Json` | `src/Application/Json.{h,cpp}` | 50 + 437 | 内部极简 JSON（`\uXXXX`、代理对、深度上限 64），不对外 | 可用 |
| 4 | `PluginScanner` | `include/Application/` `src/Application/` | 27 + 28 | 目录列表 → `ScannedPlugin`，读不出也保留 | 可用，但只是 Loader 的循环包装 |
| 5 | `PluginLoader` | 同上 | 45 + 116 | dll 三段：open → descriptor → ABI 闸门 → entry | 可用，`close()` 无调用者 |
| 6 | `PluginRegistry` | 同上 | 47 + 81 | 组件名 → `GoldingWidgetClass*`，同名驳回 | 可用 |
| 7 | `PageRegistry` | 同上 | 42 + 87 | 页面 id → `GoldingPageDecl*`，按 `order` 排序 | 可用（内容区特化） |
| 8 | `SettingsRegistry` | 同上 | 40 + 89 | 设置区块 id → `GoldingSettingsDecl*` | 可用（设置页特化） |
| 9 | `ServiceRegistry` | 同上 | 32 + 78 | 服务名 → vtable，插件间唯一横向通道 | 可用，不记 provider |
| 10 | `PluginLibrary` | 同上 | 37 + 90 | 插件档案（名字/标题/版本/角色/状态/依赖），`deque` 保指针 | 可用，`dependsOf` 无人使用 |
| 11 | `Resolver` | 同上 | 49 + 105 | Kahn 拓扑 + 缺失 + 环；纯函数 | 算得对，处置缺失 |
| 12 | SDK C ABI | `include/sdk/golding_plugin.h` | 197 | `GoldingHostApi` + 4 个 Decl + descriptor | v5，缺 region 契约 |
| 13 | SDK C++ 面 | `include/sdk/GoldingPluginBase.h` | 202 | `GOLDING_DECLARE_PLUGIN` 链式宏 | 可用，单组件/单页面/单区块上限 |
| 14 | 入口编排 | `host/main.cpp` | 757 | 全部启动流程 + 自检 + 截图 | **待拆** |

### 1.1 `main.cpp` 的实际成分

| 段 | 行范围 | 内容 | 归属 |
| --- | --- | --- | --- |
| 工具函数 | 34-87 | `check` / `describe` / `findByName` / `dumpTree` | 测试 |
| `LoadedEntry` | 90-98 | 扫描结果 + 加载结果 + `GoldingHostApi` + provider + entryOk | 产品，但位置错 |
| HostApi 回调 | 100-270 | 12 个 C 回调 → 转发到四张表 | **产品** |
| 全局重建状态 | 228-238 | `g_rebuildRequested` / `g_rebuildAction` / `g_firstFrameAction` | **产品**，但用文件级全局量 |
| `HostScene` | 273-319 | 跑 4 帧 → 截图 → 自动关窗 | 测试脚手架 |
| 阶段 1 编排 | 348-493 | 扫描 → 加载 → 档案 → 解析 → 按序 entry | **产品** |
| 阶段 2 装配 | 500-561 | 选面板插件 → `SetDesignSize` → 建窗 → create 根组件 → 重建闭包 | **产品** |
| 阶段 3 自检 | 563-750 | 三栏几何断言、换页、展开、换肤、覆盖层、命中探针 | **测试，且硬编码 shell 节点名** |

产品逻辑约 400 行，测试与脚手架约 350 行，两者在同一个 `main()` 里交替出现。

---

## 2. 启动主链路（实际发生的事）

```
PathFinder::plugins()            →  <exe>/plugin/*/  （一层，按路径字典序）
  ↓
PluginScanner::scan()            →  ManifestLoader::read() 逐个读 plugin.json
  ↓  过滤 status != Pass || !hasDll
PluginLoader::open(dll, ABI=5)   →  descriptor  →  abiVersion != 5 则拒绝
  ↓
PluginLibrary::add()             →  档案表（state 由 main 手算成 0..5 魔数）
  ↓  同时另建一份 PluginNode 数组
Resolver::resolve(nodes)         →  order / missing / cycle
  ↓  仅 printf，不改可用性
按 order 调 entry                →  插件往四张表注册
  ↓
挑面板插件：role == "core" 的最后一个
  ↓
SetDesignSize(1280,720) → WindowConfig → AssetConfigure(core/assets)
  ↓
PluginRegistry::findClass(descriptor->rootWidgetClass) → create(&scene)
  ↓
scene.Relayout() → 断言 → Show()
```

四个观察：

- **`PluginNode` 与 `PluginLibrary` 是同一批数据的两次构造**（`main.cpp:387-439`）。`PluginLibrary` 已经存了 `depends`，还提供了 `dependsOf()`，但从未被调用。
- **`state` 是 `main` 里的 if-else 链算出来的 0..5**（`main.cpp:389-409`），SDK 里只有一行注释说明含义（`golding_plugin.h:98`）。这不是枚举，没有类型保护。
- **依赖解析的结论不回流**。`missing` / `cycle` 只打印（`main.cpp:451-458`），`state` 仍按"加载成功"记 0。缺依赖的插件照样 entry，拿不到服务时静默失败。
- **`PluginLoader::close()` 零调用者**，四张表的 `clear()` 也零调用者（只有 `PluginLibrary::reset()` 被调一次）。"完全重加载"的链路是断的。

---

## 3. 现状分级

### 3.1 已达标的（可原样搬迁）

- **ABI 闸门的位置正确**：`PluginLoader::open()` 在 `descriptor->abiVersion != expectedAbi` 时就 `FreeLibrary` 返回，绝不调 entry（`PluginLoader.cpp:64-70`）。这一条是整套设计的命门，实现无误。
- **跨 dll 边界只有 POD**：`GoldingHostApi` 全是函数指针 + `void*`，无 C++ 类型。`GoldingWidgetHandle` 是 `void*`。
- **四张表的实现套路统一**：静态类 + 函数内静态量 + 同名驳回 + 首字匹配（`PluginRegistry.cpp` / `PageRegistry.cpp` / `SettingsRegistry.cpp` / `ServiceRegistry.cpp` 结构一致）。这是有意的，好维护。
- **`PluginLibrary` 的字符串生命周期处理正确**：用 `std::deque` 存字符串、`std::vector` 存档案，避免 vector 扩容搬走 `c_str()`（`PluginLibrary.cpp:19-34`）。
- **`Resolver` 是纯函数**：不碰文件系统、不加载 dll、无全局量，可单测。Kahn 实现正确，缺失去重、自依赖防呆都做了。
- **manifest 读取的边界处理扎实**：BOM 剥离、`abiVersion` 允许数字或数字字符串并校验整数性（`ManifestLoader.cpp:56-97`）、`widgets{}` 内嵌写法直接判错而非忽略。

### 3.2 结构待改的

| # | 问题 | 证据 | 影响 |
| --- | --- | --- | --- |
| S1 | 无 `Application` / `Host` 生命周期对象 | `main.cpp:323-757` 全在 `main()`；重建动作挂在 `std::function` 全局量上（`main.cpp:228-232`） | 一次进程只能跑一个实例；无法重加载；无状态可观测 |
| S2 | 面板插件选择是隐式的"最后一个赢" | `main.cpp:501-508` 遍历 `loaded` 无条件覆盖，`loaded` 顺序来自目录名排序 | 多面板插件时无告警、无优先级、结果偶然 |
| S3 | 装配逻辑与具体插件耦合 | `refind()` 直接查 `"shell.root"` / `"shell.top"` / `"shell.body"`（`main.cpp:572-588`）；断言 `宽度==1224`、`顶栏高==40`（`main.cpp:630-641`） | 违反"换掉 core 插件宿主 exe 不动"；换面板插件自检全崩 |
| S4 | 自检与产品二进制同体 | `check()` / `g_failures` / `HostScene` 无开关隔离，每次启动都跑全部断言 | 产品启动路径不存在；启动耗时被诊断代码占据 |
| S5 | `provider` 存了但不可查 | `PluginRegistry.cpp:15` / `PageRegistry.cpp:13` / `SettingsRegistry.cpp:13` 都存了 `provider`，三份头文件都没有查询出口 | 诊断"这个页面是谁注册的"做不到 |

### 3.3 沉默缺陷（不报错，但语义不对）

| # | 缺陷 | 证据 | 后果 |
| --- | --- | --- | --- |
| D1 | `manifest.abiVersion` 与 `descriptor.abiVersion` 双源，前者从不校验 | `ManifestLoader.cpp:126` 要求它必需；`PluginLibrary::add` 只作展示记录 | 现有 8 个插件里 6 个写 `abiVersion: 4`，实际 ABI 是 5，**无人发现** |
| D2 | `manifest.apiVersion` 读了从不使用 | `ManifestLoader.cpp:126` | architecture.md §6 承诺的"拒绝加载并说明缺哪一项"不存在 |
| D3 | `manifest.dependencies`（`requires.plugins`）读了从不使用 | `ManifestLoader.cpp:156-164`；Resolver 只吃 descriptor 的 `depends` | README §3 仍把它写成必需字段，与新事实矛盾 |
| D4 | 环上的插件不进 `order`，但 `state` 仍记 0（可用） | `main.cpp:389-409` 先定 state，`Resolver` 后算环 | 档案说"可用"，实际从未 entry |
| D5 | `Resolver::cycle` 混装"在环上"与"依赖了环" | `Resolver.cpp:88-98` 把未进 order 的全部塞进 `cycle` | 诊断无法区分根因插件与被连累插件 |
| D6 | `LoadedEntry::api.hostContext = &entry.provider` 指向 vector 内元素 | `main.cpp:470-472` | 现在安全（此时 `loaded` 已定长）；一旦中途再 `push_back`，所有已注册插件的 `hostContext` 全部悬空。脆弱点，应显式记录 |
| D7 | `GoldingOverlayDecl` 在 SDK 里声明，`GoldingHostApi` 里无注册入口 | `golding_plugin.h:108-117` vs `120-162` | 声明与能力不一致；实际靠 `golding.overlay` 服务绕开，但 SDK 读起来像可用 |
| D8 | 加载失败无系统错误码 | `PluginLoader.cpp:39-44` 丢弃 `GetLastError()` | "位数不对 / 缺依赖 / 路径错"三者不可区分，插件作者无从排查 |

---

## 4. 与目标设计的差距（核心）

### Gap 1 — 「优先找 `plugins/core`」缺少运行时依据

当前事实：源码 `plugins/core/shell/` → 构建产物 `build/plugin/shell/`。**分组信息在构建时丢失**。

```cmake
# CMakeLists.txt:60-61
set(runtime_dir "${CMAKE_CURRENT_BINARY_DIR}/plugin/${name}")
set(source_dir "${CMAKE_CURRENT_SOURCE_DIR}/plugins/core/${name}")
```

`PathFinder::plugins()` 单层扫描，返回一个无序的目录列表（排序后按名字）。

**落地建议（推荐目录即事实，不引入 manifest 字段）**：

- CMake 把内置插件装到 `build/plugin/core/<name>/`，第三方仍在 `build/plugin/<name>/`。
- `PathFinder` 改为返回**有序来源列表**：`[{pluginDir/core, Tier::Bundled}, {pluginDir, Tier::External}]`。
- `PluginScanner::scan()` 接受来源列表，每个 `ScannedPlugin` 带 `tier` 字段。
- 面板插件选择：先在所有 `Bundled` 里找，找不到再退到 `External`。同一层内出现多个面板插件 → **显式告警并取 `order` 最小者**，不再"最后一个赢"。

选目录而非 manifest 字段的理由：你说的是"`plugins/core` 下的插件"，目录本身就是权威；manifest 声明可伪造，而记忆里的既有决议是"官方清单由后端数据库维护"，本就不靠本地自证。

### Gap 2 — 「面板插件」没有正式契约

现在它只是一个 `role: "core"` 字符串（`ManifestLoader.cpp:134-142`，`main.cpp:504`）+ 一个 `descriptor->rootWidgetClass`（`main.cpp:527`）。没有任何校验保证：

- 面板插件**必须**提供 `rootWidgetClass`；
- 面板插件**必须**唯一；
- 没有面板插件时程序必须仍能启动（README 验收清单第一条）；
- 面板插件缺席时，其它插件的注册应当被驳回而非崩溃。

命名也建议一并改：`role: "core"` 与源码目录 `plugins/core/` 是两件事，容易混淆。建议 `role: "panel"`。

### Gap 3 — 「注册区域（region）」机制缺失

这是最大的一处。你的原话：

> 这个面板插件决定了当前项目有哪些地方可以注册，比如说菜单栏，内容等。

**当前 core 能注册的地方只有两处，且都是硬编码的**：

| 区域 | 现状实现 | 可扩展性 |
| --- | --- | --- |
| 内容区 | `PageRegistry` + `GoldingPageDecl` | 插件可注册，但仅限"整页"粒度 |
| 设置页 | `SettingsRegistry` + `GoldingSettingsDecl` | 插件可注册区块 |
| **菜单栏** | shell 内部两个硬编码按钮（`refind` 里查的是 `shell.top` 的子节点） | **零扩展性** |
| 侧栏 / 状态栏 / 工具栏 | 不存在 | 无 |

也就是说：**"面板插件决定有哪些区域"这件事，被两张特化表替代了**。加一个新区域（比如左侧启动栏，HANDOVER §4 #4 记着这件事）就得改 core 或改壳，而不是加插件。

**建议的契约形状**（沿用现有 C ABI + 静态类 + 同名驳回的风格）：

```c
/* 面板插件声明它提供哪些区域 */
typedef struct GoldingRegionDecl {
    const char* id;        /* "menubar" / "content" / "sidebar" / "settings" */
    const char* title;     /* 诊断用 */
    int order;             /* 同层内排序 */
} GoldingRegionDecl;

/* 任意插件往区域里放东西 */
typedef struct GoldingRegionItemDecl {
    const char* regionId;
    const char* id;        /* 区域内唯一 */
    const char* title;
    const char* icon;      /* 可为空 */
    int order;
    GoldingWidgetHandle (*create)(void* hostContext, void* parent);
    void (*onActivate)(void* hostContext);   /* 可空：菜单项只有动作不画界面 */
} GoldingRegionItemDecl;
```

HostApi 增四个函数指针：

| 函数 | 谁调 | 作用 |
| --- | --- | --- |
| `registerRegion` | 面板插件 | 声明区域 |
| `regionCount` / `regionAt` | 面板插件 | 枚举本机所有区域 |
| `registerRegionItem` | 普通插件 | 往某个区域投递条目 |
| `regionItemCount` / `regionItemAt` | 面板插件 | 取出某个区域的条目并按 `order` 排 |

三条处置规则（与既有原则一致）：

1. 往**不存在的区域**注册 → 返回 `GOLDING_REGISTER_INVALID`，宿主记诊断，**不中断该插件其余注册**（"一个插件坏了只影响它自己"）。
2. 面板插件缺席 → 区域表为空 → 所有 region item 注册失败，但**程序正常启动、插件档案仍完整**（README 验收清单第一条）。
3. 区域内的条目**只由面板插件消费**，core 不解释 `regionId` 的语义。core 只保证"表能存、能查、同名驳回"。

`PageRegistry` / `SettingsRegistry` 的归位有两条路，可择一：

- **渐进**：保留两张表，`content` / `settings` 作为约定区域名由面板插件自行实现，core 不感知。改动最小，但 core 里仍有特化代码。
- **归并**：把页面与设置区块降为 `content` / `settings` 区域的条目，`GoldingPageDecl` / `GoldingSettingsDecl` 删除。core 彻底不认识"页面"和"设置"。改动大，但符合"core 只提供机制"。

建议：**先做渐进，dev001 站稳后再归并**。

---

## 5. 待完成事项

### P0 — 搬去 `dev001` 之前必须做

| # | 事项 | 落点 | 理由 |
| --- | --- | --- | --- |
| P0-1 | **提交当前全部改动** | git | `git log` 只有 2 个提交，`git checkout` 一次全部作废 |
| P0-2 | **拆 `host/main.cpp`** | 新增 `host/HostApp.{h,cpp}`（编排 + 回调）、`host/SelfTest.cpp`（自检 + 截图） | 现在产品与测试同体，无法作为库搬迁 |
| P0-3 | **把重建状态从全局量搬进 `HostApp` 实例** | `HostApp` 成员 | 重加载的前提；也是"一次进程一个实例"的解除 |
| P0-4 | **自检不再引用 shell 节点名** | `SelfTest.cpp` 改成"只断言 core 自己的契约"（根节点铺满、页面表非空、Region 表可枚举） | 违反"换面板插件宿主不动" |
| P0-5 | **`state` 换成枚举** | `include/Application/PluginState.h`，SDK 里同步注释 | 现在是 0..5 魔数，跨文件口头约定 |
| P0-6 | **删掉或启用 `manifest.abiVersion`** | 二选一：参与校验（与 descriptor 比对，不一致则判 Invalid）或从必需字段移除 | 双源已脱节（6/8 个插件写着 4），留着必然误导 |
| P0-7 | **修 `LoadedEntry::api.hostContext` 的悬空风险** | 把 `provider` 搬进 `std::deque` 或改存插件序号 | 见 D6，现在靠"刚好没扩容"活着 |

### P1 — 核心补齐（dev001 的主线）

| # | 事项 | 对应 Gap | 判断标准 |
| --- | --- | --- | --- |
| P1-1 | **插件来源分层** | Gap 1 | 内置插件装到 `plugin/core/`，`PathFinder` 返回有序来源，面板插件优先取 Bundled |
| P1-2 | **面板插件正式化**：`role: "panel"` + 唯一性校验 + 缺席兜底 | Gap 2 | 删掉 shell 后程序仍启动、档案完整、界面为空、退出码 0 |
| P1-3 | **Region 契约进 SDK（ABI v6）** | Gap 3 | 菜单栏可由第三方插件注册一项并点击生效 |
| P1-4 | **Resolver 结论回流可用性** | D4 / D5 | 缺依赖/在环上的插件 `state` 标为不可用且不 entry；`cycle` 拆成 `cycleRoots` 与 `blocked` |
| P1-5 | **`apiVersion` 校验** | D2 | 插件声明高于宿主支持 → 拒绝加载并说明缺哪项 |
| P1-6 | **重加载链路接通** | `close()` 零调用者 | 四张表 `clear()` + `PluginLoader::close()` 全部接上，支持"魔法阵启停" |
| P1-7 | **注册表暴露 provider** | S5 | `providerOf(name)` 可查，诊断日志能答"谁注册的" |

### P2 — 后续

| # | 事项 |
| --- | --- |
| P2-1 | `PluginNode` 与 `PluginLibrary` 合并为单一构造（消掉 `main.cpp:387-439` 的重复） |
| P2-2 | `PluginLoader` 保留 `GetLastError()`，`LoadStatus` 拆出 `ArchMismatch` / `DependencyMissing` |
| P2-3 | `GoldingOverlayDecl` 要么进 HostApi，要么从 SDK 删除（D7） |
| P2-4 | `GOLDING_DECLARE_PLUGIN` 目前只支持单个 widget / page / section（`GoldingPluginBase.h:176-179`），多页面插件得走 `onEntry` 手写 |
| P2-5 | 纯数据插件 `widgets{}` 的装配路径（manifest 已读，无消费者） |
| P2-6 | 平台层：`PluginLoader` 与 `PathFinder` 的 `#else` 分支是空实现（`PluginLoader.cpp:75-79`），跨平台需补 dlopen |

---

## 6. 搬迁到 `dev001` 的前置条件

**边界已定（2026-10-23）**：`dev001` 只承载 `golding_core` + 插件开发包，不含 `host/`、`plugins/`、
`third_party/`。理由、代价与 `HostApp` 的归属见 `docs/Options.md` §13。

五条前置条件现状：

1. 全部改动已提交 —— **已完成**（`0a0bb41` / `ba8f31f` / `f9a1321`，工作区干净）。
2. `main.cpp` 已拆出 `HostApp` —— **已完成**（`main.cpp` 284 行；`HostApp.cpp` 780 行，零 UI 依赖）。
3. 自检不引用具体插件的节点名 —— **已完成**（`grep -c "shell\." host/main.cpp` = 0；`selftest`
   没有 `depends`，只断言对任何面板都成立的契约）。
4. `state` 有枚举、`abiVersion` 不再双源 —— **已完成**。
5. `dev001` 承载边界 —— **已定**，见上。

**"开发包"形态三件的处置（2026-10-23 夜）**：

| # | 动作 | 结果 |
| - | - | - |
| M-1 | 插件源码支持仓库外路径 | **不做**：插件各有专属文件夹（`plugins/core/<name>/`、`plugins/dev/<name>/`），按名字拼路径就是对的 |
| M-2 | `CMakeLists.txt` 按 core / 宿主 / 插件拆开 | **已完成**：根（只有 core + 单测）/ `host/` / `plugins/` + `cmakes/GoldingPlugin.cmake`。开发包检出即使误开 `ON` 也只打一行 STATUS 后照常构建 core |
| M-3 | 零 UI 的最小插件样例 | **待定**：就是"只声明能力 + 在 `entry` 里注册服务、一行 InkingUIKit 都不 include"的插件。不做的话，"纯逻辑插件不被迫依赖 UI 库"只有头文件层面成立，没有活样本 |

验收（已实测）：只含 `include/ src/ tests/ cmakes/ CMakeLists.txt` 的检出，
`-DGOLDING_WITH_INKING=OFF` 与 `ON` 两种配置都 2/2；完整仓库 12/12。

M-2 踩到的两个坑，都写在 `Options.md` §13：`golding_host` 必须用 `RUNTIME_OUTPUT_DIRECTORY`
钉在顶层二进制目录（否则 exe 落 `build/host/`，`PathFinder` 一个插件都扫不到）；
`golding_add_plugin` 的源码参数按仓库根相对写、函数里补 `CMAKE_SOURCE_DIR` 前缀
（它现在被 `plugins/CMakeLists.txt` include，`CMAKE_CURRENT_SOURCE_DIR` 是 `plugins/`）。

**不在搬迁范围内的两条**（都属于"实现选择"而非"能不能搬"）：卸载/重载链路（`PluginLoader::close`
零调用者）与 `PluginRegistry` 收窄成"面板根工厂"。前者要么补栈式撤销、要么在 `dev001` 上明确
"不支持热重载"；后者只是接口形状，越早改越便宜。

---

## 7. 处理进度（2026-10-22 21:40 更新）

已完成（提交 `44f3e14` 及后续）：

| 项 | 结果 |
| --- | --- |
| P0-1 提交 | `8b26550` 落地，工作区此后保持干净 |
| P0-5 state 枚举 | 新增 `Application/PluginState.h`；**顺带修掉一个真 bug**：`state` 在 entry 调用之前计算，`!entry.entryOk` 恒为真，所有加载成功的插件实际都被标成 EntryFailed |
| P0-6 abiVersion 双源 | `Manifest` 删除该字段，8 个 `plugin.json` 同步删除；唯一来源是 descriptor |
| P0-7 hostContext 悬空 | `loaded` 改 `std::deque` |
| P1-1 来源分层 | `DataStruct/PluginSource.h`（`PluginTier`），`PathFinder` 返回有序 `sources`（Bundled 在前），CMake 装到 `plugin/core/<name>/`，同名外部插件标 `shadowed` 让位 |
| P1-2 面板插件 | `role: "panel"`；选择规则=只认装成功的 + 首个命中（即内置优先）+ 多个时告警；缺席时界面为空、档案完整、退出码 0 |
| P1-3 Region 契约 | ABI v6。`GoldingRegionDecl` / `GoldingRegionItemDecl` + 6 个 HostApi 函数 + `RegionRegistry`（第五张表）。shell 声明 `menubar`，home 往里放"首页"入口，顶栏从两个写死按钮变成三个（第三个来自插件） |
| P1-4 Resolver 回流 | `ResolveResult` 改为 `order`（只含能装的）+ `unresolved`（下标/原因/缺失名），区分 `InCycle` / `DependencyUnavailable`；结论用 `PluginLibrary::setState` 回灌为 `Unresolved` |
| P1-5 apiVersion 校验 | 数值最小版本；宿主声明 `GOLDING_CORE_API_VERSION`；高于则拒绝 + `ApiTooNew(7)` |
| P1-7 provider 暴露 | 四张表各加 `providerOf`（Region 分 region/item 两个） |
| P0-2 拆 `main.cpp` | 新增 `host/HostApp.{h,cpp}`：扫描/加载/建档/解析/entry/选面板，**无 InkingUIKit 依赖**，可无头自检。main 只剩建窗口+装配+自检。`requestRebuild` 由 main 经 `setRebuildHandler` 注入 |

顺带修的：坏插件也进档案表（此前只有 manifest 通过且有 dll 的才登记，与"能列出全部插件"的验收项矛盾）；
自检断言「依赖图无环」改成不变量「每个插件都有去处」，因为缺依赖是可接受的运行状态，不是机制失败。

仍待做（2026-10-23 核对后只剩一条）：

| 项 | 现状 |
| --- | --- |
| P1-6 重加载链路 | `close()` / 各表 `clear()` 仍零调用者。**前置已解除**：`requestRebuild` 通道在 `hardening` T-05 整条删除（全仓 0 命中），`main.cpp` 里已无重建状态 |
| ~~P0-3 重建状态去全局~~ | 随 `requestRebuild` 删除一并消失，不再是待办 |
| ~~P0-4 自检不引 shell 节点名~~ | 已完成：`grep -c "shell\." host/main.cpp` = 0，自检已插件化且无 `depends` |

Region 落地时**没有**把 `PageRegistry` / `SettingsRegistry` 归并进去——那是刻意选择：
先让机制存在并可验证，归并留到 `dev001` 站稳之后。当前是"机制 + 两张特化表并存"的过渡态。

**拆分后的边界（重要）**：

- `host/HostApp.{h,cpp}` = 产品编排（阶段 1），纯 C++，**不依赖 InkingUIKit**，可无头自检。
- `host/main.cpp` = 命令行解析 + 建窗口装配（阶段 2）+ 几何/行为自检（阶段 3），依赖 InkingUIKit 单例，**仍硬编码 shell 节点名**（`shell.root` / `shell.top` 等），这是 P0-4 的遗留。
- `requestRebuild` 是界面层能力，由 main 经 `HostApp::setRebuildHandler` 注入（HostApp 不认识界面）。

---

## 附：本次梳理的确定性边界

- 所有结论基于源码静态阅读，**未重新构建、未跑自检**。HANDOVER 记录"8 种模式全部失败项：0"，该结论未在本轮复核。
- 行号引用对应 2026-10-22 13:20 的文件状态。
- `plugins/` 侧的问题（`store` 卡片无点击、`magic.json` 未读取、左侧栏未做）不在本报告范围，见 `docs/HANDOVER.md` §4。
