#include <QAction>
#include <QDir>
#include <QFile>
#include <QApplication>
#include <QHash>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <functional>
#include <utility>
#include <QtGlobal>

#include "AppItem.h"
#include "AppScanner.h"
#include "AppUninstaller.h"
#include "Controller.h"
#include "BackdropProvider.h"
#include "Config.h"
#include "DebugTrace.h"
#include "EverythingSearch.h"
#include "HotkeyManager.h"
#include "LauncherWindow.h"
#include "UiFont.h"

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

    /*
     * --software：强制走软件渲染（Qt Quick 的 raster 后端）。
     *
     * 只给自动化验证用：默认的 D3D11 后端下，PrintWindow 抓到的是空白
     * （Qt 在独立的交换链上画，窗口 DC 里没有内容），截出来的图没法看。
     * 软件渲染时内容直接画进窗口 DC，PrintWindow 就能拿到真实界面。
     */
    if (app.arguments().contains(QStringLiteral("--software")))
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

    // 主线程 COM（Shell 图标 / IShellLink / AUMID 激活都要用）
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wlTrace("COM ready");

    // ---- 单实例：已有实例则通知它弹出面板后退出 ---------------------------
    // 诊断参数（--test-everything）不参与单实例协商：否则托盘里已经常驻一个实例时，
    // 自检会在这里被静默吞掉，什么都不打印，看起来像"跑不起来"。
    const bool diagnosticRun = app.arguments().contains(QStringLiteral("--test-everything"))
                               || app.arguments().contains(QStringLiteral("--dump-apps"))
                               || app.arguments().contains(QStringLiteral("--dump-groups"))
                               || app.arguments().contains(QStringLiteral("--test-uninstall"));

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

    // 主题排查用：确认引擎拿到的到底是不是配置里的值
    wlTrace(QStringLiteral("config: path=%1 darkMode=%2 followSystemTheme=%3")
                .arg(config.configPath())
                .arg(config.darkMode())
                .arg(config.followSystemTheme()));

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

    /*
     * 自检：--dump-apps  同步跑一遍扫描，把**最终收录**的清单写成文本
     * （类型 / 名字 / 目标 / 来源），不建窗口。用来核对过滤规则到底留下了什么。
     */
    if (app.arguments().contains(QStringLiteral("--dump-apps"))) {
        attachParentConsole();
        QFile::remove(diagnosticsPath());

        AppScanner::Options opt;
        opt.startMenu = true;
        opt.registry = config.includeRegistry();
        opt.uwp = false;

        const QList<AppScanner::Task> tasks = AppScanner::buildTasks(opt);
        const QString outPath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("winlauncher.apps.txt"));
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            report(QStringLiteral("dump failed: ") + outPath);
            return 0;
        }

        QTextStream ts(&out); // Qt 6 默认 UTF-8
        QSet<QString> seen;   // 和 UI 一样按 seed 去重（多个 .lnk 可能指向同一个 exe）
        int kept = 0;
        int nonExe = 0;
        int grouped = 0;
        for (const AppScanner::Task &t : tasks) {
            ScannedApp a;
            if (!AppScanner::runTask(t, &a))
                continue;
            if (seen.contains(a.seed))
                continue;
            seen.insert(a.seed);
            ++kept;
            const bool isExe = a.target.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive);
            if (!isExe)
                ++nonExe;
            if (!a.group.isEmpty())
                ++grouped;
            ts << (isExe ? "exe  " : "other") << '\t' << a.category << '\t' << a.name << '\t'
               << a.target << '\t' << a.sourcePath << '\t' << a.group << '\n';
        }
        ts.flush();
        out.close();

        report(QStringLiteral("tasks=%1 kept=%2 nonExe=%3 grouped=%4")
                   .arg(tasks.size())
                   .arg(kept)
                   .arg(nonExe)
                   .arg(grouped));
        report(QStringLiteral("list: ") + outPath);
        return 0;
    }

    /*
     * 自检：--dump-groups  把「同源多入口归组」的结果打成文本报表，不建窗口。
     *
     * 调规则（阈值 / 系统文件夹黑名单）之后跑一次，看哪些文件夹被折叠、
     * 哪些该折却没折 —— 比盯界面数格子直接得多。同时写 winlauncher.groups.txt。
     */
    if (app.arguments().contains(QStringLiteral("--dump-groups"))) {
        attachParentConsole();
        QFile::remove(diagnosticsPath());

        AppScanner::Options opt;
        opt.startMenu = true;
        opt.registry = config.includeRegistry();
        opt.uwp = false;

        const QList<AppScanner::Task> tasks = AppScanner::buildTasks(opt);
        QList<ScannedApp> apps;
        QSet<QString> seen;
        for (const AppScanner::Task &t : tasks) {
            ScannedApp a;
            if (!AppScanner::runTask(t, &a))
                continue;
            const QString key = a.target.isEmpty() ? a.seed.toLower() : a.target.toLower();
            if (key.isEmpty() || seen.contains(key))
                continue; // 与 UI 的 finishScan 去重方式保持一致
            seen.insert(key);
            apps.append(a);
        }

        const QString outPath = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("winlauncher.groups.txt"));
        QFile out(outPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            report(QStringLiteral("dump failed: ") + outPath);
            return 0;
        }
        QTextStream ts(&out);

        // 纯统计用的归组：不依赖 Controller/AppItem，规则与 rebuildGroups 一致
        QHash<QString, QList<const ScannedApp *>> buckets;
        QStringList order;
        for (const ScannedApp &a : std::as_const(apps)) {
            if (a.group.isEmpty())
                continue;
            if (!buckets.contains(a.group))
                order.append(a.group);
            buckets[a.group].append(&a);
        }

        const int threshold = config.groupThreshold();
        int folded = 0;
        int foldedItems = 0;
        QList<QString> report1; // 折叠的
        QList<QString> report0; // 没折叠的
        for (const QString &key : std::as_const(order)) {
            const QList<const ScannedApp *> members = buckets.value(key);
            const QString label = key.section(QLatin1Char('\\'), -1);
            QString line = QStringLiteral("%1\t%2\t%3")
                               .arg(label, QString::number(members.size()), key);
            if (members.size() >= threshold) {
                ++folded;
                foldedItems += members.size();
                // 组内主导分类（和 Controller::openGroup / rebuildGroups 同一套算法）
                QHash<QString, int> counts;
                QString dominant;
                int best = 0;
                for (const ScannedApp *m : members) {
                    const int n = ++counts[m->category];
                    if (n > best) {
                        best = n;
                        dominant = m->category;
                    }
                }
                ts << "[FOLD] " << line << '\t' << dominant << '\n';
                for (const ScannedApp *m : members)
                    ts << "        - " << m->name << "  (" << m->target << ")\n";
                report1.append(QStringLiteral("%1 (%2)").arg(label).arg(members.size()));
            } else {
                report0.append(QStringLiteral("%1 (%2)").arg(label).arg(members.size()));
                ts << "[keep] " << line << '\n';
            }
        }
        ts.flush();
        out.close();

        report(QStringLiteral("apps=%1 groups=%2 folded=%3 foldedItems=%4 singles=%5")
                   .arg(apps.size())
                   .arg(order.size())
                   .arg(folded)
                   .arg(foldedItems)
                   .arg(apps.size() - foldedItems));
        report(QStringLiteral("folded: ") + report1.join(QStringLiteral(" | ")));
        report(QStringLiteral("kept  : ") + report0.join(QStringLiteral(" | ")));
        report(QStringLiteral("report: ") + outPath);
        return 0;
    }

    /*
     * 自检：--test-uninstall <程序名或 exe 路径>
     * 打印这个条目匹配到的注册表卸载项，用来核对「卸载此程序」的匹配逻辑。
     * **不执行卸载**。
     */
    {
        const QStringList a = app.arguments();
        const int idx = a.indexOf(QStringLiteral("--test-uninstall"));
        if (idx >= 0) {
            attachParentConsole();
            QFile::remove(diagnosticsPath());

            const QString what = (idx + 1 < a.size()) ? a.at(idx + 1) : QString();
            if (what.isEmpty()) {
                report(QStringLiteral("usage: --test-uninstall <name-or-exe-path>"));
            } else {
                AppItem probe;
                if (what.contains(QLatin1Char('\\')) || what.contains(QLatin1Char('/')))
                    probe.setTarget(what);
                else
                    probe.setName(what);

                const AppUninstaller::Entry e = AppUninstaller::findFor(&probe);
                report(QStringLiteral("query       = %1").arg(what));
                report(QStringLiteral("matched     = %1").arg(e.isValid() ? QStringLiteral("yes")
                                                                        : QStringLiteral("no")));
                if (e.isValid()) {
                    report(QStringLiteral("key         = %1").arg(e.key));
                    report(QStringLiteral("displayName = %1").arg(e.displayName));
                    report(QStringLiteral("icon        = %1").arg(e.displayIcon));
                    report(QStringLiteral("installLoc  = %1").arg(e.installLocation));
                    report(QStringLiteral("uninstall   = %1").arg(e.uninstallString));
                }
            }
            return 0;
        }
    }

    HotkeyManager hotkeys;
    Controller controller(&config, &everything);
    UiFont uiFont;

    /*
     * --keep-open：强制关掉「失焦收起」。
     * 给自动化验证用 —— 脚本点完面板后焦点很容易跑到别的窗口，面板一收
     * 截图就全空了。以前是靠直接改 %LOCALAPPDATA% 的 config.json，
     * 但那个目录不一定可写（沙箱），改成进程内覆盖，不落盘。
     */
    if (app.arguments().contains(QStringLiteral("--keep-open")))
        config.setHideOnFocusOut(false);

    // 菜单图标字体是否可用：不可用时 QML 会退回纯文字菜单（不会出现豆腐块）
    wlTrace(QStringLiteral("UiFont: menuIconFamily='%1' available=%2")
                .arg(uiFont.menuIconFamily())
                .arg(uiFont.menuIconsAvailable() ? 1 : 0));

    qmlRegisterType<LauncherWindow>("WinLauncher.Core", 1, 0, "LauncherWindow");
    qmlRegisterSingletonInstance("WinLauncher.Core", 1, 0, "Ctrl", &controller);
    qmlRegisterSingletonInstance("WinLauncher.Core", 1, 0, "AppConfig", &config);
    // 菜单图标字体（Segoe Fluent Icons）的可用性 + 字形码点
    qmlRegisterSingletonInstance("WinLauncher.Core", 1, 0, "UiFont", &uiFont);
    wlTrace("qml types registered");

    QQmlApplicationEngine engine;

    // 面板玻璃底图：C++ 抓屏 + 模糊后经 image://wlbg/panel?<token> 交给 QML。
    // addImageProvider 会接管所有权（engine 比 controller 先析构），所以这里 new 完不管。
    // 必须在 load() 之前注册，否则 QML 里的 image://wlbg/... 会解析失败。
    auto *backdrop = new BackdropProvider();
    engine.addImageProvider(QStringLiteral("wlbg"), backdrop);
    controller.attachBackdropProvider(backdrop);

    engine.load(QUrl(app.arguments().contains(QStringLiteral("--qml-test"))
                         ? QStringLiteral("qrc:/qml/LayoutTest.qml")
                         : QStringLiteral("qrc:/qml/Main.qml")));
    wlTrace("qml loaded");
    if (engine.rootObjects().isEmpty()) {
        qCritical("QML 加载失败，程序退出");
        return 1;
    }

    auto *window = qobject_cast<LauncherWindow *>(engine.rootObjects().constFirst());
    if (!window) {
        // --qml-test 加载的是普通 Window，不是 LauncherWindow：只验证布局就到此为止
        if (app.arguments().contains(QStringLiteral("--qml-test"))) {
            auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            if (!w)
                return 1;
            QTimer::singleShot(2500, w, [w] {
                const QString out = QDir(QCoreApplication::applicationDirPath())
                                        .filePath(QStringLiteral("shot-qmltest.png"));
                const QImage shot = w->grabWindow();
                shot.save(out);
                QStringList found;
                std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                    if (!item)
                        return;
                    if (item->objectName() == QLatin1String("testLabel")) {
                        found << QStringLiteral("%1[%2x%3] vis=%4 text='%5'")
                                     .arg(item->objectName())
                                     .arg(int(item->width()))
                                     .arg(int(item->height()))
                                     .arg(item->isVisible() ? 1 : 0)
                                     .arg(item->property("text").toString());
                    }
                    const QList<QQuickItem *> kids = item->childItems();
                    for (QQuickItem *kid : kids)
                        walk(kid);
                };
                walk(w->contentItem());
                wlTrace(QStringLiteral("qmltest: %1 labels, saved %2")
                            .arg(found.size())
                            .arg(out));
                for (const QString &s : std::as_const(found))
                    wlTrace(QStringLiteral("  %1").arg(s));
                QCoreApplication::quit();
            });
            return app.exec();
        }
        qCritical("QML 根对象不是 LauncherWindow");
        return 1;
    }

    controller.attachWindow(window);
    wlTrace("window attached");

    /*
     * --open-folder：扫描完成后自动进第一个文件夹。
     * 给自动化验证用 —— 想截「组内视图」，靠脚本点坐标既不准也脆弱，
     * 这里直接调 Ctrl.openGroupKey()，走的是和点瓦片完全一样的代码路径。
     */
    if (app.arguments().contains(QStringLiteral("--open-folder"))) {
        auto *t = new QTimer(window);
        t->setInterval(700);
        QObject::connect(t, &QTimer::timeout, window, [&controller, t] {
            const QList<QObject *> tiles = controller.gridItems();
            for (QObject *tile : tiles) {
                if (tile->property("isFolder").toBool()) {
                    const QString key = tile->property("groupKey").toString();
                    wlTrace(QStringLiteral("open-folder: %1").arg(key));
                    controller.openGroupKey(key);
                    t->stop();
                    return;
                }
            }
        });
        t->start();
    }

    /*
     * --dump-icons：把网格的渲染结果和每个瓦片的图标状态都落盘。
     *
     * 排查「界面出来了但图标是空白」用的 —— 光看截图分不清是
     *   「QML 没绑到 app」/「图片路径不对」/「异步解码还没完」/
     *   「其实画好了，是抓图方式（BitBlt/PrintWindow）拿不到 GPU 合成的内容」。
     * 所以这里两手都有：窗口 grabWindow() 存 PNG（Qt 自己合成的画面，
     * 不含窗口外框，但内容是准的）+ 逐格打印 Image.status/source。
     *
     * 只给自动化验证用，正常启动不走这条路。
     *
     * 追加的 --test-search <关键词>（配合 --dump-icons 一起用）：脚本化验证
     * 「搜索有结果」+「清空后恢复全量」。直接调 Controller::setQuery，
     * 不走键盘注入 —— 面板弹在真桌面上时，别的窗口一抢焦点面板就收了
     * （hideOnFocusOut），靠 SendInput 敲字验证不稳。
     */
    QString testTerm;
    {
        const QStringList args = app.arguments();
        const int i = args.indexOf(QStringLiteral("--test-search"));
        if (i >= 0 && i + 1 < args.size())
            testTerm = args.at(i + 1);
    }

    /*
     * --test-windows（配合 --dump-icons）：脚本化打开「设置」「编辑」两个独立窗口
     * 并各抓一张图。
     *
     * 这两个是 QML Window，不跟主面板一起显示，按热键弹不出来；靠点鼠标又要猜
     * 坐标（面板是自绘的，UI 树里没有按钮节点）。所以直接调它们的 openWindow()。
     * 验主题配色时这两张图是必需的 —— 它们是颜色最密集的两个界面。
     */
    const bool testWindows = app.arguments().contains(QStringLiteral("--test-windows"));

    if (app.arguments().contains(QStringLiteral("--dump-icons"))) {
        /*
         * 面板是热键按下去才弹的，脚本按热键的时机对不齐，所以用
         * 「面板一可见就抓一张」的轮询。grabWindow 抓的是 Qt 自己合成的画面，
         * 不受 BitBlt / PrintWindow 在锁屏与 GPU 合成下的限制。
         *
         * 注意：grabWindow 必须让面板处于可见状态，而且**宽度不是 16 的整数倍时
         * D3D 后端会翻车**（Qt 6 已知问题），所以抓之前临时把宽度 +16 再改回来。
         */
        auto *dumpState = new QObject(window);
        dumpState->setProperty("shots", 0);
        dumpState->setProperty("sawVisible", false);

        const auto dumpIcons = [window, dumpState](const QString &suffix) {
            const QString path = QDir(QCoreApplication::applicationDirPath())
                                     .filePath(QStringLiteral("winlauncher.icons%1.txt").arg(suffix));
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return;
            QTextStream ts(&f);
            /*
             * 自己走可视树（childItems），不要用 QObject::findChildren：
             * QML 组件内部声明的对象不在 QObject 的父子链上，
             * findChildren("appTile") 会返回 0 —— 别问我怎么知道的。
             */
            QList<QQuickItem *> tiles;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                if (!item)
                    return;
                if (item->objectName() == QLatin1String("appTile"))
                    tiles.append(item);
                const QList<QQuickItem *> kids = item->childItems();
                for (QQuickItem *kid : kids)
                    walk(kid);
            };
            walk(window->contentItem());

            /*
             * GridView 只给「可见区间」的 delegate 尺寸，其余是 0×0；而且 QML 里
             * 直接声明 `width: grid.cellWidth` 的 delegate，QObject 树上看不到尺寸。
             * 所以只统计有尺寸的那些，免得打点里全是 0 让人误判。
             */
            QList<QQuickItem *> live;
            for (QQuickItem *t : std::as_const(tiles)) {
                if (t->width() > 0)
                    live.append(t);
            }
            tiles = live;

            ts << "appTile items: " << tiles.size() << '\n';
            int ready = 0;
            int nullSource = 0;
            for (QQuickItem *tile : tiles) {
                auto *img = tile->findChild<QQuickItem *>(QStringLiteral("tileIcon"));
                const QString src = img ? img->property("source").toString() : QString();
                const int st = img ? img->property("status").toInt() : -1;
                const QString name = tile->property("appName").toString();
                const QObject *app = tile->property("app").value<QObject *>();
                const QString iconPath = app ? app->property("iconPath").toString() : QStringLiteral("(no app)");
                if (st == 1 /*Ready*/)
                    ++ready;
                if (src.isEmpty())
                    ++nullSource;
                ts << name << '\t' << "status=" << st << '\t' << "iconPath=" << iconPath
                   << '\t' << src << '\n';
            }
            ts << "ready=" << ready << " emptySource=" << nullSource << '\n';
            ts.flush();
            f.close();
            wlTrace(QStringLiteral("icons: appTile=%1 ready=%2 empty=%3 shots=%4")
                        .arg(tiles.size())
                        .arg(ready)
                        .arg(nullSource)
                        .arg(dumpState->property("shots").toInt()));
        };

        const auto grab = [window](const QString &name) {
            const bool pad = window->width() % 16 != 0;
            const QSize keep(window->width(), window->height());
            if (pad)
                window->setWidth(keep.width() + 16);
            const QImage shot = window->grabWindow();
            if (pad) {
                window->setWidth(keep.width());
                window->setHeight(keep.height());
            }
            const QString out = QDir(QCoreApplication::applicationDirPath()).filePath(name);
            if (shot.save(out))
                wlTrace(QStringLiteral("grab: %1 (%2x%3)")
                            .arg(name).arg(shot.width()).arg(shot.height()));
        };

        // 不自动弹：面板必须「脚本按热键」才出现。以前这里 singleShot(300) 弹一次，
        // 结果和脚本的热键变成「开→关」两次 toggle，面板瞬间又收起，
        // 打点全是 tiles=0（排查了半天，记在这儿）
        auto *poll = new QTimer(window);
        poll->setInterval(1200);
        // 捕获 &controller / testTerm 是给下面的 --test-search 用的：
        // 嵌套 lambda 无法访问外层 lambda 没捕获的东西（C3493）
        QObject::connect(poll, &QTimer::timeout, window,
                         [window, grab, dumpState, dumpIcons, &controller, testTerm,
                          testWindows] {
            const bool visible = window->isVisible();
            if (visible && !dumpState->property("sawVisible").toBool()) {
                dumpState->setProperty("sawVisible", true);
                const int n = dumpState->property("shots").toInt() + 1;
                dumpState->setProperty("shots", n);

                // 面板稳定后再去开「设置 / 编辑」两个窗口（见 --test-windows 说明）
                if (testWindows) {
                    QTimer::singleShot(2500, window, [window] {
                        const auto shoot = [window](const char *prop, const QString &file) {
                            auto *w = qobject_cast<QQuickWindow *>(
                                window->property(prop).value<QObject *>());
                            if (!w) {
                                wlTrace(QStringLiteral("window-test: %1 取不到窗口").arg(file));
                                return;
                            }
                            QMetaObject::invokeMethod(w, "openWindow");
                            QTimer::singleShot(900, w, [w, file, window] {
                                const QImage shot = w->grabWindow();
                                const QString out =
                                    QDir(QCoreApplication::applicationDirPath()).filePath(file);
                                if (shot.save(out))
                                    wlTrace(QStringLiteral("window-test: %1 (%2x%3) "
                                                           "mainWindow.darkBackdrop=%4")
                                                .arg(file).arg(shot.width()).arg(shot.height())
                                                .arg(window->property("darkBackdrop").toBool()));
                                else
                                    wlTrace(QStringLiteral("window-test: %1 保存失败").arg(file));
                            });
                        };
                        shoot("settingsWindow", QStringLiteral("shot-settings.png"));
                        shoot("editWindow", QStringLiteral("shot-edit.png"));
                    });
                }
                // 面板刚可见时图标可能还在异步解码，隔 1.5s 再抓一张并打点
                grab(QStringLiteral("shot-panel-%1.png").arg(n));
                // 时间线：面板可见后 0.5 / 1.5 / 3 / 6 / 10 秒各抓一张，
                // 用来看「到底什么时候才真正画出来」——
                // 离线截图里出现过「有图标没名字」和「什么都没有」两种互相矛盾的画面。
                for (int k : {0, 1, 2, 3, 4}) {
                    const int delay = (k == 0) ? 500 : (k == 1 ? 1500 : (k == 2 ? 3000 : (k == 3 ? 6000 : 10000)));
                    QTimer::singleShot(delay, window, [grab, dumpIcons, n, k, window, delay] {
                        grab(QStringLiteral("shot-t%1-%2.png").arg(delay).arg(n));
                        // 只打几何：标签宽度是 0 还是正常值，一眼就知道是布局问题还是绑定问题
                        int withSize = 0;
                        int labelOk = 0;
                        QString first;
                        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                            if (!item)
                                return;
                            if (item->objectName() == QLatin1String("appTile") && item->width() > 0) {
                                ++withSize;
                                auto *label = item->findChild<QQuickItem *>(QStringLiteral("tileLabel"));
                                const int lw = label ? int(label->width()) : -1;
                                if (lw > 0)
                                    ++labelOk;
                                if (first.isEmpty() && label)
                                    first = QStringLiteral("tile=%1x%2 label=%3x%4 y=%5 text='%6'")
                                                .arg(int(item->width())).arg(int(item->height()))
                                                .arg(lw).arg(int(label->height()))
                                                .arg(int(label->y()))
                                                .arg(label->property("text").toString());
                            }
                            const QList<QQuickItem *> kids = item->childItems();
                            for (QQuickItem *kid : kids)
                                walk(kid);
                        };
                        walk(window->contentItem());
                        wlTrace(QStringLiteral("geometry +%1ms: sized tiles=%2, labels width>0=%3 | %4")
                                    .arg(delay).arg(withSize).arg(labelOk).arg(first));

                        /*
                         * 网格几何：一屏到底几行、格子多高。
                         * cellHeight 由 QML 按 `Math.floor(height / 4)` 算出来，
                         * 这里把 height 和 cellHeight 并排打出来 —— 两者对不上
                         * （height / cellHeight 明显 > 4）就说明绑定拿到的是旧的
                         * height，界面上会表现为"第 5 行露半截"。
                         */
                        if (auto *g =
                                window->findChild<QQuickItem *>(QStringLiteral("appGrid"))) {
                            wlTrace(QStringLiteral("gridgeom +%1ms: y=%2 h=%3 cellW=%4 cellH=%5 "
                                                   "rows=%6 contentY=%7 items=%8")
                                        .arg(delay)
                                        .arg(int(g->y()))
                                        .arg(int(g->height()))
                                        .arg(g->property("cellWidth").toInt())
                                        .arg(g->property("cellHeight").toInt())
                                        .arg(g->height()
                                             / qMax(1, g->property("cellHeight").toInt()))
                                        .arg(g->property("contentY").toDouble(), 0, 'f', 1)
                                        .arg(g->property("count").toInt()));
                        }

                        /*
                         * 可视树 dump + 根探测：只在最后一条时间线（k==4，10s）打，
                         * 否则 5 条时间线各刷 120 行，trace 会被淹掉。
                         * 用途：判断「树里压根没这个节点」还是「objectName 对不上」，
                         * 以及 contentItem() 走不下去时 QML 树到底挂在哪。
                         * 定位「AppTile 拿到 0×0」就是靠这两条打点。
                         */
                        if (k == 4) {
                            QString tree;
                            int nodes = 0;
                            std::function<void(QQuickItem *, int)> dump =
                                [&](QQuickItem *it, int depth) {
                                    if (!it || nodes > 120)
                                        return;
                                    const QString on = it->objectName();
                                    if (!on.isEmpty()) {
                                        ++nodes;
                                        tree += QStringLiteral("\n  %1%2[%3x%4] vis=%5")
                                                    .arg(QString(depth * 2, QLatin1Char(' ')))
                                                    .arg(on)
                                                    .arg(int(it->width()))
                                                    .arg(int(it->height()))
                                                    .arg(it->isVisible() ? 1 : 0);
                                    }
                                    const QList<QQuickItem *> kids = it->childItems();
                                    for (QQuickItem *kid : kids)
                                        dump(kid, on.isEmpty() ? depth : depth + 1);
                                };
                            dump(window->contentItem(), 0);
                            wlTrace(QStringLiteral("tree +%1ms: named nodes=%2%3")
                                        .arg(delay)
                                        .arg(nodes)
                                        .arg(tree));

                            auto *ci = window->contentItem();
                            const auto byObj = window->findChildren<QQuickItem *>();
                            QString objNames;
                            int objNamed = 0;
                            for (QQuickItem *it : byObj) {
                                if (it->objectName().isEmpty())
                                    continue;
                                ++objNamed;
                                if (objNamed <= 8)
                                    objNames += QStringLiteral(" %1[%2x%3]")
                                                    .arg(it->objectName())
                                                    .arg(int(it->width()))
                                                    .arg(int(it->height()));
                            }
                            wlTrace(QStringLiteral("root +%1ms: contentItem=%2 ciKids=%3 "
                                                   "windowKids=%4 qobjItems=%5 named=%6 |%7")
                                        .arg(delay)
                                        .arg(ci ? QStringLiteral("ok") : QStringLiteral("NULL"))
                                        .arg(ci ? ci->childItems().size() : -1)
                                        .arg(window->children().size())
                                        .arg(byObj.size())
                                        .arg(objNamed)
                                        .arg(objNames));
                            dumpIcons(QStringLiteral("-panel%1").arg(n));
                        }
                    });
                }

                /*
                 * --test-search：等扫描真正结束（total>0）再动手，否则
                 * 「扫描中 → 全量 0」会把结果全打成 0，看不出对错。
                 * 步进：0 等扫描 → 设关键词并打点；1 抓搜索结果图 + 清空；
                 *       2 抓恢复图 + 打完退出。
                 */
                if (!testTerm.isEmpty()) {
                    auto *seq = new QTimer(window);
                    seq->setInterval(300);
                    QObject::connect(seq, &QTimer::timeout, window,
                                     [&controller, seq, grab, testTerm, step = 0]() mutable {
                        if (step == 0) {
                            if (controller.scanning() || controller.totalCount() == 0)
                                return; // 扫描还没完，下一拍再看
                            wlTrace(QStringLiteral("search-test: 扫描结束 total=%1")
                                        .arg(controller.totalCount()));
                            controller.setQuery(testTerm);
                            wlTrace(QStringLiteral("search-test: 设关键词 '%1' -> match=%2")
                                        .arg(testTerm)
                                        .arg(controller.matchCount()));
                            ++step;
                            return;
                        }
                        if (step == 1) {
                            grab(QStringLiteral("shot-search-%1.png").arg(testTerm));
                            controller.setQuery(QString());
                            wlTrace(QStringLiteral("search-test: 清空关键词 -> match=%1")
                                        .arg(controller.matchCount()));
                            ++step;
                            return;
                        }
                        if (step == 2) {
                            grab(QStringLiteral("shot-search-cleared.png"));
                            wlTrace(QStringLiteral("search-test: 完成（清空后 match=%1）")
                                        .arg(controller.matchCount()));
                            seq->stop();
                            seq->deleteLater();
                        }
                    });
                    seq->start();
                }
            } else if (!visible) {
                dumpState->setProperty("sawVisible", false);
            }
        });
        poll->start();

        // 面板弹出后再打点：面板没弹时网格是空的（tiles=0），别把它当结论
        QTimer::singleShot(9000, window, [dumpIcons] { dumpIcons(QString()); });
        // 最后再来一次：图标是分批提取 + 异步解码的，早打点会看到一堆 status=0
        QTimer::singleShot(25000, window, [dumpIcons, grab] {
            dumpIcons(QStringLiteral("-final"));
            grab(QStringLiteral("shot-final.png"));
        });
    }

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
