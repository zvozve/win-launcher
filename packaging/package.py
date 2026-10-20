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
#
#  产物: dist/WinLauncher.exe (单文件, 双击即用; dist/ 已在 .gitignore)
# ======================================================================
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
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

    out = pack(exe, args.evb)
    mb = out.stat().st_size / 1024 / 1024
    log(f"完成 -> {out}  ({mb:.1f} MB)  v{ver}")
    log("单文件可直接拷走双击运行(配置仍写在 %LOCALAPPDATA%\\WinLauncher)")


if __name__ == "__main__":
    main()
