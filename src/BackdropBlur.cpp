#include "BackdropBlur.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <vector>

#include "DebugTrace.h"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

constexpr int kChannels = 4; // R G B A

/*
 * 单次分离式 box blur（只做水平或只做垂直）。
 * 用前缀和把窗口求和降到 O(1)，整条扫描线一趟走完 —— 全图也就 O(w*h)。
 * 边界用 clamp 复制，和「贴边像素向外延伸」的直觉一致。
 *
 * 四个通道都模糊（含 alpha）：阴影层是带 alpha 的形状，只模糊 RGB 会糊不出柔和边缘。
 * 统一走 Format_ARGB32_Premultiplied，预乘值直接模糊在数学上也是正确的。
 */
QImage boxBlurPass(const QImage &src, int radius, bool horizontal)
{
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0)
        return src;

    QImage dst = src.copy();

    const int n = horizontal ? w : h;     // 一条扫描线的长度
    const int lines = horizontal ? h : w; // 扫描线条数

    std::vector<int> sums[kChannels];
    for (int c = 0; c < kChannels; ++c)
        sums[c].resize(n + 1);
    std::vector<quint32> line(n);

    const int shiftR = 16;
    const int shiftG = 8;
    const int shiftB = 0;
    const int shiftA = 24;

    for (int l = 0; l < lines; ++l) {
        if (horizontal) {
            const QRgb *row = reinterpret_cast<const QRgb *>(src.constScanLine(l));
            for (int i = 0; i < n; ++i)
                line[i] = row[i];
        } else {
            for (int i = 0; i < n; ++i)
                line[i] = reinterpret_cast<const QRgb *>(src.constScanLine(i))[l];
        }

        for (int c = 0; c < kChannels; ++c)
            sums[c][0] = 0;
        for (int i = 0; i < n; ++i) {
            const quint32 p = line[i];
            sums[0][i + 1] = sums[0][i] + int((p >> shiftR) & 0xffu);
            sums[1][i + 1] = sums[1][i] + int((p >> shiftG) & 0xffu);
            sums[2][i + 1] = sums[2][i] + int((p >> shiftB) & 0xffu);
            sums[3][i + 1] = sums[3][i] + int((p >> shiftA) & 0xffu);
        }

        for (int i = 0; i < n; ++i) {
            const int lo = std::max(0, i - radius);
            const int hi = std::min(n - 1, i + radius);
            const int cnt = hi - lo + 1;
            const quint32 v = (quint32((sums[0][hi + 1] - sums[0][lo]) / cnt) << shiftR)
                              | (quint32((sums[1][hi + 1] - sums[1][lo]) / cnt) << shiftG)
                              | (quint32((sums[2][hi + 1] - sums[2][lo]) / cnt) << shiftB)
                              | (quint32((sums[3][hi + 1] - sums[3][lo]) / cnt) << shiftA);
            if (horizontal)
                reinterpret_cast<QRgb *>(dst.scanLine(l))[i] = v;
            else
                reinterpret_cast<QRgb *>(dst.scanLine(i))[l] = v;
        }
    }

    return dst;
}

QPainterPath roundedPath(const QRect &rect, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(rect), radius, radius);
    return path;
}

/*
 * 把 image 的 alpha 乘以「圆角矩形蒙版」，圆角外变透明。
 *
 * 为什么不用 QPainter 的 CompositionMode_DestinationIn + fillPath（更短）：
 * 实测在 Qt 6.8 + ARGB32_Premultiplied 上那条路整个不生效 —— 面板外的 alpha
 * 依然是 255，于是窗口整个边距区都被不透明像素填满，看着就是「圆角外面套了个
 * 直角矩形」。蒙版本身仍用 QPainter 画（普通 SourceOver，透明底上刷白色，
 * 这个组合是稳的），乘 alpha 自己算，一次 O(w*h) 遍历。
 */
QImage applyRoundedMask(QImage image, const QRect &panel, qreal radius)
{
    if (image.isNull() || radius <= 0.0)
        return image;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage mask(image.size(), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        p.drawPath(roundedPath(panel, radius));
    }

    const int w = image.width();
    const int h = image.height();
    for (int y = 0; y < h; ++y) {
        const QRgb *ml = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        QRgb *il = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const quint32 ma = (quint32(ml[x]) >> 24) & 0xffu;
            if (ma == 255u)
                continue;
            if (ma == 0u) {
                il[x] = 0u;
                continue;
            }
            // 预乘像素：alpha 乘多少，RGB 也要乘多少
            const quint32 p = il[x];
            const quint32 a = (((p >> 24) & 0xffu) * ma) / 255u;
            const quint32 r = (((p >> 16) & 0xffu) * ma) / 255u;
            const quint32 g = (((p >> 8) & 0xffu) * ma) / 255u;
            const quint32 b = ((p & 0xffu) * ma) / 255u;
            il[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    return image;
}

/*
 * 把底图最外圈 featherPx 的 alpha 渐变到 0。
 *
 * 阴影的可见扩散约 ±3×shadowBlurPx，只要边距配得不够宽，衰减没结束的地方就会被
 * 窗口边界一刀切平，那条切口就是「圆角面板外面跟了块直角背景」。边距按公式配好是
 * 主要手段，这个渐隐是第二道保险 —— 边缘像素本来就该是全透明的。
 */
QImage featherEdges(const QImage &src, int featherPx)
{
    if (src.isNull() || featherPx <= 0)
        return src;

    QImage out = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = out.width();
    const int h = out.height();
    if (w <= 2 || h <= 2)
        return out;
    const int f = qBound(1, featherPx, qMin(w, h) / 4);

    for (int y = 0; y < h; ++y) {
        const int dy = qMin(y, h - 1 - y);
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int d = qMin(qMin(x, w - 1 - x), dy);
            if (d >= f)
                continue;
            // 预乘像素：alpha 缩多少倍，RGB 也要跟着缩，否则边缘会发亮
            const quint32 p = line[x];
            const quint32 a = (((p >> 24) & 0xffu) * quint32(d)) / quint32(f);
            const quint32 r = (((p >> 16) & 0xffu) * quint32(d)) / quint32(f);
            const quint32 g = (((p >> 8) & 0xffu) * quint32(d)) / quint32(f);
            const quint32 b = ((p & 0xffu) * quint32(d)) / quint32(f);
            line[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    return out;
}

} // namespace

namespace BackdropBlur {

QImage grabScreen(const QRect &rectPx)
{
    if (rectPx.width() <= 0 || rectPx.height() <= 0)
        return QImage();

    const QRect vrect(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                      GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if (vrect.width() <= 0 || vrect.height() <= 0)
        return QImage();

    const QRect want = rectPx.intersected(vrect);
    // 只要有一边被裁掉了，抓回来的图和预期尺寸就不一致，面板在底图里的位置会整体偏移。
    // 与其画歪，不如当作抓屏失败，让调用方走纯色兜底。
    if (want != rectPx) {
        wlTrace(QStringLiteral("backdrop: 抓屏区域越界 rect=(%1,%2 %3x%4) virtual=(%5,%6 %7x%8)")
                    .arg(rectPx.x())
                    .arg(rectPx.y())
                    .arg(rectPx.width())
                    .arg(rectPx.height())
                    .arg(vrect.x())
                    .arg(vrect.y())
                    .arg(vrect.width())
                    .arg(vrect.height()));
        return QImage();
    }

    HDC screenDC = GetDC(nullptr);
    if (!screenDC)
        return QImage();

    const int w = want.width();
    const int h = want.height();

    HDC memDC = CreateCompatibleDC(screenDC);
    HBITMAP bmp = CreateCompatibleBitmap(screenDC, w, h);
    if (!memDC || !bmp) {
        if (bmp)
            DeleteObject(bmp);
        if (memDC)
            DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        wlTrace("backdrop: CreateCompatibleDC/Bitmap 失败");
        return QImage();
    }

    HGDIOBJ oldBmp = SelectObject(memDC, bmp);

    QImage img(w, h, QImage::Format_RGB32);
    bool ok = false;
    // 只抓需要的那一块：双屏下整块虚拟桌面是 4800×2200，BitBlt + GetDIBits 要搬
    // 1000 万像素，实测占了整个底图生成耗时的绝大部分。
    if (BitBlt(memDC, 0, 0, w, h, screenDC, want.x(), want.y(), SRCCOPY)) {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // 负数 = 自上而下，和 QImage 的行序一致
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        ok = GetDIBits(memDC, bmp, 0, static_cast<UINT>(h), img.bits(), &bmi, DIB_RGB_COLORS) != 0;
    }

    SelectObject(memDC, oldBmp);
    DeleteObject(bmp);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    if (!ok) {
        wlTrace("backdrop: BitBlt/GetDIBits 失败");
        return QImage();
    }

    return img;
}

QImage blur(const QImage &src, int radiusPx, int downsample)
{
    if (src.isNull() || radiusPx <= 0)
        return src;

    const int ds = qBound(1, downsample, 8);
    QImage work = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (ds > 1) {
        work = work.scaled(qMax(1, src.width() / ds), qMax(1, src.height() / ds),
                           Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    // 半径按降采样比例一起缩，等效半径才等于 radiusPx
    const int r = qMax(1, radiusPx / ds);
    for (int i = 0; i < 3; ++i) {
        work = boxBlurPass(work, r, true);
        work = boxBlurPass(work, r, false);
    }

    if (ds > 1) {
        work = work.scaled(src.width(), src.height(), Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
    }
    return work;
}

QImage saturate(const QImage &src, qreal factor)
{
    if (src.isNull() || qFuzzyCompare(factor, 1.0))
        return src.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QImage out = src.convertToFormat(QImage::Format_RGB32);
    const int w = out.width();
    const int h = out.height();

    // 全整数实现：先算灰度，再把每个通道朝「远离灰度」的方向拉。
    // factor 用 1/256 定点表示，省掉浮点乘法（这张图有 ~80 万像素）。
    const qreal f = qBound(0.0, factor, 4.0);
    const int fQ8 = qRound(f * 256.0);
    const int q8 = 256; // 1.0 的定点表示

    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb p = line[x];
            const int r = qRed(p);
            const int g = qGreen(p);
            const int b = qBlue(p);
            const int gray = (r * 299 + g * 587 + b * 114) / 1000;
            const int nr = qBound(0, gray + (((r - gray) * fQ8) >> 8), 255);
            const int ng = qBound(0, gray + (((g - gray) * fQ8) >> 8), 255);
            const int nb = qBound(0, gray + (((b - gray) * fQ8) >> 8), 255);
            line[x] = qRgb(nr, ng, nb);
            Q_UNUSED(q8)
        }
    }
    return out.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage buildPanelBackdrop(const QImage &screenShot, const PanelOptions &opt)
{
    if (screenShot.isNull() || opt.panelRectInImage.isEmpty())
        return QImage();

    const qreal dpr = opt.dpr > 0.0 ? opt.dpr : 1.0;
    const QRect panel = opt.panelRectInImage;
    const qreal radius = qMax(0.0, opt.radiusPx * dpr);

    // 诊断：几何不透明度的采样（-1 = 越界）
    const auto alphaAt = [](const QImage &img, int x, int y) -> int {
        if (img.isNull() || x < 0 || y < 0 || x >= img.width() || y >= img.height())
            return -1;
        return qAlpha(img.pixel(x, y));
    };
    const int probeX = qMax(1, panel.x() / 2);
    const int probeY = qMax(1, panel.y() / 2);
    const int centerX = panel.center().x();
    const int centerY = panel.center().y();

    // ---- 1) 玻璃：模糊 + 提饱和 ----
    QImage glass = blur(screenShot, qRound(opt.blurRadiusPx * dpr), opt.downsample);
    if (!qFuzzyCompare(opt.saturation, 1.0))
        glass = saturate(glass, opt.saturation);
    glass = glass.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    wlTrace(QStringLiteral("  buildPanel: panel=(%1,%2 %3x%4) r=%5 fmt=%6")
                .arg(panel.x())
                .arg(panel.y())
                .arg(panel.width())
                .arg(panel.height())
                .arg(radius)
                .arg(static_cast<int>(glass.format())));
    wlTrace(QStringLiteral("  buildPanel: after blur   alpha outside=%1 center=%2")
                .arg(alphaAt(glass, probeX, probeY))
                .arg(alphaAt(glass, centerX, centerY)));

    // ---- 2) 只在面板区域内保留像素，圆角外透明 ----
    glass = applyRoundedMask(glass, panel, radius);

    wlTrace(QStringLiteral("  buildPanel: after round  alpha outside=%1 center=%2")
                .arg(alphaAt(glass, probeX, probeY))
                .arg(alphaAt(glass, centerX, centerY)));

    /*
     * ---- 3) 外阴影：偏移后的黑色圆角矩形 → 模糊 → 垫到玻璃下面 ----
     *
     * 这里**不需要**把面板区域从阴影层里挖掉：合成用的是 DestinationOver
     * （源在目标下面），面板区域的目标 alpha 是 255，混合公式
     * result = src*(1-dst.a) + dst*dst.a 会把阴影完全吃掉，颜色不会发灰。
     */
    if (opt.shadowAlpha > 0 && opt.shadowBlurPx > 0) {
        QImage shadow(glass.size(), QImage::Format_ARGB32_Premultiplied);
        shadow.fill(Qt::transparent);
        {
            QPainter p(&shadow);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::black);
            p.drawPath(roundedPath(panel.translated(0, qRound(opt.shadowOffsetYPx * dpr)), radius));
        }

        shadow = blur(shadow, qRound(opt.shadowBlurPx * dpr), 2);

        {
            QPainter p(&glass);
            p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
            p.setOpacity(opt.shadowAlpha / 255.0);
            p.drawImage(0, 0, shadow);
        }
    }

    wlTrace(QStringLiteral("  buildPanel: after shadow alpha outside=%1 center=%2")
                .arg(alphaAt(glass, probeX, probeY))
                .arg(alphaAt(glass, centerX, centerY)));

    // ---- 4) 最外圈 alpha 渐隐，兜掉任何可能残留的裁断硬边 ----
    return featherEdges(glass, qRound(opt.featherPx * dpr));
}

} // namespace BackdropBlur
