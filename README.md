# GoldingCore

插件式客户端的核心库：扫描插件、校验 ABI、解析依赖、维护注册表、按拓扑序把插件装起来。
界面不在这里——core 不认识任何具体控件，也不认识 `menubar` 这种区域名。

## 快速开始

要求：CMake ≥ 3.22、C++20 编译器。core 本身不依赖任何 UI 库。

```bash
# 开发包：只建 core 与它的单测（不链 InkingUIKit）
cmake -S . -B build-dev -G Ninja -DGOLDING_WITH_INKING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-dev
ctest --test-dir build-dev --output-on-failure      # 2 项
```

调试构建用 `-DCMAKE_BUILD_TYPE=Debug`。

**两种构建不要共用一个目录**：

| 场景 | 目录 | 开关 | 产物 |
| --- | --- | --- | --- |
| 只做 core / 写插件 | `build-dev` | `OFF` | `libgolding_core.a` + 两个测试 |
| 跑完整程序（有 `host/` 的检出） | `build` | `ON` + `-DCMAKE_PREFIX_PATH=<msys2 根>` | `golding_host.exe` + `plugin/` |

`build` 里的 `plugin/` 是运行时装配物，`build-dev` 不产出它。混用会让 `PathFinder`
扫到上一轮的旧插件。

## 把 core 接进你的宿主

四个阶段，顺序不能换：

```cpp
#include "HostApp.h"

golding::HostApp app;

app.discover();     // 定位 plugin/core/（内置）与 plugin/（外部）
app.prepare();      // 扫描 + 建拓扑 + 加载 dll + 校验依赖与能力（只能调一次）
app.registerAll();  // 按拓扑序调每个插件的 Register

// —— 以下属于宿主：建窗口，用面板根工厂建骨架，再把根交回 core
app.setUiRoot(rootWidget);
app.setBootMode(bootMode);   // 命令行原样交出，谁认谁解释
app.notifyReady();           // 装配后回调，返回失败项总数
```

界面根的来源只有一处：

```cpp
const GoldingWidgetClass* factory = golding::PanelRootRegistry::root();
// factory == nullptr 是合法状态（没有面板插件）：界面为空，程序照常
void* rootWidget = factory != nullptr ? factory->create(hostContext, parent, "") : nullptr;
```

`GoldingHostApi` 由 `HostApp::fillHostApi` 填写，每个插件一份；**必须活到进程结束**
（插件会长期持有它，点击回调不在 `Register` 的调用栈里）。

## 写一个插件

### 代码

继承 `golding::Plugin`，实现两个钩子，末尾一行导出宏：

```cpp
#include "sdk/GoldingPlugin.h"

// 你的页面：返回挂在 parent 上的子树根。真实实现用你的 UI 库建节点。
GoldingWidgetHandle CreateHelloPage(void* /*hostContext*/, void* /*parent*/)
{
    return nullptr;   // 返回 nullptr = 创建失败，宿主不做清理
}

class HelloPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        static const GoldingPageDecl kPage{"hello.page", "你好", 100, &CreateHelloPage};
        return host.AddPage(kPage);
    }

    int Ready(const golding::Host& host) override
    {
        (void)host;
        return 0;   // 返回失败项数；没有断言就返回 0
    }
};

GOLDING_EXPORT_PLUGIN(HelloPlugin)
```

- **代码里不写任何静态描述**：名字、版本、类型、依赖、能力全在 `plugin.json`。
- `GOLDING_EXPORT_PLUGIN` 生成 `golding_plugin_descriptor` 与 `golding_plugin_entry`
  两个 C 符号，不要手写 `extern "C"`。
- 注册一律走 `host.AddXxx()`，不要自己拼 C 调用——`Host` 每次都留一行日志。

### plugin.json

```json
{
  "name": "hello",
  "title": "你好",
  "version": "0.1.0",
  "apiVersion": 2,
  "requires": { "plugins": [] },
  "provides": { "hello.api": 1 },
  "needs": { "golding.widgets": 1 },
  "targets": { "win64": { "lib": "bin/hello.dll" } }
}
```

必填：`name`（非空）、`title`、`version`、`apiVersion`、`requires.plugins`（可为空数组）、
`targets.win64.lib`。
可选：`role`（类型标签，`panel` = 提供界面骨架）、`provides` / `needs`（能力名 → 版本）。

顶层是**白名单**：写别的键判 `Invalid`，启动日志会点名是哪个键。

放哪里：`plugin/core/<name>/` 是随宿主分发的内置插件，`plugin/<name>/` 是外部安装的。
同名时内置优先，外部那个会被标 `shadowed` 跳过。

### CMake

```cmake
include(${CMAKE_SOURCE_DIR}/cmakes/GoldingPlugin.cmake)
golding_add_plugin(hello plugins/dev/hello/src/HelloPlugin.cpp dev)
```

函数会编译出 `hello.dll`，连同 `plugin.json` 装到 `build/plugin/core/hello/`。

## 文档

| 文档 | 内容 |
| --- | --- |
| `docs/API.md` | C ABI、SDK、core 的全部公开接口 |
| `docs/NOTES.md` | 必须知道的边界与坑 |
| `docs/Options.md` | 已定决策与它们排除掉的路 |
