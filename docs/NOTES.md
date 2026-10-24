# 注意事项

## 流程与生命周期

**`prepare()` 只能成功一次。** 重复调用会静默留下半个旧世界：`_loaded` / `_nodes` 都是追加的，
第二轮把同一批插件再装一遍。已加守卫，第二次直接返回 false 并打日志。

**四个阶段的顺序不能换**：`discover → prepare → registerAll →（装配）→ notifyReady`。
`registerAll` 之前骨架不存在，所以插件在这一步只能注册、不能建界面。

**`GoldingHostApi` 必须活到进程结束。** 插件会长期持有指针（点击回调不在 `Register` 的调用栈里）。
`HostApp` 把它放在 `LoadedEntry` 里，而 `LoadedEntry` 存在 `deque` 中——换成 `vector` 会让
扩容搬走指针，所有已注册插件的 `hostContext` 一起悬空。

**`prepare()` 不释放 dll。** 装载成功的句柄归进程所有，退出时由 OS 回收。在退出路径上
`FreeLibrary` 反而危险：它会执行插件的静态析构，而五张表仍存着指向 dll 数据段的 `const TDecl*`。

## 所有权

| 谁 | 拥有什么 |
| --- | --- |
| core | 注册表的**索引**，不拥有对象 |
| 插件 | 它 `create` 出来的界面节点（挂在 `parent` 上，随父节点销毁） |
| 宿主 exe | 窗口、界面根、所有 dll 句柄 |

所以 `create` 返回 `nullptr` 时宿主**不做清理**——它没有可清理的东西。

## 明确不支持

| 不支持 | 原因 |
| --- | --- |
| 进程内卸载 / 热重载 | 缺三件前置：销毁界面树、插件反注册契约、进程级静态量复位。**重载 = 重启进程** |
| 崩溃隔离 | 插件与宿主同进程。`catch (...)` 只接得住标准 C++ 异常：SEH 不走它；插件静态链 libstdc++ 时异常在 dll 边界直接 `terminate` |
| 多个 `HostApp` 实例 | `uiRoot` / `bootMode` / provider 映射都是文件级静态量，一个进程只有一份 |
| 无 dll 的"纯数据插件" | `targets.win64.lib` 是必需字段，缺了判 `Invalid` |

## 版本策略

两个版本号**只记不拒**（`docs/Options.md` §2）：

- 服务 vtable 的 `abiVersion`：`Use<T>(name, minVersion)` 读到低于要求时只 WARN，照常返回。
- `descriptor->sdkVersion`：与宿主不同只 WARN，照常加载。

`abiVersion` 与 `sdkId` 是**严格相等**，不等即拒——那两个是命门。

`needs` 的能力版本是**严格比较**：需求方写 0 表示不关心版本；提供方写 0 而需求方要版本，
判不足（0 不能当成"任意版本"）。

## 坑

**`Open()` 之后不要再调 `Show()`。** `Show()` 的语义就是 `Open()` + `while (PumpOnce())`
（库头文件的注释原话）。窗口已经打开时它会按"切场景"把打开流程重走一遍，
**真实驱动下直接段错误**（dummy 驱动下看不出来，因为这个组合只在真窗口上炸）。
宿主因此手动跑 `while (PumpOnce())`，交互模式与 `--frames` 模式共用同一条 `stepFrame`。

**面板插件的三个实测坑（0.4.0–0.5.0）**：

1. **`MenuBar` 自身 z=100、它的项 z=101。** 任何想压在顶栏之上的东西（自绘窗口按钮）
   z 要 > 100，否则几何全对、绘制列表里也有，就是被顶栏白底盖住——表现是"看不见"，
   不是"没画"。
2. **`MenuBarStyle::captionButtons` 会插一根 `fillMain` 弹簧**，它和两个窗口按钮的宽度
   在"项还没挂全"的那次布局里就定了。实测结果是两个按钮被顶到 `x=1920`（正好等于画布宽，
   即画布外）。要自绘窗口按钮就关掉 `captionButtons`，把按钮挂在**根**上按绝对坐标放。
3. **直接 `new InkingStaticButton(parent, data)` 时，文字要自己写进 `ButtonData::label`。**
   `MenuBar::AddButton` 那条路由它代填，直接构造这条没有——漏了就是"白底无字"，
   在白底面板上等于没画。这个坑一次踩了两处（顶栏窗口按钮、侧栏条目）。

**布局求解器不跳过不可见子项。** `SetVisible(false)` 之后那个节点仍然占着自己在 Stack
里的一份尺寸：想用"隐藏侧栏 + 重排"实现抽屉收拉，结果是侧栏空着 220、内容区不吃满。
收拉必须落到**几何**上（改宽度）。静态面板没有 `Resize`，所以会变尺寸的容器只能是
`InkingDynamicWidget`（侧栏就是为此做成动态档的）。

**`fillMain` 之后的项会被挤出容器。** 求解器按顺序给 `fillMain` 分"剩余空间"，但**不预留**
它后面那些项的尺寸：MenuBar 的弹簧因此把两个窗口按钮顶到画布外；设置弹窗里"弹簧 + 底部
按钮"也因此只剩一条边。规则：**`fillMain` 的项放最后**；要固定贴边的元素（关闭按钮）
挂到不参与布局的父级上按绝对坐标放。

**`bootMode` 必须在装配之前交给插件。** 面板的 `create` 里要按启动模式决定初始形态
（`--collapsed` 一进来就收着侧栏）。宿主要是等装配完再 `setBootMode`，插件读到的永远是
空串——而且只有"插件真的读它"时才暴露，不读的插件一切正常。

**（侧栏动态档的两条纪律）** 不要声明 `OwnsSubtreeHit()`（子孙不进命中表，条目点不到）；
`HitTest` 要自己答"点在不在我这块矩形上"，否则容器在命中上是个透明洞。

**路径一律用 UTF-8。** 别用 `path.string()`（MinGW 下按 "C" locale 解码，中文路径抛
`Illegal byte sequence`），也别用 `path(std::string)`（那是 ANSI）。用 `PathFinder::toUtf8` /
`fromUtf8`。

**日志走 `stdout`。** `hostLog` 就是一句 `printf`，没有分级开关、没有文件输出。要接自己的
日志系统，改 `HostApp.cpp` 里那个回调。

**`PluginLoader::open` 丢弃了 `GetLastError()`。** "位数不对""缺依赖库""路径错"三者不可区分，
都报 `LibraryError`。要查根因用 `dumpbin /dependents` 或 Process Monitor。

**dll 加载失败不影响拓扑。** 依赖来自 `plugin.json`，扫描阶段就建好图——装不上的插件仍会
出现在 `unresolved` 里，诊断能看到"你依赖的那个装不上"，而不是笼统的"缺依赖"。

**同名插件内置优先。** `plugin/core/<name>/` 在扫描顺序里排在前，`plugin/<name>/` 同名会被标
`shadowed` 并跳过，日志有一行说明。

## 构建

**两种构建不要共用一个目录**：`build-dev`（`GOLDING_WITH_INKING=OFF`，只建 core 与单测）
和 `build`（`ON`，建宿主 exe + `plugin/`）。混用会让 `PathFinder` 扫到上一轮的旧插件。

`build-dev` 的产物只有 `libgolding_core.a`、`core_tests`、`sdk_host_tests`，没有 exe。

**改了 `plugin.json` 却没生效？** 那个文件是 `POST_BUILD` 拷的，而 `POST_BUILD` 只在目标
真的重链接时才跑——只改 json 不会重链接，于是运行的是上一份，且没有任何提示
（`provides` / `needs` / `role` 全都会静默用旧值）。`golding_add_plugin` 已把 json 记成
`LINK_DEPENDS`：改 json 会触发一次重链接，再把新 manifest 拷过去。

**SDK 版本**：当前对 `inking-0.5.0-win64` 构建（`host/CMakeLists.txt` 的候选表，
`-DGOLDING_INKING_DIR=<包根>` 优先）。0.5.0 有破坏性改动——指针钩子加了按钮维度
（`onPointerPress(PointerButton, bool)` / `onPointerClick(PointerButton, x, y)`），
触摸轻点=Primary、长按=Secondary。我们的插件没有重写这些钩子，所以升级零迁移；
**以后要重写时按新签名写**，旧签名不保留。

**`host/` 里只有 `main.cpp`**：`HostApp` 属于 core（零 UI 依赖）。宿主 exe 不参与任何判定，
它只做四件事——解析命令行、跑四个阶段、建窗口装面板根、决定退出码。所以"换面板插件，
宿主 exe 一个字节不动"这句话在源码层是可验证的：这个目录不认识任何插件名、节点名、区域名。

**两条"只有一处"的常量**（改了不会漏，因为别处都是引用）：

| 事实 | 唯一定义处 |
| --- | --- |
| 设计画布尺寸 1920×1080 | `host/main.cpp` 的 `kDesignWidth` / `kDesignHeight`；插件从父节点量，不抄 |
| 窗口清屏色 | InkingUIKit 的 `prefab/PrefabTheme.h` → `theme::kSceneBackground`；宿主编译期引用，不写死数值 |

清屏色为什么不能在这里写一个数：`PrefabTheme.h` 明说它和面板底色必须是同一个值，
两处各写一遍迟早出现"窗口底和面板底差一档灰"。宿主只引用库的常量。

**InkingUIKit 预编译包按候选表定位**（`host/CMakeLists.txt` 顶部），`-DGOLDING_INKING_DIR=<包根>`
优先。包是 **MinGW ABI**，MSVC 链不上。
