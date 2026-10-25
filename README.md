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
ctest --test-dir build --output-on-failure          # 7 项
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
- **要读 JSON 就 `#include <sdk/golding_json.h>`**（`golding::json::parse` / `dump`），
  不要自己写解析器，也不要引 core 的内部头——那些头有编译期守卫，引了在编译期就 `#error`。

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
| 多层级菜单 | 服务 `golding.menu`：`open("my.entry", &decl)` 在自己的顶栏按钮下面弹一级，`children` 非空的项再往下开一层 |

两条约定：

- 区域项有两种形态——**只填 `onActivate`** 就是一个按钮；**填 `create`** 就自己画，
  `parent` 由面板给（顶栏里 `create` 出来的项排在菜单栏右侧按钮之前之外的位置，
  要精确控制位置就用按钮形态）。
- 侧栏**不自动列页面**：页面是"内容"，侧栏是"入口"。哪个页面需要入口，由那个插件
  自己往 `sidebar` 注册一项（`onActivate` 里调导航服务即可）。这样"有哪些页"与
  "哪儿有入口"不会变成两份互相追的清单。

弹窗是**模态**的：遮罩盖住整个窗口（含顶栏），而且**点遮罩不关闭**——只认右上角那个
X（或代码里调 `pop()`），同一时刻只有一层。

插件不画关闭键：`GoldingPopupDecl::create` 收到的 `parent` 是弹窗里的**内容容器**
（与弹窗同尺寸，由弹窗拥有），自己用 `AnchorData` 的 `selfAnchor` / `traceAnchor`
把节点摆到合适位置就行。X 的节点名是 `popup.close`，内容要避开右上角那一小块。

### 多层级菜单（`golding.menu`）

菜单层级归**面板插件**，不归库的 `prefab::MenuPopup`：那个第一版明确不做子菜单，
而且两个 `InkingFixedPopup` 并存时会互相当作"点外部"（各自判自己的命中）、还会互相
抢键盘焦点——父菜单在点开子菜单的同一帧就自我关闭。所以浮窗只当画框，层级、落位、
关闭仲裁都在面板里（`plugins/common/GoldingMenu.h` 是契约）。

```cpp
golding::MenuItem recent[] = {
    { "recent.a", "最近文件 A", nullptr, 0, &onItem },
};
golding::MenuItem file[] = {
    { "file.new",    "新建",     nullptr, 0, &onItem },
    { "file.recent", "最近打开", recent,  1, nullptr },   // children 非空 = 入口
};
golding::MenuItem root[] = {
    { "menu.file", "文件", file, 2, nullptr },
};
golding::MenuDecl decl{ "my.menu", root, 1 };

golding::MenuService* menu = host.Use<golding::MenuService>(golding::kMenuServiceName, 1);
menu->open(nullptr, "my.entry", &decl);   // 在「我的入口」下面展开
```

四条要点：

- **锚点是区域项 id，不是坐标**：面板才知道那个按钮在哪儿，插件只交身份。
- **声明只需在 `open` 调用期间有效**：面板打开那一刻把整棵树深拷贝进自己的表，
  所以菜单内容可以现场拼（比如"设置区块是装配后才有的"那种动态列表）。
- **回调带 `itemId`**：`void (*)(void* hostContext, const char* itemId)`，
  一个静态函数就能服务所有项（按 id 分派），不必为每项造一个函数指针。
- 关闭规则：点空白 / 点叶子收起整串；点已展开的项切分支时，更深的层先收。

**侧栏收拉**：顶栏最左的 `≡` 或**全局 `Tab`** 切换。收起是宽度归 0（内容区吃满整个
宽度），不是"藏起来但仍占位"——布局求解器不跳过不可见项。开合走 160ms 的抽屉动画
（`EaseOut`），动画期间侧栏把内容剪在自己那块里，条目不会溢到内容区上。想让程序一
启动就收着，用 `golding_host.exe --collapsed`（`--collapsed` 由面板自己解释，宿主管不着）。

`Tab` 绑在窗口的**全局快捷键表**上（`MenuBar::BindShortcut`），不依赖焦点；代价是它
会先被消费掉，输入框拿不到 `Tab`。要改组合就改 `GoldingGamestoryShell.cpp` 里那句
`Shortcut::Parse("Tab")`。

**顶栏进出全屏**：`MenuBarStyle::hideInFullscreen` 默认开，全屏时整条顶栏消失、
`GetCurrentHeight()` 变 0。顶栏以下那一块是**动态档**（`BodyLayer`），每逻辑步问一次
"顶栏现在占多高"，变了就把自己贴到它下面、高度吃掉剩下的整块——所以全屏切换后内容
立刻向上长满，不会在顶上留一条 44px 的空白。静态面板做不到这件事（几何构造即定型）。

**配色**：白窗口 / 白内容区 / 承载面（浅灰紫）侧栏，顶栏下一条 1px 分隔线；
页面是白底卡片时靠层次与分隔线区分，不靠整块换色。菜单浮窗是"白卡片 + 1px 描边 +
一层阴影"：内容区是纯白，没有这两样它就会糊在背景里。

### 设置入口（`settings` 插件）

`plugins/core/settings` 是**普通插件**（不是面板）：往 `menubar` 注册一项「设置」，
点开是一串**定点浮窗菜单**（每项对应一个已注册的设置区块），点某一项才弹模态框，
并且**只显示那一块**。

它不认识任何具体设置项——加一个设置项只要自己的插件调 `host.AddSettingsSection(...)`，
这里不用改。面板支持菜单时走浮窗，不支持时自动退回"直接弹完整设置框"。

它还提供 `golding.settings` 服务，让**别的插件用自己的按钮复用这份内容**：

```cpp
const golding::SettingsService* settings =
    host.Use<golding::SettingsService>(golding::kSettingsServiceName, 1);
settings->openMenuAt(nullptr, "my.entry");   // 在「我的入口」下面弹设置菜单
settings->openAll(nullptr);                  // 或者直接弹完整设置框
settings->openSection(nullptr, "my.section");// 或者只弹某一块
```

依赖它的插件在 `plugin.json` 里这样写：

```json
"requires": { "plugins": ["settings"] },
"needs": { "golding.settings": 1 }
```

`requires` 保证面板先 entry（区域项要求**区域已经存在**，否则注册被驳回）；
`needs` 让"面板不在"在装配前就能判出来，而不是运行时才发现拿不到服务。

### 程序管理（`appmanager` 插件）

`plugins/core/appmanager` 是一个**页面插件**：侧栏一行一个应用，右侧一页一个应用
（启动按钮 + 齿轮设置按钮）。数据分两层，正对"有哪些"与"具体是什么"这两件事：

```
%APPDATA%\GoldingCore\appmanager\
  apps.json              清单：有哪些、什么顺序（只存 id）
  apps\vscode\app.ini    这一项的路径 / 标题 / 图标 / 背景
  apps\vscode\icon.png   用户选的图标（复制进来，删掉即回落内置图标）
  apps\vscode\bg.mp4     用户选的背景（图片或视频，喂给页面铺满）
```

`app.ini` 的字段：`title` / `target` / `args` / `workdir` / `icon` / `builtin` /
`background.type` / `background.path` / `reuse`（`reuse=0` = 总是新开）。

- **侧栏只注册一个区域项**（`appmanager.sidebar`），它自管一个纵向盒子。这样运行期
  导入的新应用立刻出现，不依赖面板重扫区域表（`create` 收不到"我是哪一项"）。
- **导入**：点侧栏底部「＋ 添加应用」→ 文件选择器（`exe` / `lnk` / `bat` / `cmd`）→
  建目录、写 ini、追加清单，然后选中它并切到它的页面。
- **启动**：先按 exe **完整路径**找已在运行的实例（`Toolhelp32` 枚举进程 → 找主窗口），
  找到就还原并切到那个窗口——不会重复开一个；抢不到前台时短暂置顶兜底。找不到才走
  `ShellExecuteW`（`.lnk` / `.bat` 也是这条路）。`app.ini` 里 `reuse=0` 可关掉"切已有窗口"：
  `explorer` 预置就是 0（它的进程永远在，切前台等于切桌面，用户要的是新窗口）。
  注意这套匹配对 **Store 应用**（`WindowsApps` 下的转发器 exe）无效——真实进程路径
  与转发器不同；那类程序自己的单实例逻辑通常会把窗口带出来。
- **布局**：标题在左上（半透明卡片，深色背景上也看得清），「启动」与设置在**右下角**
  并排（与标题成对角，谁也不压壁纸的主体）；设置按钮是**三横线**图标
  （`assets/icons/menu.svg`），与启动按钮同族（浅底 + 描边）。
- **设置**（那个三横线按钮）：换图标 / 换背景 / 清背景。素材复制进 `apps/<id>/`，
  页面就地重建（导航服务同页重挂）。
- **预置十项**（VS Code / WeGame / DeepSeek Harness / WorkBuddy / 网易云音乐 /
  哔哩哔哩 / 文件管理器 / 微信 / QQ / 浏览器）：首次运行按**候选路径**探测本机安装位置，
  一个都找不到就留空并在日志里点名——不猜一个不存在的路径。
- 背景为空 = 纯白页面；深色背景时标题区垫一块半透明白卡片，文字才看得清。

**改它之前先读 [docs/APPMANAGER.md](docs/APPMANAGER.md)**：数据格式、三层代码各自的职责、
几个"别动"的约定（为什么只注册一个区域项、为什么素材走绝对路径、为什么按钮文字不能用浅色）。

### 开机自启动（`autostart` 插件）

`plugins/core/autostart` 往「设置」里加一块「开机自启动」，一个开关控制
**写 / 删启动脚本**：

```
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\GoldingCoreAutostart.cmd
```

脚本内容就是 `start "" "<本程序 exe>" --fullscreen`。选脚本而不是注册表 `Run` 键：
用户看得见、改得动、删掉即禁用，也不需要管理员权限。脚本按**系统 ANSI 代码页**写
（cmd.exe 解析 `.cmd` 用的是代码页，写 UTF-8 会让中文路径变乱码——开机时"闪一下就没"）。
判断"已启用"要求脚本里的 exe 路径与当前进程一致：换了安装目录就自动算未启用。

细节见 [docs/AUTOSTART.md](docs/AUTOSTART.md)（为什么用 ANSI 代码页、自检怎么做到不碰真实启动项）。


### 命令面板（`commands` 插件）

`plugins/core/commands` 提供 `golding.commands` 服务：任何插件交一份 `CommandDecl`
就能让它的动作**被搜到、被一键执行**，不必再往顶栏挤一个入口。入口是顶栏「命令」
一项 + `Ctrl+K`（绑不上就只剩顶栏，不报错）。

```cpp
const golding::CommandService* commands =
    host.Use<golding::CommandService>(golding::kCommandsServiceName,
                                      golding::kCommandsServiceVersion);
if (commands != nullptr) {
    static const golding::CommandDecl decl{
        "my.action", "打开我的东西", "open dakai",   // 第三个是拼音/英文检索词，可空
        [](void*) { /* ... */ },                     // hostContext 恒为 nullptr
    };
    commands->add(nullptr, &decl);
}
```

四条要点：

- **命令只描述"做什么"**：`CommandDecl` 里没有位置 / 尺寸 / 图标。怎么搜、怎么排、
  面板长什么样，是面板的事（`plugins/common/GoldingCommands.h` 是契约）。
- **三个串被深拷贝**：调用方可以现场拼字符串（例如 `"打开 " + 名字`）。
- **id 重复的 `add` 被驳回**（返回非 0），不覆盖——"后来者悄悄改掉前者"最难查。
- **依赖是可选的，别写 `needs`**：命令面板不在时你的插件照常工作，只是少一条命令。
  写了 `needs` 就变成"没有命令面板就装不上"。

面板本体：非模态浮层 + 搜索框 + 结果列表（≤ 8 条，每次输入重建）。焦点归输入框
（IME / 中文输入才正常），`Enter` 执行**第一条**匹配；**不做上下键选择**——焦点
只能给一个节点，给了列表输入框就收不到键（见 `docs/TASK-COMMANDS.md` §7）。

**注册顺序有个前提**：`Use` 是在各自 `Register` 里调的，所以命令面板必须**先于**
注册命令的插件 entry。同层（都只依赖面板）的顺序就是扫描顺序（目录名字典序），
`commands` 排在 `settings` / `spellbook` 之前。顺序万一反过来，结果是"少几条命令 +
一行 WARN"，不是崩溃。

### 自定义指针（`cursor` 插件）

`plugins/core/cursor` 把系统光标藏起来，在**场景**上挂一块跟着指针走的画布
（z=900，压过面板内部所有图层），自己画指针。六种内置图案
（arrow / ring / cross / dot / spark / quill），按下会形变并炸一圈涟漪，
移动时留一段拖尾。

入口**只有设置里一处**：设置 → 鼠标指针（`GoldingGamestoryShell` 的顶栏不再挂
「指针」——选指针是"设一次就不动"的事，不该占着顶栏）。别的插件用 `golding.cursor`
服务（契约 `plugins/common/GoldingCursor.h`）换图案、换成一张自己的图、关掉这一层、
或者在代码里手动炸一圈涟漪：

```cpp
const golding::CursorService* cursor =
    host.Use<golding::CursorService>(golding::kCursorServiceName, 1);
if (cursor != nullptr) {
    cursor->setStyle(nullptr, "cross");   // 内置图案按 id 切；未知 id 保持原样
    // 或换成一张图：绝对路径 + 显示尺寸 + 热点（图案里"真正指着的那一点"）
    cursor->setImage(nullptr, "D:/icons/pen.svg", 40, 40, 20, 20);
    cursor->pulse();                       // 和真按键同一份动画
}
```

命令面板里还有一条 `cursor.next`（`Ctrl+K` 搜"指针"），命令面板缺席时只是少这一条。

### 用户指针：用户自己加的那些图

内置图案之外，用户可以加**自己的图**。入口：设置 → 鼠标指针 → 「用户指针…」，
里面能导入（自绘文件选择器）、改热点（在预览图上点一下 = 热点定在那儿）、删。

**格式定案**（细节见 [`docs/CURSOR.md`](docs/CURSOR.md)）：

- **一个用户指针 = 用户指针目录里的一张图片**（PNG / JPG / SVG）。
  目录：Windows `%APPDATA%\GoldingCore\cursors\`，其它 `~/.golding/cursors/`
  —— 放在用户目录而不是插件目录，因为**程序升级会覆盖插件目录**。
- **清单 `cursors.index`**（同目录，TSV，一行一条）：
  `文件名 <TAB> hotX <TAB> hotY [<TAB> 显示名]`。清单只管**热点**（图案里真正
  指着的那一点），文件列表以目录为准。**为什么是 TSV 不是 JSON**：插件层拿不到
  core 的 JSON 解析器（core 编进宿主 exe，不给插件链接），而 TSV 一行一条就能
  `split`，人也能直接打开改。
- 导入有三条路（界面 / 手动丢进目录 / 命令行 `--cursor-import=<路径>`），走的是
  **同一份实现**。
- 用户指针**也在服务的样式列表里**（id 形如 `user:pen`），别的插件遍历样式时
  自然会看见它们。
- **选择会被记住**：挑的图案（含用户指针）、启用开关、按下特效、尺寸三档都写进
  `%APPDATA%\GoldingCore\cursor.ini`（TSV，一行一对键值，格式见
  [`docs/CURSOR.md`](docs/CURSOR.md) §8），下次启动读回来。`setImage` 换的那张
  临时图**不持久化**——它不在用户指针目录里，记住一个可能被删掉的绝对路径没有意义。

三条要点：

- **整层不参与命中**（`SetHitPassThrough`）：它盖在所有东西上面，一旦吃点击，
  底下的按钮就全部失灵。`Ready` 里专门断言了这一条，不是"顺手写对了"。
- **藏系统指针用的是 Win32 `ShowCursor`**：InkingUIKit 不暴露光标管理，而
  `ShowCursor` 是个**显示计数**，`SetCursor`（SDL 换形状走的那条路）盖不住它。
  计数是进程级的，所以停用（`setEnabled(0)`）时由本插件负责把它还回来——
  少还一次，光标在整个进程里都回不来。
- **`--cursor-demo` 启动模式**：不读真实输入，按帧把六种图案、"按下"、设置区块、
  用户指针面板（列表 / 选热点 / 选文件）各走一遍（同一个帧号永远同一张图），
  再到"关闭 / 重开弹窗"那一段——**关闭用户指针面板**曾经每次必崩（`popupDecl`
  存了栈上的声明），这条演示现在是它的回归测试。演示模式**不写** `cursor.ini`：
  它一路换图案，写下去就是拿演示覆盖用户的真实选择。

## 文档

| 文档 | 内容 |
| --- | --- |
| `docs/ARCHITECTURE.md` | **结构与原理**：分层、启动时序、core 各模块、每个插件各自的职责与内部结构 |
| `docs/API.md` | C ABI、SDK、core 的全部公开接口 |
| `docs/NOTES.md` | 必须知道的边界与坑 |
| `docs/Options.md` | 已定决策与它们排除掉的路 |
| `docs/CURSOR.md` | 用户指针的格式与用法（用户侧契约） |
| [`docs/APPMANAGER.md`](docs/APPMANAGER.md) | 程序管理：数据格式、代码分层、时序、扩展点、已知限制（**改它之前先读**） |
| [`docs/AUTOSTART.md`](docs/AUTOSTART.md) | 开机自启动：写什么文件、为什么用脚本、编码与状态判定 |
