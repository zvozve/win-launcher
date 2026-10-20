#!/usr/bin/env python3
# ======================================================================
#  package.py  -  WinLauncher 一键打包(编译 -> 部署 Qt -> 打进单文件 exe)
#
#  一条命令出单文件绿色版, 做法与 icloud-mon/packaging/package.py 同构:
#      build.bat(MSVC+Ninja) -> deploy.bat(windeployqt + Everything DLL)
#      -> gen_evb.py 生成工程 -> enigmavbconsole 压成一个 exe
#
#  用法:
#    python packaging/package.py                # 编译 + 打包
#    python packaging/package.py --skip-build   # 只重新打包(用现有 build/Release)
#    python packaging/package.py --qt <Qt目录> --evb <enigmavbconsole.exe>
#    python packaging/package.py --sign         # 打包后签名(需 packaging/certs/*.pfx)
#
#  产物:
#    dist/WinLauncher.exe                      单文件, 双击即用
#    dist/WinLauncher-v<ver>-win64.zip         分发用压缩包(内含解除锁定脚本)
#   dist/ 已在 .gitignore
# ======================================================================
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

# ── 默认可执行/工具路径(可用参数或环境变量覆盖) ──────────────────────
DEF_QT = os.environ.get("QTDIR", r"C:/Container/System/Library/QT/6.8.3/msvc2022_64")
DEF_EVB = os.environ.get(
    "EVB_CONSOLE", r"C:/Program Files (x86)/Enigma Virtual Box/enigmavbconsole.exe")

ROOT = Path(__file__).resolve().parent.parent
APP = "WinLauncher"


def log(msg):
    print(f"[package] {msg}", flush=True)


def fail(msg, code=1):
    print(f"[package][ERROR] {msg}", file=sys.stderr, flush=True)
    sys.exit(code)


def run(cmd, **kw):
    log("$ " + " ".join(str(c) for c in cmd))
    return subprocess.run([str(c) for c in cmd], **kw)


# ── 版本号(单一真相源在 CMakeLists.txt) ─────────────────────────────
def read_version(cmake: Path) -> str:
    m = re.search(r"project\(\s*WinLauncher\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
                  cmake.read_text(encoding="utf-8"))
    return m.group(1) if m else "0.0.0"


# ── 编译(复用 build.bat, 不自己拼 MSVC 环境) ─────────────────────────
def kill_running() -> None:
    """链接时 exe 正在跑会让链接静默失败(build.bat 仍打印 BUILD OK),
    所以打包前先结束它 —— 这条坑在项目记忆里踩过。"""
    r = subprocess.run(["taskkill", "/F", "/IM", f"{APP}.exe"],
                       capture_output=True, text=True)
    if r.returncode == 0:
        log(f"已结束运行中的 {APP}.exe")


def newest_source_mtime() -> int:
    """源码树里最新的 mtime(判「exe 是否真的比源码新」)。"""
    best = 0
    for p in ROOT.rglob("*"):
        if not p.is_file():
            continue
        if p.suffix.lower() in {".cpp", ".h", ".qml", ".qrc", ".rc", ".txt"}:
            # 跳过构建目录和第三方
            parts = set(p.parts)
            if "build" in parts or "third_party" in parts or "dist" in parts:
                continue
            try:
                best = max(best, p.stat().st_mtime_ns)
            except OSError:
                pass
    return best


def build(config: str) -> Path:
    exe = ROOT / "build" / config / f"{APP}.exe"
    bat = ROOT / "build.bat"
    if not bat.exists():
        fail(f"找不到构建脚本: {bat}")
    log(f"编译 {config} ... (build.bat 内含 windeployqt 部署)")
    # 不捕获输出: build.bat 是 GBK 中文, 直接透传到控制台更清楚
    subprocess.run(["cmd", "/c", str(bat), config], cwd=str(ROOT))
    if not exe.exists():
        fail("编译后找不到 exe, 检查 build.bat 输出")
    # build.bat 末尾若被安全策略拦了某个工具会返回非 0, 但编译其实成功,
    # 所以不看返回码, 看「exe 是否比源码新」——这正是「链接被运行中的 exe
    # 静默跳过」的判据(踩过: build.bat 照样打印 BUILD OK, exe 却没更新)。
    exe_m, src_m = exe.stat().st_mtime_ns, newest_source_mtime()
    if exe_m < src_m:
        fail(f"exe 比源码旧 —— 链接多半被运行中的进程拦了。\n"
             f"        exe: {time.strftime('%H:%M:%S', time.localtime(exe_m / 1e9))}"
             f"  最新源码: {time.strftime('%H:%M:%S', time.localtime(src_m / 1e9))}\n"
             f"        先退出 WinLauncher(含托盘)再重试")
    log(f"编译完成: {exe}")
    return exe


def check_deployed(exe: Path, qt: str) -> None:
    """确认 Qt 运行时确实躺在 exe 旁边(否则单文件里缺 DLL, 双击报错)。"""
    need = ["Qt6Core.dll", "Qt6Quick.dll", "platforms/qwindows.dll"]
    missing = [n for n in need if not (exe.parent / n).exists()]
    if not missing:
        return
    log(f"缺少部署文件 {missing}, 调用 deploy.bat 补齐")
    r = subprocess.run(["cmd", "/c", str(ROOT / "packaging" / "deploy.bat"),
                        "Release"], cwd=str(ROOT))
    missing = [n for n in need if not (exe.parent / n).exists()]
    if missing:
        fail(f"部署后仍缺少 {missing} —— windeployqt 是否失败? (Qt: {qt})")


# ── VC 运行时 ────────────────────────────────────────────────────────
#  windeployqt 拷的是 vc_redist.x64.exe(**安装器**, 24 MB), 双击单文件时它
#  不会被自动执行 —— 换到没装 VC++ 2015-2022 运行时的机器上照样启动失败。
#  发布版要的是 DLL: 直接从 System32 取同名 CR 运行时(微软可再分发组件),
#  总共约 1 MB, 比安装器小两个数量级, 且真正解决问题。
CRT_DLLS = [
    "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    "msvcp140_atomic_wait.dll", "msvcp140_codecvt_ids.dll",
    "concrt140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
]
CRT_SRC = Path(os.environ.get("SystemRoot", r"C:/Windows")) / "System32"


def stage_crt(exe_dir: Path) -> None:
    installer = exe_dir / "vc_redist.x64.exe"
    if installer.exists():
        installer.unlink()
        log("已移除 vc_redist.x64.exe(安装器, 24 MB, 单文件里无用)")

    copied, missing = [], []
    for name in CRT_DLLS:
        src = CRT_SRC / name
        if not src.exists():
            missing.append(name)
            continue
        shutil.copy2(src, exe_dir / name)
        copied.append(name)
    if not copied:
        fail(f"未能取得 VC 运行时 DLL(查找目录 {CRT_SRC})")
    mb = sum((exe_dir / n).stat().st_size for n in copied) / 1024 / 1024
    log(f"已内嵌 VC 运行时 {len(copied)} 个 DLL ({mb:.1f} MB)"
        + (f", 缺: {missing}" if missing else ""))


# ── 代码签名(可选) ───────────────────────────────────────────────────
# "Unknown publisher" 只有一个来源: exe 没有 Authenticode 签名。这个字段
# 只认签名者的证书, 和 VERSIONINFO(产品名/版本/版权)完全无关 —— 补版本
# 信息不会让发布者变成已知。
#
# 能消掉它的只有公共 CA 签发的代码签名证书(OV/EV, 按年付费)。
# **自签名证书无效**: 把自建 CA 装进"受信任的根证书颁发机构"虽然能让链
# 验证通过, 但那等于给整台机器开了一个任意签发口子, 为了一个提示降低
# 系统安全面, 不值得。
#
# 证书不入库(.gitignore 已屏蔽 packaging/certs/)。买了证书后:
#     WINLAUNCHER_PFX_PASS=密码  python packaging/package.py --sign
def find_signtool():
    base = Path(os.environ.get("ProgramFiles(x86)",
                               r"C:/Program Files (x86)")) / "Windows Kits" / "10" / "bin"
    if not base.is_dir():
        return None
    cands = sorted(base.glob("*/x64/signtool.exe"), reverse=True)
    return str(cands[0]) if cands else None


def sign_exe(exe: Path, pfx: Path, password: str) -> None:
    tool = find_signtool()
    if not tool:
        fail("找不到 signtool.exe(需要 Windows 10 SDK)")
    base = [tool, "sign", "/fd", "SHA256", "/f", str(pfx), "/p", password]
    log(f"签名 {exe.name} (证书 {pfx.name}) ...")
    # 时间戳让签名在证书过期后仍然有效; 服务器偶发拒连时退化为无时间戳
    r = run(base + ["/tr", "http://timestamp.digicert.com", "/td", "SHA256", str(exe)])
    if r.returncode != 0:
        log("时间戳服务器不可用, 改为不带时间戳签名")
        r = run(base + [str(exe)])
    if r.returncode != 0:
        fail("签名失败(检查 pfx 是否过期 / 密码是否正确)")


def resolve_cert(args):
    """--sign 时定位 pfx 与密码。"""
    certs = ROOT / "packaging" / "certs"
    pfx = Path(args.pfx) if args.pfx else next(iter(certs.glob("*.pfx")), None)
    if not pfx or not Path(pfx).exists():
        fail(f"--sign 但找不到证书: 把 .pfx 放到 {certs} 或用 --pfx 指定")
    pwd = args.pfx_pass or os.environ.get("WINLAUNCHER_PFX_PASS", "")
    if not pwd:
        f = certs / "password.txt"
        if f.exists():
            pwd = f.read_text(encoding="utf-8").strip()
    if not pwd:
        fail("缺少证书密码: 设环境变量 WINLAUNCHER_PFX_PASS, "
             f"或写 {certs / 'password.txt'}, 或用 --pfx-pass")
    return Path(pfx), pwd


# ── 分发压缩包 ───────────────────────────────────────────────────────
# 单文件 exe 从网盘/聊天软件/浏览器传到别人机器上会带 Mark-of-the-Web,
# 于是双击就被 SmartScreen 拦("Windows protected your PC")。签名能减轻
# 但不能免除(未累积信誉的新文件照样提示), 真正零门槛的办法是让接收方
# 先清掉这个标记 —— zip 里放个一键脚本, 比让用户自己翻属性页靠谱。
UNBLOCK_BAT = """@echo off
REM ------------------------------------------------------------------
REM  Run this ONCE after unpacking.
REM  It clears the "downloaded from the Internet" mark (Mark-of-the-Web)
REM  on every file here, which is what makes SmartScreen block the exe
REM  with "Windows protected your PC / Unknown publisher".
REM  After this, WinLauncher.exe starts without any prompt.
REM ------------------------------------------------------------------
setlocal
echo Clearing Mark-of-the-Web in "%~dp0" ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Get-ChildItem -LiteralPath '%~dp0' -File | Unblock-File; Write-Host 'OK'"
echo.
echo Done. Double-click WinLauncher.exe to start.
pause
"""

README_TXT = """WinLauncher v{ver}
================================================================

【第一次运行】
  1. 先双击 Unblock-First.bat
     （它会清掉文件的"来自 Internet"标记）
  2. 再双击 WinLauncher.exe

  跳过第 1 步的话，Windows 可能弹出
  "Windows Defender SmartScreen 已阻止无法识别的应用"。
  那是 SmartScreen 对"从网络下载的、没有数字签名的程序"的默认拦截，
  不是病毒告警。此时点「更多信息」→「仍要运行」也能启动，
  但每次都弹，先跑一次 Unblock-First.bat 就一劳永逸。

【为什么没有数字签名】
  代码签名需要向公共 CA 购买证书（OV/EV，按年计费）。本项目为个人
  自用工具，未购买证书，所以发布者显示为 Unknown。
  彻底消除提示的唯一办法是购买证书后用 signtool 签名，见 readme。

【数据位置】
  配置与缓存写在 %LOCALAPPDATA%\\WinLauncher\\
  本程序为绿色版，删掉 exe 即可卸载（配置目录可手动清理）。
"""


def make_zip(exe: Path, ver: str) -> Path:
    zpath = exe.parent / f"{APP}-v{ver}-win64.zip"
    if zpath.exists():
        zpath.unlink()
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        # exe 用 STORED? 不必 —— 单文件 exe 内部已经是压缩过的 EVB 载荷,
        # 再 deflate 一次收益很小但也不亏, 统一 DEFLATED 即可。
        z.write(exe, exe.name)
        z.writestr("Unblock-First.bat", UNBLOCK_BAT.replace("\n", "\r\n"))
        z.writestr("ReadMe.txt", README_TXT.format(ver=ver).replace("\n", "\r\n"))
    mb = zpath.stat().st_size / 1024 / 1024
    log(f"分发包 -> {zpath}  ({mb:.1f} MB)")
    return zpath


# ── 目录版（免自解压）────────────────────────────────────────────────
#  EVB 单文件每次运行都要把约 90MB 运行时解压到临时目录。未签名 + 自解压
#  行为恰好是 SmartScreen 的高频拦截对象，而且每次重新打包哈希都变，
#  "点过一次仍要运行"的记忆也就失效了 —— 表现就是"每次启动都有弹窗"。
#  目录版是普通 PE：dll 就在 exe 旁边，没有解压动作、没有释放子进程，
#  和任何普通程序一样，风险面小得多。
def make_portable(exe: Path, ver: str) -> Path:
    dist = ROOT / "dist" / f"{APP}-portable"
    if dist.exists():
        shutil.rmtree(dist, ignore_errors=True)
    dist.parent.mkdir(parents=True, exist_ok=True)

    ignore = shutil.ignore_patterns(
        "*.pdb", "*.ilk", "*.exp", "*.lib", "*.obj", "*.trace", "winlauncher.trace",
        "shot-*.png", "*.evb", "log", "cache", "CMakeFiles", "*.ninja*", ".cmake",
        "winlauncher_autogen", "cmake_install.cmake", "CMakeCache.txt",
    )
    shutil.copytree(exe.parent, dist, ignore=ignore, dirs_exist_ok=True)

    zpath = ROOT / "dist" / f"{APP}-v{ver}-win64-portable.zip"
    if zpath.exists():
        zpath.unlink()
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in sorted(dist.rglob("*")):
            if f.is_file():
                z.write(f, str(Path(dist.name) / f.relative_to(dist)))
        z.writestr(f"{dist.name}/ReadMe.txt",
                   ("WinLauncher 目录版（免安装）\r\n"
                    "直接双击 WinLauncher.exe 运行即可，不需要解压到临时目录。\r\n"
                    "整个文件夹可以随便挪动，配置写在 %LOCALAPPDATA%\\WinLauncher。\r\n")
                   .encode("utf-8"))
    mb = zpath.stat().st_size / 1024 / 1024
    log(f"目录版 -> {zpath}  ({mb:.1f} MB)")
    return dist


# ── 打进单文件 exe ───────────────────────────────────────────────────
def pack(exe: Path, evb_console: str) -> Path:
    dist = ROOT / "dist"
    dist.mkdir(parents=True, exist_ok=True)
    out_exe = dist / f"{APP}.exe"
    # 上一轮的产物先删: EVB 若因为文件被占用失败, 会留下一个看似"成功"的旧 exe
    for stale in (out_exe, dist / f"{APP}.evb"):
        if stale.exists():
            stale.unlink()

    gen_evb = ROOT / "packaging" / "gen_evb.py"
    r = run([sys.executable, str(gen_evb), str(exe.parent), str(exe), str(out_exe)])
    if r.returncode != 0:
        fail("生成 EVB 工程失败")

    evb = dist / f"{APP}.evb"
    if not Path(evb_console).exists():
        fail(f"找不到 Enigma Virtual Box 命令行: {evb_console}\n"
             f"        安装后或用 --evb 指定 enigmavbconsole.exe")
    log("Enigma Virtual Box 打包中(压缩整个 Qt 运行时, 约 1 分钟) ...")
    t0 = time.time()
    r = run([evb_console, str(evb)])
    if r.returncode != 0 or not out_exe.exists():
        fail("Enigma Virtual Box 打包失败")
    log(f"EVB 用时 {time.time() - t0:.0f}s")
    return out_exe


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("--qt", default=DEF_QT, help="Qt 6.8 msvc2022_64 目录")
    ap.add_argument("--evb", default=DEF_EVB, help="enigmavbconsole.exe 路径")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--skip-build", action="store_true", help="跳过编译, 只重新打包")
    ap.add_argument("--sign", action="store_true",
                    help="打包后用 signtool 签名(需 packaging/certs/*.pfx)")
    ap.add_argument("--pfx", default="", help="签名证书 .pfx 路径")
    ap.add_argument("--pfx-pass", default="", help="证书密码(默认读环境变量/文件)")
    ap.add_argument("--no-zip", action="store_true", help="不生成分发压缩包")
    ap.add_argument("--portable-only", action="store_true",
                    help="只出目录版, 跳过 EVB 单文件(不装 Enigma Virtual Box 也能用)")
    ap.add_argument("--no-portable", action="store_true",
                    help="跳过目录版(发布时只出单文件+zip; 也避开清理 1300 文件的删除确认)")
    args = ap.parse_args()

    ver = read_version(ROOT / "CMakeLists.txt")
    log(f"打包 {APP} v{ver} ({args.config})")

    kill_running()

    if args.skip_build:
        exe = ROOT / "build" / args.config / f"{APP}.exe"
        if not exe.exists():
            fail(f"--skip-build 但产物不存在: {exe}")
        log(f"跳过编译, 用现有: {exe}")
    else:
        exe = build(args.config)

    check_deployed(exe, args.qt)
    stage_crt(exe.parent)

    # 目录版默认出（几秒的事），发布时用 --no-portable 跳过。
    if not args.no_portable:
        make_portable(exe, ver)

    if args.portable_only:
        log(f"完成(仅目录版) -> {ROOT / 'dist' / (APP + '-portable')}")
        log("双击该目录里的 WinLauncher.exe 即可，无需解压到临时目录。")
        return

    out = pack(exe, args.evb)

    # 顺序不能反: EVB 会重写输出 exe, 签名必须在它之后; zip 又要装签好的 exe。
    if args.sign:
        pfx, pwd = resolve_cert(args)
        sign_exe(out, pfx, pwd)
    if not args.no_zip:
        make_zip(out, ver)

    mb = out.stat().st_size / 1024 / 1024
    log(f"完成 -> {out}  ({mb:.1f} MB)  v{ver}")
    log("单文件可直接拷走双击运行(配置仍写在 %LOCALAPPDATA%\\WinLauncher)")
    if not args.sign:
        log("未签名: 经网盘/聊天软件传出去后首次运行会被 SmartScreen 拦, "
            "让对方先跑 zip 里的 Unblock-First.bat")
        log("每次启动都弹窗的话改用目录版: dist\\WinLauncher-portable\\ (无自解压行为)")


if __name__ == "__main__":
    main()
