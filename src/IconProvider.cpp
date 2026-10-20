#include "IconProvider.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QThread>

#include "DebugTrace.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>       // IMAGELISTDRAWPARAMS / IMAGEINFO
#include <commoncontrols.h> // IImageList / SHGetImageList
#include <shellapi.h>
#include <shlobj.h>

namespace {

constexpr int kJumboSize = 256;

// 扫描跑在工作线程上。QFont / QPixmap / QFileIconProvider 依赖 GUI 线程的全局
// 状态，在工作线程里使用会破坏进程状态（实测表现为后续 QML 引擎构造段错误），
// 所以这些路径只在 GUI 线程启用。
bool onGuiThread()
{
    auto *app = QCoreApplication::instance();
    return !app || QThread::currentThread() == app->thread();
}

// 用 DrawIconEx 画到 32bpp DIB，绕过 HICON 自带 alpha/mask 的坑
QImage hiconToImage(HICON hicon, int size)
{
    QImage result;
    if (!hicon)
        return result;

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        HGDIOBJ old = SelectObject(memDc, dib);
        DrawIconEx(memDc, 0, 0, hicon, size, size, 0, nullptr, DI_NORMAL);
        SelectObject(memDc, old);

        const QImage view(static_cast<const uchar *>(bits), size, size,
                          QImage::Format_ARGB32_Premultiplied);
        result = view.copy();
    }

    if (dib)
        DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return result;
}

// 优先取 256px Jumbo 图标，失败退回系统给的那张
HICON jumboIconFor(const QString &path)
{
    SHFILEINFOW sfi{};
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();

    if (!SHGetFileInfoW(native.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_ICON))
        return nullptr;

    HICON icon = nullptr;
    IImageList *imageList = nullptr;
    if (SUCCEEDED(SHGetImageList(SHIL_JUMBO, IID_IImageList, reinterpret_cast<void **>(&imageList)))
        && imageList) {
        const HRESULT hr = imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &icon);
        imageList->Release();
        if (FAILED(hr))
            icon = nullptr;
    }

    if (!icon && sfi.hIcon)
        icon = CopyIcon(sfi.hIcon);
    if (sfi.hIcon)
        DestroyIcon(sfi.hIcon);

    return icon;
}

// 兜底 1：Qt 自带图标提供器（仅 GUI 线程）
QImage qtFallbackIcon(const QString &path, int size)
{
    QFileIconProvider provider;
    const QIcon icon = provider.icon(QFileInfo(path));
    if (icon.isNull())
        return QImage();
    return icon.pixmap(QSize(size, size)).toImage();
}

// 兜底 2：灰底首字母占位块（仅 GUI 线程；用到 QFont）
QImage placeholderIcon(const QString &name, int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(140, 145, 155, 190));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(1.5, 1.5, size - 3.0, size - 3.0), size * 0.22, size * 0.22);

    const QChar ch = name.isEmpty() ? QChar('?') : name.at(0).toUpper();
    QFont f = p.font();
    f.setPixelSize(static_cast<int>(size * 0.45));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 235));
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QString(ch));
    p.end();
    return img;
}

// shell 的 jumbo 图像列表里，有些条目只是把小图标塞在左上角。
// 检测非透明包围盒，明显没铺满时裁剪后放大居中，避免 UI 里出现「图标只有一小块」。
QImage normalizeIcon(const QImage &src)
{
    if (src.isNull())
        return src;

    const int canvas = src.width();
    int minX = canvas, minY = canvas, maxX = -1, maxY = -1;
    for (int y = 0; y < src.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < canvas; ++x) {
            if (qAlpha(line[x]) > 8) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }

    if (maxX < 0)
        return src; // 整张全透明
    const int w = maxX - minX + 1;
    const int h = maxY - minY + 1;
    if (w >= canvas * 3 / 4 || h >= canvas * 3 / 4)
        return src; // 本来就铺满

    const qreal scale = (canvas * 0.92) / qMax(w, h);
    const QImage scaled = src.copy(minX, minY, w, h)
                              .scaled(qMax(1, int(w * scale)), qMax(1, int(h * scale)),
                                      Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QImage out(canvas, canvas, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawImage((canvas - scaled.width()) / 2, (canvas - scaled.height()) / 2, scaled);
    return out;
}

} // namespace

/*
 * 图标缓存的根目录。
 *
 * 踩过的坑：本工程**没有设置 organizationName**，于是
 *   QStandardPaths::AppConfigLocation  -> %LOCALAPPDATA%\WinLauncher   （能写）
 *   QStandardPaths::AppLocalDataLocation -> %LOCALAPPDATA%\LocalLow\WinLauncher（**写不进去**）
 * 两个解析结果不一样，而 LocalLow 在这台机器上连目录都建不出来 ——
 * 表现就是「界面正常、网格正常、图标全是首字母占位块」，
 * 因为 img.save() 每次都失败（写过一次 trace 才看出来）。
 *
 * 所以这里不猜：拿到候选目录后**实际写一个探针文件**，写不进去就换下一个。
 * 正常机器上第一个候选就是对的，不会留下垃圾。
 */
QString IconProvider::cacheDir()
{
    QStringList candidates;
    const QString localData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString configData =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    candidates << localData << configData;

    // 兜底：直接按 LOCALAPPDATA 拼（AppConfigLocation 理论上就是这个）
    const QString env = qEnvironmentVariable("LOCALAPPDATA");
    if (!env.isEmpty())
        candidates << QDir(env).filePath(QCoreApplication::applicationName());

    /*
     * 最后的兜底：临时目录。
     * 本机（受限令牌 / 沙箱）下 AppData\Local\WinLauncher 只剩读权限，
     * 连 LocalLow 的目录都建不出来 —— 这时把缓存挪到 %TEMP%\WinLauncher 下，
     * 图标照样能生成。代价是缓存可能被清理，那就下次重新提取一遍。
     */
    const QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (!tmp.isEmpty())
        candidates << QDir(tmp).filePath(QCoreApplication::applicationName());

    candidates.removeAll(QString());
    candidates.removeDuplicates();

    for (const QString &base : std::as_const(candidates)) {
        const QString dir = QDir(base).filePath(QStringLiteral("cache/icons"));
        if (!QDir().mkpath(dir)) {
            wlTrace(QStringLiteral("cacheDir: mkpath 失败 -> %1").arg(dir));
            continue;
        }
        // 探针：mkpath 成功不代表能写（只读重定向的目录就是这样）
        const QString probe = QDir(dir).filePath(QStringLiteral(".write-probe"));
        QFile f(probe);
        if (!f.open(QIODevice::WriteOnly)) {
            wlTrace(QStringLiteral("cacheDir: 不可写 -> %1").arg(dir));
            continue;
        }
        f.close();
        f.remove();
        wlTrace(QStringLiteral("cacheDir: %1").arg(dir));
        return dir;
    }

    wlTrace(QStringLiteral("cacheDir: 所有候选都写不进去，图标缓存停用"));
    return QString();
}

QString IconProvider::iconForPath(const QString &path)
{
    if (path.isEmpty())
        return QString();

    const QFileInfo fi(path);
    const QString dir = cacheDir();
    if (dir.isEmpty())
        return QString(); // 没有可写目录：交给 QML 画占位块
    QDir().mkpath(dir);

    // key 带上 mtime/size，源文件更新后缓存自动失效
    const QString seed = fi.absoluteFilePath() + QLatin1Char('|')
                         + QString::number(fi.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|')
                         + QString::number(fi.size());
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
    const QString outPath = QDir(dir).filePath(hash + QStringLiteral(".png"));

    if (QFile::exists(outPath))
        return outPath;

    QImage img;
    if (fi.exists()) {
        HICON hicon = jumboIconFor(fi.absoluteFilePath());
        if (hicon) {
            img = hiconToImage(hicon, kJumboSize);
            DestroyIcon(hicon);
        }
    }

    if (img.isNull() && fi.exists() && onGuiThread())
        img = qtFallbackIcon(fi.absoluteFilePath(), kJumboSize);

    if (img.isNull()) {
        // 工作线程到这里就交白卷，交给 QML 侧画占位块，避免碰 GUI 侧资源
        if (!onGuiThread())
            return QString();
        img = placeholderIcon(fi.fileName(), kJumboSize);
    }

    img = normalizeIcon(img);
    if (img.isNull() || !img.save(outPath, "PNG")) {
        wlTrace(QStringLiteral("icon: 生成失败 path=%1 out=%2 null=%3")
                    .arg(path, outPath)
                    .arg(img.isNull() ? 1 : 0));
        return QString();
    }

    return outPath;
}

QString IconProvider::associatedIcon(const QString &ext)
{
    QString e = ext.trimmed().toLower();
    if (e.startsWith(QLatin1Char('.')))
        e.remove(0, 1);
    if (e.isEmpty())
        return QString();

    const QString dir = cacheDir();
    if (dir.isEmpty())
        return QString();
    QDir().mkpath(dir);

    // 同一个扩展名全盘命中会复用同一张图（8 个 pdf 只提取一次）
    const QString outPath = QDir(dir).filePath(QStringLiteral("assoc_") + e + QStringLiteral(".png"));
    if (QFile::exists(outPath))
        return outPath;

    /*
     * SHGFI_USEFILEATTRIBUTES：把路径当成「不存在但类型已知的文件」，shell 只查
     * 注册表里的关联图标，不打开文件、不加载该类型的第三方扩展 handler。
     * 不带这个标志时 SHGetFileInfo 会真的走扩展（pdf 尤其慢，实测能把 GUI 卡死）。
     */
    const QString fake = QStringLiteral("x.") + e;
    SHFILEINFOW sfi{};
    const std::wstring native = QDir::toNativeSeparators(fake).toStdWString();
    const DWORD flags = SHGFI_SYSICONINDEX | SHGFI_ICON | SHGFI_USEFILEATTRIBUTES;
    if (!SHGetFileInfoW(native.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags))
        return QString();

    HICON icon = nullptr;
    IImageList *imageList = nullptr;
    if (SUCCEEDED(SHGetImageList(SHIL_JUMBO, IID_IImageList, reinterpret_cast<void **>(&imageList)))
        && imageList) {
        const HRESULT hr = imageList->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &icon);
        imageList->Release();
        if (FAILED(hr))
            icon = nullptr;
    }
    if (!icon && sfi.hIcon)
        icon = CopyIcon(sfi.hIcon);
    if (sfi.hIcon)
        DestroyIcon(sfi.hIcon);
    if (!icon)
        return QString();

    QImage img = hiconToImage(icon, kJumboSize);
    DestroyIcon(icon);
    if (img.isNull())
        return QString();

    img = normalizeIcon(img);
    if (img.isNull() || !img.save(outPath, "PNG"))
        return QString();

    return outPath;
}

void IconProvider::clearCache()
{
    QDir dir(cacheDir());
    if (!dir.exists())
        return;
    const QStringList files = dir.entryList({QStringLiteral("*.png")}, QDir::Files);
    for (const QString &f : files)
        dir.remove(f);
}
