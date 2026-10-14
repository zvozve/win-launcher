#pragma once

#include <QImage>
#include <QRect>
#include <QtGlobal>

/*
 * 面板玻璃底图（自绘，不依赖系统材质）。
 *
 * 为什么不用 Windows 的 Mica / Acrylic：
 *   1. SetWindowCompositionAttribute + ACCENT_ENABLE_ACRYLICBLURBEHIND 是 Win10 1803 的
 *      实现，Win11 上带明显噪点、模糊半径偏小、还自带一层灰调 —— 和 macOS 的
 *      vibrancy（大半径、干净、高饱和的模糊）气质差很远。
 *   2. 更致命的是它由 DWM 按「矩形」合成，SetWindowRgn 裁不到它：窗口区域被裁成圆角，
 *      材质却仍是直角，四个角上会露出一块方方正正的玻璃 —— 这就是「圆角不生效」。
 *   3. 无边框窗口拿不到 DWM 的原生外阴影，面板贴不出「浮起来」的层次。
 *
 * 所以改成：弹出前抓一次该区域的桌面像素 → 模糊 + 提饱和 → 裁圆角 → 烘上外阴影，
 * 整张图交给 QML 铺满窗口。模糊强度、圆角、阴影、配色就全归我们管了。
 *
 * 阴影是**烘进位图**的：QML 侧的 DropShadow / MultiEffect 都要离屏渲染，
 * 对一整块含 GridView 的面板做 FBO 每帧重绘不划算，而烘进位图又不花额外代价。
 */
namespace BackdropBlur {

/*
 * 面板底图的参数（长度单位都是逻辑像素，函数内部按 dpr 换算成物理像素）
 *
 * ★ shadowBlurPx 和窗口的阴影边距（LauncherWindow::kShadowMargin）必须配套：
 *   3 次 box blur 半径 r 的等效 σ ≈ r，阴影的可见扩散约 ±3σ = ±3×shadowBlurPx。
 *   边距小于这个值，阴影就会在窗口边界被**硬裁**出一条直角边 —— 表现为
 *   「圆角面板外面还跟着一块直角背景」。所以 kShadowMargin 必须 ≥
 *   3×shadowBlurPx + shadowOffsetYPx。
 */
struct PanelOptions {
    QRect panelRectInImage;   // 面板在底图（= 整个窗口）中的位置
    qreal radiusPx = 26.0;    // 面板圆角
    qreal dpr = 1.0;
    int blurRadiusPx = 36;    // 玻璃模糊半径
    int downsample = 4;       // 模糊前降采样倍数（4 倍在 1080p 上看不出差别）
    qreal saturation = 1.6;   // 饱和度；macOS vibrancy 的经验值 ≈ 1.8，1.0 = 不改
    int shadowBlurPx = 20;    // 外阴影柔和度（见上面的配套关系）
    int shadowOffsetYPx = 8;  // 外阴影下移量
    int shadowAlpha = 100;    // 外阴影不透明度（0-255）
    int featherPx = 6;        // 底图最外圈 alpha 渐隐宽度；把可能残留的裁断边擦掉
};

/*
 * 抓虚拟桌面坐标 rectPx 处的屏幕像素（**物理像素**，就是 Win32 的坐标）。
 * 不要传 Qt 的逻辑坐标：多屏不同 DPR 时逻辑坐标和物理坐标不是简单的比例关系，
 * 调用方应该用 GetWindowRect 拿窗口的物理矩形再传进来。
 *
 * 用 BitBlt + GetDIBits 而不是 QScreen::grabWindow：多屏 / 负坐标下前者可控性更好。
 * 失败（远程桌面、受保护内容、区域越界等）返回空图，调用方自行回退。
 */
QImage grabScreen(const QRect &rectPx);

/*
 * 降采样 → 3 次 box blur（分离式，横 + 竖）→ 升采样回原尺寸。
 * 3 次 box blur 依中心极限定理已经很接近高斯，而单次是 O(n) 的前缀和实现，
 * 比真高斯核快一个量级。radiusPx 是原图尺度的模糊半径（物理像素）。
 */
QImage blur(const QImage &src, int radiusPx, int downsample);

// 饱和度调整（对应 macOS 的 backdrop-filter: blur() saturate()）
QImage saturate(const QImage &src, qreal factor);

/*
 * 由抓到的屏幕像素生成最终底图：尺寸与 screenShot 一致，可直接铺满窗口。
 * 面板圆角外为透明，透明处叠着烘好的外阴影。
 */
QImage buildPanelBackdrop(const QImage &screenShot, const PanelOptions &opt);

} // namespace BackdropBlur
