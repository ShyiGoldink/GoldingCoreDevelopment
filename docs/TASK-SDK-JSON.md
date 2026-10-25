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

- T-03 给 SDK 加极小 `dump`（写侧）— 已完成（2026-10-25：`golding_json.h` 新增
  `appendEscaped` + `dump`：转义只转 `"` / `\` / `<0x20`（后者里五个用具名转义，再其余
  转成 u 型转义），非 ASCII 原样输出 UTF-8 不转；number 走 `%.17g`，整数值不输出小数点，
  非有限数落 `null`（兜住手拼的 Value，保证输出永远是合法 JSON）。`sdk_host` 新增 9 条
  `json.dump` 断言：往返（引号 / 反斜杠 / 换行 / 中文 / 嵌套数组）、非 ASCII 不转转义、
  整数不带小数点、负整数、`%.17g` 精度）

---

### T-04 appmanager 清单读写换共享件

- T-04 appmanager 清单读写换共享件 — 已完成（2026-10-25：删掉 `ReadManifestIds` /
  `WriteManifestIds` 那段手写扫描，换成按路径的 `ReadManifestFile` / `WriteManifestFile`
  （对外，喂得进临时文件才写得出断言）；解析走 `json::parse`、落盘走 `json::dump`。
  `ManifestPath()` 仍只出现在 `Load` / `SaveManifest` 两处。自检新增
  `[ok] appmanager.清单读写：转义/嵌套/往返`（临时目录，读到 3 项 a / 中文 / b、
  嵌套数组项跳过、写回再读一致，跑完删目录）；原有 18 条 appmanager 断言不加改动仍绿，
  build 8 项全绿。门槛里的"改名验证"用损坏清单实测：日志出
  `清单不是合法 JSON 对象` → 改名 `apps.json.bak` → 重新播种 10 项 → 用户清单已还原。
  `docs/APPMANAGER.md` §3.2 已补"整份作废"口径）

---

### T-05a spellbook 抽纯函数并立断言

- T-05a spellbook 抽纯函数并立断言 — 已完成（2026-10-25：把"文本 → PluginShape"抽成纯函数
  `ParseShapeDocument(text, hash, out)`（`hash` 由插件名派生、作为入参传入，这才是纯函数）；
  `LoadShape` 只剩"读文件 + 转调"。失败时 `out` 一个字节都不动（先装临时容器再搬）。
  `Ready` 从 `return 0` 改成跑三条断言并计入退出码：完整样例（2 层 + 顶层 orbit / radius）、
  省略字段取默认（钉住 `orbitDefault` 继承没被抽丢）、非法输入拒收且不改 out。
  三条全绿，`golding_host --frames=1` 输出 `[ok] spellbook.shape …`，build 8 项仍全绿。
  **实现未动**，仍是它自己那份解析器——这是 T-05b 的对比基线）

---

### T-05b spellbook 换用 SDK 解析器

- T-05b spellbook 换用 SDK 解析器 — 已完成（2026-10-25：删掉自写 `namespace json` 340 行与
  那句"独立于 core 的解析器，插件不链 core"的注释，改 `#include <sdk/golding_json.h>`；
  保留 4 个取值糖但搬进 `shapejson`（`Text` / `Float` / `Number` / `Object`，约 45 行）。
  T-05a 那三条断言**不加改动仍绿**（唯一的行为判据）。
  实测数字：源码 2079 → 1890 行（净减 189；门槛写的 ≥300 未达成——差额是 T-05a 必须
  新增的 95 行断言）；`grep -c "^namespace json"` == 0；
  `spellbook.dll` 248,788 → 253,687 B（**没有变小**，与验收预期相反：分离测量
  = 换解析器 +1,919 B + 自检 +2,980 B；共享解析器比自写那份做的事更多——u 型转义、
  代理对、深度 64）。动手前已用 `json.load` 确认三个 `shape.json` 都是严格 JSON）

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
| T-03 | 给 SDK 加极小 `dump`（写侧） | 0.2.1-b | 已完成 |
| T-04 | appmanager 清单读写换共享件 | 0.2.1-b | 已完成 |
| T-05a | spellbook 抽纯函数并立断言 | 0.2.1-b | 已完成 |
| T-05b | spellbook 换用 SDK 解析器 | 0.2.1-b | 已完成 |
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
