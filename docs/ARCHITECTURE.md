# GoldingCore 结构与原理

这份文档回答两个问题：**东西都在哪**（结构），**为什么是这样**（原理）。
它不重复接口字段清单，也不重述踩过的坑——那些各有专文（见下表）。

| 文档 | 回答什么 | 什么时候读 |
| --- | --- | --- |
| `README.md` | 怎么构建、怎么跑、怎么写第一个插件 | 上手 |
| 本文 | 谁在哪一层、谁认谁、为什么这样切 | 改动前、交接时 |
| `API.md` | C ABI / SDK / core 公开接口的字段级说明 | 查签名 |
| `NOTES.md` | 必须知道的边界与坑 | 出问题 |
| `Options.md` | 已定决策与它们排除掉的路 | 想推翻某个设计之前 |
| `SHAPE.md` / `CURSOR.md` | 两类数据文件（法阵形状、用户指针）的用户侧格式 | 改数据 |
| `TASK-COMMANDS.md` | 命令面板的规划与不做清单 | 改 `commands` |

---

## 1. 全景

四个部分，依赖方向是单向的：

```
            ┌──────────────────────────────────────┐
            │  golding_host.exe      (host/main.cpp)│
            │  命令行 → 四阶段 → 建窗 → 跑帧 → 退出码 │
            └──────┬───────────────────────┬────────┘
        链接 core  │                       │ 链接 inking::inking
                   ▼                       ▼
        ┌────────────────────┐   ┌────────────────────────┐
        │ libgolding_core.a  │   │ InkingUIKit（预编译包） │
        │ include/ + src/    │   │ MinGW ABI，不给 MSVC   │
        └─────────┬──────────┘   └───────────▲────────────┘
                  │ 只有两个 C 符号            │
                  │ descriptor / entry         │
                  ▼                            │
        ┌────────────────────┐                 │
        │ plugin/core/*/*.dll│─────────────────┘
        └────────────────────┘
```

三条铁律，任何改动都不该破坏：

1. **core 不 include InkingUIKit。** 于是 `GOLDING_WITH_INKING=OFF` 也能编、也能跑单测，
   "core 可无头自检"不依赖预编译包是否装得上。
2. **插件不链接 core。** 插件只拿 `include/sdk/` 两个头（可选再加 InkingUIKit），
   与宿主之间只有 C ABI。core 的 C++ 类是宿主的实现细节。
3. **宿主是唯一同时认识两边的地方。** 而且只有 `host/main.cpp` 一个文件——
   宿主不认识任何插件名、节点名、区域名、页面 id。

推论：**换掉 `plugin/core/` 里的面板插件，界面就换一套，exe 一个字节不动。**
这条是全项目最硬的一条验收语，下面几乎所有设计都在为它服务。

---

## 2. 目录结构

| 路径 | 内容 | 改这里会波及 |
| --- | --- | --- |
| `include/sdk/` | 插件的全部公开面：`golding_plugin.h`（C ABI）、`GoldingPlugin.h`（C++ 基类 + `Host`） | 所有插件（ABI 事件） |
| `include/Application/` | core 的类：`HostApp` / 五张表 / 扫描 / 加载 / 解析 / JSON | 宿主、单测 |
| `include/DataStruct/` | 纯数据：`Manifest`、`PluginSource` | 扫描与加载 |
| `include/Tool/` | `PathFinder`（路径定位与 UTF-8 转换） | 全项目 |
| `include/HostApp.h` | 启动编排类的公开面 | 宿主 |
| `src/Application/`、`src/Tool/` | 上面那些类的实现，编成 `golding_core` 静态库 | — |
| `host/` | 宿主 exe：`main.cpp` + 找 InkingUIKit 的 CMake | 产品的可执行行为 |
| `plugins/core/<名字>/` | 随宿主分发的内置插件（每个一个目录） | 产品功能 |
| `plugins/common/` | **服务契约头**：`GoldingServices.h` / `GoldingMenu.h` / `GoldingCommands.h` / `GoldingSettings.h` / `GoldingCursor.h` | 提供方与消费方必须同步 |
| `cmakes/GoldingPlugin.cmake` | `golding_add_plugin()`：编译 + 安装到运行时目录 | 所有插件构建 |
| `tests/` | `core_tests`（core 纯函数）、`sdk_host_tests`（跨 dll 契约） | — |
| `docs/` | 本目录 | — |

构建产物落在 `<build>/plugin/core/<名字>/`（`plugin.json` + `bin/<名字>.dll` + 可选
`shape.json` / `assets/`）。**运行时目录本身就是"内置插件"这个事实**，不是另外记一份清单。

---

## 3. core

### 3.1 它认识什么、不认识什么

| 认识 | 不认识 |
| --- | --- |
| `plugin.json` 的九个键、dll 的两个导出符号 | 窗口、场景、控件、布局 |
| 注册表：页面 / 设置区块 / 区域与区域项 / 服务 / 插件档案 / 面板根槽位 | 任何具体区域名（`menubar`、`sidebar` 都不是 core 的词） |
| 依赖图、能力声明与运行时注册 | 页面内容、菜单层级、指针样式 |
| 拓扑序、失败状态码 | 任何插件名（除了日志里打印） |

这张表就是"core 能做多小"的定义。想把某个概念加进 core 之前，先问它是否属于第二列。

### 3.2 启动时序

一次进程一条直线，六个阶段。**顺序不能换**，每一步的理由写在下面。

```
discover ──► prepare ─────────────────────────────► registerAll ──► 装配 ──► notifyReady ──► 跑帧
 定位来源     扫描 → 加载 dll → 建档 → 依赖解析 → 校验 needs       entry           宿主建界面      插件自检
```

| 阶段 | 入口 | 做的事 | 失败语义 |
| --- | --- | --- | --- |
| 1a 定位 | `HostApp::discover()` | `PathFinder` 找 `<exe>/plugin/core/`（内置）与 `<exe>/plugin/`（外部），内置在前 | 扫描故障 → 退出码 1；没有插件是**合法**的 |
| 1b 准备 | `HostApp::prepare()` | 扫描 → 同名让位 → `apiVersion` 闸门 → 打开 dll 并比 ABI/SDK → 建档 → 拓扑 → 静态能力校验 | 只允许调用一次；单个插件失败只降级它自己 |
| 1c 注册 | `HostApp::registerAll()` | 按拓扑序填 `HostApi` 并调 `entry`；随后 `validateProvides()` + `selectPanel()` | 单插件 entry 失败 → `EntryFailed`，其余照装 |
| 2 装配 | **宿主** | `setBootMode` → `SetDesignSize` → 窗口配置 → 场景 → 面板根工厂 `create` → `setUiRoot` | 设计尺寸被拒 / 窗口打不开 → 1 |
| 3 自检 | `HostApp::notifyReady()` | 按拓扑序调每个插件的 `ready`，汇总失败项数 | 断言失败 → 2（不撤销任何注册） |
| 4 跑帧 | **宿主** | 给 `--frames=N` 就跑 N 帧退出，不给就常驻 | 退出码如实反映阶段 3 的结果 |

几处顺序的理由（每一条都曾经是 bug）：

- **`apiVersion` 闸门在打开 dll 之前。** 不信任的二进制连 `LoadLibrary` 都不该发生。
- **ABI 闸门在进 C++ 之前。** ABI 不对时调用 `entry` 的表现是崩溃，不是报错。
- **`needs` 校验在 `entry` 之前。** 放到之后就只能看到"已经注册成功的"，而那正是它要防的静默失败
  （插件拿着 `nullptr` 自己降级，谁也不报错）。
- **`setBootMode` 在装配之前。** 面板的 `create` 里要按它决定初始形态（`--collapsed`），
  晚设一步插件读到的是空串。
- **`SetDesignSize` 在任何场景/窗口构造之前。** 设计尺寸在那一刻烘进根锚点，之后会被拒。
- **`validateProvides` 在 `selectPanel` 之前。** 否则一个声明错的面板插件会被选上，
  然后在几百行之外装配失败。

### 3.3 模块逐个看

#### `HostApp`（`include/HostApp.h`）

编排者。它不建窗口、不画界面，因此可以无头自检。内部只做记账：

- `_loaded`：**每个**被扫到的插件一行（含坏掉的），`LoadedEntry` 里有 `api`、
  `entryOk`、`blocked`；
- `_nodes` / `_nodeToLoaded`：只有"过了加载阶段"的插件才进图，两套下标**不是一回事**；
- `_libraryIndex`：`_loaded` 下标 → 档案表下标，回灌状态用。

`prepare()` 用 `_prepared` 挡重复调用：不是怕崩，是怕**静默留下半个旧世界**
（容器都是追加的，第二轮会把同一批插件再装一遍，而注册全被驳回）。

析构**故意不卸载 dll**。`FreeLibrary` 会跑插件静态析构，而五张表仍持有指向 dll 数据段的
`const TDecl*`，谁先谁后没有一处代码能保证。所以"重载 = 重启进程"。

#### 路径与扫描

- `PathFinder`：`executableDir()` → `pluginDir()` → `plugins()`。
  两层来源，层内按路径排序（面板"取首个"因此是确定的，不是目录迭代顺序）。
  `resolve(base, relative)` 三条规则：绝对路径拒绝、`..` 逃出拒绝、按词法归一但**不查存在性**。
  `toUtf8` / `fromUtf8` 是唯一正确的 UTF-8 通道（`path.string()` 在 MinGW 下遇中文路径会抛）。
- `PluginScanner`：逐个来源读 `plugin.json`。名字是唯一键，**先到先得**，
  后到的标 `shadowed` 保留在结果里（由调用方决定怎么报，不悄悄丢）。
- `ManifestLoader` + `Json`：读 `plugin.json` 并校验。顶层是**白名单**九键
  （`name` `title` `version` `apiVersion` `role` `requires` `provides` `needs` `targets`），
  写别的键判 `Invalid` 并在 `detail` 里点名。`dllPath` 在读到时**当场**解析成绝对路径，
  所以下游消费者不必知道基准目录是谁。JSON 解析器是自写的、不对外（`Options.md` §12）。

#### `PluginLoader`

`LoadLibraryW` → 取两个符号 → `descriptor()` → **先比 `abiVersion`（严格相等）**
→ 再比 `sdkId`（严格相等，空指针算不匹配）→ `sdkVersion` 只记不拒。
`callEntry` 是另一支，`open` 成功之后才可能被调到。

#### `PluginLibrary`（档案表）

core 侧"有哪些插件"的**唯一**事实来源。魔法阵、宿主日志、依赖/能力查询都只读它。
字符串单独存在 `deque` 里，保证 `c_str()` 指针全程有效（`vector` 扩容会搬走指针）——
这条直接支撑了 `pluginDir` / `pluginDependencyAt` 这类跨 dll 返回 `const char*` 的接口。

#### `Resolver`

纯函数，不碰文件系统。三步：**缺失检测**（依赖名找不到 → 判死，不参与拓扑）
→ **Kahn 拓扑**（同层保持输入顺序）→ **区分环与连带**（没进 order 的节点，
用朴素 DFS 判断"能否回到自己"）。输出 `order` **只含能装的插件**，调用方不必再判可用性。

三种失败原因分开记，因为责任人不同：

| 原因 | 含义 | 排查动作 |
| --- | --- | --- |
| `MissingDependency` | 依赖的名字没人用 | 查拼写 / 查那个插件是否被 shadowed |
| `InCycle` | 自己在环上 | 先拆环，别查它的依赖 |
| `DependencyUnavailable` | 依赖了装不上的插件 | 往上找根因 |

**处置三者相同**：那个插件标 `Unresolved`，其余照常装配。

#### 五张表 + 一个槽位

| 名字 | 形态 | 谁写 | 谁读 |
| --- | --- | --- | --- |
| `PanelRootRegistry` | **单槽位**（容量 1） | 面板插件（`role: "panel"`，注册口就拦） | 宿主（`root()`） |
| `PageRegistry` | `SlotTable<GoldingPageDecl>` | 任意插件 | 面板插件 |
| `SettingsRegistry` | `SlotTable<GoldingSettingsDecl>` | 任意插件 | `settings` 插件 |
| `RegionRegistry` | 区域表 + 每区域一张 `SlotTable<GoldingRegionItemDecl>` | 面板插件声明区域，任意插件放条目 | 面板插件 |
| `ServiceRegistry` | 名字 → vtable | 任意插件 | 任意插件 |
| `PluginLibrary` | 档案表（见上） | core | 全部 |

`ServiceRegistry` 之上还叠了一层 `CapabilityRegistry`（下一节）。六处是同一套写法：
静态类 + 函数内静态量 + 同名驳回 + `RegistryStats` 计数。

`SlotTable` 是三张表的同一个模板，规则做成显式参数，避免"排序规则抄三遍"：

| 规则 | 页 / 设置 | 区域条目 | 说明 |
| --- | --- | --- | --- |
| 同 `order` 时 | `ById` 字典序 | `ByArrival` 先到先得 | 前者要让"谁先加载都一样"，后者因为重排会让用户眼前跳一下 |
| `requireCreate` | 是 | 否 | 纯动作条目（菜单项）没有 `create` |
| `allowDuplicateIds` | 否 | 由区域自己声明 | "最近打开"这类槽位可能需要重名 |
| `scope` | 空 | `"区域 menubar 内"` | 驳回原因要能定位到哪个区域 |

#### `RegistryStats`

四个数：接受数、驳回数、最后一条驳回原因（人话）、表长。存在的理由是**可断言**：
"应当驳回 3 次"在自检里能写成一行，而不必人肉看日志。

#### `PluginState`

跨 dll 的整数契约（`GoldingPluginInfo.state`），宿主内部用枚举、只在填档案时转 int。
只描述结果，不替调用方判断：`Unresolved` 同时覆盖"缺依赖"与"成环"，
想知道哪一种去看 `ResolveResult::unresolved`。**编号 4 保留为空缺、不重排**——
这些整数是历史日志与插件侧判断的锚点。

### 3.4 数据与身份的划分（ABI v11 的核心）

同一个事实只能有一个权威来源。按这个标准，插件的属性被切成两半：

| 属性 | 权威来源 | 何时可读 | 为什么 |
| --- | --- | --- | --- |
| 名字 / 版本 / 标题 / role | `plugin.json` | 扫描阶段 | 宿主不必等 dll 就能建拓扑、选面板 |
| `requires.plugins` / `provides` / `needs` | `plugin.json` | 扫描阶段 | 依赖图与能力声明必须**在加载 dll 之前**成立 |
| `targets.win64.lib` | `plugin.json` | 扫描阶段 | 不读它不知道怎么打开 dll |
| `apiVersion`（需要的 core 能力版本） | `plugin.json` | 扫描阶段 | 参与"要不要打开 dll"的判断 |
| `abiVersion` / `sdkId` / `sdkVersion` | **dll 里的 descriptor** | 加载之后 | 它们是编译器的产物；写进 json 必然与二进制脱节 |
| `ready` 钩子 | dll 里的 descriptor | 加载之后 | 行为，不是数据 |

`abiVersion` 曾经在 json 里也有副本，结果是 8 个插件里 6 个写着旧版本而无人发现。
现在它只剩 descriptor 一处，`plugin.json` 写 `abiVersion` 会被白名单拒掉。

### 3.5 能力校验的两层

`CapabilityRegistry` 刻意保留两层，不合并：

1. **声明面**（`declare`）：扫描完把每个插件 `provides` 的名字与版本汇总。没有 vtable，
   所以"我要的能力有没有人**声称**提供、提供到第几版"在 `entry` 之前就能判。
   → `validateNeeds()`：默认严格（缺能力或版本不足 → 该插件 `Unresolved`，
   **不装配**，其余照常）；可切放任（只留痕）。
2. **运行时面**（`registerCapability`）：插件在 `entry` 里真正注册的 vtable，落到
   `ServiceRegistry`，另记提供方。→ `validateProvides()` 双向校验：

| 情形 | 判定 | 理由 |
| --- | --- | --- |
| 声明了 `provides` 却没注册 | 该插件判 `EntryFailed` | 说得到做不到 = 它坏了 |
| 注册了却没声明 | 允许，只打一行提示 | 内部能力（不打算给别人用的服务）是合法形态 |

版本语义：`provides` 的版本 0 = "没写版本"，不能当成"第 0 版"放行；
`needs` 的版本 0 = "不关心版本"。`Host::Use<T>(name, minVersion)` 的三种结果
（缺能力 / 版本不足放行 / 正常取用）在日志里必须可区分。

### 3.6 一次注册怎么走到底

```
插件代码                SDK（编进插件 dll）          宿主（编进 exe）
host.AddPage(decl)  →  report(...)             →  hostRegisterPage(hostContext, decl)
                       ↑ 留一行日志                 ↓ context → provider 名字
                                                   PageRegistry::registerPage(decl, provider)
                                                     └ SlotTable::insert(...)
```

三个要点：

- **`hostContext` 就是 `&entry.provider`**（一个 `std::string*`，住在 `deque` 里所以地址稳定）。
  它是"谁在注册"的唯一标识。
- **`mountPage` / `mountSettingsSection` 会反查提供方**：`create` 收到的 `hostContext`
  必须属于**页面 / 区块的提供方**，不是调用方（面板）。否则页面插件拿别人的身份查目录就是错的。
  这张映射表 `providerContexts()` 在 `registerAll` 时逐次重建。
- **只有面板插件能注册面板根**，闸门在注册口而不是装配时——否则"界面空、日志全绿"。

### 3.7 失败模型

| 情形 | 结果 | 退出码 |
| --- | --- | --- |
| 插件目录扫描故障 / `prepare` 失败 / 设计尺寸被拒 / 窗口打不开 | 启动期硬失败 | 1 |
| 某个插件 manifest 坏、dll 打不开、ABI 或 SDK 不匹配、apiVersion 过高 | 该插件不进图，其余照常 | 0（可能仍是全绿） |
| 某个插件缺依赖 / 成环 / 被连带 | 该插件 `Unresolved`，其余照常 | 0 |
| 某个插件缺能力（严格模式） | 该插件不装配，其余照常 | 0 |
| 某个插件 entry 返回 false 或抛异常 | `EntryFailed`，其余照常 | 0 |
| 某个插件 `ready` 报失败项 | 计数累加，**不撤销注册** | 2 |
| 一个插件都没扫到 | 界面为空，档案为空——**合法状态** | 0 |

贯穿全表的一条：**降级粒度是单个插件**。"一个插件坏了只影响它自己"不是口号，
`validateNeeds` 里就写着为什么不做硬失败——硬失败会让一个能力缺席带崩整条链。

`entry` / `ready` 外面的 `try/catch` 是**尽力兜底，不是隔离**：SEH（空指针写、除零）
在 MinGW 下不经过 `catch(...)`，异常要穿过 dll 边界还要求两侧同一个运行库。

### 3.8 ABI 规则

当前：`GOLDING_PLUGIN_ABI_VERSION = 13`、`GOLDING_CORE_API_VERSION = 4`。

- **改任何跨 dll 结构体 → ABI +1**，且必须整批重编（不向后兼容）。
- **只许末尾追加字段**，不许插中间、不许改已有字段类型——中间插一个会让所有旧结构体错位。
- `golding::Plugin` 只有 `Register` / `Ready` + 虚析构，之后冻结。
  **所有注册面都长在 `Host` 上**：加一种注册不动任何插件的虚表。
- 宿主**永不调插件对象的虚函数**。跨 dll 只有两个 C 符号；虚调用全部发生在插件 dll 内部
  （`detail::EntryThunk` / `ReadyThunk`），所以 Debug/Release 混编也不会崩。
- `GOLDING_SDK_ID` / `GOLDING_SDK_VERSION` 的真源是根 `CMakeLists.txt` 的 `project(VERSION)`，
  经 `target_compile_definitions` 传下去。头里的 `#ifndef` 只是兜底，兜底值被单测断言"不等"。

`apiVersion` 与 `abiVersion` 分工：前者是"我用了哪些 core 能力"（宿主判"给不给得起"），
后者是"二进制布局是否一致"（宿主判"敢不敢碰"）。

---

## 4. 插件体系

### 4.1 一个插件的组成部分

```
plugin/core/<名字>/
├── plugin.json      描述（必需）：名字/版本/role/依赖/能力/dll 路径
├── shape.json       可选：魔法阵里的形状（spellbook 读）
├── assets/          可选：自己的资源（面板的 grimoire.svg 等）
└── src/*.cpp        实现（可多文件）
```

运行时是 `<build>/plugin/core/<名字>/{plugin.json, bin/<名字>.dll, shape.json, assets/}`。
`plugin.json` 里的 `targets.win64.lib` 写相对路径（`bin/<名字>.dll`），
由 core 解析成绝对路径——插件作者不知道安装到哪个盘。

外部插件放 `<exe>/plugin/<名字>/`。**同名时内置赢**，外部那份标 `shadowed` 跳过：
"目录即事实"，不需要额外配置。

### 4.2 代码骨架

```cpp
class MyPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override;   // 注册；false = 自己认为没装成
    int  Ready(const golding::Host& host) override; // 装配后自检；返回失败项数
};
GOLDING_EXPORT_PLUGIN(MyPlugin)
```

三条纪律（都是踩出来的）：

1. **回调要活到进程结束。** `Host` 只有两个指针，按值存成文件级静态量是标准用法——
   点击 / 快捷键 / `create` 回调都不在 `Register` 的调用栈上。
2. **`Register` 里碰不得 `InkingWindow::Instance()`。** 单例首调会把设计尺寸烘死，
   宿主随后 `SetDesignSize` 被拒 = 启动期硬失败。走窗口层的东西（快捷键 Bind）只能放 `Ready`。
3. **挂上界面树的节点用裸指针，别包 `unique_ptr`。** 节点归父节点所有，再包一层就是
   退出时析构两次。

### 4.3 构建

`plugins/CMakeLists.txt` 只列"有哪些插件"，规则在 `cmakes/GoldingPlugin.cmake`：

```cmake
golding_add_plugin(commands
    plugins/core/commands/src/CommandsPlugin.cpp
    plugins/core/commands/src/CommandPalette.cpp)
```

参数表里 `.cpp/.cc/.cxx` 结尾的是源码，其余是 `plugins/<那一层>/<名字>/` 的层名
（目前都在 `core`）。函数做四件事：

- 建 `SHARED` 目标，链 `golding_sdk` + `inking::inking`，开 `-Wall -Wextra -Wpedantic`
  （插件是进程内代码，一个未初始化变量就是一次崩溃，零警告要当门槛）；
- `POST_BUILD` 把 `plugin.json` / `shape.json` / `assets/` 拷到 `build/plugin/core/<名字>/`；
- 把 `plugin.json` 记成 `LINK_DEPENDS`——否则"改了 manifest 却毫无反应"，且没有任何提示
  （`provides` / `needs` / `role` 都栽在这上面）；
- `add_dependencies(golding_host <插件>)`，让 exe 总是与插件同批更新。

### 4.4 当前插件一览

| 插件 | role | apiVersion | requires | needs | provides | 界面入口 |
| --- | --- | --- | --- | --- | --- | --- |
| `GoldingGamestoryShell` | `panel` | 2 | — | — | `golding.navigation` / `golding.popup` / `golding.menu`（均 v1） | 面板根（顶栏 / 侧栏 / 内容区 / 弹窗 / 菜单） |
| `settings` | 普通 | 4 | Shell | `golding.popup` 1 | `golding.settings` 1 | 顶栏「设置」(order 100) → 菜单 → 单区块模态框 |
| `commands` | 普通 | 4 | Shell | —（可选依赖） | `golding.commands` 1 | 顶栏「命令」(order 40) + `Ctrl+K` |
| `spellbook` | 普通 | 3 | Shell | `golding.popup` 1、`golding.menu` 1 | — | 顶栏「插件」(order 60) → 菜单；两条命令 |
| `cursor` | 普通 | 4 | Shell | —（弹窗可选） | `golding.cursor` 1（语义 v2） | 设置区块「鼠标指针」+ 命令 `cursor.next` |

表里的版本是 `plugin.json` 的 `provides` 声明值。`golding.popup` 的结构体 `abiVersion`
是 **2**（`kPopupServiceVersion`）——声明面写 1、语义要用 2 的地方，`Use` 会出一条
"版本不足，放行"的 WARN，这是实验期故意留的可见痕迹。

依赖关系（`requires` 只表达"装配顺序"，`needs` 只表达"能力前提"）：

```
GoldingGamestoryShell（面板：区域 + golding.navigation / popup / menu）
   ├── settings   提供 golding.settings，挂命令 settings.open
   ├── spellbook  挂命令 spellbook.open / spellbook.folder
   ├── cursor     提供 golding.cursor，挂命令 cursor.next
   └── commands   提供 golding.commands ←── 上面三个往它里面注册
```

**为什么除 `settings` 外都不写 `needs`**：能力缺席时它们只是"少一个入口 / 少几条命令"，
而写了 `needs` 就变成"没有那个服务就装不上"。`settings` 写 `needs: golding.popup`
是因为它注册的区域项要求区域已存在、且它的主功能就是弹窗。

---

### 4.5 `GoldingGamestoryShell` —— 面板插件

**定位**：只提供"地方"，不带任何业务。它认识的四个名词（`menubar` / `sidebar` / 内容区 /
弹窗）全是**它自己定的约定**，core 一个都不认识。

**登记了什么**

| 注册 | 内容 |
| --- | --- |
| `AddWidgetClass` | 面板根工厂 `GoldingGamestoryShell.root` |
| `AddRegion` | `menubar`（order 10）、`sidebar`（order 20），都不允许重名条目 |
| `AddService` | `golding.navigation`、`golding.popup`（语义 v2）、`golding.menu` |

**内部结构**

```
root（静态面板，白底，**不做布局**）
├── menubar（prefab::MenuBar，44 高，hideInFullscreen）
│   ├── ≡ 抽屉开关
│   ├── "Golding Gamestory" 标题
│   └── 各插件的 menubar 区域项（按 order 摆；指针留进 barItems 供菜单定位）
├── body（BodyLayer，动态档，横向 Stack）
│   ├── sidebar（SideLayer，动态档，220 宽 → 收起为 0）
│   │   └── 各插件的 sidebar 区域项
│   └── content（fillMain，纵向 Stack）
│       └── 当前页面（一次只挂一页）
├── 窗口按钮（- / ×，绝对定位，z=200）
├── backdrop（按钮，z=300，常驻但不可见）
├── popup（静态面板，z=400，每次 push 重建）
└── menu 层 0..3（懒创建的 InkingFixedPopup，z=260 / 270 / 280 / 290）
```

**四块为什么这么写**

1. **根不做布局，三块各自定位。** 顶栏全屏时会整条消失（高度 44 → 0），
   用 Stack 纵排会把配置高度固定分给它，顶上永远空一条。
2. **`BodyLayer` 必须是动态档。** 它每逻辑步问一次"顶栏现在占多高"，变了就把自己贴下去、
   高度吃掉剩下的整块。静态面板"几何构造即定型"，改不了自己的几何——那正是
   "全屏后顶上留一条 44px 空白"的来源。它顺带当侧栏动画的节拍器（收起后宽度为 0 的
   侧栏自己收不到 Tick）。
3. **侧栏收起 = 宽度归 0，不是 `SetVisible(false)`。** 布局求解器**不跳过不可见项**，
   隐藏之后它仍占 220、内容区不吃满。而一隐藏它就从绘制列表里消失，展开动画也没法从 0 起步。
   开合走 160ms 的 `EaseOut` 抽屉，动画期间侧栏把内容剪在自己那块里。
4. **窗口按钮挂根上、按绝对坐标贴右上角。** 用 `MenuBar` 自带的 `captionButtons` 会插一根
   `fillMain` 弹簧，弹簧宽度是在"项还没挂全"那次布局里定的——实测把两个按钮顶到画布外。
   z=200 必须高过顶栏（100/101）才看得见，同时低于遮罩（300）所以弹窗一开就被盖住。

**弹窗（`golding.popup` v2）**

- 同一时刻**只有一层**：重复 `push` 是替换，不是叠起来（叠层的层级与焦点规则没人定义过）。
- **内容与关闭键分开**：`create` 收到的是"内容容器"（与弹窗同尺寸、同圆角的静态面板），
  关闭键 `popup.close` 由面板提供并压一层 z。插件重建自己的内容时才不会连它一起删掉。
- **点遮罩不关闭**：遮罩只负责挡界面、吃点击（回调留空而不是不设，否则点空白会穿到顶栏）。
- 声明是**值拷贝**（连 `id` 文本一起）。契约只保证"声明在 `push` 期间有效"，
  存指针的话 `pop` 那一刻读到的就是被覆盖的栈帧——关闭弹窗时随机闪退的根因。
- 旧弹窗**只摘不删**，排到下一帧末销毁（`graveyard`）：`push` 可能就在旧弹窗里某个按钮的
  回调栈上，当场删掉它等于 use-after-free。

**多层级菜单（`golding.menu`）**

层级归**面板插件**，不归库的 `prefab::MenuPopup`：那个第一版明确不做子菜单，
而且两个 `InkingFixedPopup` 并存时会互相当作"点外部"（各自判自己的命中）并互相抢焦点——
父菜单会在点开子菜单的同一帧自我关闭。

所以 `MenuStack` 全权接管：浮窗关掉 `SetCloseOnOutsideClick` 与 `SetTakeFocusOnOpen`，
只当"画框 + 吃点击"的容器；层级、落位、关闭仲裁都在面板里。

- **锚点是区域项 id，不是坐标**：只有面板知道那个按钮在哪，插件只交身份。
- **声明在 `open` 期间有效**：面板打开时把整棵树深拷贝进 `nodes`（子层在父层之前落盘，
  于是"连续下标段"对父子都准）。菜单内容因此可以现场拼（比如设置区块是装配后才有的）。
- 关闭规则：点空白（**只在按着时判**，hover 不算）收起整串；点已展开的项切分支时，
  更深的层先收；点叶子先关整串再执行动作（动作里可能再弹模态框，显隐顺序反了会打架）。
- 上限 4 层（`kMenuMaxLayers`），每深一层 z +10（否则父层容器里的项会盖住子层容器）。

**侧栏与全局键**：`≡` 与 `Tab` 走同一个落点（`toggleSidebar`）。`Tab` 绑在窗口的全局
快捷键表（`MenuBar::BindShortcut`）上，不依赖焦点；代价是它会先被消费掉，输入框拿不到 `Tab`。
`--collapsed` 在装配期用 `SnapTo` 直落——首帧不该是"正在划入"的中间态。

**区域项的两种形态**：只填 `onActivate` 就是按钮（文字由面板填）；填 `create` 就自己画。
顶栏里 `create` 出来的项会排在菜单栏右侧弹簧与窗口按钮之后，要精确位置就用按钮形态。
**侧栏不自动列页面**：页面是内容、侧栏是入口，哪个页面要入口由那个插件自己注册一项。

**`Ready` 断言**（8 条，全部是这块面板自己的契约）：根铺满父级；三块都在；
`content.h + bar.h == root.h`；`content.w + sidebar.w == root.w`（收起时侧栏宽为 0）；
内容区非空；`body.y == bar.h` 且 body 吃到剩余高度；`Tab` 在快捷键表里；
抽屉按时间推进（手动推两拍，验中间值与终值，然后复原）。页面能不能装、条目能不能建
属于**通用契约**，由各自的插件断言——面板不越界。

---

### 4.6 `settings` —— 设置入口

**定位**：普通插件（不是面板）。往顶栏注册一项「设置」，被点后把**别人注册的设置区块**
列成菜单，点某一项才弹那**一块**。它自己不认识任何具体设置项。

**两条路径，一条主一条降级**

| 路径 | 条件 | 行为 |
| --- | --- | --- |
| 浮窗菜单（主） | 拿到 `golding.menu` | `openSettingsMenu("settings.entry")`，每项 = 一个区块 |
| 模态框（降级） | 没有菜单服务 | 直接弹完整设置框（`settings.popup.all`，720×560，居中） |

设置经常是"看一眼就走"的动作，所以主路径用不阻断的菜单浮窗；点外面就收。

**`create` 是 C 函数指针，没有参数位传"点了哪一项"**——`g_onlySectionId` 是个文件级
静态量：点击回调写它，`create` 读它。两次调用在同一条点击栈上，不会串。

菜单项分两类：区块的 `onActivate` 非空就直接执行（ABI v13 的"动作型设置项"，
例如「插件图谱」点一下直接开图），否则弹单区块框（`settings.popup.one`，560×420）。
单区块时标题用那块自己的名字——从菜单点进来的人已经选过一次，再写"设置"会让人以为进错地方。

**服务 `golding.settings`**：`openAll` / `openSection` / `openMenuAt(regionItemId)`，
让别的插件用自己的按钮复用这份内容（比如"设置"项挂在别处）。`openMenuAt` 拿不到菜单时
同样退回弹窗，所以调用方不必判断环境。

**两条依赖分开写**：`requires: ["GoldingGamestoryShell"]` 保证面板先 entry
（区域项要求**区域已经存在**，否则注册被驳回）；`needs: { golding.popup: 1 }`
让"面板不在"在装配前就能判出来，而不是运行时才发现拿不到服务。

---

### 4.7 `commands` —— 命令面板

**定位**：任何插件交一份 `CommandDecl` 就能让它的动作**被搜到、被一键执行**，
不必再往顶栏挤一个入口。它不认识任何具体命令名（除自己内建的两条）。

**两半，一个文件**

- `CommandTable`：命令住哪。深拷贝 `id` / `title` / `keywords`（调用方可以现场拼字符串），
  用 `vector<unique_ptr<OwnedCommand>>` 存——`vector` 扩容会搬元素，而 `at()` 交出去的
  `CommandDecl*` 必须一直有效。
- `CommandPalette`：命令怎么被找到并跑起来。`InkingFixedPopup` + 搜索框 + 结果列表
  （≤ 8 条，每次输入重建）。

**为什么是浮层而不是 `golding.popup`**：命令面板要"点外面就收"，而模态遮罩会把整个界面挡住——
和"快速执行一条命令"的语义相反。

**焦点只给输入框**：`SetKeyboardFocus` 只能给一个节点。给了结果列表，输入框就收不到键、
IME / 中文输入失效。所以 `Enter` 执行**第一条**匹配，其余靠鼠标点，不做上下键选择。

**快捷键只能在 `Ready` 里绑**：`InkingWindow::Instance()` 首调会把设计尺寸烘进根锚点，
而宿主是在 `registerAll()` **之后**才 `SetDesignSize`——在 `Register` 里碰它会直接让宿主
启动期硬失败。绑不上不算失败（`ShortcutRegistry` 先到先得），退回"只靠顶栏入口"。

**自检用真实内建命令当材料**：`commands.open` / `commands.close` 既是功能也是探针。
五条断言走与真机同一条代码路径：`Open()` → 改搜索文本 → **Tick 一帧**（重滤发生在
`onTick`，不是 `SetQuery`）→ 换更窄关键字（2 → 1）→ `Activate("commands.item.0")`
执行 `commands.close`（可观测结果就是面板关了）→ `Activate("commands.entry")` 让面板又开
（把"区域项 → 面板"整条链一起验了）。自检完把面板关掉，否则首帧截图会变。

**注册顺序有个前提**：命令都靠 `Use` 在各自 `Register` 里注册，所以 `commands` 必须
**先于**它们 entry。同层（都只依赖面板）的顺序就是扫描顺序（目录名字典序），
`commands` 排在 `settings` / `spellbook` 之前。顺序万一反过来，结果是"少几条命令 + 一行 WARN"。

---

### 4.8 `spellbook` —— 插件图谱（魔法阵）

**定位**：把所有已加载插件画成一张同心法阵（**只读**）。点某一环，右侧显示那个插件的档案。

**入口**：顶栏「插件」(order 60) → 菜单两项（插件图谱 / 打开插件文件夹）；
命令面板里 `spellbook.open` / `spellbook.folder`。没有菜单服务时直接开图谱（功能不丢），
没有弹窗服务时只打 WARN。

**形状从哪来**：`plugin/<名字>/shape.json`；没有就按**名字哈希**派生（FNV-1a → 稳定色相）。
一个插件 = 若干层，每层是一条极坐标闭合曲线，层间只差几个数（半径偏移、线宽、波瓣、振幅、
转速、颜色）。格式见 `SHAPE.md`。半径撞在一起就是叠成一团，各层带 alpha，叠起来像混色
（`CanvasContext` 没有加法混合）。

**一张图的数据一次算完**（`GrimoireState`）：插件表、形状、依赖、几何分配在 `create` 里
算一次；插件表在 `Ready` 之后运行期不变，这些不该每帧重算。之后每帧只推进两件事：
累计时间（`double`，`float` 跑几小时后 `seconds × speed` 会丢精度）与指针最近层。

**渲染 / 命中 / 包围盒同源**：曲线交给 `ShapeSpec::PolygonPath`（同一份点列，96 点），
描边交给 `StrokeShape`（一半在外一半在内）。

**命中为什么不用"落在哪个带里"**：环带放宽之后本来就会重叠，靠 z 序裁决必然出现
"点着 A 选中 B"。所以点击统一走 `PickNearest()`——选指针最近的那一层。环带按钮的作用
只剩**悬停高亮**（按钮 hover 态就是这一层的环带）。带要按"手指"给而不是按线宽给：
星形/多边形还要更宽（星尖在外、星凹在内，否则只有尖上能点中）。

**缩放/平移后要切渲染路**：SVG 底纹与环带按钮都是 1:1 的静态几何，视图一旦离开 1:1
就把它们藏起来，改用画布自绘的盘（它天生跟着 zoom/pan 走），否则环会跑到盘外面、hover 也会亮错地方。

**详情是"接口占位"**：`PluginDetail` 现在只有宿主档案这一路来源
（`GoldingPluginInfo` + ABI v12 的依赖/能力查询）。将来若由插件自己提供详情，
只需改 `MakeDetail()`，右侧界面代码不动。所有详情文字**一律单行 + 省略号**：
`Stack` 不会因为"文字换行变高"而重排兄弟，多行会把下一段压上来。

**没有插件是合法状态**：`Ready` 只打 WARN，不算失败——图谱画出中心就完了。

---

### 4.9 `cursor` —— 自定义鼠标指针

**定位**：把系统光标藏起来，在**场景**上挂一块跟着指针走的画布，自己画指针；
按下形变 + 涟漪，移动留拖尾；外观可换（内置图案 / 用户自己的图），选择会记住。

**挂载点与三条生死线**

| 决定 | 理由 |
| --- | --- |
| 挂**场景**（不是面板根），z = 900 | 唯一不该被任何东西盖住的图层。面板根内部有自己的层级约定（100/101/200/260/300/400），挂场景上只要比根大即可 |
| 层 = 200×200 动态画布，热点恒在正中心 | 不需要给每种图案各算一次偏移：偏移只有一个值（画布半宽） |
| `SetHitPassThrough(true)` | 它盖在所有东西上面，一旦吃点击，底下的按钮**全部**失灵。`Ready` 里专门断言了这条 |
| 藏原生光标用 Win32 `ShowCursor` | InkingUIKit 不暴露光标管理；`ShowCursor` 是**显示计数**，`SetCursor`（SDL 换形状走的那条路）盖不住它。计数是进程级的，所以停用时必须由本插件 `ShowCursor(TRUE)` 还回来 |

**状态是"电平"而不是"事件 + 动画"**：按下这件事有两路人需要——图案要**持续**变形
（按住就一直是瘪的），涟漪要**一次性**触发。所以存 0..1 的 `press` 电平：形变每帧读它，
涟漪在电平**上升沿**生成一次。两者共用同一份 `press`，不会出现"图案弹回来了但涟漪才刚出来"。

**时钟来自 `onTick`（动态组件每帧一次），画面来自 `Invalidate()`（静态画布"脏了才重画"）。**
特性在这个组合下可以互相独立地开关（`effects` 与图案正交——"要准星但别抖"是常见组合，
写成八种样式就得为每种答一遍要不要特效）。

**入口只有设置一处**（设置区块 id `cursor`，order 30）：指针是"设一次就不动"的事，
不该占顶栏。非界面入口有两个：`golding.cursor` 服务（别的插件换图案）与命令 `cursor.next`。

**服务（`golding.cursor`，语义 v2）**：`styleCount` / `styleIdAt` / `styleTitleAt` /
`setStyle` / `style` / `setImage` / `setEnabled` / `isEnabled` / `setEffects` / `isEffects` /
`setScale` / `scale` / `pulse`。

- **样式列表 = 内置图案 + 用户指针**（用户指针 id 形如 `user:pen`）。别的插件不必知道
  "用户指针"这个概念，照样遍历、照样按 id 选。
- `setStyle` 给未知 id 时**当前图案保持不变**——拼错名字变成"透明的那层影"比报错难查得多。
- `setEnabled(0)` 的语义是"把系统指针还回来"，不是"隐藏自己的图案"（后者会让屏幕上
  一个指针都没有）。

**持久化分开两处，各管一件事**

| 文件 | 管什么 | 位置 |
| --- | --- | --- |
| `cursors.index`（TSV） | 用户指针的**热点**（文件名 / hotX / hotY / 显示名）；文件列表以目录为准 | `%APPDATA%\GoldingCore\cursors\` |
| `cursor.ini`（TSV） | 用户**选了**哪一只、启用开关、特效开关、尺寸系数 | `%APPDATA%\GoldingCore\` |

放用户目录而不是插件目录，因为**升级会覆盖插件目录**。都是 TSV 不是 JSON：插件层拿不到
core 的 JSON 解析器，一行一条就能 `split`，人也能直接改。坏行 / 未知键 / 坏值一律跳过并保留
默认——这是用户可编辑的文件，一个手误不该让三项一起回出厂。

`setImage` 换的那张临时图**不持久化**：它不在用户指针目录里，记住一个可能被删掉的绝对路径没有意义。

**`--cursor-demo`**：不读真实输入，按帧把六种图案、"按下"、设置区块、用户指针面板
（列表 / 选热点 / 选文件）各走一遍（同帧号同图，可复现截图），再走到"关闭 / 重开弹窗"那一段。
演示模式**不写** `cursor.ini`（它一路换图案，写下去就是拿演示覆盖用户的真实选择）。
演示动作必须排到**帧末**（`RunAfterFrames`，从第一帧开始）：tick 里改界面树会让正在遍历
绘制列表的场景拿到失效指针；而 `Ready` 排在 `Open()` 之前，排在那儿会被清空。

---

### 4.10 服务契约矩阵

| 服务 | 提供方 | 消费方 | 契约头 |
| --- | --- | --- | --- |
| `golding.navigation` | Shell | （预留：页面插件切页） | `plugins/common/GoldingServices.h` |
| `golding.popup`（v2） | Shell | `settings`、`spellbook`、`cursor` | 同上 |
| `golding.menu` | Shell | `settings`、`spellbook` | `plugins/common/GoldingMenu.h` |
| `golding.settings` | `settings` | `cursor`（演示模式开自己的区块） | `plugins/common/GoldingSettings.h` |
| `golding.commands` | `commands` | `settings`、`spellbook`、`cursor` | `plugins/common/GoldingCommands.h` |
| `golding.cursor`（语义 v2） | `cursor` | （预留：写字 / 演示类插件） | `plugins/common/GoldingCursor.h` |

规矩是**契约在提供方，规格在头文件，谁用谁 include**。服务结构体不进 SDK：
`popup` 的字段形态是**这个面板插件自己的约定**，换一个面板可能是抽屉、可能没有弹窗；
抬进 SDK 就等于让 core 认识"弹窗"这个词，core 一旦认识就会长出第二套特化表。

服务名是唯一键，同名后到者被驳回。结构体首字段必须是 `abiVersion`，取用方用
`Use<T>(name, minVersion)` 表达"我要哪一版语义"。

---

## 5. 反复出现的原理

1. **一个事实只有一个权威来源。** 每个属性都问一遍"它写在哪几处"：
   `abiVersion` 从 json 删除；依赖只在 json（descriptor 里那份已删）；
   侧栏不自动列页面（页面表管"有哪些页"，区域表管"哪儿有入口"）；
   设置区块只有宿主那张表一份清单；`CommandTable` 是命令的唯一权威，服务只是它的只读窗口。
2. **描述与内容分离。** `plugin.json` 是描述（扫描阶段可读），dll 是内容（加载后才可信）。
   这条让"依赖图在打开 dll 之前就能算完"成立。
3. **降级而非硬失败，粒度 = 单个插件。** 缺依赖 / 缺能力 / entry 失败都只影响它自己；
   能判早的错就判在最早那一步（`apiVersion` 不满足连 dll 都不开）。
4. **能用日志回答"谁、往哪、什么结果"。** 每次注册留一行；`dumpRegistries` 打全表；
   插件 `Ready` 返回失败项数；宿主汇总成退出码。进程内不隔离崩溃，日志是唯一排查面。
5. **界面树的改动只能在安全时机。** tick 里改树 = 间歇性段错误（场景正在遍历绘制列表），
   要改就排到帧末；`Open()` 会清空待办队列，所以 `RunAfterFrames` 必须在窗口开之后排；
   插件不得跨弹窗持有节点裸指针。
6. **静态档 vs 动态档。** 静态面板"几何构造即定型"，动态组件每帧可 `Resize` / `Relayout`。
   凡是要跟着别的东西改几何的（顶栏高度、侧栏宽度、动画），必须落在动态档上。

---

## 6. 扩展指南

### 加一个插件

1. `plugins/core/<名字>/src/*.cpp`：继承 `golding::Plugin`，`Register` 里用
   `host.AddXxx()` 注册，末尾 `GOLDING_EXPORT_PLUGIN`。
2. `plugin.json`：九个白名单键，`targets.win64.lib` 写 `bin/<名字>.dll`。
   依赖面板就写 `requires.plugins`；只是"少个入口"的能力**不要**写 `needs`。
3. `plugins/CMakeLists.txt` 加一行 `golding_add_plugin(<名字> <源码...>)`。
4. 需要的话加 `shape.json`（图谱形状）与 `assets/`。
5. 在 `Ready` 里写自检断言（任何几何 / 契约上的承诺都该有一条），`return` 失败项数。

### 加一种注册面

改一处就要改一串，顺序如下：`golding_plugin.h` 里 `GoldingHostApi` **末尾**追加函数指针
→ ABI +1（`GOLDING_PLUGIN_ABI_VERSION`）→ 需要的话 `GOLDING_CORE_API_VERSION` +1
→ core 侧新增或复用一张表 → `HostApp::fillHostApi` 填上 → `Host` 加一个 `AddXxx()`
（带一行日志）→ `API.md` 补字段说明。**不要**往 `golding::Plugin` 上加方法：那是虚表变更。

### 加一个服务

1. 在 `plugins/common/` 新增契约头：服务名常量、版本常量、结构体（首字段 `abiVersion`，
   其余只有函数指针，`self` 参数留给提供方）。
2. 提供方 `plugin.json` 写 `provides`，`Register` 里 `host.AddService`。
3. 消费方 `host.Use<T>(name, minVersion)`，**取不到就降级并打 WARN**，不要写 `needs`
   除非它真的装不了。
4. 契约结构体变了就 +版本（结构体语义扩展可以不动结构体，见 `golding.cursor` 的 v2）。

### 换一套界面 / 改设计尺寸

- 换界面：写一个新的 `role: "panel"` 插件放进 `plugin/core/`（同名会 shadow 掉旧的），
  自己声明区域与服务。宿主 exe 不动。
- 改尺寸：只改 `host/main.cpp` 的 `kDesignWidth` / `kDesignHeight`。面板从父节点量尺寸，
  插件不抄这两个数——抄了就会变成"四处对齐的手工活"。

---

## 7. 已知边界

| 不支持 | 原因（详见 `Options.md` / `NOTES.md`） |
| --- | --- |
| 进程内卸载 / 热重载 | 缺界面树销毁、反注册契约、进程级静态量复位。**重载 = 重启进程** |
| 崩溃隔离 | 同进程；`catch(...)` 只兜标准 C++ 异常，SEH 穿不过去 |
| 无 dll 的纯数据插件 | 它进不了依赖图、不能声明能力、不会被 entry，唯一效果是档案表多一行 |
| 多个 `HostApp` 实例 | `uiRoot` / `bootMode` / provider 映射都是文件级静态量 |
| 弹窗叠层 | `golding.popup` 只保留一层；叠层的层级与焦点规则没有定义 |
| 非 Windows | dll 装载（`PluginLoader`）只有 Windows 分支；`ShowCursor` / `ShellExecuteW` 亦然。`PathFinder` 虽有 mac/Linux 的取 exe 路径分支，但没有插件可加载 |

测试现状：`ctest --test-dir build-dev` 2 项（`sdk_host` 测跨 dll 契约，`core` 测 core 的纯函数，
后者约 67 条断言）；`ctest --test-dir build` 7 项（上面 2 项 + `host.default` / `host.fullscreen` /
`host.bootmode` / `host.collapsed` / `host.cursordemo`）。窗口层测试用 `SDL_VIDEO_DRIVER=dummy` 无头跑，
断言在插件自己的 `Ready` 里，宿主只验"能启动、能装配、退出码如实"。
