# 问题清单与决议

## 决议速览（2026-10-21）

| # | 问题 | 决议 |
| --- | --- | --- |
| 1 | 命名空间 | **B** 统一 `golding`，`PathFinder` 一并迁移 |
| 2 | 魔法阵能力 | **B 的变体**：可启停（移出/移回插件目录）+ 触发重载；**不允许创建组件** |
| 3 | JSON 实现 | **C** 保持自写，出现第二个需要 JSON 的模块再换 |
| 4 | 插件标识 | **D** 只用 `name`，不设 `id` 字段；官方插件库由后端数据库维护 |
| 5 | 注册原子性 | **A** 预检后提交，并保留**快照**：内容未变则不重复注册 |
| 6 | `PluginRegistry` 形态 | **A** 静态类，照宿主 `ButtonLibrary` |
| 7 | dll 边界 | **B** C ABI + 函数指针表（GDExtension 式） |
| 8 | 构建系统 | **A** 补全为可独立构建的最小工程 |
| 9 | `widgets{}` 内容位置 | **已废弃**（2026-10-23，T-11 删字段）：配置数据改为「插件自己的文件 + 插件自己解释」（`magic.json` 那种形态） |
| 10 | 插件卸载 | 不支持卸载，只支持**完全重加载** |

## 各项的落地后果

### 1. 命名空间 `golding`
`PathFinder` / `PluginsStatus` / `PluginsResult` / `Manifest` / `ManifestLoader` 全部收进 `golding`；内部 JSON 位于 `golding::detail::json`。已落地。

### 2. 魔法阵：只能启停，不能造组件
- 启用 / 禁用 = 把插件目录移到别处（如 `plugin-disabled/`），本身不改 manifest。
- 重载 = 重新跑一次「加载组件」流程，不做运行期热替换。
- 因此 `PluginRegistry` 对魔法阵暴露：查询 + 启停请求 + 重载请求；**不暴露 Create**。
- 推论：魔法阵不需要拥有对象，也就不参与所有权问题。

### 3. JSON：先自写
解析器留在 `src/Application/Json.cpp`，不对外。等第二个模块需要 JSON 时整体替换，公共头不受影响。

### 4. 依赖用 `name`
`Manifest` 不设 `id`。`requires.plugins` 存插件名。官方插件的权威清单由后端数据库维护，本地只做比对。

### 5. 预检 + 快照
注册流程：
1. 扫描 → 读 manifest → 得到插件集
2. 计算快照（目录 + manifest 内容摘要）
3. 与上次快照一致 → 跳过注册；不一致 → 全量预检后提交
4. 保留「不留中间态」：任一冲突则整插件放弃

### 6. 静态类
`PluginRegistry` 用函数内静态量存储（与 `ButtonLibrary` / `SceneLibrary` 一致），避免静态初始化顺序问题。不加锁，只在 UI 线程使用。

### 7. C ABI
dll 只导出 C 符号，跨边界只传 POD 与句柄，不接受 `golding::` 的 C++ 对象引用。宿主侧再包一层 C++ 注册表。
**README 第 4 节当前的 `inking_plugin_entry(inking::PluginRegistry&)` 描述已失效，需要改写。**

### 8. 构建
`CMakeLists.txt` 已补全：`golding_core` 静态库，C++20，默认不链接 InkingUIKit（开关 `GOLDING_WITH_INKING`）。

### 9. `widgets` 用外链（已废弃，2026-10-23）

> **这条已作废，保留原文只为回看当时怎么想的。** `Manifest::widgets` 与 `WidgetDecl`
> 已删除，`plugin.json` 顶层键改成白名单后写 `widgets` 会判 `Invalid`。
> 取代它的是 `magic.json` 那条路：数据文件由**插件**自己读、自己解释，core 只给目录
> （`Host::PluginDir()`，ABI v10）。理由见 `docs/Options.md` §10。

原文：
```json
"widgets": { "helloLabel": "widgets/helloLabel.json" }
```
理由：
- `plugin.json` 保持短小，可读、可 diff；
- 组件配置独立成文件，未来可视化编辑器只需改单个文件；
- 可以直接指向已有的 CXXCSS 配置，不必搬家。

代价：多一层文件读取；需要校验路径不越出插件目录（`../` 逃逸）。**该校验归装配阶段，尚未实现。**
内嵌写法（值为对象）当前判 `Invalid`，避免作者以为能内嵌却不生效。

### 10. 只支持完全重加载
不做运行期卸载，`PluginRegistry` 只增不减，直到一次完整重加载才整体重建。所有权问题简化为「随重载统一回收」。

## 仍未细化

- C ABI 的具体形状：`descriptor` 字段、HostApi 表里放哪些函数。
- 快照粒度：manifest 内容哈希，还是文件修改时间。
- ~~`widgets` 路径越界校验的时机与报错方式~~（字段已删，2026-10-23）。
- 重加载时已创建组件的处置顺序。
