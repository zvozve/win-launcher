#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

/*
 * 把 C++ 侧刚抓屏模糊好的面板背景交给 QML：
 *
 *   Image { source: "image://wlbg/panel?" + Ctrl.backdropToken }
 *
 * URL 尾巴上的 token 是为了绕过 QML 的图片缓存（同一个 URL 只会请求一次），
 * 每抓一次新背景 token 就 +1，QML 才会重新取图。
 */
class BackdropProvider : public QQuickImageProvider
{
public:
    BackdropProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

    void setImage(const QImage &image);
    bool hasImage() const;

private:
    mutable QMutex m_mutex;
    QImage m_image;
};
