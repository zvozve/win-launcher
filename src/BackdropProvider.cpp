#include "BackdropProvider.h"

BackdropProvider::BackdropProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage BackdropProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    Q_UNUSED(id)
    Q_UNUSED(requested)

    QMutexLocker locker(&m_mutex);
    if (size)
        *size = m_image.size();
    return m_image;
}

void BackdropProvider::setImage(const QImage &image)
{
    QMutexLocker locker(&m_mutex);
    m_image = image;
}

bool BackdropProvider::hasImage() const
{
    QMutexLocker locker(&m_mutex);
    return !m_image.isNull();
}
