#include <QAction>
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
#include <QtGlobal>

#include "app/controller.h"
#include "core/config.h"
#include "core/debugtrace.h"
#include "core/everythingsearch.h"
#include "core/hotkeymanager.h"
#include "core/launcherwindow.h"

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
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setApplicationDisplayName(QStringLiteral("WinLauncher"));
    app.setQuitOnLastWindowClosed(false); // 关掉面板不退出，靠托盘常驻

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // 主线程 COM（Shell 图标 / IShellLink / AUMID 激活都要用）
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wlTrace("COM ready");

    // ---- 单实例：已有实例则通知它弹出面板后退出 ---------------------------
    {
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
    if (!server.listen(kPipeName))
        qWarning("单实例管道监听失败：%s", qPrintable(server.errorString()));

    // ---- 核心对象（先不扫描：扫描线程会碰 Shell/COM，必须等引擎与窗口就绪）----
    Config config;
    config.load();

    EverythingSearch everything;
    HotkeyManager hotkeys;
    Controller controller(&config, &everything);

    qmlRegisterType<LauncherWindow>("WinLauncher.Core", 1, 0, "LauncherWindow");
    qmlRegisterSingletonInstance("WinLauncher.Core", 1, 0, "Ctrl", &controller);
    qmlRegisterSingletonInstance("WinLauncher.Core", 1, 0, "AppConfig", &config);
    wlTrace("qml types registered");

    QQmlApplicationEngine engine;
    engine.loadFromModule("WinLauncher", "Main");
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
