# API 参考

三个版本号，分工不同：

| 号 | 值 | 管什么 | 不匹配 |
| --- | --- | --- | --- |
| `GOLDING_PLUGIN_ABI_VERSION` | 11 | 二进制布局（结构体 / 函数表顺序） | 拒绝，`state=3` |
| `GOLDING_SDK_ID` | `"golding"` | 给哪套 SDK 编的 | 拒绝，`state=8` |
| `GOLDING_SDK_VERSION` | `0.2.0` | CMake 的 `project(VERSION)` | 只记不拒 |
| `GOLDING_CORE_API_VERSION` | 2 | 插件用到的最小 core 能力版本（在 `plugin.json`） | 拒绝，`state=7` |

---

## 1. C ABI（`include/sdk/golding_plugin.h`）

### 1.1 两个导出符号

```c
const GoldingPluginDescriptor* golding_plugin_descriptor(void);
int golding_plugin_entry(const GoldingHostApi* hostApi, const char* pluginName);
```

宿主**先读 descriptor 比对 ABI，再调 entry**。ABI 不对时调 entry 的表现是崩溃，不是报错。

`pluginName` 由宿主从 `plugin.json` 读出后传进来，只用于日志。

### 1.2 `GoldingPluginDescriptor`

```c
typedef struct GoldingPluginDescriptor
{
    unsigned int abiVersion;
    const char* sdkId;
    const char* sdkVersion;
    int (*ready)(const GoldingHostApi* hostApi, const char* pluginName);
} GoldingPluginDescriptor;
```

只有二进制身份与钩子。**所有数据（名字 / 版本 / 类型 / 依赖 / 能力）在 `plugin.json`。**

`ready` 在装配之后调用，返回**失败项数**，0 = 全绿；可空。

### 1.3 `GoldingHostApi`

全部是 C 函数指针，跨 dll 只传 POD 与 `void*`。

| 字段 | 用途 |
| --- | --- |
| `abiVersion` / `hostContext` | 表版本 + 宿主上下文（插件原样回传） |
| `log` | 写日志 |
| `registerWidgetClass` | 面板根工厂。**只有 `role: "panel"` 能调**，只留一项 |
| `registerPage` | 登记内容页 |
| `pageCount` / `pageAt` / `mountPage` | 枚举页面；把某页装进 `parent` |
| `registerSettingsSection` | 登记设置区块 |
| `settingsSectionCount` / `settingsSectionAt` / `mountSettingsSection` | 同上 |
| `pluginCount` / `pluginAt` | 只读插件档案（`GoldingPluginInfo`） |
| `registerService` / `queryService` | 插件之间唯一的横向通道 |
| `registerRegion` / `regionCount` / `regionAt` | **面板插件**声明本机可注册的区域 |
| `registerRegionItem` / `regionItemCount` / `regionItemAt` | 任意插件往区域里放一项 |
| `uiRoot` | 装配后的界面根（只在 `ready` 里可用） |
| `bootMode` | 宿主的启动模式串，原样交出 |
| `pluginDir` | 某个插件被扫描到的目录（UTF-8 绝对路径） |

注册函数统一返回 `GOLDING_REGISTER_OK` / `NAME_TAKEN` / `INVALID`。

### 1.4 声明结构

```c
GoldingWidgetClass   { name, create(hostContext, parent, configName), destroy }
GoldingPageDecl      { id, title, order, create(hostContext, parent) }
GoldingSettingsDecl  { id, title, order, create(hostContext, parent) }
GoldingRegionDecl    { id, title, order, allowDuplicateIds }
GoldingRegionItemDecl{ regionId, id, title, icon, order, create, onActivate }
GoldingPluginInfo    { name, title, version, role, abiVersion, state }
GoldingOverlayDecl   { id, height, create, onClose }
```

`create` 的约定：把新建子树的根**挂到 `parent` 上**（由 parent 拥有），返回该根；
返回 `nullptr` 表示失败，宿主不做清理。`destroy` 通常留空——节点随父节点销毁。

`hostContext` 是**提供方自己的**上下文（不是调用方的）。`mountPage` / `mountSettingsSection`
会按页面 / 区块的注册者反查，取不到才退回调用方。

`GoldingOverlayDecl` 不走 HostApi——它是 `golding.overlay` 服务的参数，跨 dll 传 POD 需要公共定义。

### 1.5 `PluginState`（`GoldingPluginInfo.state`）

| 值 | 含义 |
| --- | --- |
| 0 | 可用 |
| 1 | manifest 不合法 |
| 2 | 打不开 dll |
| 3 | ABI 不匹配 |
| 4 | **已废弃**（曾表示无 dll 的纯数据插件），编号保留为空缺 |
| 5 | entry 失败 |
| 6 | 依赖不满足（缺失 / 成环 / 被连累） |
| 7 | apiVersion 高于宿主支持 |
| 8 | sdkId 与宿主不一致 |

---

## 2. C++ SDK（`include/sdk/GoldingPlugin.h`）

### 2.1 `golding::Plugin`

```cpp
class Plugin
{
public:
    virtual ~Plugin() = default;
    virtual bool Register(Host& host) = 0;
    virtual int Ready(const Host& host) { return 0; }
};
```

两个钩子 + 虚析构，**发布后只许末尾追加**。`Register` 返回 false 会被记成 `EntryFailed`。

### 2.2 `golding::Host`

注册面：

| 方法 | 说明 |
| --- | --- |
| `AddWidgetClass` | 面板根工厂，只有 `role: "panel"` 能用 |
| `AddPage` / `AddSettingsSection` | 登记页面 / 设置区块 |
| `AddService(name, vtable)` | 登记服务，`vtable` 首字段必须是 `abiVersion` |
| `AddRegion` / `AddRegionItem` | 区域与区域项 |
| `MountPage` / `MountSettingsSection` | 把别人登记的页面 / 区块装进容器 |

查询面：`PageCount` / `PageAt` / `SettingsSectionCount` / `SettingsSectionAt` /
`RegionCount` / `RegionAt` / `RegionItemCount` / `RegionItemAt` / `PluginCount` / `PluginAt`。

其余：`Info` / `Warn` / `Error`（写日志）、`UiRoot()`、`BootMode()`、`PluginDir()`、
`provider()`、`Context()`、`Valid()`。

```cpp
template <typename T> const T* Use(const char* name, unsigned int minVersion = 0) const;
```

按名字取服务。三种结果可区分：缺能力（返回 `nullptr`）、版本不足（**仍返回**，只 WARN）、正常。
`T` 的首字段必须是 `abiVersion`。

### 2.3 导出

```cpp
GOLDING_EXPORT_PLUGIN(HelloPlugin)
```

生成两个 C 符号并填 descriptor。要求类型继承 `golding::Plugin`、有默认构造函数、构造函数
不依赖运行时状态。

---

## 3. core 的 C++ 接口

### 3.1 `HostApp`（`include/HostApp.h`）

```cpp
bool discover();      // 定位 plugin/core/ 与 plugin/
bool prepare();       // 扫描 → 建拓扑 → 加载 dll → 校验依赖与能力；只能成功一次
void registerAll();   // 按拓扑序调 entry
int  notifyReady();   // 装配后回调，返回失败项总数
void setUiRoot(void*);
void setBootMode(std::string);
const LoadedEntry* panel() const;          // 可能为 nullptr（合法）
const ResolveResult& resolved() const;
const std::deque<LoadedEntry>& entries() const;
```

`prepare()` 的顺序是 `scan → 建节点（读 json 的依赖）→ resolve → 校验 needs → 加载 dll`。

### 3.2 五张表 + 一个槽位

六个都是静态类，只能在 UI 线程用，不加锁。

| 类 | 写 | 读 |
| --- | --- | --- |
| `PanelRootRegistry` | `setRoot(class*, provider)` | `root()` / `provider()` |
| `PageRegistry` | `registerPage(decl*, provider)` | `findPage` / `allPages` / `count` / `providerOf` |
| `SettingsRegistry` | `registerSection(decl*, provider)` | `findSection` / `allSections` / `count` / `providerOf` |
| `RegionRegistry` | `registerRegion` / `registerItem` | `allRegions` / `regionCount` / `itemCount` / `itemAt` |
| `CapabilityRegistry` | `declare(name, version, declarer)` / `registerCapability` | `isDeclared` / `versionOf` / `declarerOf` / `query` / `providerOf` |
| `ServiceRegistry` | `registerService(name, vtable)` | `queryService` / `allNames` |

每个都有 `clear()` 与 `stats()`（接受 / 驳回次数 + 最后一次驳回原因）。
`PanelRootRegistry` 容量为 1，第二次 `setRoot` 返回 `AlreadySet`。

### 3.3 无状态工具

| 类 | 接口 |
| --- | --- |
| `PathFinder` | `executableDir` / `pluginDir` / `plugins()` / `resolve(base, relative)` / `toUtf8` / `fromUtf8` |
| `ManifestLoader` | `read(pluginDir)` → `ManifestResult{manifest, status, detail}` |
| `PluginScanner` | `scan(sources)` → `ScannedPlugin[]`（带 `tier` / `shadowed`） |
| `PluginLoader` | `open(dllPath, expectedAbi)` / `callEntry(library, api, name)` |
| `PluginLibrary` | 插件档案：`add` / `setState` / `at` / `indexOf` / `directoryOf` / `dependsOf` / `providesOf` / `needsOf` |
| `Resolver` | `resolve(nodes)` → `order` / `unresolved`（带 `ResolveReason`） |

---

## 4. `plugin.json`

| 键 | 必需 | 类型 | 说明 |
| --- | --- | --- | --- |
| `name` | ✔ | string | 唯一键，非空 |
| `title` | ✔ | string | 显示名 |
| `version` | ✔ | string | 插件自己的版本 |
| `apiVersion` | ✔ | int | 用到的最小 core 能力版本 |
| `requires` | ✔ | object | 必须有 `plugins` 数组，可为空 |
| `targets` | ✔ | object | 必须有 `win64.lib`（相对插件目录的 dll 路径） |
| `role` | | string | 类型标签；`panel` = 提供界面骨架 |
| `provides` | | object | `{ "能力名": 提供版本 }` |
| `needs` | | object | `{ "能力名": 最小版本 }` |

顶层是白名单，未知键判 `Invalid` 并在 `detail` 里点名。

`targets.win64.lib` 读完当场解析成绝对路径；写绝对路径或用 `..` 逃出插件目录判 `Invalid`。
