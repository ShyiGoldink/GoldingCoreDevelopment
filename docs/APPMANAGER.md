# 程序管理（appmanager）：原理与详情

> 给**下一个改它的人**看：数据长什么样、为什么这么分层、哪几处是"改之前先读"的约定、
> 以及已知的坑与扩展点。用户侧的一句话用法在 `README.md` 的「程序管理」那一节。

---

## 1. 一句话

侧栏一行一个应用、右侧一页一个应用（启动 + 设置）。数据分两层：
`apps.json` 管**有哪些、什么顺序**，`apps/<id>/app.ini` 管**这一项具体是什么**，
用户选的图标和背景就躺在同一个小文件夹里。

---

## 2. 代码地图

| 文件 | 行数 | 职责 | 依赖 |
| --- | --- | --- | --- |
| `src/AppStore.h` / `.cpp` | 102 + 764 | 数据层：清单、ini、素材复制、内置图标、**进程/窗口** | 只用 Win32 + 标准库 |
| `src/AppManagerUi.h` / `.cpp` | 66 + 793 | 界面层：侧栏盒子、页面、三个弹窗 | InkingUIKit + `GoldingServices.h` |
| `src/AppManagerPlugin.cpp` | 206 | 接线：注册页面/区域项、拿服务、装配后自检 | `sdk/GoldingPlugin.h` |
| `assets/icons/*.svg` | 11 个 | 内置图标（每个 24×24、纯矢量） | — |

依赖方向是**单向**的：`Plugin → Ui → Store`。

- `Store` 不知道按钮和面板——所以它可以被自检直接调纯函数（`TitleFromPath` / `IsLaunchablePath`）。
- `Ui` 不知道注册与装配——它只认 `UiState` 里那几个指针（服务、侧栏容器、选中 id）。
- 于是"改数据格式"只动 Store，"改观感"只动 Ui，两件事不会互相牵连。

---

## 3. 数据

### 3.1 目录

```
%APPDATA%\GoldingCore\appmanager\
  apps.json                 清单：id 数组 + version
  apps\<id>\app.ini         这一项的字段
  apps\<id>\icon.*          自定义图标（用户选的，复制进来）
  apps\<id>\background.*    自定义背景（图片或视频）
```

**为什么在 `%APPDATA%` 而不是插件目录**：升级插件会覆盖 `plugin/core/appmanager/`，
而用户自己加的应用不该被一次升级抹掉。这和 `cursor` 插件把用户指针放 `%APPDATA%`
是同一条理由。

### 3.2 `apps.json`

```json
{
  "version": 1,
  "apps": ["vscode", "wegame", "deepseek-harness"]
}
```

- **只存 id 与顺序**，别的都不在这里——"有哪些"是一个很短的事实，手改也不会错。
- 手改的最快路径：复制一个 `apps/<id>/` 改 `app.ini`，再往 `apps` 数组里塞一个 id。
- **清单坏了 / 被删了**：`Load` 会把它改名成 `apps.json.bak`，然后**重新播种**十项
  （见 `SeedDefaults`）。这是"宁可给你一排默认的，也不留一个空界面"的取舍。

### 3.3 `apps/<id>/app.ini`

`key=value`，UTF-8 无 BOM，`#` 开头是注释。当前字段：

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `title` | id | 侧栏与页面标题 |
| `target` | 空 | exe / lnk / bat / cmd 的**绝对路径**（UTF-8） |
| `args` | 空 | 启动参数（`ShellExecuteW` 的 lpParameters） |
| `workdir` | 空 | 工作目录 |
| `icon` | 空 | **相对本目录**的文件名（如 `icon.png`）；空 = 用内置 |
| `builtin` | `app` | 内置图标名（见 3.4） |
| `background.type` | `none` | `none` / `image` / `video` |
| `background.path` | 空 | 相对本目录的文件名 |
| `reuse` | `1` | 已在运行时切到已有窗口（`0` = 总是新开，见 §5.5） |

**加字段要改三处，漏一处就是"写了不生效"**：`AppEntry`（`AppStore.h`）、`Load`
（读）、`SaveEntry`（写）。这是本项目别的模块反复踩过的坑（`docs/NOTES.md` 里
"InkingUIKit/AGENTS 第 83 条"那一类）。

### 3.4 内置图标

| 名字 | 用途 | 谁在用 |
| --- | --- | --- |
| `app` | 通用程序窗口 | 兜底（找不到别的就用它） |
| `code` | 尖括号 | vs code |
| `game` | 手柄 | wegame |
| `terminal` | 终端提示符 | deepseek-harness |
| `music` | 音符 | 网易云 |
| `video` | 屏幕 + 播放 | bilibili |
| `folder` | 文件夹 | 文件管理器 |
| `chat` | 气泡 | 微信、QQ |
| `globe` | 地球 | 浏览器 |
| `menu` | **三横线** | 页面右下角那个设置按钮 |
| `settings` | 齿轮 | 目前没被页面用（留作备用） |

- 文件在 `plugins/core/appmanager/assets/icons/`，由 `golding_add_plugin` 的
  POST_BUILD 拷到 `<build>/plugin/core/appmanager/assets/icons/`。
- 运行期路径 = `host.PluginDir() + "/assets/icons/<name>.svg"`（`BuiltinIconPath`）。
- **自检里有一份同样的名字清单**（`runReady` 的"内置图标齐全"）——加图标时两处都要动，
  否则少一个只会静默退化成通用图标。

---

## 4. 运行流程（时序）

### 4.1 `Register`（装配之前）

1. `host.Use<PopupService>` / `host.Use<NavigationService>` 拿服务（拿不到只 WARN，不失败）。
2. `SetPluginDir(host.PluginDir())` —— 内置图标要靠它定位。
3. `Load(apps, &seeded)`：读清单与每个 ini；没有清单就建目录并**播种十项**。
4. `selectedId = apps.front().id`（一进来就有内容，不是空页）。
5. `host.AddPage({appmanager.page, "程序管理", 1000, createPage})`。
6. `host.AddRegionItem({sidebar, appmanager.sidebar, ..., createSidebar})`。

> 顺序不能反：面板在 `createShell` 里先扫区域项再挂页面，两者都在 `registerAll` 之后。

### 4.2 面板建骨架 → 调我们的 `createSidebar`

- 面板把我们的节点挂进**侧栏容器**（纵向 Stack、宽 200、有 10px padding）。
- 我们建一个"盒子"（`InkingStaticPanel`，纵向 Stack，高 1400），里面：
  每个应用一行（按钮 + 子图标的 `InkingStaticImage`），最后一行是「＋ 添加应用」。
- 盒子高度写死 1400（约 26 行），**超出部分被侧栏裁掉**（侧栏有 `HasSubtreeClip`）。
  不做滚动是取舍：侧栏是"入口列表"，不是文件管理器。

### 4.3 面板挂页面 → 调我们的 `createPage`

面板的 `mountPage` **没有缓存**：每次导航都重新 `create` 一份页面、再摘掉旧的。
我们正是靠这一点做"切应用 / 改设置后就地刷新"。

`createPage` 依次做：

1. 根面板（尺寸 = 内容区，白底，**不排布**，子项全用绝对坐标）。
2. 背景（`addBackground`）：`image` 用 `InkingStaticImage`（`Cover` + `hitPassThrough`），
   `video` 用 `InkingStaticVideo`（`Cover` + `loop` + `muted` + `autoplay`）。都设
   `zIndex = InkingZIndex::background`。
3. 标题区：一块半透明白卡片（zIndex 1）+ 标题（zIndex 2）+ 路径小字（zIndex 2）。
4. 右下角按钮组：「启动」（240×64）+ 三横线设置按钮（64×64），间距 12、右边距 48。

### 4.4 点侧栏某一行

```
行按钮 onClicked → SelectApp(id)
  → selectedId = id
  → refreshRowSelection()        只改文字色，不重建行（见 §5.6）
  → RefreshPage() → nav->go("appmanager.page")   → 面板重建页面
```

### 4.5 导入一个程序

```
「＋ 添加应用」→ OpenImportPopup() → 面板弹 popup（内容 = 标题 + FilePicker 子面板）
  → 选中文件、点「添加」→ FilePicker::accepted
  → CreateApp(path)      生成 id、建 apps/<id>/、写 ini、追加清单
  → appendRow(created)   摘「添加」行 → 挂新行 → 「添加」行挂回去（不销毁任何节点）
  → SelectApp(created.id) → 切到它的页面
  → popup->pop()
```

### 4.6 改一个应用的图标 / 背景

```
三横线按钮 → OpenSettingsPopup(id)
  → 「更换图标…」/「更换背景…」→ openPickPopup(kind)  （再弹一层，只有一层，替换掉设置）
  → FilePicker::accepted → CopyAsset(id, path, "icon"|"background")
  → 图标：entry.icon = 新文件名 → SaveEntry → updateRowIcon()（只 LoadAsset，不重建行）
     背景：entry.backgroundType/Path → SaveEntry → RefreshPage()
  → popup->pop() → 重新 OpenSettingsPopup(id)   （回到设置，让用户看见结果）
```

---

## 5. 关键决策与理由（**改之前先读**）

### 5.1 侧栏只注册**一个**区域项，自管盒子

`GoldingRegionItemDecl::create(hostContext, parent)` **收不到"我是哪一项"**，而面板的
区域项是**装配期一次性扫完**的（`createShell` 里 `regionItemCount` 循环）。所以：

- "一个应用一个区域项" ⇒ 运行期导入的应用**永远不会出现**，除非重启；
- "一个区域项 + 自管盒子" ⇒ 增删行就是 `AddChild` / `RemoveChild`，完全自主。

代价：我们看不见、也不需要面板的帮助；换来的是"导入立刻生效"。

### 5.2 新行插在「添加」行前面，且**不销毁**任何节点

`appendRow` 的手法：`RemoveChild(addRow)` 拿回 `unique_ptr` → 挂新行 → 把 addRow
`AddChild` 挂回去。全程没有 `delete`，所以在**行按钮自己的点击回调栈上**做这件事也安全
（导入是从弹窗的回调里发起的，但刷新选中色/切页同理）。

### 5.3 图标、背景、素材一律用**绝对路径**

`AssetConfigure` 是**进程级**的，宿主把资源根设成了**面板插件**的 `assets/`。
我们如果写相对路径，就会被解析到面板的目录里。
`inking::AssetResolve` 对**绝对路径原样返回**，所以：

- 自定义素材：`AppDir(id) / 文件名` 的绝对路径；
- 内置图标：`host.PluginDir() + "/assets/icons/x.svg"` 的绝对路径。

`ImageData` 的 `runtimeAsset = true` 会让它在构造时/`LoadAsset` 时按这个路径读字节，
`.svg` 会被按目标设备像素光栅化。

### 5.4 设置按钮是"按钮 + 子图"，不是"按钮的图片外观"

`ButtonData` 的 `ButtonAppearance::Image` **只认生成期烘进来的字节**
（`imageBytes` / `imageSize`），没有"运行期按路径读"的开关；那是 `ImageData` 才有的
`runtimeAsset`。所以"运行期换图标的按钮"只有一条路：

```
InkingStaticButton（浅底 + 描边）
  └ InkingStaticImage（图标，hitPassThrough = true）
```

`hitPassThrough` 必须开，否则点图标那一格不会落到下面的按钮上。

### 5.5 `explorer` 的 `reuse=0`，以及 Store 应用的例外

"启动"会先按 exe **完整路径**枚举进程、找主窗口、还原并切前台；找不到才真启动。
两个必须知道的边界：

- `explorer.exe` 的进程**永远在**（它就是桌面），切前台等于切桌面，所以它预置为
  `reuse=0`——每次新开一个资源管理器窗口；
- **Store 应用**（`WindowsApps` 下的转发器 exe）匹配不到：转发器进程和真实进程路径不同
  （记事本就是例子）。这类程序一般自己带单实例逻辑，会把窗口带出来，影响不大。

### 5.6 选中态只改文字色，不重建行

重建行要么销毁节点（在回调栈上 = use-after-free），要么走延迟销毁（需要一个每帧钩子，
而本插件**没有**）。所以 `SelectApp` 只对现有行 `SetTextColor`；行本身是常驻的。

### 5.7 弹窗是"一层"，改完设置要 `pop()` 再 `push()`

面板的 `golding.popup` 同一时刻只有一层。所以"设置 → 选文件"是**替换**，不是叠加；
选完文件想回到设置，只能 `pop` 再 `push` 一份新的（面板对 `pop`/`push` 有延迟销毁保护，
见 `docs/NOTES.md` 的 graveyard 那条）。

### 5.8 弹窗里 `FilePicker` 必须挂在一个**有尺寸的子面板**上

`FilePicker` 用 `parent->GetWidth()/GetHeight()` 算自己的块布局。直接挂在弹窗内容容器上，
它会占满整块并与我们的标题重叠；所以先建一块 `height - 54` 的子面板，再把选择器挂进去。

### 5.9 按钮文字**不要用浅色**

这条管线的静态按钮在浅色文字（纯白 / 近白）下会画成**空心描边**（只剩抗锯齿边缘，
字心是透明的）。所以「启动」是"浅紫底 + 深紫字"，不是常见的"紫底白字"。
`docs/NOTES.md` 里有这条记录。

---

## 6. 扩展点（照抄就能加）

### 6.1 加一个预置应用

改 `AppStore.cpp` 的 `kSeeds`：`{id, title, 图标名, reuse, {候选路径...}}`。
候选路径按顺序取第一个存在的；一个都不在就留空（启动时给提示，让用户去设置）。
`SeedCount()` 会跟着变，自检里的"预置表 N 项"是动态的。

**播种只在"清单不存在"时发生**。已有的数据目录不会因为改了 `kSeeds` 而更新——
加预置项之后想让它生效，删掉 `apps.json` 重启（会重新播种，但**旧的 `apps/` 目录还在**，
新条目的目录会被写出来）。

### 6.2 加一个内置图标

1. 放一个 24×24 的 SVG 到 `assets/icons/`；
2. 在 `runReady` 的图标清单里加名字；
3. 需要的话在 `kSeeds` 里引用它。

### 6.3 加一个字段

`AppEntry` → `Load` → `SaveEntry`（三处），再补本文档 §3.3 的表格。
需要"老 ini 没有这个键也能工作"时，在 `Load` 里给默认值（参照 `reuse` 的写法）。

### 6.4 加一个"桌面视图"页（一屏网格）

这是最自然的下一步：复用同一份 `apps`，只换呈现。

- 新页面 id 例如 `appmanager.desktop`，`AddPage` 时给一个更小的 `order`（让它抢先挂载）；
- 根面板开 `layout.kind = Grid`、`layout.columns = 5`、`rowGap/columnGap`；
- 每格 = "图标（`InkingStaticImage`，64×64）+ 名字（`InkingStaticLabel`）"装在一个按钮里；
- 点击 → `SelectApp(id)` 之后可以直接 `Launch(entry)`（桌面习惯是"点一下就开"）。

注意：网格里的项是**静态**节点，尺寸由 `LayoutKind::Grid` 算，不要自己 `Resize`。

### 6.5 运行态角标（"这个程序开着"）

`RunningInstanceCount(target)` 已经有了，缺的是**每帧节拍**：本插件没有 `onTick`。
两条路：

- 在页面里挂一个**动态档节点**（`InkingDynamicWidget` 派生，尺寸给 1×1 也算可见），
  在它的 `onTick` 里每 N 拍查一次进程数并改角标；
- 或者只做"点开页面时查一次"——成本最低，但切到别的应用再回来才会刷新。

---

## 7. 已知限制与 TODO

| 限制 | 影响 | 建议 |
| --- | --- | --- |
| `FilePicker` 没有盘符列表 | 换盘只能在路径框手输 `D:\` 回车 | 从 **InkingUIKit** 的 `FilePicker` 解决（那条"快捷位置" `OptionBar` 加盘符最自然） |
| 侧栏盒子高 1400，无滚动 | 超过约 26 个应用看不见，也点不到 | 需要滚动时给盒子开 `clipChildren` + `scroll`，或改成虚拟列表 |
| 没有删除应用 | 只能手删 `apps/<id>/` + 改 `apps.json` | 设置弹窗里加一个"从侧栏移除"（注意别在回调栈上销毁行） |
| 没有拖拽排序 | 顺序 = 清单顺序 | 要动态档 + 拖放命中，成本较高 |
| Store 应用匹配不到进程 | 点了会重复开（程序自己通常会激活） | Windows 限制，不改 |
| 目标只收 exe/lnk/bat/cmd | 不能把 URL / 文件夹当应用 | 加 `url` 类型时 `Launch` 用 `ShellExecuteW` 的 `open` + 直接给 URL |

---

## 8. 自检清单（`runReady`）

每条都在**启动时跑**，红了只算本插件的失败项（不影响别的插件），并计入退出码。

| 断言 | 在验什么 | 怎么破坏它 |
| --- | --- | --- |
| 数据目录可写 | `%APPDATA%` 下能建目录、能写文件 | 把数据目录设成只读 |
| 清单里有应用 | 播种 / 读取成功 | 清空 `apps.json` 且让播种失败 |
| 选中了一个应用 | `selectedId` 指向一个存在的条目 | 手删 `selectedId` 对应的目录 |
| 内置图标齐全 | 11 个名字都能解析到文件 | 删一个 svg |
| 侧栏行数 = 应用数 + 1 | 盒子真的按清单建了行 | 改 `createSidebar` 少建一行 |
| 右侧页面已装配 | 面板自动挂上了我们的页面 | 把 `order` 调大让别的页面抢走 |
| 背景只在配了时铺 | 没有背景时不该有背景节点 | 在 `none` 时也建背景节点 |
| 导入类型判定 | exe/bat/lnk 收，png/txt 拒 | 放宽扩展名 |
| 默认标题 = 文件名去扩展 | `TitleFromPath` | 改成用完整文件名 |
| 能枚举到正在运行的进程 | `Toolhelp32` 那条路通（用 explorer 当样本） | 用错 API / 权限 |
| 不存在的程序不假装激活 | 找不到进程必须返回 false | 让 `ActivateRunning` 无条件 true |
| 新建应用的目录与配置 | `CreateApp` 真建了目录 + ini（跑完即清理） | 不写 ini / 不清清单 |
| 点侧栏切应用并重建页面 | 行回调 → 选中 → `nav->go` 整条链 | 去掉 `SetOnClicked` |
| 设置弹窗能建 | `create` 被调用（`PopupService` 没有查询口） | `popup->push` 失败 |
| 添加弹窗能建 | 同上 | 同上 |

**自检的两条纪律**：不启动任何外部程序（`Launch` 不测）、不改用户真实的启动项/数据
（`CreateApp` 用副本 + 跑完删目录 + 重写清单）。
