# GoldingCore 整体结构（2026-10-22 定稿草案）

目标：**以后只完善 core、只写插件，就能做出新项目。**
宿主不认识任何具体业务；业务全部由插件提供；插件之间只通过 core 的登记表认识彼此。

---

## 1. 三层与依赖方向

```
        ┌─────────────────────────────────────────┐
        │  插件（第三方 / 官方）                   │
        │  dll + json + assets                    │
        └───────────────┬─────────────────────────┘
                        │ 只依赖 ↓（单向）
        ┌───────────────┴─────────────────────────┐
        │  SDK 头（作者下载的那一份）              │
        │  golding_plugin.h    C ABI               │
        │  GoldingPlugin.h     Plugin 基类 + Host   │
        │  GoldingWidgets.h    构件包装（要 UI 才拿）│
        └───────────────┬─────────────────────────┘
                        │
        ┌───────────────┴─────────────────────────┐
        │  宿主 core（GoldingCore）                │
        │  扫描 / 注册 / 装配 / 动作表 / 画布      │
        └─────────────────────────────────────────┘
```

**core 不认识任何插件**，插件也不认识 core 的内部实现 —— 只有 SDK 头这一层是共同语言。

---

## 2. 插件的三件形态

一个插件是一个文件夹，由**三个来源**共同描述，各管一件事：

| 来源 | 内容 | 谁读 |
| --- | --- | --- |
| `plugin.json` | 清单：`name` `title` `version` `apiVersion` `abiVersion` `role` `requires` `targets` | core 扫描期 |
| `bin/<name>.dll` | 代码：界面、逻辑、绘制 | core `LoadLibrary` |
| `magic.json` | 魔法阵元描述：环半径、颜色、参数旋钮、预设、点列 | 魔法阵插件 |
| `assets/` | 图片等资源 | 插件自己 |

**依赖声明放在头文件里**（`GOLDING_EXPORT_PLUGIN` 把 `PluginManifest::depends` 带进
descriptor 的静态字面量），不在 json 里。理由：依赖是**编译期就知道**的事实，写两处必然对不上；
json 只放"给人和给可视化看"的东西。

**魔法阵的元描述放 json**。理由：它是**展示**，不是逻辑；插件作者换一套视觉
不该重新编译；而且它天然可以由编辑器生成。

### 2.1 装配时序（ABI v9）

| 阶段 | 谁被调 | 那时候有什么 |
| --- | --- | --- |
| 扫描 / 加载 / 解析 | — | 只有文件系统与 dll 里的 descriptor（含 ABI / sdkId / 静态能力宣言） |
| `entry`（每个插件一次，拓扑序） | `golding_plugin_entry` | 注册表空着，界面还没建 |
| 装配 | 面板插件的 `create`（`rootWidgetClass`） | 宿主给一个父节点（场景根） |
| `ready`（每个插件一次，拓扑序） | descriptor 的 `ready` | `uiRoot` 已可查、页面已挂上 |

**自检挂在 `ready`**：断言"根铺满了吗""每一页都装得上吗"必须发生在**装配之后**，
而 `entry` 太早（那时连窗口都没有）。`ready` 返回失败项数，宿主汇总成退出码的一部分。

**宿主自己的冒烟检查不能搬进插件**（"扫到几个插件""有没有一个装成功""面板在不在"）：
自检插件没被加载、或加载失败时，插件化的自检会**静默通过**——而那正是最该报错的情形。

**能力目录（ABI v9）**：每个插件在 `Manifest().provides` 里静态声明"我提供哪些能力"，
在 `Register()` 里真正 `AddService` 注册 vtable；`needs` 声明"我需要哪些能力"。
宿主把声明汇总成目录，于是三件事都能在**装配之前**或**注册之后立刻**判出来：

| 情形 | 处置 |
| --- | --- |
| `needs` 里的能力没人声明提供 | 该插件标 `state=6`、不装配，**其余照常**（`CapabilityRegistry::setStrict(false)` 可退成只留痕） |
| 声明了 `provides` 却没注册 | 该插件标 `state=5`（它是坏的），在 `selectPanel()` **之前**判，免得面板被选上再装配失败 |
| 注册了却没声明 | 只提示一行（允许"内部能力"），不影响状态 |
| 服务 `abiVersion` 低于插件要的 | **放行**并注明（`Options.md` §1） |

**模式怎么传给插件**：宿主只认窗口层那几个（`--stay` / `--fullscreen`），界面层的
（`--expanded` / `--page=N` / `--theme=` / `--overlay` / `--probe`）由宿主经
`bootMode` 原样交出、谁认谁跑——"`--expanded` 是什么意思"是面板插件的知识，core 不知道。

---

## 3. SDK 头：作者唯一要拿的东西

SDK 分两层，**按需要拿**：

| 头 | 给谁 | 依赖 |
| - | - | - |
| `sdk/golding_plugin.h` | 所有人（C ABI 契约，只有 POD 与函数指针） | 无 |
| `sdk/GoldingPlugin.h` | 所有人（`Plugin` 基类 + `Host` 注册面） | 无 UI 依赖 |
| `sdk/GoldingWidgets.h` | 要画界面的插件（构件的便利包装，见 T-03） | InkingUIKit |

```cpp
// include/sdk/GoldingPlugin.h
class MyPlugin final : public golding::Plugin
{
public:
    const golding::PluginManifest& Manifest() const override
    {
        static const char* const kDepends[] = {"shell"};
        static const golding::PluginManifest kManifest = {
            .name = "my.plugin", .version = "0.1.0", .kind = "panel",
            .rootWidgetClass = "MyRoot",
            .depends = kDepends, .dependCount = 1,
        };
        return kManifest;
    }

    bool Register(golding::Host& host) override
    {
        host.AddPage(kPage);                      // 一个插件可以注册多个页面
        host.AddService("my.service", &kVtable);
        return true;
    }

    int Ready(const golding::Host& host) override { return 0; }   // 可选：装配后自检
};

GOLDING_EXPORT_PLUGIN(MyPlugin)   // 只导出 descriptor / entry 两个 C 符号
```

三条设计取向：

1. **基类只有三个钩子 + 虚析构，且冻结**。想加能力就往 `Host` 上长（宿主侧 C 结构体
   末尾追加）——中间插虚函数会让所有旧插件的虚表错位（`Options.md` §8）。
2. **宿主永不调插件对象的虚函数**。跨 dll 只有两个 C 符号；虚调用全部发生在插件
   dll 内部（`detail::EntryThunk<T>`），所以 Debug/Release 混编也不崩。
3. **`kind` 是自由字符串，core 只存不解释**（`Options.md` §3）；面板插件的选择仍走
   `plugin.json` 的 `role`。

### 3.1 构件服务：`golding.widgets`（ABI v9）

"要一个按钮但不想 include InkingUIKit"的插件走服务，而不是自己 new 组件：

```cpp
const auto* widgets = golding::WidgetsFrom(host);                    // 可能是 nullptr
widgets->create(nullptr, root.get(), "button",
                R"({"label":"启动引擎","width":200,"height":48,"offsetX":72,"offsetY":268})");
widgets->bind(nullptr, handle, "click", &onClicked, nullptr);
```

| 层 | 在哪 | 换掉它会怎样 |
| --- | --- | --- |
| 契约（`create` / `bind` / `set` / `get` 的形状） | `include/sdk/golding_plugin.h` | 换 = SDK 升版 |
| 规格（`"button"` 认哪些键） | `plugins/core/widgets/include/widgets_schema.h`（插件私有） | 换 = 换一套核心插件，SDK 一行不动 |

`create` 收"**名字 + 配置串**"而不是"每种构件一个函数指针"：加构件（slider / switch）
不改 SDK 版本。服务缺席时插件要能降级成"不画"——一个构件提供方没装，不该让页面消失。

---

## 4. 魔法阵的数据流

```
plugin/<name>/plugin.json      ─┐
plugin/<name>/bin/*.dll        ─┼─→ core 的 PluginInfo（名字/标题/版本/依赖/状态）
plugin/<name>/magic.json       ─┘        │
                                          ↓
                              magic 插件（dll，自己实现 ICanvasPainter）
                                          │  读 PluginInfo + magic.json
                                          ↓
                              CanvasContext：FillShape / StrokeShape / PolygonPath
                                          │  每帧只改 TransformSpec（姿态）
                                          ↓
                                      InkingStaticCanvas
```

- **绘制属于插件，不属于 core**：core 只提供画布 API 与插件档案。
  「core 里不放只有某个程序才需要的东西」这条因此成立。
- **姿态（自转 / 跟随 / 绕点转）做成 SDK 头里的 header-only 工具**，
  不是 core 里的二进制。所有魔法阵作者共用一份实现，core 不必知道"姿态"这个词。

---

## 4.5 服务表与覆盖层（2026-10-22 加）

**服务表**是插件之间**唯一**的横向通道。插件 A 不认识插件 B 的任何类型，
只按名字取一份函数指针表，首字段是 `abiVersion` 由双方各自校验。

```c
registerService(host, "golding.apps", &vtable);      // 提供方
const void* s = queryService(host, "golding.apps");  // 使用方
```

现有服务：

| 名字 | 提供方 | 用途 |
| --- | --- | --- |
| `golding.apps` | addapp | 应用清单（count / at） |
| `golding.overlay` | shell | 弹出底部面板（push / pop） |
| `golding.navigation` | shell | 切页（go(pageId)） |

**加载顺序必须按依赖拓扑**：服务在 entry 里注册，所以依赖方必须后跑。
`Resolver` 的结果直接驱动 entry 的遍历顺序，不是只打印。

**覆盖层**：`GoldingOverlayDecl{ id, height, create, onClose }`，由骨架渲染成
"遮罩 + 底部抽屉"。实现约束（2026-10-22 迁移任务书 T-04 之后）：

1. 遮罩与底部抽屉**一次性建出来并常驻**（挂在骨架根上，z=90 / 95），初始
   `SetVisible(false)`；弹出 = `SetVisible(true)` + 按声明改尺寸，关闭 = `SetVisible(false)`。
   内容只在"换了另一份声明"时重建一次。
2. 这条能成立的前提是 InkingUIKit 0.4.0 修好了 A2/A3（事后 `AddChild` 的子树进
   渲染列表、隐藏过再显示能渲染）。0.4.0 之前只能"弹出即建、关闭即重建"。
3. **不要再声明 `OwnsSubtreeHit()`**：动态容器一旦声明，子孙就不进命中表，而
   场景的深挖只走"静态表命中且是容器"那一支——抽屉里的按钮会永远点不到。
   这条是实测出来的边界（`ShellPlugin.cpp` 的 `ShellLayer` 注释里有完整来龙去脉）。

## 4.6 区域表（2026-10-22 21:50 加，ABI v6）

**面板插件决定这台机器有哪些地方可以注册**。这是"换掉面板插件，界面就换一套，
宿主 exe 一个字节不动"这句话的落地机制。

```
面板插件 entry → registerRegion("menubar")        ← 声明：我这儿能放东西
其它插件 entry → registerRegionItem("menubar", …)  ← 声明：我要往这儿放一项
面板插件 create → regionItemCount/At("menubar")    ← 消费：依次渲染出来
```

**core 不认识任何具体区域名**。`menubar` / `sidebar` / `statusbar` 都只是面板插件
自己的约定；core 只保证四件事：存得下、查得到、同名驳回、按 order 有序。

三条处置规则：

1. 往**不存在的区域**注册 → 驳回（`GOLDING_REGISTER_INVALID`），记日志，
   **不中断该插件其余注册**——"一个插件坏了只影响它自己"。
2. 面板插件缺席 → 区域表为空 → 所有 region item 被驳回，但**插件档案完整、
   程序正常启动、退出码 0**（验收清单第一条）。
3. 区域内条目只由面板插件消费，core 不解释语义。`create` 为空表示"只有动作"
   （菜单项），面板插件据此决定是画控件还是只挂回调。

当前唯一实现的区域：`shell` 声明 `menubar`（顶栏），`home` 往里面放了一个"首页"入口。
`PageRegistry` / `SettingsRegistry` 是**区域机制出现之前的特化实现**，暂时并存：
它们分别相当于 `content` 与 `settings` 两个区域，但 core 里仍有专门代码。
归并留到 `dev001` 站稳之后——先让机制存在，再让机制统一。

---

## 5. 每帧重绘的裁决

**接受：魔法阵每帧 `Invalidate()`。**

- 量级：当前一帧约 15 笔（3 环 + 星 + 6 节点 + 标注），全量重录 float 的代价远低于重算顶点；
  顶点表是静态的，`PolygonPath` 记录时拷贝一次就固定。
- 约束：**只有魔法阵这块画布**这么干。其余界面仍走脏驱动。
- 优化点（记下，不现在做）：若将来笔数上到几百，再给 core 加"只改变换、
  不重录命令表"的通道。届时不需要改插件代码，只改 core 与 SDK 的 `onPaint` 契约。

---

## 6. 版本策略

三个号，各管一段：

| 号 | 在哪 | 管什么 | 不匹配时 |
| --- | --- | --- | --- |
| `abiVersion` | descriptor（仅此一处） | 二进制接口（结构体布局、函数指针表） | **拒绝加载**，绝不让 dll 进 C++ |
| `apiVersion` | manifest | 插件用到的最小 core 能力版本（单调递增的数字） | 插件声明高于宿主支持 → 拒绝加载并说明当前支持到几 |
| `magicSchemaVersion` | magic.json | 元描述文件的结构 | 高版本**忽略该文件**，退回默认外观；低版本按缺省补齐 |

**单向兼容**：新 core 必须读得懂所有旧 schema；旧 core 读到新 schema 明确拒绝，
而不是猜。理由：双向兼容会让 schema 永远不能删字段，而这套格式还没定型。

---

## 7. 明确不做

- **布尔运算**：`ShapeSpec` 没有布尔节点，SDF 的 min/max 需要动形状层。
  blend mode 只能近似 or/and，做不了 xor。
- **位平面混合**：需要 SDL_GPU，独立工程。
- **点选某个环**：画布内容不参与命中，整块魔法阵只命中本体。
- **运行期卸载插件**：只支持完全重加载。

---

## 8. 全屏与 MenuBar 的硬提醒

`MenuBarStyle::hideInFullscreen` 会让顶栏在全屏时整条消失。**如果设计时按
"顶栏占 40px" 排了版，全屏就会在顶上留一条空白**——原来顶栏的位置空着，
下面所有内容整体上移，看起来像少了一块。

所以设计界面时必须二选一：

- **要么** 关掉 `hideInFullscreen`，顶栏常驻；
- **要么** 承认顶栏会消失：内容区自己吃满整个高度，**不能**把顶栏的 40px 算进布局常量。

Shell 选的是第二条，且已经做到底（2026-10-22，迁移任务书 T-01）：顶栏高度当成**运行时量**，
主体高度 = `设计窗口高 − MenuBar::GetCurrentHeight()`（顶栏隐藏时该值为 0），
根节点因此**不做 Stack 纵排**——布局会给顶栏固定分 40，那正是留白的来源。
`golding_host --fullscreen` 是这条的正面证据：顶栏整条隐藏时主体 y=0、高度 = 整个窗口。
设计尺寸由 `host/main.cpp` 的 `kDesignWidth` / `kDesignHeight` **一处**决定（当前 1920×1080）：
面板从父节点量尺寸，自己**不抄**常量——所以改设计尺寸不用动任何插件，断言也自动跟着走。

## 9. 与当前实现的距离（2026-10-22 更新）

已经有的：扫描、manifest、dll 三段加载（ABI 闸门）、组件表、页面表、设置区块表、
插件档案（`PluginLibrary`）、依赖解析（`Resolver`，且驱动加载顺序）、服务表、
区域表、覆盖层（常驻节点 + 显隐 / 就地改几何）、画布 API 在包里已就绪。**ABI 已到 v7**。

**"整树重建"那条通道已删除**（迁移任务书 T-05）：抽屉 / 覆盖层就地改几何，换肤靠
`ThemeVersion` 自动重查。`GoldingHostApi` 里不再有请求重建的入口——界面怎么变，
由界面自己决定，不再去求宿主重来一遍。

还差的：

1. `magic.json` 的读取与 `magicSchemaVersion`（第三个版本号已定，代码未写）
2. magic 插件改成 `ICanvasPainter` 实现（现在是 Panel 拼圈）
3. 左侧应用启动栏（等面板设计确认）
4. 动作表（setParam / playSound / …）
