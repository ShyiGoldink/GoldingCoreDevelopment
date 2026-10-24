# 已定决策

只记**现在仍然成立**的约定，以及它们排除了哪条路。历史演进见 git。

---

## 1. ABI 严格相等，不向后兼容

`PluginLoader::open` 在 `descriptor->abiVersion != expectedAbi` 时直接拒绝，不调 `entry`。
当前 **v11**。

**原因**：全部插件在同一个 CMake 工程内，重编是一条命令；兼容分支会长期活着，成为第二套路径。
实验期不背这个包袱。

**解除条件**：出现第三方分发、且不能跟随 core 重编的插件时，才引入"最低 ABI + 可选字段"。

---

## 2. 版本号：两个放行，两个严格

| 号 | 策略 |
| --- | --- |
| `abiVersion`（descriptor） | **严格相等**，不等即拒 |
| `sdkId`（descriptor） | **严格相等**，不等即拒 |
| `sdkVersion`（descriptor） | 只记不拒 |
| 服务 vtable 的 `abiVersion` | 版本不足仍返回，只 WARN |
| `apiVersion`（json） | 高于宿主支持即拒 |
| `needs` 里的能力版本 | **严格比较**，不足即不装配 |

**原因**：前两个是命门（布局 / SDK 身份），错了就是崩溃。`sdkVersion` 没有可信真源——
编译栈要等正式版前才定，那时它才能代表"头文件 + 编译器 ABI"这个组合。

**风险**：长期放行会让版本号沦为装饰。所以每条的解除条件都写成**外部事件**（库发版、编译栈选定），
不写成"等有空"。

---

## 3. 插件崩溃不隔离

插件与宿主同进程，不捕获 SEH，不做子进程。

**原因**：用户已明确接受（实验期优先简单）。`catch (...)` 只接得住标准 C++ 异常，两件做不到的
必须记住：SEH（空指针写、栈溢出）在 MinGW 下不经过 `catch(...)`；插件静态链 libstdc++ 时，
异常在 dll 边界直接 `terminate`。

**后果**：这是**兜底**，不是隔离。日志是唯一的排查面，所以每次注册都要留痕。

---

## 4. `plugin.json` 描述，dll 才是内容

- **json 管数据**：`name` / `title` / `version` / `apiVersion` / `role` / `requires` /
  `provides` / `needs` / `targets`。
- **dll 管逻辑**：`abiVersion` / `sdkId` / `sdkVersion` / `ready` / `entry`。

`GoldingPluginDescriptor` 只有这四项，`Plugin` 基类只有 `Register` / `Ready` 两个钩子。

**原因**：同一事实写两处必然对不上。历史上 `abiVersion` 曾同时写在 json 与 dll 里，8 个插件
有 6 个写着旧版本而无人发现；依赖也曾两处都写，实测三个插件的两份依赖完全不一致。
**写了不生效**比写错更坏——它让人以为配置生效了。

**边界**：`entry` 与 `ready` 都收一个 `pluginName`，由宿主从 json 读出后传进去，dll 不再自称名字。

---

## 5. 依赖读 json，拓扑在加载 dll 之前

```
scan → 建节点（读 json 的 requires）→ resolve → 加载 dll → entry
```

**原因**：依赖是数据，扫描阶段就完整。这样 dll 加载失败的插件也能进依赖图，诊断能说
"A 依赖 B，而 B 的 dll 打不开"，而不是笼统的"缺依赖"。

---

## 6. 不卸载；重载 = 重启进程

没有 `PluginLoader::close`，没有反注册契约。

**原因**：进程内重载缺三件前置——销毁界面树（窗口是单例，`Show()` 阻塞）、插件反注册、
进程级静态量复位（`uiRoot` / `bootMode` / provider 映射）。而进程重启由 OS 一次性回收，
没有半拆状态。

**推论**：`prepare()` 只能成功一次，重复调用直接拒绝。

---

## 7. 一次进程一个 `HostApp`

`uiRoot` / `bootMode` / provider→context 映射都落在文件级静态量。

**原因**：插件回调只拿得到 `hostContext`，拿不到宿主实例。改成实例指针要动 ABI 与全部取值点。

---

## 8. core 不认识具体区域名

区域（`menubar` / `sidebar` / …）由**面板插件**声明，core 只存表、查表、同名驳回。

**原因**：一旦 core 认识某个区域名，就长回第二张特化表，"换面板插件 = 换一套 UI"不再成立。
同理：装配器不许硬编码组件类型。

**尚未归并**：`PageRegistry` / `SettingsRegistry` 是区域机制之前的特化表（≈ content / settings
两个保留区），暂与 `RegionRegistry` 并存。归并会让 `allRegions()` 凭空多两项，面板遍历时
把页面当条目摆——拿插件可见的语义换一个类名，不划算。

---

## 9. 没有"无 dll 的纯数据插件"

`targets.win64.lib` 是必需字段，缺了整份 `Invalid`。`PluginState` 的 4 号**保留为空缺**
（这些整数是跨 dll 契约，重排会让历史日志错位）。

**原因**：它没有装配路径（进不了依赖图、不能声明能力、不会被调 `entry`），也没有数据载体
（`widgets{}` 已删）。唯一效果是在档案表里多一行。

**如果将来要"只改配置就加组件"**：不要恢复无 dll 插件，那是把装配责任塞回 core。正路是
插件自己读配置——配置文件由**插件**解释，core 只给目录（`Host::PluginDir()`）。

---

## 10. `plugin.json` 顶层是白名单

写了未知键判 `Invalid`，键名写进 `ManifestResult::detail` 并打进启动日志。

**原因**：静默忽略正是 `abiVersion` 写在 json 里那种死法。白名单让"写了不生效"当场变成
一句人话。

---

## 11. 内置插件优先，目录即事实

`plugin/core/<name>/` 是随宿主分发的内置插件，`plugin/<name>/` 是外部安装的。
扫描顺序内置在前，同名外部插件标 `shadowed` 跳过。

**原因**：目录本身就是权威，不靠 manifest 自证（声明可以伪造）。选目录而非字段，也省掉
一个"分组字段忘了写"的失败模式。

---

## 12. JSON 解析器自写，不对外

`src/Application/Json.cpp`，只在 `golding::detail::json` 里用。

**原因**：core 只读两种文件（`plugin.json` 与插件自己的数据），需求固定。出现第二个需要
JSON 的模块时再整体替换，公共头不受影响。
