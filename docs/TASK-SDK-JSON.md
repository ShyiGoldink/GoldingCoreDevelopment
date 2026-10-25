# 任务书：JSON 解析器收口为 SDK 头（自身规划 / 用户提问「做成插件还是改 core」）

> 状态：**待执行**（本文件按任务逐条跟踪，完成规则见 §1）  
> 日期：2026-10-25  
> 来源：用户提问「下一个拿 nlohmann 做一个 json 解析器插件？还是直接改 core？」→ 核实结论见 §2  
> 目标版本：`0.2.1-a`（能力与收口）/ `0.2.1-b`（消费方迁移）/ `0.2.1-c`（文档口径）  
> 涉及层：`include/sdk`、`include/Application`、`src/Application`、`plugins/core/appmanager`、
> `plugins/core/spellbook`、根 `CMakeLists.txt`、`tests/`、`docs/`

---

## 0. 结论（TL;DR）

**不动 ABI、不加服务、不引第三方。** core 里那份 437 行读取器本来就写了"不如共用这一份"，
但它被编进 `golding_core`，而插件只链 `golding_sdk` ⇒ 头可见、符号不可用，于是同一份
JSON 在仓库里有**三份**读法（core 437 行 / spellbook 340 行 / appmanager 59 行）。

| 编号 | 任务 | 批次 | 对应来源 | 落点 | 代价 |
| - | - | - | - | - | - |
| T-01 | 把读取器搬成 `include/sdk/golding_json.h` | 0.2.1-a | 条目 1 | 新头；删 `Json.h` / `Json.cpp`；`ManifestLoader.cpp:8,17` | 中 |
| T-02 | 给 core 内部头加编译期守卫 | 0.2.1-a | 条目 4 | `include/Application/CoreInternal.h` + 各内部头首行 | 低 |
| T-03 | 给 SDK 加极小 `dump`（写侧） | 0.2.1-b | 条目 2 | `include/sdk/golding_json.h` | 低 |
| T-04 | appmanager 清单读写换共享件 | 0.2.1-b | 条目 3 | `AppStore.cpp:133-210`、`Load` / `SaveManifest` | 中 |
| T-05a | spellbook 抽纯函数并立断言 | 0.2.1-b | 条目 3、10 | `SpellbookPlugin.cpp:683-810`、`Ready` | 中 |
| T-05b | spellbook 换用 SDK 解析器 | 0.2.1-b | 条目 3 | `SpellbookPlugin.cpp:244-583`（删 340 行） | 低 |
| T-06 | 文档与契约口径同步 | 0.2.1-c | 条目 4、6 | `ARCHITECTURE.md` / `API.md` / `NOTES.md` / `CURSOR.md` | 低 |

两条不变量：

1. **能落在 `include/sdk` 的不做成服务。** 纯函数过 dll 边界要"不透明句柄 + 三段式 API"，
   还要定义重复键 / 保序语义；为一个纯函数付这份契约成本不值（见 §7）。
2. **先立断言、再换实现。** spellbook 今天一条断言都没有（§2 条目 10），所以 T-05a 必须
   先于 T-05b。appmanager 已有自检台，断言与替换可同任务（T-04）。

---

## 1. 任务维护规则（本文件的写法约定）

### 1.1 每个任务必须写三段

| 段 | 要求 |
| - | - |
| **准备怎么改** | 决策与选型：做什么、走哪条路、为什么；**放弃的候选要写出来** |
| **大致改动逻辑** | 代码级顺序：改哪个文件、加哪个函数、谁调谁、哪一步不能反。不贴大段代码 |
| **与改动前对比** | 表格；**至少一行"改动后仍不变"的边界**，否则是过度承诺 |

另附 **落点**、**验收**（可断言：自检函数名 / 探针输出 / ctest 名）、**风险与回退**（至少各一条）。

### 1.2 完成即压缩（核心规则）

任务完成后，把该任务整段压缩成一行：

```
- T-01 <标题>-已完成（<YYYY-MM-DD>，<一句可验证的结论>）
```

删除「准备怎么改 / 大致改动逻辑 / 与改动前对比 / 验收 / 风险」五个细目，并把 §6.2 进度表
的状态改为 `已完成`。**两处必须同改。** 细目不留在本文件里——它在 git 历史中，回看用
`git log --grep=T-01`。

### 1.3 四种状态

| 情况 | 写法 |
| - | - |
| 完成 | `- T-01 <标题>-已完成（日期，结论）` |
| 放弃 | `- T-01 <标题>-已放弃（原因；替代方案）` |
| 拆分 | `- T-01 <标题>-已拆分为 T-01a / T-01b`，各自走完整三段 |
| 阻塞 | `状态：阻塞（原因）`，保留全部细目，不压缩 |

### 1.4 完成的定义（DoD）

四条全中才算完成：编译通过 / 验收断言在自检里跑绿（不是临时探针）/ 涉及文档同步改 /
「与改动前对比」每行能对上实测。

---

## 2. 基线事实（核实结论）

| # | 条目 | 判定 | 证据 |
| - | - | - | - |
| 1 | core 有一份可用读取器，且自述"应共用" | 成立 | `include/Application/Json.h:7-9`；`src/Application/Json.cpp` 437 行，`#include` 只有 `<cctype>` `<cstdlib>`（可 inline） |
| 2 | 插件拿不到它：头可见、符号不在 | 成立 | `CMakeLists.txt:24-25`（`golding_sdk` = INTERFACE + `include/`）与 `:31-33`（`Json.cpp` 在 `golding_core` 源表）；`cmakes/GoldingPlugin.cmake:44` 插件只链 `golding_sdk inking::inking` |
| 3 | 同一份 JSON 有**三份**读法 | 成立 | core 437 行；`spellbook/src/SpellbookPlugin.cpp:244-583` 340 行（注释 `:238-240` 自述"独立于 core 的解析器，插件不链 core"）；`appmanager/src/AppStore.cpp:133-190` 59 行按 `find("\"apps\"")→find('[')→find(']')` 扫描 |
| 4 | 文档说"插件只拿 `include/sdk` 两个头"，构建实际暴露整棵 `include/` | 成立（**文档与构建不一致**） | `docs/ARCHITECTURE.md:45` vs `CMakeLists.txt:25` |
| 5 | 现有插件里有越界引用 core 内部头 | 不成立 | `grep -rn "Application/\|HostApp.h\|Tool/PathFinder" plugins/` 无命中 ⇒ 收口安全 |
| 6 | cursor 用 TSV 的原因是"拿不到解析器" | 成立 | `docs/CURSOR.md:83-84` 原文 |
| 7 | `settings/shape.json`、`GoldingGamestoryShell/shape.json` 是 JSON 消费方 | 不成立 | 全仓 `grep shape.json` 只命中 spellbook 自己的注释；core 侧无读取代码 ⇒ 与本任务无关 |
| 8 | 引 nlohmann/json 可行 | 部分成立 | 技术可行（header-only），但 25k 行/TU、默认抛异常（本仓禁令：异常不得穿 dll 边界）、且与现 `Value` 语义不同（保序、重复键全留）⇒ 记入 §7 |
| 9 | 需要因此升 `apiVersion` / `abiVersion` | 不成立 | `src/Application/HostApp.cpp:719-724` 只拒绝"高于宿主支持"的值；头文件不进 dll 边界，宿主不校验头 |
| 10 | spellbook 现有自检能当回归面 | 不成立 | `SpellbookPlugin.cpp:2068-2077` 的 `Ready` 直接 `return 0`，没有任何断言 ⇒ T-05a 必须先立 |

---

## 3. 第一批：`0.2.1-a`（能力存在 + 陷阱收口）

### T-01 把读取器搬成 `include/sdk/golding_json.h`

- T-01 把读取器搬成 `include/sdk/golding_json.h` — 已完成（2026-10-25：新头全 `inline`、命名空间
  `golding::json`，`Json.h` / `Json.cpp` 已删、core 源表已去；`ctest --test-dir build-dev` 2 项与
  `ctest --test-dir build` 8 项全绿，`core` 的 manifest 用例一行未改仍绿；`sdk_host` 新增 18 条
  `json.*` 断言（object / array / 转义 / `\uXXXX` 与代理对 / 深度超限 / `find` 未命中 / 严格性）
  且该目标只链 `golding_sdk` 不链 core；实测每 TU 编译增量约 600 ms（空 TU 110 ms → 706 ms））

---

### T-02 给 core 内部头加编译期守卫

- T-02 给 core 内部头加编译期守卫 — 已完成（2026-10-25：新增
  `include/Application/CoreInternal.h`，未定义 `GOLDING_CORE_INTERNAL` 时 `#error`；16 个内部头
  （14 个 `Application/*.h` + `HostApp.h` + `Tool/PathFinder.h`）首行已加守卫；宏只给
  `golding_core` / `golding_host` / `core_tests`，`sdk_host_tests` 不给（它是外部使用者样本）。
  探针：`AppManagerPlugin.cpp` 顶部加 `#include "Application/ManifestLoader.h"` → 编译期报出
  那句 `#error`，探针已撤；`grep -L "CoreInternal.h" include/Application/*.h include/HostApp.h`
  为空；build-dev 2 项与 build 8 项仍全绿）

---

## 4. 第二批：`0.2.1-b`（写侧 + 两个消费方）

### T-03 给 SDK 加极小 `dump`（写侧）

- **状态**：未开始
- **落点**：`include/sdk/golding_json.h`（`dump` + 内部 `escape`）

**准备怎么改**

读取器只读不写，而 `apps.json` 是要写的。加**一个** `std::string dump(const Value&)`。
不提供 pretty（缩进会把"格式化"变成第二个语义决策点，需要时再加参数，默认行为不变）；
不提供构造器（`Value` 成员公开，拼装十行就够，加了反而多一套要维护的 DSL）。
转义策略定死：只转 `"`、`\` 与 `<0x20` 控制字符（`\n \t \r \b \f` 用具名转义），
其余**原样输出 UTF-8**、不转 `\u`（与 `parse` 能读回来的最小集合对称）。

**大致改动逻辑**

1. 头里加 `inline void appendEscaped(std::string& out, const std::string& text)`。
2. `inline std::string dump(const Value&)` 按 `type` 分派；number 用 `%.17g` 保往返精度，
   整数值不输出小数点（`31.0` → `31`）。
3. 数组 / object 用 `,` 无空格（体积最小；要好看是 pretty 的活，不做）。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 需要写 JSON 的插件 | 只能手拼字符串（无转义） | `json::dump(value)` |
| 往返 | 不可断言 | `parse(dump(v))` 结构相等（T-03 验收里钉死） |
| `dump` 对非 ASCII | — | 原样输出 UTF-8（**不转 `\u`**，`parse` 照样读得回来） |
| 读取器 `parse` | — | **不变**，本任务不碰它 |

**验收**：`sdk_host` 新增 `[ok] json.dump 往返`（含 `"` `\` 换行 中文 各一条）与
`[ok] json.dump 整数不带小数点`。

**风险与回退**：风险 = 浮点精度选错（`%g` 默认 6 位会吃掉精度）⇒ 固定 `%.17g` 并用往返用例钉住。
回退 = `dump` 是新增函数，没人调就没有影响，删掉即回退。

---

### T-04 appmanager 清单读写换共享件

- **状态**：未开始
- **落点**：`plugins/core/appmanager/src/AppStore.cpp:133-190`（`ReadManifestIds`）、
  `:192-210`（`WriteManifestIds`）、`:540`（`Load`）、`:620`（`SaveManifest`）；
  `plugins/core/appmanager/src/AppManagerPlugin.cpp`（自检）

**准备怎么改**

用 `json::parse` / `json::dump` 换掉手写扫描，并**顺手把读写抽成按路径的纯函数**
（`ReadManifestFile(path, ids)` / `WriteManifestFile(path, ids)`），因为断开路径才喂得进
临时文件、才写得出断言——现在是硬绑 `ManifestPath()`，自检只能碰用户真实清单。

不做的事：ini 读写不动（ini 不是 JSON，换解析器与它无关）；不抽 `ManifestValue` 转换层
（多一层只为好看，调用点只两个）。

**大致改动逻辑**

1. 把 `ReadManifestIds` 改名为 `ReadManifestFile(path, ids)`：`parse` 成功且 `apps` 是
   字符串数组才收；单个元素类型不对就跳过并 `warn`（与现状"能读几个是几个"一致）。
2. `WriteManifestFile(path, ids)`：拼 `Value{object}`（`version`=1、`apps`=字符串数组），
   交给 `dump` 落盘。
3. `Load` / `SaveManifest` 改调这两个，`ManifestPath()` 只在这两处出现（保持"路径只有一个来源"）。
4. 自检新增：临时目录写一份 `{"version":1,"apps":["a","\u4e2d\u6587"]}` → `ReadManifestFile`
   读回 2 项且第二项是 `中文`；再 `WriteManifestFile` → 重读仍相等；跑完删目录。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 正常清单 | 手写扫描 | `json::parse` + 取 `apps` |
| id 里带 `\uXXXX` 转义 | 读成 `u4e2d` 字面量（跳过 `\` 后按普通字符收） | 正确解成 `中` |
| 清单里有嵌套数组 | `find(']')` 在第一个 `]` 截断，尾部 id 全丢 | 按结构解析，不再截断 |
| 写出的格式 | 手拼，无转义 | `dump`，转义合法 |
| 清单整体损坏 | 读不到 `"apps"` 就返回空 → 走 `.bak` + 重新播种 10 项 | **不变**（同样的兜底路径，只是判定更严格） |
| ini 读写 / `apps/<id>/` 目录 | 手写 ini | **不变**（本轮不动） |

**验收**：`[ok] appmanager.清单读写：转义/嵌套/往返`（临时目录，跑完清理）；
现有 10 条 appmanager 断言不加改动仍然绿；`host.collapsed` 与 `host.immersive` 仍绿。

**风险与回退**：风险 = 判定变严（部分损坏的清单以前能读出几条，现在整份失败 → 触发重新播种，
用户会看到预置十项而不是自己那几条）。这是**有意的行为变化**，需在 `docs/APPMANAGER.md`
§3.2 补一句"清单解析失败 = 整份作废"。
回退 = `ReadManifestFile` 保留旧实现作 fallback 分支是多余的，直接 `git revert`；用户数据未被删
（`.bak` 机制不动），最坏情况是清单被改名后重新播种。

---

### T-05a spellbook 抽纯函数并立断言

- **状态**：未开始
- **落点**：`plugins/core/spellbook/src/SpellbookPlugin.cpp:683-810`（`ReadLayer` 与顶层解析）、
  `:2068-2077`（`Ready`）

**准备怎么改**

spellbook 现在**一条断言都没有**（`Ready` 直接 `return 0`），所以换实现之前必须先有回归面。
做法：把"文本 → `ShapeDoc`"抽成一个纯函数 `ParseShapeDocument(const std::string&, ShapeDoc&)`，
`Ready` 里用两条内联样例断言。**实现暂不动**（仍用它自己那份解析器），这样 T-05b 才有
"断言不变、实现换掉"的干净对比。

**大致改动逻辑**

1. 把 `:683-810` 那段"读 `layers` 数组 → 每层 `ReadLayer(node, base, orbitDefault)`"从
   "读文件"里剥出来，文件读取只负责 `ifstream` 成 `std::string`。
2. `ShapeDoc` 只需三样东西给断言用：层数、`orbit` 默认值、第一层的 `kind`/`radius`/`color`。
3. `Ready` 加两条断言：
   - 完整样例（2 层 + 顶层 `orbit`）→ 层数 2、第一层 `kind` 与 `radius` 命中；
   - **省略字段**的样例（只写 `kind`）→ 其余取默认值（这条钉住"`base` 默认值"没在抽函数时丢掉）。
4. 非法输入（`{"layers": 3}`）→ 返回 false 且不改 `out`。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| `shape.json` 解析结果 | 由文件读取内联完成，无法单独调用 | 纯函数可调用，**结果不变**（同一份解析器） |
| spellbook 自检 | `Ready` 直接 `return 0`，零断言 | 3 条断言进 `Ready`，计入退出码 |
| 文件缺失 / 空文件 | 走"按名字哈希派生"的兜底 | **不变**（兜底还在，只是解析挪了位置） |
| 插件行为 | — | **不变**，本任务只抽函数 |

**验收**：`golding_host.exe --frames=1` 输出 `[ok] spellbook.shape 解析…` 三条；退出码 0；
`ctest --test-dir build` 8 项仍全绿。

**风险与回退**：风险 = 抽函数时把"`base` 提供默认值"这条语义抽丢（`ReadLayer` 的第三个参数
`orbitDefault` 就是从上层继承的）。第 3 条断言专门钉它。
回退 = 纯函数只是新入口，`Ready` 那三条删掉即恢复原状。

---

### T-05b spellbook 换用 SDK 解析器

- **状态**：未开始
- **落点**：`plugins/core/spellbook/src/SpellbookPlugin.cpp:238-583`（注释 + 340 行自写解析器）；
  `:683-810` 的调用点

**准备怎么改**

删掉整块 `namespace json`（340 行）与那句"独立于 core 的解析器，插件不链 core"的注释，
改用 `#include <sdk/golding_json.h>`。**保留 4 个取值助手**（`Text` / `Float` / `Number` /
`Object`，约 20 行）——它们是"带默认值的取值"糖，不属于解析器，改指向新 `Value` 即可。

**大致改动逻辑**

1. 删 `:244-583`，`#include <sdk/golding_json.h>`，`namespace json = golding::json;`（一处别名，
   让 `:683-810` 的调用点基本不用改）。
2. 成员名对齐：`Type::Array/String/Number/...` → `Type::array/string/number/...`；
   `->array` 不变、`->text` 不变（对照 `Json.h:31-46`）。
3. 4 个助手改为读 `golding::json::Value`，语义照旧（缺键取默认值）。
4. 顶层键名逐个核对：`layers` / `orbit` / 每层的 `kind radius width lobes innerRatio
   amplitude phase spin orbit alpha color points marks count` —— 一个都不许漏，
   漏了就是"某一层静默退回默认形状"。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| T-05a 的三条断言 | 绿 | **仍绿**（这是本任务唯一的行为判据） |
| `shape.json` 里带 `\u` 转义 | 自写解析器不支持（按字面量收） | 支持（共享解析器有 `\uXXXX` 与代理对） |
| `shape.json` 里出现注释 / 尾逗号 | 自写解析器可能容忍 | 共享解析器一律拒绝（严格 JSON）⇒ 现有 `shape.json` 必须仍是合法 JSON |
| spellbook 源码 | 一份 340 行解析器副本 | 净减约 320 行 |
| 其它插件 | — | **不变**（本任务只碰 spellbook） |

**验收**：T-05a 那三条断言全绿；`spellbook.dll` 大小下降（记录到完成行里）；
`grep -c "namespace json" plugins/core/spellbook/src/SpellbookPlugin.cpp` == 0。

**风险与回退**：风险 = 现有 `shape.json` 里写了注释或尾逗号，换严格解析后**整份失效**，
法阵退回默认形状且只有一行日志。规避：动手前先跑一次 `python -c "import json;json.load(open(...))"`
确认它是合法 JSON。回退 = `git revert` 该提交，自写解析器整块回来，T-05a 的断言仍然有效。

---

## 5. 第三批：`0.2.1-c`（文档口径）

### T-06 文档与契约口径同步

- **状态**：未开始
- **落点**：`docs/ARCHITECTURE.md:45`（三条铁律第 2 条与 SDK 表面表）、`docs/API.md`、
  `docs/NOTES.md`、`docs/CURSOR.md:83-84`、`docs/APPMANAGER.md` §3.2、`README.md` 插件一节

**准备怎么改**

四处口径现在与代码**对不上**，趁这次一起收：

1. `ARCHITECTURE.md:45` 说"插件只拿 `include/sdk/` 两个头"，而构建实际暴露整棵 `include/`
   （§2 条目 4）。改成"两个头 + `golding_json.h`（可选）"，并补一句"core 内部头有编译期守卫（T-02）"。
2. `CURSOR.md:83-84` 把"用 TSV"归因成"拿不到解析器"。**这个原因本轮消失**，但格式**不迁**
   （用户数据在 `%APPDATA%`，迁移要写迁移器 + 备份）。口径改成"历史原因 + 迁移成本"。
3. `NOTES.md` 补一条坑：**头可见但符号在别的库里 = 链接期才炸**，附一句"spellbook 因此自带
   340 行解析器"作为代价证据，并指向 T-02 的守卫。
4. `API.md` 加"插件可用的 SDK 头"清单（现在只有两个，加第三个必须写清支持/不支持什么）；
   `APPMANAGER.md` §3.2 补"清单解析失败 = 整份作废"（T-04 的行为变化）；`README.md` 一句话。

**大致改动逻辑**

先改 `API.md` 的清单（它是对外承诺），再改 `ARCHITECTURE.md` / `NOTES.md` 的口径，
最后改 `CURSOR.md` 与 `APPMANAGER.md` 的具体段落。**顺序不能反**：承诺先立，理由后补——
反了就会出现"文档解释了为什么，但没说要给什么"。

**与改动前对比**

| 场景 | 改动前 | 改动后 |
| - | - | - |
| 新插件作者想解析 JSON | 文档里找不到，只看到两份手写实现 | `API.md` 明确列出 `golding_json.h` |
| `CURSOR.md` 的 TSV 理由 | "插件拿不到解析器"（已过期） | "历史 + 迁移成本"（仍然成立） |
| `ARCHITECTURE.md` 与构建 | 说法不一致 | 一致，且注明守卫的存在 |
| 已完成的语法与行为 | — | **不变**，本任务只改字 |

**验收**：`grep -c "插件拿不到 core 的 JSON 解析器" docs/CURSOR.md` == 0；
`grep -l "golding_json.h" docs/*.md` 至少 3 个文件；`check_task_book.py docs/TASK-SDK-JSON.md` 0 错误。

**风险与回退**：风险 = 文档承诺多于实现（例如写了 `dump` 却没写它不转 `\u`）。
规避：每条承诺都从 T-01~T-05 的验收里抄。回退 = 文档改动单独一个提交，`git revert` 即可。

---

## 6. 批次门槛与进度

### 6.1 门槛

| 批次 | 发布前必须满足 |
| - | - |
| `0.2.1-a` | `ctest --test-dir build-dev`（2 项）与 `ctest --test-dir build`（8 项）全绿；`sdk_host` 含 `json.*` 用例；探针证明"插件引 core 内部头"是**编译期**错误（探针已撤）；`grep -L "CoreInternal.h" include/Application/*.h include/HostApp.h` 为空 |
| `0.2.1-b` | appmanager 新增清单断言绿、现有 10 条不变；spellbook 三条断言绿且源码净减 ≥300 行；手动把 `apps.json` 改名验证一次 `.bak` + 重新播种仍生效 |
| `0.2.1-c` | 四处口径改动全部落地；`check_task_book.py` 0 错误；`ctest` 仍 10 项全绿 |

### 6.2 进度表

| 编号 | 任务 | 批次 | 状态 |
| - | - | - | - |
| T-01 | 把读取器搬成 `include/sdk/golding_json.h` | 0.2.1-a | 已完成 |
| T-02 | 给 core 内部头加编译期守卫 | 0.2.1-a | 已完成 |
| T-03 | 给 SDK 加极小 `dump`（写侧） | 0.2.1-b | 未开始 |
| T-04 | appmanager 清单读写换共享件 | 0.2.1-b | 未开始 |
| T-05a | spellbook 抽纯函数并立断言 | 0.2.1-b | 未开始 |
| T-05b | spellbook 换用 SDK 解析器 | 0.2.1-b | 未开始 |
| T-06 | 文档与契约口径同步 | 0.2.1-c | 未开始 |

完成任务后，本节状态改 `已完成`，§3~§5 对应段落按 **§1.2** 压缩成一行——两处必须同时改。

---

## 7. 明确不做（避免反复讨论）

| 不做的事 | 原因 |
| - | - |
| 做成插件 / 服务 `golding.json` | 纯函数过 dll 边界 ⇒ 不透明句柄 + `parse/query/free` 三段式，还要把"重复键怎么办""保不保序"写进契约；更糟的是它自带"服务不在就退回手写"的悖论式降级 |
| 引 nlohmann/json | 25k 行/TU 的编译成本；默认抛异常（本仓铁律：异常不得穿 dll 边界）；与现 `Value` 语义不同（保序、重复键全留）⇒ 两份方言，正是本项目反复付账的"一个事实两个来源" |
| 把 core 内部头搬出 `include/` | 收益与 T-02 相同，代价是 include 布局 + `host/` + `tests/` 路径全改、diff 噪声大。先把陷阱变成编译期错误，搬目录留到真需要单独发布 core 头时再说 |
| 把 `cursors.index` 迁成 JSON | 用户数据在 `%APPDATA%`，迁移要写迁移器 + 备份 + 回滚；TSV 仍然合法（口径见 T-06） |
| 支持 JSON5 / 注释 / 尾逗号 | 解析器保持窄：换实现时 API 不变、语义不扩。要注释就换配置格式，不要在 JSON 上加方言 |
| 借这次升 `apiVersion` / `abiVersion` | 头文件不进 dll 边界；`HostApp.cpp:719-724` 只拒绝"高于宿主支持"的值。两个版本号与本次改动无关 |
| 给 `dump` 加 pretty / 构造器 DSL | 缩进会把"格式化"变成第二个语义决策点；构造器是多余的 DSL（`Value` 成员公开，拼装十行） |
