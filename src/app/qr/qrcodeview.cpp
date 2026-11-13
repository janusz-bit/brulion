#include "qrcodeview.h"

#include <QPainter>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>

namespace brulion::qrview {

QrCodeView::QrCodeView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    // Sharp edges: no antialiasing, and the texture scaled with its nearest pixels where the item
    // sits between two device pixels.
    setAntialiasing(false);
    setSmooth(false);
    setFillColor(Qt::transparent);
}

void QrCodeView::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    Q_EMIT textChanged();
    m_code = qr::encode(text);
    Q_EMIT codeChanged();
    updateModulePixels();
    update();
}

qreal QrCodeView::devicePixelRatio() const
{
    const QQuickWindow *shown = window();
    return shown ? shown->effectiveDevicePixelRatio() : 1.0;
}

int QrCodeView::pixelsPerModule(int modules, qreal side, qreal devicePixelRatio)
{
    if (modules <= 0)
        return 0;
    const int total = modules + 2 * qr::QuietZone;
    // (A hair over the size counts: 2.5 × 1.2 is 2.9999… in floating point.)
    const int pixels = int(std::floor(std::max<qreal>(0, side) * devicePixelRatio + 1e-6));
    return std::max(1, pixels / total);
}

void QrCodeView::updateModulePixels()
{
    const int pixels =
            m_code.isNull() ? 0 : pixelsPerModule(m_code.size, std::min(width(), height()), devicePixelRatio());
    if (pixels == m_modulePixels)
        return;
    m_modulePixels = pixels;
    Q_EMIT modulePixelsChanged();
}

void QrCodeView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    updateModulePixels();
}

void QrCodeView::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange) {
        updateModulePixels();
        update();
    }
}

void QrCodeView::paint(QPainter *painter)
{
    if (m_code.isNull() || m_modulePixels <= 0)
        return;
    // Everything on whole device pixels: the painter's coordinates are logical ones, so each
    // position is a whole number of device pixels divided by the ratio.
    const qreal ratio = devicePixelRatio();
    const int side = (m_code.size + 2 * qr::QuietZone) * m_modulePixels;
    const int left = std::max(0, (int(std::floor(width() * ratio + 1e-6)) - side) / 2);
    const int top = std::max(0, (int(std::floor(height() * ratio + 1e-6)) - side) / 2);
    const auto at = [ratio](int devicePixels) { return devicePixels / ratio; };
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->fillRect(QRectF(at(left), at(top), at(side), at(side)), Qt::white);
    // The dark modules a row at a time, one rectangle for each run of them.
    const int origin = qr::QuietZone * m_modulePixels;
    for (int y = 0; y < m_code.size; ++y) {
        const int rowTop = top + origin + y * m_modulePixels;
        for (int x = 0; x < m_code.size;) {
            if (!m_code.dark(x, y)) {
                ++x;
                continue;
            }
            int end = x + 1;
            while (end < m_code.size && m_code.dark(end, y))
                ++end;
            const int runLeft = left + origin + x * m_modulePixels;
            painter->fillRect(QRectF(at(runLeft), at(rowTop), at((end - x) * m_modulePixels), at(m_modulePixels)),
                              Qt::black);
            x = end;
        }
    }
}

QImage QrCodeView::picture(const qr::Code &code, int scale)
{
    if (code.isNull() || scale < 1)
        return {};
    const int side = (code.size + 2 * qr::QuietZone) * scale;
    QImage image(side, side, QImage::Format_Grayscale8);
    for (int y = 0; y < side; ++y) {
        uchar *line = image.scanLine(y);
        const int row = y / scale - qr::QuietZone;
        for (int x = 0; x < side; ++x)
            line[x] = code.dark(x / scale - qr::QuietZone, row) ? 0 : 255; // outside the code: light
    }
    return image;
}

} // namespace brulion::qrview
