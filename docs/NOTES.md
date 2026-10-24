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
