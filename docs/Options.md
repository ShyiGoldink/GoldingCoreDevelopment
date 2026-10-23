# Options（已定取舍清单）

> 状态：**生效中**
> 日期：2026-10-23
> 用途：记录**有意为之的妥协**——已经决定"就这样"，不是欠账也不是 bug。
> 新来者看到某处行为"不太对"时先查这里，避免重新讨论一遍；每条都给解除条件与检查点。
> 与 `docs/TASK-CORE-UPING.md` 配套：任务书管"要做什么"，本文件管"决定不做什么"。

---

## 1. 服务版本与 SDK 版本：校验暂时放行（`sdkId` 除外）

| 项 | 内容 |
| - | - |
| **决定** | 放行两项：① 服务 vtable 的 `abiVersion`——`Host::Use<T>(name, minVersion)` 读到低于 `minVersion` 时只打 `（版本不足，放行）` 并照常返回 vtable；② descriptor 的 `sdkVersion`——与宿主不同时只打 `（SDK 版本差异，放行）` 并照常加载。**不放行**：`sdkId`（必须相等，不等即拒）与 `abiVersion`（必须相等，不等即拒）。 |
| **原因** | 两个版本号都还没有可信的真源。InkingUIKit 尚未发布正式版，其 ABI 还会变；SDK 的编译栈按用户 2026-10-23 的决议，要等正式版发布前才选定（"选择一个比较合适的编译栈"）——在编译栈定下来之前，`sdkVersion` 无法真正代表"头文件集合 + 编译器 ABI"这个组合，强行校验只会得到一个必须每次手改的数字。 |
| **现状证据** | `GoldingServices.h:16/28/44` 三个服务结构体都有 `abiVersion` 首字段，但**全仓库零处读取**。本决定把这件"从未发生过的校验"显式写成"暂时不校验"，而不是继续假装它在。 |
| **解除条件** | 两条分开：① 服务 `abiVersion` → InkingUIKit 发布 1.0 且 ABI 冻结后改为拒绝；② `sdkVersion` → 正式版选定编译栈、并让 `GOLDING_SDK_VERSION` 有可信真源（git tag 或 `CMakeLists.txt` 的 `project(VERSION)`）之后改为严格。 |
| **检查点** | `docs/TASK-CORE-UPING.md` T-06 第 3 步（`sdkId` 严格 / `sdkVersion` 记日志）、T-07 第 4 步（服务版本放行）；三处日志必须互相可区分："能力缺失（返回 nullptr）" / "版本不足（放行）" / "SDK 版本差异（放行）"。 |
| **风险** | 长期放行会让版本号彻底沦为装饰——这正是 `plugin.json` 里那份 `abiVersion` 的死法。因此解除条件都写成**外部事件**（库发版 / 编译栈选定），不写成"等有空"。 |

---

## 2. 插件崩溃不隔离

| 项 | 内容 |
| - | - |
| **决定** | 不做 SEH / 进程外插件 / 异常边界。插件在 `entry`、`ready` 或点击回调里崩，整个宿主进程一起崩。 |
| **原因** | 用户明确取向：允许瑕疵，优先把核心内容做对；崩溃靠日志定位。隔离方案（子进程插件、崩溃重启）与"一个 dll 一个插件"的轻量模型冲突，成本远高于当前收益。 |
| **补偿措施** | 每次注册、每次能力查询都留一行日志（`HostApp.cpp:66-283` 已如此，T-02 的 `Host` 继续沿用）；档案表 8 态如实反映每一步结果。**没有这些日志，本决定不成立。** |
| **解除条件** | 出现"插件崩溃导致用户数据丢失"或第三方插件进入分发阶段。 |
| **检查点** | 排查任一插件故障时，日志必须能回答"最后一步在做什么、往哪张表、结果如何"。答不上来就说明补偿措施退化了，那比崩溃本身严重。 |

---

## 3. `kind` 不做强类型枚举，core 不解释它

| 项 | 内容 |
| - | - |
| **决定** | `PluginManifest::kind` 是自由字符串（`"panel"` / `"page"` / `"logic"` / `"widgets"` …），core 只存、只展示、**绝不 switch**。唯一的例外是既有代码里 `role == "panel"` 的面板插件选择，那条沿用 manifest 的 `role` 字段，不迁到 `kind`。 |
| **原因** | 一旦 core 认识 `"panel"` 以外的 kind，就会长出第二张特化表——`PageRegistry` / `SettingsRegistry` 与 `RegionRegistry` 并存的那笔账就是这么来的。 |
| **解除条件** | 不需要解除。这是不变量，不是妥协。 |
| **检查点** | code review 时 grep `kind`，若出现 `==` 或 `strcmp` 就是违规。 |

---

## 4. 升 ABI 版本不向后兼容

| 项 | 内容 |
| - | - |
| **决定** | `PluginLoader` 的闸门保持严格相等（`descriptor->abiVersion != expectedAbi` 即拒）。当前是 **v10**（`TASK-CORE-PLUGINS.md` T-10a）：升一次，所有插件全部被拒，必须同一次重编。 |
| **原因** | 全部插件在同一个 CMake 工程内，重编是一条命令；而兼容分支（"v9 插件没有 pluginDir 怎么办"）会长期活着，成为第二套路径。实验期不背这个包袱。 |
| **解除条件** | 有第三方分发的插件、且它们不能跟随 core 重编时，才引入"最低 ABI + 可选字段"机制。 |
| **检查点** | `docs/TASK-CORE-UPING.md` T-06；验收里明确"把一个插件的 abiVersion 改成 8，宿主应报 status=3 且不崩"。 |

---

## 5. `plugin.json` 的 `requires.plugins` 只过校验，不参与解析

| 项 | 内容 |
| - | - |
| **决定** | `ManifestLoader` 仍要求它存在且是数组（可为 `[]`），否则整份 manifest 判 `Invalid`；但它被写入 `Manifest::dependencies` 后**没有消费者**，真实依赖只认 descriptor 的 `depends`（即 `Manifest().depends`）。 |
| **原因** | 依赖是编译期事实，写两处必然对不上（历史上 8 个插件有 6 个 json 写着旧 `abiVersion`，无人发现）。保留"字段必须存在"是过渡态，避免旧文档与旧插件同时失效。 |
| **解除条件** | T-05（全部插件迁到新接口）完成后，可以把该字段从必需降为可选、或彻底移出 manifest，让 json 只剩展示信息。 |
| **检查点** | `ManifestLoader.cpp:157-165`；`README.md` §3 对该字段的描述已过时，改文档时一并处理。 |

---

## 6. 一次进程一个 `HostApp`

| 项 | 内容 |
| - | - |
| **决定** | `HostApp::uiRoot()` / `bootMode()` 落在**文件静态量**（`HostApp.cpp:30-50`），而不是实例成员。因此一个进程内只能有一个 `HostApp`，开第二个会互相覆盖。 |
| **原因** | 插件的回调只拿得到 `hostContext`（= `&entry.provider`，一个字符串指针），拿不到宿主实例。改成实例指针要动 ABI 与全部插件的取值点。 |
| **解除条件** | 真出现第二个宿主实例（例如同时开两个窗口跑两套插件）时，把 `hostContext` 换成宿主上下文结构体并同步升 ABI。 |
| **检查点** | `HostApp.h` 头注释已写明该约定；新建 `HostApp` 的地方必须只有 `main.cpp` 一处。 |

---

## 7. 两张特化表与 Region 并存

| 项 | 内容 |
| - | - |
| **决定** | `PageRegistry`（内容区）与 `SettingsRegistry`（设置页）保留为专门代码，与通用 `RegionRegistry` 并存，不归并。 |
| **原因** | 先让通用机制存在并可验证，再统一；`dev001` 站稳之前归并会同时动 5 个插件与 2 个 core 模块。沿用 `docs/hardening-task-book.md` §7 的决定。 |
| **解除条件** | `dev001` 站稳、且 Region 机制被至少两种面板插件验证过。 |
| **检查点** | 三者并存期间，新增可注册位置**一律走 Region**，不许再长第四张特化表。 |

---

## 8. `Plugin` 基类的虚表冻结在三个钩子

| 项 | 内容 |
| - | - |
| **决定** | `golding::Plugin` 只有 `Manifest()` / `Register()` / `Ready()` 三个虚函数（+ 虚析构），**发布后只许末尾追加**。所有新增能力长在 `Host` 上（宿主侧 C 结构体末尾追加），不长在基类。 |
| **原因** | 基类由插件作者继承，中间插虚函数会让所有旧插件错位；而 `Host` 由宿主持有，追加字段已经被 `GoldingHostApi` 的 v5→v6→v7→v8 验证过三次。 |
| **解除条件** | 不需要解除。这是不变量。 |
| **检查点** | 改 `Plugin` 基类就是 ABI 事件：必须同时升 `GOLDING_PLUGIN_ABI_VERSION` 并重编全部插件。 |

---

## 9. 纯数据插件（无 dll）暂不装配

| 项 | 内容 |
| - | - |
| **决定** | 没有 `targets.win64.lib` 的插件，档案里标 `NoDll(4)`，不进依赖图、不装配。（原先这条写的是"声明了 `widgets{}` 的插件"；`widgets{}` 已在 T-11 删除，2026-10-23，判据改为"没有 dll"。） |
| **原因** | 装配路径（"读配置 → 建组件子树"）一行代码都没有；先让它**可见地不工作**，好过假装支持。配置数据的正路已经由 `magic.json` 走通：**插件自己读、自己解释**，不需要 core 认识字段。 |
| **解除条件** | 需要"只改配置不改代码就能加一个组件"时再做——那时也应当先问"为什么不是插件自带这份配置"。 |
| **检查点** | `HostApp.cpp` 里 `!manifest.hasDll` 那条分支；注意副作用：`NoDll` 插件不在解析图里，依赖它的插件会被判 `MissingDependency` 而不是 `NoDll`——排查时别找错人。 |

---

## 10. `plugin.json` 顶层键白名单

| 项 | 内容 |
| - | - |
| **决定** | 顶层只收 `name` / `title` / `version` / `apiVersion` / `role` / `requires` / `targets`；别的键判 `Invalid`，键名写进 `ManifestResult::detail` 并打进启动日志。 |
| **原因** | 同一个病已经犯过一次：`abiVersion` 曾同时写在 json 与 dll 里，8 个插件有 6 个写着旧版本而没有任何代码发现。**写了不生效**比写错更坏——它让人以为配置生效了。`widgets{}` 挂在 manifest 里半年、零消费者，正是同一种死法。 |
| **解除条件** | 出现"第三方插件必须带自己的元数据"的真实需求时，改为"白名单 + 一个 `extra{}` 袋子"，且 `extra` 只展示不解释。 |
| **检查点** | `ManifestLoader::read` 里那张 `kKnownKeys`；`tests/core_tests.cpp` 的"未知键 widgets → Invalid"一条。 |

---

## 11. `entry` / `ready` 只做"尽力兜底"，不做插件隔离

| 项 | 内容 |
| - | - |
| **决定** | `HostApp::registerAll` 与 `notifyReady` 用 `try/catch` 包住插件的两个钩子。接住就把该插件判 `EntryFailed(5)` 并继续；`ready` 抛异常则**只记 1 项失败、不撤销它的注册**（它已经装好了，撤销反而留下半装配状态）。 |
| **原因** | 解析层早就做到了"一个插件坏了只影响它自己"，执行层没有——一个插件抛异常就把整个宿主带走，前面 9 个已经装好的插件全部白装。包一层成本近乎为零，收益是这条原则终于闭环。 |
| **解除条件（做不到的部分）** | ① **结构化异常（SEH）**——空指针写、栈溢出、除零——在 MinGW 下不经过 `catch(...)`；② 异常要穿过 dll 边界，两侧必须用同一套运行库，插件静态链 `libstdc++` 时抛出点直接 `terminate`，宿主根本看不到。要真隔离只能换进程边界。 |
| **检查点** | `HostApp.cpp` 两处 `catch`；实测方式见 HANDOVER §12（临时让 selftest 抛一次，日志出 `entry 抛异常：selftest`，10 个插件照常解析、`== 失败项 ==` 正常收尾）。 |

---

## 12. 页表 / 设置表 / 区域条目收成一个 `SlotTable`，但不合成一张表

| 项 | 内容 |
| - | - |
| **决定** | 三处共用一个模板 `SlotTable<TDecl>`（`include/Application/SlotTable.h`）：id 唯一、按 order 排好、带提供方、同名驳回、清空。页表与设置表是它的两个实例；区域表的**每个区域**各持一个实例。**没有**把页和设置改成"两个保留区域"。 |
| **原因** | 重复的是**规则**（排序、去重、提供方、统计、清空各写三遍），不是身份。真按"两个保留区域"合并，core 就得把 `page` / `settings` 当成区域暴露给插件：`allRegions()` 凭空多两项、面板遍历时会把页面当条目摆——为一个内部实现改动**插件可见的语义**，换来的只是少一个类名。 |
| **唯一按表不同的行为** | 同 order 时：页 / 设置按 id 字典序（与加载先后无关）；区域条目先到先得（面板按注册顺序摆，重排会让用户眼前跳）。这条由 `SlotTieBreak` 表达，单测各钉一条。 |
| **不做的事** | 不把 `GoldingPageDecl` / `GoldingSettingsDecl` 合成一个 SDK 结构体——那是 ABI 变更，会逼 10 个插件重编，收益只是省一个字段名。 |
| **解除条件** | 出现"页面也要 icon / onActivate"这类需求时，页面本来就该变成区域条目，那时再合并身份是顺水推舟。 |
| **检查点** | `SlotTable.h` 的 `Rules`（`requireCreate` / `allowDuplicateIds` / `scope`）；`tests/core_tests.cpp` 的排序两条、重名放行两条。 |

---

## 13. `dev001` 只承载 core + 插件开发包

| 项 | 内容 |
| - | - |
| **决定** | `dev001` 上只有 `golding_core` 静态库与插件开发包：`include/`（SDK 头 + core 头）、`src/`（14 个 cpp）、`tests/`（`core_tests` + `sdk_host_tests`，两者零 UI 依赖）、插件构建函数、`docs/`。**不含** `host/`（窗口与 InkingUIKit）、`plugins/`（10 个产品插件）、`third_party/`。 |
| **原因** | 这个分支的用途是"给别的宿主和第三方插件一个稳定的编译面"。带上 `host/` 就等于把 InkingUIKit、SDL3、FFmpeg 一起变成 core 的依赖——`GOLDING_WITH_INKING=OFF` 那条"core 可无头自证"的性质当场作废，而那正是这个库唯一的卖点。 |
| **代价** | `dev001` 上没有可运行的宿主：`ctest` 只有 `core`（49 项）与 `sdk_host` 两个 test，界面相关的一切只能在当前仓库里验。这是有意的取舍——`dev001` 保证"能编、能测"，当前仓库保证"能跑、能看"。 |
| **`HostApp` 归谁** | **已随 core（2026-10-23）**：`host/HostApp.{h,cpp}` 本来就零 InkingUIKit 依赖（只 include core 头 + `sdk/golding_plugin.h`），UI 相关全在 `host/main.cpp`，所以它是"位置在 `host/` 的 core 模块"。现在挪成了 `include/HostApp.h` + `src/Application/HostApp.cpp`（编进 `golding_core`），`host/` 只剩 `main.cpp`。连带影响：宿主工程的 include 路径改由 `golding_core` 的 PUBLIC include 目录提供，`#include "HostApp.h"` 写法不变。 |
| **构建入口怎么分** | `CMakeLists.txt`（根）= **只有 core 与它的单测**，不含任何 InkingUIKit / 插件 / 宿主字样；`host/CMakeLists.txt` 只在 `GOLDING_WITH_INKING=ON` **且 `host/` 存在**时才被 `add_subdirectory`；插件清单在 `plugins/CMakeLists.txt`，规则在 `cmakes/GoldingPlugin.cmake`。开发包检出即使误开 `ON` 也只打一行 STATUS 后照常构建 core。 |
| **M-1 插件源码在仓库外** | **不做**（2026-10-23）。插件按约定有自己专属的文件夹（`plugins/core/<name>/`、`plugins/dev/<name>/`），拿名字拼路径就是对的。真出现外部插件工程时再加参数，那时才知道它长什么样。 |
| **M-2 拆 `CMakeLists.txt`** | **已做**（2026-10-23）：根 / `host/` / `plugins/` + `cmakes/GoldingPlugin.cmake` 四处。两处必须记住的细节：`golding_host` 用 `RUNTIME_OUTPUT_DIRECTORY` 钉在**顶层**二进制目录（否则 exe 落到 `build/host/`，`PathFinder` 按"exe 旁边的 plugin/"就一个插件都扫不到）；`golding_add_plugin` 的源码参数按**仓库根**相对写，函数里补 `CMAKE_SOURCE_DIR` 前缀。 |
| **M-3 零 UI 插件样例** | **不做**（2026-10-23）。判据是"无 UI 插件要不要额外逻辑"——答案是不需要：清单里 `role` 可选，core 唯一跟 UI 沾边的判断是挑 `role == "panel"`；`SlotTable::Rules::requireCreate` 虽然默认 true，但 `RegionRegistry` 早就设成 false（"纯动作条目没有 create"）；`registerWidgetClass / mountPage / mountSettingsSection` 全是宿主主动调。所以"纯逻辑插件不被迫依赖 UI 库"在头文件和登记表两层都已经成立，再补一个样例只是"少写几个函数"，没有新分支可验证。解除条件：真出现第三方插件照文档写不出来时，把那个具体卡点记进 `open-questions.md`。 |
| **检查点** | 只有 `include/ src/ tests/ cmakes/ CMakeLists.txt` 的检出：`cmake ... -DGOLDING_WITH_INKING=OFF && ctest` = 2/2；即使 `ON` 也只构建 core 并 2/2（实测 2026-10-23）。 |

---

## 附：本文件与任务书的分工

| 文件 | 管什么 | 什么时候删改 |
| - | - | - |
| `docs/TASK-CORE-UPING.md` | 还没做的改造，一条条任务 | 任务完成即压缩成一行，细目进 git |
| `docs/Options.md`（本文件） | 决定不做的妥协，每条带解除条件 | 只在解除条件成立时改（改成"已解除"并注明日期） |

新增一条的判据：**有人问过两次"这里为什么不 XXX"**。只被问一次的写进 review 或 commit message，不够格进这里。
