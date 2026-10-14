# WinLauncher

Windows 上仿 macOS 26/27 的**弹出式**启动台（Spotlight「应用程序」浮层那种，不是旧版全屏启动台）。

按热键弹出一块圆角毛玻璃面板：最左是搜索类型，中间搜索栏，下面是图标网格；自动读取系统已安装软件，支持拖入 `.lnk` 添加，图标增删和设置各有一个独立窗口。

技术栈：**Qt 6.8.3 (msvc2022_64) + MSVC 2022 Build Tools + Ninja**

---

## 目录结构

```
win-launcher/
├── CMakeLists.txt              单目标 WinLauncher；AUTOMOC + AUTORCC
├── build.bat                   一键构建（Debug / Release + windeployqt）
├── readme.md
├── doc/
│   ├── 1.开发需求,md
│   └── 2.方案设计.md           选型、里程碑、实测踩坑记录
├── inc/                        全部头文件（平铺，PascalCase）
├── src/                        全部实现（平铺，与 inc 同名）
├── ui/                         全部 QML（平铺，PascalCase）
├── resources/
│   └── winlauncher.qrc         QML 打包进 exe，引擎用 qrc:/qml/Main.qml 加载
├── packaging/
│   ├── deploy.bat              windeployqt + 补拷 Everything64.dll
│   └── fetch-everything-sdk.bat  取回 Everything SDK 的 DLL
├── third_party/everything/     SDK DLL 落地处（DLL 不入库）
└── build/
    ├── Debug/WinLauncher.exe
    └── Release/WinLauncher.exe
```

约定（与 `icloud-mon` 保持一致）：

- 头文件在 `inc/`，实现在 `src/`，**都在 target 的 sources 里显式列出** —— AUTOMOC 只扫 target sources，漏列就会「无法解析的外部符号 staticMetaObject」。
- QML 在 `ui/`，通过 `resources/winlauncher.qrc` 打包；**AUTORCC 必须开**，否则改 QML 不触发重新打包，跑出来还是旧界面。
- 版本号只在 `project(VERSION)` 维护一处，编译期以 `APP_VERSION` 注入。
- `.bat` 必须是纯 ASCII + CRLF：cmd 按 GBK 读，UTF-8 中文注释会被切成乱码 token。

## 构建 / 运行

```bat
build.bat              :: Release（推荐）：编译 + windeployqt，产物可双击
build.bat Debug        :: Debug
build.bat both         :: 两个都来

build\Release\WinLauncher.exe
```

`build.bat` 里包含 windeployqt，所以产物目录可以**直接双击运行**。
只想重跑部署：`packaging\deploy.bat Release`。

## 使用

| 操作 | 说明 |
|---|---|
| `Ctrl+Alt+Space` | 唤起/收起面板（可在设置里改） |
| 搜索栏最左 | 搜索类型：应用 / 文档 / 图片 / 视频 / 音乐 / 压缩包 / 全部 |
| 搜索栏右侧 | 搜索源：**本地**（只过滤已扫到的条目）/ **全盘**（Everything 索引） |
| 面板「编辑」 | 图标增删（拖 `.lnk` / `.exe` / `.url` 进窗口即可添加） |
| 面板「设置」 | 热键 / 开机自启 / 失焦收起 / 默认搜索源 / 每行图标数 / 扫描范围 |
| Esc / 失焦 | 收起面板 |
| 托盘 | 打开启动台 / 编辑 / 设置 / 开机自启 / 重新扫描 / 退出 |

配置与数据：

- 配置 `%LOCALAPPDATA%\WinLauncher\config.json`
- 手动添加的条目 `%LOCALAPPDATA%\WinLauncher\layout.json`
- 图标缓存 `%LOCALAPPDATA%\WinLauncher\cache\icons\*.png`

## 排错

```bat
WinLauncher.exe --trace                     :: 打点 + Qt/QML 警告写入 <exe目录>\winlauncher.trace
WinLauncher.exe --test-everything chrome    :: 直查 Everything 索引，结果写 winlauncher.diagnostics.txt
```

- 双击报「缺少 Qt6Cored.dll」= 没跑 windeployqt，执行 `packaging\deploy.bat Release`。
- 托盘已常驻时再启动 exe 会静默退出（单实例设计，会把已有实例的面板叫出来）。
- 「全盘」搜不到东西：确认 Everything 在运行，且 `Everything64.dll` 在 exe 旁
  （`packaging\fetch-everything-sdk.bat` 取回，`build.bat` 会自动拷）。

更多设计细节与踩坑记录见 `doc/2.方案设计.md`。
