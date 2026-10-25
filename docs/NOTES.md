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

**头可见 ≠ 符号可用：误引 core 内部头要到链接期才炸。** 构建把整棵 `include/`
暴露给插件（`golding_sdk` 是 INTERFACE + `include/`），但 `Application/*.h` /
`HostApp.h` / `Tool/PathFinder.h` 的实现编在 `golding_core`，而插件只链 `golding_sdk`。
于是插件 `#include` 它们**编译全过**，报错发生在链接期、且是干巴巴的
`undefined reference`——离错误现场十万八千里。这份代价已经付过一次：spellbook 因为
它自带了一份 340 行手写 JSON 解析器（"插件不链 core"那句注释就写在上面），同一份
JSON 在仓库里因此有三种读法。
现在的处理：那些内部头首行 `#include "Application/CoreInternal.h"`，未定义
`GOLDING_CORE_INTERNAL` 时直接 `#error`（宏只给 `golding_core` / `golding_host` /
`core_tests`，**`sdk_host_tests` 不给**——它是"外部使用者"的样本）。
真正需要的能力（如 JSON）提成 `include/sdk/` 下的 header-only 头，不要引内部头。

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

**反过来：收起的容器也不要 `SetVisible(false)`。** 宽度 0 已经不画、不命中，而一隐藏
它就从绘制列表里消失，动态档的 `Tick` 也跟着停——展开动画根本没法从 0 起步。抽屉就靠
"宽度是唯一状态"这一条撑着。

**顶栏下面的那一块必须是动态档。** `MenuBarStyle::hideInFullscreen` 全屏时让整条顶栏
消失，`GetCurrentHeight()` 从 44 变 0；而 `MenuBar` 是静态面板，改不了自己的尺寸，
布局求解器又不跳过不可见项 ⇒ **在 Stack 里给它固定高、或把 body 的 y/高在 `create`
里算一次就完事，全屏后顶上都会留一条 44px 的空白**。正确做法：body 也用
`InkingDynamicWidget`，每逻辑步问一次 `GetCurrentHeight()`，变了就贴到顶栏下面并
`Relayout()`。启动即全屏（`--fullscreen`）看不出这个 bug——那时 `create` 读到的
`GetCurrentHeight()` 本来就是 0，只有**运行期**切全屏才暴露。

**（侧栏动态档的两条纪律）** 不要声明 `OwnsSubtreeHit()`（子孙不进命中表，条目点不到）；
`HitTest` 要自己答"点在不在我这块矩形上"，否则容器在命中上是个透明洞。抽屉动画还要
`HasSubtreeClip()` 返回 true：动画中途条目得剪在侧栏那块里，否则会溢到内容区上。

**`fillMain` 之后的项会被挤出容器。** 求解器按顺序给 `fillMain` 分"剩余空间"，但**不预留**
它后面那些项的尺寸：MenuBar 的弹簧因此把两个窗口按钮顶到画布外；设置弹窗里"弹簧 + 底部
按钮"也因此只剩一条边。规则：**`fillMain` 的项放最后**；要固定贴边的元素（关闭按钮）
挂到不参与布局的父级上按绝对坐标放。

**`bootMode` 必须在装配之前交给插件。** 面板的 `create` 里要按启动模式决定初始形态
（`--collapsed` 一进来就收着侧栏）。宿主要是等装配完再 `setBootMode`，插件读到的永远是
空串——而且只有"插件真的读它"时才暴露，不读的插件一切正常。

**`InkingStaticButton` 的文字在浅色下会画成空心描边。** 同一条按钮，`textColor`
是深色（`0xFF33303C`）时是实心字；换成纯白或近白（`0xFFF6F3FB`）就只剩一圈抗锯齿
边缘——字心是透明的。这不是"白色被当透明"，因为浅紫底色照常画得出来。规避办法：
**深底配深字、浅底配深字**，别指望白色文字。`appmanager` 的启动按钮因此是
"浅紫底 + 深紫字"，而不是常见的"紫底白字"。

**`graveyard`（延迟销毁的旧节点）不能用 `RunAfterFrames` 清。** 那条路在
`--frames`（CI / 截图）模式下根本不派发，节点会一直躺着，直到进程退出时随静态
`g_parts` 一起析构——而那时场景已经销毁，`~InkingWidget` 里的 `RemoveDrawNode`
是一次 use-after-free，**退出码 0xC0000005**，日志却一切正常。正确做法：挂在
动态档的 `onTick` 开头清（`BodyLayer::SetDeferredSweep`），两个模式走同一条路。

**`InkingFixedPopup` 的描边其实画不出来。** 它在 `.cpp` 里实现了 `onRenderDecoration`
（描边走装饰遍），但**没有重写 `HasRenderDecoration()`**——那个默认返回 false，场景因此
从不把它放进装饰栈。表现是"白卡片只有一条底部阴影、没有边框"，在白内容区上等于没有浮层。
派生一层、补 `bool HasRenderDecoration() const noexcept override { return true; }` 即好。
（这是 0.5.0 的实测结论；库侧修法是给 `InkingFixedPopup` 补同一个覆盖。）

**两个 `InkingFixedPopup` 并存会互相踢掉。** 每个浮窗的"点外部关闭"判的是**自己**的
`HitTest`：点子菜单（在父菜单矩形之外）时，父菜单同一帧收到 `anyDownEdge && !hovered`
就自我关闭。焦点同理——基类会在每次指针变化时把焦点抢回自己。所以多层级菜单**不能**
靠叠加现成浮窗：要么关掉这两个自动行为、由外层统一仲裁（我们的做法），要么等库出原生
子菜单。这也是 `prefab::MenuPopup` 第一版"不做子菜单"的真实原因。

**多层级浮窗的 z 必须逐层递增。** `InkingFixedPopup::AddItem` 把项抬到"本容器 z + 1"，
如果所有层用同一个 z，父层的项（z+1）会盖住子层容器（z）——几何全对、`QueryHit` 也对，
就是视觉上"新开的菜单被旧菜单的项切断"。我们按 `260 + 层号 × 10` 给。

**菜单声明的生命周期**：`golding.menu` 的 `MenuDecl` 只需要在 `open` 调用期间有效——
面板打开那一刻把整棵树**深拷贝**进自己的表。所以插件可以现场拼栈上的 `std::vector`
（"设置区块装配后才有"这类动态内容就是这么来的），open 返回后就能扔。

**弹窗声明也是同一个规矩，但面板必须自己兜住。** `GoldingPopupDecl` 只承诺在 `push`
调用期间有效：插件把 `const GoldingPopupDecl decl{...}` 写在栈上是**最自然**的写法。
面板要是把它当指针存下来，`pop` 那一刻读到的就是那个栈帧被覆盖之后的内存——表现是
**每次关闭弹窗都闪退**，而且崩在 `onClose` 上，看起来像插件写错了。现在
`GoldingGamestoryShell` 存的是 `std::optional<GoldingPopupDecl>` + 自己那份 `id`
字符串（值拷贝）。**任何时候都别在服务里存调用方的声明指针。**

**不要在按钮的回调栈里销毁按钮（或它的祖先）。** 两条实测过的路：

1. `push` 一个新的模态框会重建弹窗。如果这个 `push` 是"设置区块里的
   「用户指针…」按钮"发起的，旧弹窗（连同那颗按钮）就在回调栈上——当场
   `RemoveChild` 就是 use-after-free。面板的做法是**先摘下来放进 `graveyard`、
   下一帧末再销毁**（`RunAfterFrames(1, ...)`；不能排本帧末，因为演示 / 脚本
   本身就在帧末任务里 push）。
2. 列表重建同理：用户指针面板的「删」按钮，它的回调要重建列表，而
   `ClearChildren()` 会把正在执行回调的那颗按钮一起删掉。做法一样——
   `DetachChildrenDeferred` 摘下来、帧末销毁。

**插件在 `Register` 里碰不得 `InkingWindow::Instance()`。** 那个单例第一次被调用就把
设计尺寸烘进根锚点，而宿主是在 `registerAll()` **之后**才调 `SetDesignSize(1920,1080)`——
提前一碰，`SetDesignSize` 被拒，宿主直接启动期硬失败（`commands` 插件实测踩到）。
要走窗口层的东西（快捷键表、最小化、关窗），只能在 `Ready` 里做：装配之后、跑帧之前。

**`ShortcutRegistry` 的表在窗口层，注册者要先死就得自己 `Unbind`。** 插件没有析构钩子
（重载 = 重启进程），所以快捷键实际上活到进程结束——这正是"没有卸载"这条边界的
另一面，不用处理，但别指望"插件没了快捷键还在"。

**命令面板的依赖是可选的，顺序靠扫描序。** 注册命令的插件在 `Register` 里 `Use`
`golding.commands`，所以命令面板必须先 entry。同层（都只依赖面板）的顺序 = 扫描顺序
（目录名字典序）。反过来只是"少几条命令 + 一行 WARN"，不是崩溃——这正是"可选依赖"
与 `requires.plugins` 的区别：后者反过来会让插件装不上。

**挂在界面树上的节点不要再包一层智能指针。** `AddChild` / `AddItem` 以后节点归父级
（`unique_ptr`），自己再存一份 `unique_ptr` 就是退出时**析构两次**——`commands` 插件的
面板实测约 10% 的启动在退出路径上段错误（比例不定 = ASLR 把它时而藏住）。插件里
指向树内节点一律用裸指针；"拥有"只发生在树上那一份。

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
