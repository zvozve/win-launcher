# Everything SDK（第三方二进制，不入库）

`Everything64.dll` 来自 voidtools 的 Everything SDK，本目录只放**取回脚本**，
DLL 由脚本下载解包，git 忽略。

## 取回

```
scripts\fetch-everything-sdk.bat
```

下载 <https://www.voidtools.com/Everything-SDK.zip>，解出 `dll/Everything64.dll` 到本目录。
之后 `scripts\build.bat` 会自动把它拷到 `build\<Config>\`（exe 同目录），
`EverythingSearch` 就能通过 IPC 查 Everything 的索引。

## 为什么必须放 exe 同目录

`EverythingSearch::probe()` 按以下顺序找 DLL（见 `src/core/everythingsearch.cpp`）：

1. `Everything64.dll`（优先走 exe 目录 / PATH）
2. `<exe目录>\Everything64.dll`
3. `C:\Program Files\Everything\Everything64.dll`
4. `C:\Program Files (x86)\Everything\Everything64.dll`

注意：**Everything 本体安装目录里没有这个 DLL** —— Everything.exe 不带 SDK。
要么用本脚本放到 exe 旁，要么把 SDK DLL 手动丢进上面第 3 条那个目录。

## 许可

Everything SDK 由 voidtools 提供，允许在商业/非商业程序中免费使用。
详见 <https://www.voidtools.com/support/everything/sdk/>。
