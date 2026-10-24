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

# 完整程序：core + 宿主 exe（要 InkingUIKit 预编译包）
cmake -S . -B build -G Ninja -DGOLDING_WITH_INKING=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="C:/msys64/ucrt64"
cmake --build build
ctest --test-dir build --output-on-failure          # 5 项
```

宿主 exe 只认**窗口层**的开关，界面层的模式原样经 `bootMode` 交给插件。
**不给 `--frames` 就常驻**——"跑 N 帧就退"是 CI 与截图的行为，不是默认行为：

```bash
build/golding_host.exe                              # 开窗常驻；ESC / 关窗退出
build/golding_host.exe --fullscreen --expanded      # 全屏；--expanded 由插件解释
build/golding_host.exe --frames=3 --capture=shot.png   # 跑完就退，退出码即信号
```

调试构建用 `-DCMAKE_BUILD_TYPE=Debug`。

**两种构建不要共用一个目录**：

| 场景 | 目录 | 开关 | 产物 |
| --- | --- | --- | --- |
| 只做 core / 写插件 | `build-dev` | `OFF` | `libgolding_core.a` + 两个测试 |
| 跑完整程序 | `build` | `ON` + `-DCMAKE_PREFIX_PATH=<msys2 根>` | `golding_host.exe` + `plugin/` |

`build` 里的 `plugin/` 是运行时装配物，`build-dev` 不产出它。混用会让 `PathFinder`
扫到上一轮的旧插件。

InkingUIKit 的预编译包按候选表定位（`-DGOLDING_INKING_DIR=<包根>` 显式指定优先），
见 `host/CMakeLists.txt` 顶部。**必须用 MinGW 工具链**：该包是 MinGW ABI，MSVC 链不上。

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

## 内置面板插件：GoldingGamestoryShell

`role: "panel"`，**无插件依赖**。它只提供"地方"，内容全由别的插件填：

| 提供物 | 怎么用 |
| --- | --- |
| `menubar` 区域 | `host.AddRegionItem({ "menubar", "my.entry", "我的入口", nullptr, 10, nullptr, &onClick })` |
| `sidebar` 区域 | 同上，`regionId` 写 `"sidebar"`；条目自动拉满侧栏宽度 |
| 右侧内容区 | 注册 `GoldingPageDecl`；用服务 `golding.navigation` 的 `go(pageId)` 切页 |
| 弹窗 | 服务 `golding.popup`：`push(&decl)` / `pop()`，`GoldingPopupDecl` 见 `plugins/common/GoldingServices.h` |

两条约定：

- 区域项有两种形态——**只填 `onActivate`** 就是一个按钮；**填 `create`** 就自己画，
  `parent` 由面板给（顶栏里 `create` 出来的项排在菜单栏右侧按钮之前之外的位置，
  要精确控制位置就用按钮形态）。
- 侧栏**不自动列页面**：页面是"内容"，侧栏是"入口"。哪个页面需要入口，由那个插件
  自己往 `sidebar` 注册一项（`onActivate` 里调导航服务即可）。这样"有哪些页"与
  "哪儿有入口"不会变成两份互相追的清单。

弹窗是**模态**的：遮罩盖住整个窗口（含顶栏），点遮罩或调 `pop()` 关闭，同一时刻只有一层。

**侧栏收拉**：顶栏最左的 `≡` 切换；收起是宽度归 0（内容区吃满整个宽度），
不是"藏起来但仍占位"——布局求解器不跳过不可见项。想让程序一启动就收着，用
`golding_host.exe --collapsed`（`--collapsed` 由面板自己解释，宿主管不着）。

**配色**：白窗口 / 白内容区 / 承载面（浅灰紫）侧栏，顶栏下一条 1px 分隔线；
页面是白底卡片时靠层次与分隔线区分，不靠整块换色。

### 设置入口（`settings` 插件）

`plugins/core/settings` 是**普通插件**（不是面板）：往 `menubar` 注册一项「设置」，
点开弹出一个对话框，把**所有注册过的设置区块**列出来。

它不认识任何具体设置项——加一个设置项只要自己的插件调 `host.AddSettingsSection(...)`，
这里不用改。它自己带的只有标题、空态文案和一个关闭按钮。

两条清单里的声明值得照抄：

```json
"requires": { "plugins": ["GoldingGamestoryShell"] },
"needs": { "golding.popup": 1 }
```

`requires` 保证面板先 entry（区域项要求**区域已经存在**，否则注册被驳回）；
`needs` 让"面板不在"在装配前就能判出来，而不是运行时才发现拿不到服务。

## 文档

| 文档 | 内容 |
| --- | --- |
| `docs/API.md` | C ABI、SDK、core 的全部公开接口 |
| `docs/NOTES.md` | 必须知道的边界与坑 |
| `docs/Options.md` | 已定决策与它们排除掉的路 |
