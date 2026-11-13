// QrCodeView — one QR code drawn (docs/UI.md "QR code"): `text`'s UTF-8 in one code
// (qr::encode()), black on white whatever the theme — scanners look for dark modules on a light
// ground — with its quiet zone, as large as the item allows and in whole device pixels per module,
// so that no module is blurred or thinner than its neighbours (on fractional scales too). Nothing is
// drawn for a text one code cannot hold (`modules` 0); QrShare cuts a note's text to fit.
//
//   QrCodeView { width: 320; height: 320; text: share.partText }
#pragma once

#include "qr/qrcode.h"

#include <QImage>
#include <QQuickPaintedItem>
#include <QString>

namespace brulion::qrview {

class QrCodeView : public QQuickPaintedItem {
    Q_OBJECT
    /// The text in the code.
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged FINAL)
    /// The code's modules per side without the quiet zone, 21 … 177 (0: nothing drawn).
    Q_PROPERTY(int modules READ modules NOTIFY codeChanged FINAL)
    /// Its version, 1 … 40 (0: none).
    Q_PROPERTY(int version READ version NOTIFY codeChanged FINAL)
    /// Device pixels per module as drawn (0: nothing drawn). Under 3 a phone's camera may not make
    /// the modules out.
    Q_PROPERTY(int modulePixels READ modulePixels NOTIFY modulePixelsChanged FINAL)

public:
    explicit QrCodeView(QQuickItem *parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString &text);
    int modules() const { return m_code.size; }
    int version() const { return m_code.version; }
    int modulePixels() const { return m_modulePixels; }

    void paint(QPainter *painter) override;

    /// `code` as a picture: `scale` pixels per module, the quiet zone around it, black on white; a
    /// null image for a null code.
    static QImage picture(const qr::Code &code, int scale);
    /// The device pixels per module of a code of `modules` (quiet zone left out) in a square of
    /// `side` logical pixels at `devicePixelRatio`: the most whole pixels that fit, at least 1.
    static int pixelsPerModule(int modules, qreal side, qreal devicePixelRatio);

Q_SIGNALS:
    void textChanged();
    void codeChanged();
    void modulePixelsChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    qreal devicePixelRatio() const;
    void updateModulePixels();

    QString m_text;
    qr::Code m_code;
    int m_modulePixels = 0;
};

} // namespace brulion::qrview
