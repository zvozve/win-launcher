// ======================================================================
//  CrashHandler.h  -  崩溃 / 非正常退出现场记录
//
//  为什么单独一套而不用日志: 崩溃那一刻 spdlog 的互斥量可能正被别的
//  线程持有(死锁), sink 可能已损坏, 堆可能已损坏。所以这里的写文件
//  路径是一根筋的裸 Win32: CreateFileW + WriteFile + CloseHandle,
//  不分配堆、不过锁、不碰 QString。
//
//  产出: %LOCALAPPDATA%\WinLauncher\log\crash-<时间>-<pid>.txt
//      + 同名 .dmp (MiniDump, 可用 VS / WinDbg 打开看完整调用栈)
// ======================================================================
#pragma once

#include <QString>

namespace wl {
namespace crash {

// 安装 未处理异常 / terminate / CRT 信号 的捕获。
// 在 QApplication 构造之后、日志初始化之后调用。
void install();

// 崩溃文件所在目录(与日志同目录)
QString crashDir();

} // namespace crash
} // namespace wl
