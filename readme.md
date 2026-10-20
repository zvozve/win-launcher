# WinLauncher

Windows 上仿 macOS 26/27 的**弹出式**启动台（Spotlight「应用程序」浮层那种，不是旧版全屏启动台）。

按热键弹出一块圆角毛玻璃面板：顶部搜索栏，下面是图标网格；自动读取系统已安装软件，支持拖入 `.lnk` 添加，图标增删和设置各有一个独立窗口。搜索栏输入即查全盘（Everything），结果按「名称 / 路径 / 大小 / 类型」四列详情展示。

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
│   ├── winlauncher.qrc         QML 打包进 exe，引擎用 qrc:/qml/Main.qml 加载
│   ├── winlauncher.rc.in       Win32 资源模板：exe 图标 + VERSIONINFO（属性页）
│   └── winlauncher.ico         多尺寸图标（由 packaging/make_ico.py 生成）
├── packaging/
│   ├── deploy.bat              windeployqt + 补拷 Everything64.dll
│   ├── fetch-everything-sdk.bat  取回 Everything SDK 的 DLL
│   ├── package.bat             单文件打包入口（编译 + 部署 + EVB）
│   ├── package.py              打包主流程（参考 icloud-mon）
│   ├── gen_evb.py              生成 Enigma Virtual Box 工程
│   └── make_ico.py             生成 resources/winlauncher.ico（exe 图标）
├── third_party/everything/     SDK DLL 落地处（DLL 不入库）
├── dist/                       单文件产物 + 分发 zip（不入库）
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

## 打包单文件

```bat
packaging\package.bat              :: 编译 + 部署 + 打进 dist\WinLauncher.exe
packaging\package.bat --skip-build :: 跳过编译，只重新打包
```

产物 `dist\WinLauncher.exe`（约 42 MB，单文件，拷到任何 x64 Windows 双击即用）：
Qt 运行时 + QML 模块 + `Everything64.dll` + VC 运行时 DLL 全部内嵌，
配置与缓存仍写 `%LOCALAPPDATA%\WinLauncher`。`dist/` 已在 `.gitignore` 里。

同时生成 `dist\WinLauncher-v<ver>-win64.zip` 用于分发：里面除了 exe，还有
`Unblock-First.bat`（清 Mark-of-the-Web）和 `ReadMe.txt`，原因见下节。

流程参考 `icloud-mon` 的打包脚本：`build.bat` → `deploy.bat`（windeployqt）→
`gen_evb.py` 生成 Enigma Virtual Box 工程 → `enigmavbconsole.exe` 压成单文件。
需要装 Enigma Virtual Box（默认 `C:\Program Files (x86)\Enigma Virtual Box`），
装在别处用 `--evb <enigmavbconsole.exe>` 指定。

### SmartScreen 为什么拦、怎么处理

单文件 exe 一旦经过网盘 / 聊天软件 / 浏览器传到别的机器，就会带上
**Mark-of-the-Web**（`Zone.Identifier` 区隔流）。Windows 对「带 MOTW + 没有
数字签名」的可执行文件默认弹 SmartScreen：

```
Windows protected your PC
Microsoft Defender SmartScreen prevented an unrecognized app from starting.
App: WinLauncher.exe     Publisher: Unknown publisher
```

三种处理，按代价从低到高：

| 做法 | 效果 | 说明 |
|---|---|---|
| 点「更多信息」→「仍要运行」 | 本次放行 | 每次都弹 |
| **`Unblock-File`** | **彻底不再弹** | `Unblock-File .\WinLauncher.exe`，或双击 zip 里的 `Unblock-First.bat` |
| 买证书 + `signtool` 签名 | 对外分发也不弹 | 需公共 CA 的 OV/EV 代码签名证书；见下 |

自建证书**不要用**：把自签名 CA 塞进「受信任的根证书颁发机构」，等于给这台
机器开了一个任意签发口子，只为了消一个提示，安全上是净亏损。本机自用就直接
点一次「运行」——Windows 会记住这个文件的哈希，之后不再问。

有证书后（`.pfx` 放 `packaging/certs/`，密码走环境变量 `WINLAUNCHER_PFX_PASS`
或 `certs/password.txt`）：

```bat
packaging\package.bat --sign
```

脚本会在 EVB 打包之后用 `signtool` 签名（SHA256 + DigiCert 时间戳），
签名失败时退化为无时间戳签名。`packaging/certs/` 已被 `.gitignore` 屏蔽。

## 使用

| 操作 | 说明 |
|---|---|
| `Ctrl+Alt+Space` | 唤起/收起面板（可在设置里改） |
| 搜索栏 | 输入即查全盘（Everything）：`文件名 扩展名`、`文件名 文件类型`（如「报告 图片」）都支持 |
| 面板「编辑」 | 图标增删（拖 `.lnk` / `.exe` / `.url` 进窗口即可添加） |
| 面板「设置」 | 热键 / 开机自启 / 失焦收起 / 每行图标数 / 扫描范围 |
| Esc / 失焦 | 收起面板 |
| 托盘 | 打开启动台 / 编辑 / 设置 / 开机自启 / 重新扫描 / 退出 |

配置与数据：

- 配置 `%LOCALAPPDATA%\WinLauncher\config.json`
- 手动添加的条目 `%LOCALAPPDATA%\WinLauncher\layout.json`
- 图标缓存 `%LOCALAPPDATA%\WinLauncher\cache\icons\*.png`

**开机自启默认关闭**，程序不会自己往注册表写启动项。需要时先在设置（或托盘菜单）里打开，
写入的是**当前正在运行的那个 exe** 的绝对路径 —— 所以先把 exe 放到最终位置再开，
否则挪动目录后自启会失效（开关会自己显示为关闭，重新开一次即可）。

## 排错

```bat
WinLauncher.exe --trace                     :: 额外记录扫描明细（默认已是 info 级）
WinLauncher.exe --test-everything chrome    :: 直查 Everything 索引，结果写 winlauncher.diagnostics.txt
```

**日志常开**，不需要加参数：

- 运行日志 `%LOCALAPPDATA%\WinLauncher\log\winlauncher_YYYY-MM-DD`（按天切分，保留 30 天）
- 崩溃现场 `%LOCALAPPDATA%\WinLauncher\log\crash-<时间>-<pid>.txt` + `.dmp`
  （异常码、故障地址、模块偏移、读写类型、调用栈）

"进程莫名其妙没了"先看这两个文件，里面有每次显隐、扫描、单实例握手和内存快照。

- 双击报「缺少 Qt6Cored.dll」= 没跑 windeployqt，执行 `packaging\deploy.bat Release`。
- 再次启动 exe 而托盘已有实例：新进程只把已有实例的面板叫出来，然后自己退出（单实例）。
  日志里会写明叫醒的是哪个 exe（`已有实例[ok pid=.. exe=..]`）。
- 「全盘」搜不到东西：确认 Everything 在运行，且 `Everything64.dll` 在 exe 旁
  （`packaging\fetch-everything-sdk.bat` 取回，`build.bat` 会自动拷）。

更多设计细节与踩坑记录见 `doc/2.方案设计.md`。
