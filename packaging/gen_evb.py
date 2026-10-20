#!/usr/bin/env python3
# ======================================================================
#  gen_evb.py  -  把 windeployqt 部署目录生成 Enigma Virtual Box 工程(.evb)
#
#  作用: 把 build/Release 整个目录(含 Qt DLL / 插件 / QML 模块 / Everything
#  SDK)塞进一个单文件 exe, 双击即用、无需安装 —— 即"绿色单文件版"。
#  做法与 icloud-mon/packaging/gen_evb.py 一致(同一个人维护, 保持同构)。
#
#  .evb 是 XML 格式(UTF-16), 结构:
#    根 <Files> 里一个 %DEFAULT FOLDER% 目录节点(Type=3), 它的子节点镜像
#    整个待打包目录树; 文件 Type=2(带 <File> 源路径), 目录 Type=3(带嵌套 <Files>)。
#
#  真正打包用:  "<EVB目录>/enigmavbconsole.exe" project.evb
#
#  用法:  python gen_evb.py <部署目录> <输入exe> <输出exe> [额外排除通配...]
# ======================================================================
import fnmatch
import os
import sys

# 固定排除: 基 exe 自己、调试符号、构建中间产物、运行期诊断产物。
# 通配按小写比较(Windows 文件名大小写不敏感)。
BASE_EXCLUDES = [
    # 基 exe 本身由 EVB 的 InputFile 提供, 目录里再放一份会变成"自己套自己"
    "winlauncher.exe",
    "*.pdb", "*.ilk", "*.exp", "*.lib", "*.obj",
    # CMake / Ninja 中间产物
    "cmakefiles", "winlauncher_autogen", ".cmake", ".qt",
    ".ninja_deps", ".ninja_log", "build.ninja", "cmakecache.txt",
    "cmakelists.txt", "*.ninja", "cmake_install.cmake",
    # 运行期落盘的东西(trace / 诊断报告 / 截图), 不该进包
    "winlauncher.trace", "winlauncher.*.txt", "shot-*.png", "*.evb",
    # VC 运行时安装器: 单文件里没用(不会自动执行), 真正的 DLL 由 package.py 放进去
    "vc_redist.x64.exe",
    # QML 调试插件, 发布版用不到
    "qmltooling",
]


def esc(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def skip(name: str, excludes) -> bool:
    low = name.lower()
    return any(fnmatch.fnmatch(low, pat) for pat in excludes)


def file_node(abs_path: str, name: str) -> str:
    return (
        "    <File>\n"
        "      <Type>2</Type>\n"
        f"      <Name>{esc(name)}</Name>\n"
        f"      <File>{esc(abs_path)}</File>\n"
        "      <ActiveX>false</ActiveX>\n"
        "      <ActiveXInstall>false</ActiveXInstall>\n"
        "      <Action>0</Action>\n"
        "      <OverwriteDateTime>false</OverwriteDateTime>\n"
        "      <OverwriteAttributes>false</OverwriteAttributes>\n"
        "      <PassCommandLine>false</PassCommandLine>\n"
        "    </File>\n"
    )


def collect_children(abs_path: str, excludes) -> str:
    """收集一个目录下的子节点 XML（不含外层 <File> 包装）。"""
    out = []
    try:
        entries = sorted(os.listdir(abs_path))
    except OSError as e:
        print(f"  [skip] read failed {abs_path}: {e}")
        return ""
    for entry in entries:
        if skip(entry, excludes):
            continue
        child = os.path.join(abs_path, entry)
        if os.path.isdir(child):
            out.append(dir_node(child, entry, excludes))
        else:
            out.append(file_node(child, entry))
    return "".join(out)


def dir_node(abs_path: str, name: str, excludes) -> str:
    return (
        "    <File>\n"
        "      <Type>3</Type>\n"
        f"      <Name>{esc(name)}</Name>\n"
        "      <Action>0</Action>\n"
        "      <OverwriteDateTime>false</OverwriteDateTime>\n"
        "      <OverwriteAttributes>false</OverwriteAttributes>\n"
        f"      <Files>\n{collect_children(abs_path, excludes)}      </Files>\n"
        "    </File>\n"
    )


def build_evb(deploy_dir: str, input_exe: str, output_exe: str, excludes) -> str:
    files_body = collect_children(deploy_dir, excludes)
    return (
        '<?xml encoding="utf-16"?>\n'
        "<>\n"
        f"  <InputFile>{esc(input_exe)}</InputFile>\n"
        f"  <OutputFile>{esc(output_exe)}</OutputFile>\n"
        "  <Files>\n"
        "    <Enabled>true</Enabled>\n"
        "    <DeleteExtractedOnExit>true</DeleteExtractedOnExit>\n"
        "    <CompressFiles>true</CompressFiles>\n"
        "    <Files>\n"
        "      <File>\n"
        "        <Type>3</Type>\n"
        "        <Name>%DEFAULT FOLDER%</Name>\n"
        "        <Action>0</Action>\n"
        "        <OverwriteDateTime>false</OverwriteDateTime>\n"
        "        <OverwriteAttributes>false</OverwriteAttributes>\n"
        f"        <Files>\n{files_body}        </Files>\n"
        "      </File>\n"
        "    </Files>\n"
        "  </Files>\n"
        "  <Registries>\n"
        "    <Enabled>false</Enabled>\n"
        "    <Registries></Registries>\n"
        "  </Registries>\n"
        "  <Packaging>\n"
        "    <Enabled>false</Enabled>\n"
        "  </Packaging>\n"
        "  <Options>\n"
        "    <ShareVirtualSystem>false</ShareVirtualSystem>\n"
        "    <MapExecutableWithTemporaryFile>true</MapExecutableWithTemporaryFile>\n"
        "    <AllowRunningOfVirtualExeFiles>true</AllowRunningOfVirtualExeFiles>\n"
        "  </Options>\n"
        "</>\n"
    )


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    deploy_dir = os.path.abspath(sys.argv[1])
    input_exe = os.path.abspath(sys.argv[2])
    output_exe = os.path.abspath(sys.argv[3])

    excludes = list(BASE_EXCLUDES) + [p.lower() for p in sys.argv[4:]]

    if not os.path.isdir(deploy_dir):
        print(f"deploy dir not found: {deploy_dir}")
        sys.exit(1)
    if not os.path.isfile(input_exe):
        print(f"input exe not found: {input_exe}")
        sys.exit(1)

    xml = build_evb(deploy_dir, input_exe, output_exe, excludes)

    evb_path = os.path.splitext(output_exe)[0] + ".evb"
    os.makedirs(os.path.dirname(output_exe), exist_ok=True)
    # EVB 只吃 UTF-16 的工程文件
    with open(evb_path, "w", encoding="utf-16") as f:
        f.write(xml)

    n_files = xml.count("<Type>2</Type>")
    total = 0
    for root, dirs, files in os.walk(deploy_dir):
        dirs[:] = [d for d in dirs if not skip(d, excludes)]
        for fn in files:
            if not skip(fn, excludes):
                try:
                    total += os.path.getsize(os.path.join(root, fn))
                except OSError:
                    pass
    print(f"[gen_evb] project : {evb_path}")
    print(f"[gen_evb] files   : {n_files}  ({total / 1024 / 1024:.1f} MB before compression)")
    print(f"[gen_evb] skipped : WinLauncher.exe / *.pdb / CMakeFiles / trace / shot-*.png ...")


if __name__ == "__main__":
    main()
