#pragma once

/*
 * 极简启动/运行打点（临时排查用）。
 * GUI 发行版没有控制台，qInfo 也不一定可见，所以直接写文件。
 * 用 --trace 打开，文件落 <exe 目录>/winlauncher.trace。
 */
void wlTraceEnable(bool on);
bool wlTraceEnabled();
void wlTrace(const char *message);
class QString;
void wlTrace(const QString &message);
