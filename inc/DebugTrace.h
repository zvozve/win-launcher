#pragma once

/*
 * 兼容层: 历史上到处调用的 wlTrace()/wlTraceEnable()。
 *
 * 真正的落盘已经交给 wl::log (spdlog), 这里只做转发 —— 保留原来的
 * 调用点不动(40+ 处), 但行为从"每条日志 open/close 一次文件、没有
 * 时间戳、只在 --trace 时记录"升级为"常开、按天切分、逐条 flush"。
 *
 * 为什么要常开: 崩溃和"自己退出"从来不会挑你加了 --trace 的时候发生。
 * 排查完可以再考虑降级别, 现在宁可多记。
 */
void wlTraceEnable(bool on);
bool wlTraceEnabled();
void wlTrace(const char *message);
class QString;
void wlTrace(const QString &message);
