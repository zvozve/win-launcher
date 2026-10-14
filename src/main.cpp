#include <QAction>
#include <QDir>
#include <QFile>
#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <QtGlobal>

#include "Controller.h"
#include "Config.h"
#include "DebugTrace.h"
#include "EverythingSearch.h"
#include "HotkeyManager.h"
#include "LauncherWindow.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

namespace ControllerPrivate {
void setAutoStartLaunch(bool on);
}

namespace {

const QString kPipeName = QStringLiteral("WinLauncher.SingleInstance");

// 托盘图标：四个圆角方块（启动台网格）
QIcon makeAppIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    const qreal cell = 24.0;
    const qreal gap = 6.0;
    const qreal start = (64.0 - (cell * 2 + gap)) / 2.0;
    const QColor c(0, 122, 255);

    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            QColor cc = c;
            cc.setAlpha(row == 0 ? 255 : 190);
            p.setBrush(cc);
            p.drawRoundedRect(
                QRectF(start + col * (cell + gap), start + row * (cell + gap), cell, cell), 7.0,
                7.0);
        }
    }
    p.end();
    return QIcon(pm);
}

// ---- 诊断输出 ----------------------------------------------------------
// 本程序是 WIN32 子系统（没有控制台），所以 --test-* 类诊断既尝试借用父进程
// 的控制台，也把结果写到 <exe目录>\winlauncher.diagnostics.txt，保证一定拿得到。
QString diagnosticsPath()
{
    const QString dir = QCoreApplication::instance() ? QCoreApplication::applicationDirPath()
                                                     : QDir::currentPath();
    return QDir(dir).filePath(QStringLiteral("winlauncher.diagnostics.txt"));
}

void attachParentConsole()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    FILE *f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}

void report(const QString &line)
{
    std::fprintf(stdout, "%s\n", qPrintable(line));
    QFile f(diagnosticsPath());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(line.toUtf8());
        f.write("\n");
        f.close();
    }
}

void raiseNotice(QSystemTrayIcon *tray, const QString &title, const QString &body)
{
    if (tray)
        tray->showMessage(title, body, QSystemTrayIcon::Information, 2500);
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--trace") == 0)
            wlTraceEnable(true);
    }

    // QML 绑定错误 / Qt 警告在 GUI 版看不到控制台，统一落到 trace 文件
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        const char *level = "info";
        switch (type) {
        case QtDebugMsg:    level = "debug"; break;
        case QtInfoMsg:     level = "info"; break;
        case QtWarningMsg:  level = "warn"; break;
        case QtCriticalMsg: level = "crit"; break;
        case QtFatalMsg:    level = "fatal"; break;
        }
        wlTrace(QStringLiteral("[%1] %2").arg(QString::fromLatin1(level), msg));
        if (type == QtFatalMsg)
            abort();
    });

    wlTrace("enter main()");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("WinLauncher"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setApplicationDisplayName(QStringLiteral("WinLauncher"));
    app.setQuitOnLastWindowClosed(false); // 关掉面板不退出，靠托盘常驻

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // 主线程 COM（Shell 图标 / IShellLink / AUMID 激活都要用）
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wlTrace("COM ready");

    // ---- 单实例：已有实例则通知它弹出面板后退出 ---------------------------
    // 诊断参数（--test-everything）不参与单实例协商：否则托盘里已经常驻一个实例时，
    // 自检会在这里被静默吞掉，什么都不打印，看起来像"跑不起来"。
    const bool diagnosticRun = app.arguments().contains(QStringLiteral("--test-everything"));

    if (!diagnosticRun) {
        QLocalSocket probe;
        probe.connectToServer(kPipeName);
        if (probe.waitForConnected(300)) {
            probe.write("show");
            probe.flush();
            probe.waitForBytesWritten(300);
            return 0;
        }
        QLocalServer::removeServer(kPipeName);
    }

    QLocalServer server;
    if (!diagnosticRun && !server.listen(kPipeName))
        qWarning("单实例管道监听失败：%s", qPrintable(server.errorString()));

    // ---- 核心对象（先不扫描：扫描线程会碰 Shell/COM，必须等引擎与窗口就绪）----
    Config config;
    config.load();

    EverythingSearch everything;

    // 自检：--test-everything <query>  直接查一次 Everything 索引后退出（不建窗口）
    {
        const QStringList testArgs = app.arguments();
        const int idx = testArgs.indexOf(QStringLiteral("--test-everything"));
        if (idx >= 0) {
            const QString query = (idx + 1 < testArgs.size()) ? testArgs.at(idx + 1) : QString();
            attachParentConsole();
            QFile::remove(diagnosticsPath());
            report(QStringLiteral("everything.available  = %1")
                       .arg(everything.isAvailable() ? QStringLiteral("true") : QStringLiteral("false")));
            report(QStringLiteral("everything.loadedPath = %1").arg(everything.loadedPath()));
            report(QStringLiteral("searchCategory       = %1").arg(config.searchCategory()));
            if (!query.isEmpty()) {
                const QStringList hits =
                    everything.search(query, 20, Config::categoryExtensions(config.searchCategory()));
                report(QStringLiteral("query=\"%1\"  filter=\"%2\"  hits=%3")
                           .arg(query, Config::categoryExtensions(config.searchCategory()))
                           .arg(hits.size()));
                for (const QString &h : hits)
                    report(QStringLiteral("  ") + h);
            }
            report(QStringLiteral("report file: ") + diagnosticsPath());
            return 0;
        }
    }

    HotkeyManager hotkeys;
    Controller controller(&config, &everything);

    qmlRegisterType<LauncherWindow>("WinLauncher.Core", 1, 0, "LauncherWindow");
    qmlRegisterSingletonInstance("WinLauncher.Core", 1, 0, "Ctrl", &controller);
    qmlRegisterSingletonInstance("WinLauncher.Core", 1, 0, "AppConfig", &config);
    wlTrace("qml types registered");

    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    wlTrace("qml loaded");
    if (engine.rootObjects().isEmpty()) {
        qCritical("QML 加载失败，程序退出");
        return 1;
    }

    auto *window = qobject_cast<LauncherWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        qCritical("QML 根对象不是 LauncherWindow");
        return 1;
    }

    controller.attachWindow(window);
    wlTrace("window attached");

    // 热键需要窗口已有原生句柄
    window->setHotkeyManager(&hotkeys);
    if (!hotkeys.setSequence(config.hotkey()))
        qWarning("配置中的热键 \"%s\" 不可用，沿用默认值", qPrintable(config.hotkey()));
    hotkeys.attachWindow(window->winId());
    controller.attachHotkeys(&hotkeys);
    wlTrace("hotkeys attached");

    // ---- 托盘 -------------------------------------------------------------
    QSystemTrayIcon tray(makeAppIcon());
    tray.setToolTip(QStringLiteral("WinLauncher"));

    QMenu trayMenu;
    QAction *actShow = trayMenu.addAction(QStringLiteral("打开启动台"));
    QAction *actEdit = trayMenu.addAction(QStringLiteral("编辑图标…"));
    QAction *actSettings = trayMenu.addAction(QStringLiteral("设置…"));
    trayMenu.addSeparator();
    QAction *actAutoStart = trayMenu.addAction(QStringLiteral("开机自启"));
    actAutoStart->setCheckable(true);
    actAutoStart->setChecked(config.autostart());
    QAction *actRescan = trayMenu.addAction(QStringLiteral("重新扫描"));
    QAction *actConfig = trayMenu.addAction(QStringLiteral("打开配置目录"));
    trayMenu.addSeparator();
    QAction *actQuit = trayMenu.addAction(QStringLiteral("退出"));

    QObject::connect(actShow, &QAction::triggered, &controller, &Controller::togglePanel);
    QObject::connect(actEdit, &QAction::triggered, &controller, &Controller::editRequested);
    QObject::connect(actSettings, &QAction::triggered, &controller,
                     &Controller::settingsRequested);
    QObject::connect(actRescan, &QAction::triggered, &controller, &Controller::rescan);
    QObject::connect(actConfig, &QAction::triggered, &controller, &Controller::openConfigFolder);
    QObject::connect(actQuit, &QAction::triggered, &controller, &Controller::requestQuit);
    QObject::connect(actAutoStart, &QAction::toggled, &config, &Config::setAutostart);
    QObject::connect(&config, &Config::autostartChanged, actAutoStart, [actAutoStart, &config] {
        QSignalBlocker blocker(actAutoStart);
        actAutoStart->setChecked(config.autostart());
    });
    QObject::connect(&tray, &QSystemTrayIcon::activated, &controller,
                     [&controller](QSystemTrayIcon::ActivationReason reason) {
                         switch (reason) {
                         case QSystemTrayIcon::Trigger:
                         case QSystemTrayIcon::DoubleClick:
                             controller.togglePanel();
                             break;
                         case QSystemTrayIcon::MiddleClick:
                             controller.rescan();
                             break;
                         default:
                             break;
                         }
                     });

    tray.setContextMenu(&trayMenu);
    tray.show();

    // ---- 单实例回调 -------------------------------------------------------
    QObject::connect(&server, &QLocalServer::newConnection, &controller, [&server, &controller] {
        QLocalSocket *client = server.nextPendingConnection();
        if (!client)
            return;
        QObject::connect(client, &QLocalSocket::disconnected, client, &QLocalSocket::deleteLater);
        controller.togglePanel();
    });

    // ---- 启动形态 ---------------------------------------------------------
    const QStringList args = app.arguments();
    const bool autoStart = args.contains(QStringLiteral("--autostart"));
    ControllerPrivate::setAutoStartLaunch(autoStart);

    if (!autoStart) {
        // 延时一点，等 QML 布局完成再弹出，避免首帧尺寸跳变
        QTimer::singleShot(120, &controller, [&controller] { controller.togglePanel(); });
    } else {
        raiseNotice(&tray, QStringLiteral("WinLauncher"),
                    QStringLiteral("已在托盘常驻，按 %1 打开启动台").arg(config.hotkey()));
    }

    // 扫描延后到引擎/窗口就绪之后：扫描线程走 Shell + COM，与 QML 引擎初始化重叠
    // 会破坏进程状态（实测表现为引擎构造期段错误）。
    QTimer::singleShot(300, &controller, [&controller] { controller.rescan(); });
    wlTrace("startup complete");

    const int code = app.exec();
    wlTrace("event loop exited");

    if (SUCCEEDED(comHr))
        CoUninitialize();
    return code;
}
