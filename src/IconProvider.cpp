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

QString IconProvider::cacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("cache/icons"));
}

QString IconProvider::iconForPath(const QString &path)
{
    if (path.isEmpty())
        return QString();

    const QFileInfo fi(path);
    const QString dir = cacheDir();
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
