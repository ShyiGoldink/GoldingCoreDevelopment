# GoldingCore 插件式客户端核心

## 这是什么？

此项目是一个基于InkingUIKit做的C++前端核心库
通过最小核心，实现插件化的客户端

## 当前状态

还处于开发期，不只是InkingUIKit只发布了测试版
同时该Core也在开发期。

## 核心决策

| 角色 | 干什么 | 依赖 InkingUIKit 的 |
| --- | --- | --- |
| **宿主 core** | 扫描 / 注册 / 开窗口 / 装配 | `window` `scene` `widget` |
| **core 插件** | 注册基础组件 + 定义界面骨架（region） | 全部组件 |
| **普通插件** | 具体功能（纯数据 / 数据+dll） | 通常无 |
| **魔法阵插件** | 读插件清单 → 可视化 | `widget` + 注册表只读 API |

**命门：装配器只调 `Registry::Create(name)`，一个组件类型都不许硬编码。**

## 2. 宿主 core 六个模块（全纯 C++，可无头自检）

| 模块 | 职责 |
| --- | --- |
| `Manifest` | `plugin.json` 的结构 |
| `ManifestLoader` | json → Manifest（复用 inkgen 的加载/校验，**与 `Emit` 分离**） |
| `PluginScanner` | 扫 `plugin/*/plugin.json` |
| `Resolver` | 拓扑排序 + 循环检测 + 缺失检测 |
| `PluginLoader` | dll：descriptor → ABI 检查 → entry |
| `Assembler` | Manifest → 组件树（只调 `AddChild`） |

六张登记表（写法一致：静态类 + 函数内静态量 + 同名驳回）：
`PluginRegistry`（组件类）· `PageRegistry`（页面）· `SettingsRegistry`（设置区块）·
`RegionRegistry`（区域与区域项）· `ServiceRegistry`（服务 vtable）·
`CapabilityRegistry`（**能力目录**：声明面 + 运行时面，服务表是它的存储后端）

## 3. 插件文件夹

```none
plugin/<name>/
├─ plugin.json      必需
├─ bin/<name>.dll   可选
├─ icon.png         可选
├─ assets/          可选
└─ <插件自己的数据>  可选（如 magic/magic.json：谁的数据谁解释，core 只给目录）
```

`plugin.json` 必需字段：`name` `title` `version` `apiVersion` `requires.plugins`
可选：`role`（`panel` = 面板插件，提供界面骨架）、`targets{win64:{lib}}`（dll）
顶层键是**白名单**：写了别的键判 `Invalid`，原因会打进启动日志（`↳ <目录>：未知字段 xxx`）

`apiVersion` 是**整数**（插件用到的最小 core 能力版本，单调递增）。宿主在 SDK 里
声明自己支持到几（`GOLDING_CORE_API_VERSION`），插件声明高于它就拒绝加载并说明当前支持到几。

**插件分两层，目录即事实**：`plugin/core/<name>/` 是随宿主分发的内置插件，
`plugin/<name>/` 是外部安装的。宿主先找内置的面板插件，找不到才退到外部。

**ABI 版本不写在 `plugin.json` 里**：唯一来源是 dll 导出的 `golding_plugin_descriptor().abiVersion`。
同一事实写两处必然对不上——此前 8 个插件里有 6 个 json 还写着旧版本，而没有任何代码会发现。

## 4. dll 的两个导出符号——顺序不能反

```c
① const GoldingPluginDescriptor* golding_plugin_descriptor(void);  // 纯 C POD，含 abiVersion
② int golding_plugin_entry(const GoldingHostApi* hostApi);         // 只吃函数指针表
```

**先读 ① 比对 ABI，不匹配就拒绝，绝不调 ②。**
不这么做，ABI 不对的表现是**崩溃**，不是报错。

descriptor 的字段（ABI v9）与三道闸门，**顺序不能换**：

| 字段 | 管什么 | 不匹配时 |
| --- | --- | --- |
| `abiVersion` | 二进制布局（结构体 / 函数表顺序） | 拒绝（`state=3`）。**必须第一个查**——不对时后面那些指针本身就不可信 |
| `sdkId` | 给**哪套 SDK** 编的（`"golding"`） | 拒绝（`state=8`），日志点名期望与实际 |
| `sdkVersion` | 那套 SDK 的版本（`"0.2.0"`） | **只记不拒**（编译栈未定，见 `docs/Options.md` §1） |
| `manifest()` | 静态能力宣言：`kind` / `depends` / `provides` / `needs` | 装配**之前**就能读到，所以"缺能力"能在 entry 前判出来 |

`plugin.json` 的 `apiVersion`（整数）是第四个数：插件用到的最小 core 能力版本，
高于宿主即拒（`state=7`）。四个号各有分工，别想着合并。

跨 dll 边界只允许 C 的 POD 与 `void*` 句柄，**不接受任何 C++ 类型**
（`std::string` / 容器 / 引用都不行）——否则宿主与插件必须完全同 ABI，
Debug/Release 混编就会在传容器的第一行崩掉。

宿主给插件的服务表（`GoldingHostApi`）：

| 项 | 用途 |
| --- | --- |
| `log` | 写日志 |
| `registerWidgetClass` | 登记一个组件类型（名字 → create） |
| `registerPage` | 登记一个内容页（id / 标题 / 顺序 / create） |
| `pageCount` / `pageAt` | 骨架插件据此生成导航 |
| `mountPage` | 把某个页面装进容器 |
| `registerSettingsSection` / `settingsSectionAt` / `mountSettingsSection` | 插件往"设置"里加区块 |
| `pluginCount` / `pluginAt` | 只读插件档案（魔法阵用它画图） |
| `registerRegion` / `regionCount` / `regionAt` | **面板插件**声明"本机有哪些地方可以注册"，并枚举出来 |
| `registerRegionItem` / `regionItemCount` / `regionItemAt` | 任意插件往某个区域放一项；面板插件取出来渲染 |
| `uiRoot` | **装配后**的界面根（v8，自检插件用它断言"根铺满 / 每页装得上"） |
| `bootMode` | 宿主的启动模式串原样交出（`--expanded` 是什么意思只有面板自己知道） |
| `pluginDir` | 某个插件被扫描到的**目录**（v10，绝对路径 / UTF-8）；只给"读自己带的数据文件"用 |

`plugin.json` 里声明的相对路径（`targets.win64.lib`）由 core 读完就解析成
绝对路径，写成绝对路径或用 `..` 逃出去会判 `manifest` 不合法——所以消费者不必知道基准目录是谁。
`pluginDir` 补的是另一半：**没写在 json 里、但插件自己要读的文件**（`magic.json` 这类）。

descriptor 上还有一个 `ready` 回调（v8）：**装配之后**再调每个插件一次，返回失败项数。
自检就挂在那儿——`entry` 太早（那时连窗口都没有）。宿主**不再**提供"请求整树重建"
的入口（v7 删掉）：界面怎么变由界面自己决定（抽屉就地改宽、覆盖层就地显隐、换肤靠
主题版本号自动重查）。

**区域契约**是"换面板插件 = 换一套 UI"的落点：core 不认识 `menubar` 或任何具体区域名，
只负责存表、查表、同名驳回。面板插件声明什么，本机就能在哪儿扩展；换个面板插件，
可注册位置的集合就整个换掉，宿主一个字节不动。往不存在的区域注册会被驳回，
但**不影响该插件其余注册**——面板缺席时其它插件照常工作。

**页面契约**是这套骨架的关键：core 插件（Shell）不认识任何具体页面，
抽屉里的入口全部来自页面表。新增一个页面插件，宿主和 Shell 都不用重编。

## 5. 加载三阶段

```none
阶段 1  扫描 + 依赖全图解析 + 注册      便宜 → 全做
阶段 2  该场景要的资源预读              贵   → 按需
阶段 3  装配 + 绑定动作                 最后
```

**依赖图只能在启动时全解析；「懒」只发生在阶段 2/3。**

## 6. 四个不许忘的硬约束

1. 装配器**不许硬编码组件类型**（否则「换 core 插件换一套 UI」不成立）
2. **ABI 检查必须在进 C++ 之前**
3. 装配时 `Find(name)` 返回 nullptr → **报错 + 拒绝装配**，绝不用默认值顶上
4. **一个插件坏了只影响它自己**——标「不可用」，其余照常

## 7. 第一步

**先做「纯数据插件 + 装配器 + 无头自检」，完全不碰 dll。**

一个 `helloWorld` 能被扫描、解析、装配、画出来，就算通了。dll 是第二步——它只是「多一个注册源」，别让它污染第一版的结构。
**进度就是那句话：「换掉 core 插件，界面就换一套，宿主 exe 一个字节不动。」**

## 8. 验收清单

- [ ] 删掉 core 插件 → 程序仍启动、能列出全部插件、**什么都不画**
- [ ] 装回 core 插件 → 界面出现
- [ ] 拖一个数据插件进 `plugin/` → 重启 → 出现在法阵
- [ ] 循环依赖 → 报错且不崩
- [ ] 缺依赖 → 只有那个插件不可用
- [ ] `abiVersion` 不匹配的 dll → 报错且不崩

## Q&A

**Q1**：魔法阵插件是什么？
**A1**： 一个设计上的小巧思。项目的默认插件管理器，不过表现形式上稍有差异。支持默认配置，不过有趣的可视化还是希望各位插件作者支持。
