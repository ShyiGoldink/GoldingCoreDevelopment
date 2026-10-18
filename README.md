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

两张表：`PluginRegistry`（名字 → 模板或工厂）、`ActionLibrary`（名字 → `std::function`）

## 3. 插件文件夹

```none
plugin/<name>/
├─ plugin.json      必需
├─ bin/<name>.dll   可选
├─ icon.png         可选
└─ assets/          可选
```

`plugin.json` 必需字段：`name` `title` `version` `apiVersion` `abiVersion` `requires.plugins`
可选：`widgets{}`（纯数据）、`targets{win64:{lib}}`（dll）

## 4. dll 的两个导出符号——顺序不能反

```cpp
① const InkPluginDescriptor* inking_plugin_descriptor();   // 纯 C POD，含 abiVersion
② int inking_plugin_entry(inking::PluginRegistry&);        // C++ 入口
```

**先读 ① 比对 ABI，不匹配就拒绝，绝不调 ②。**
不这么做，ABI 不对的表现是**崩溃**，不是报错。

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
