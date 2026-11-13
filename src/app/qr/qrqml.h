// QML registration of the QR codes area (library brulion_qrview, qr/area.cmake) in the app's QML
// module "Brulion" (the src/app/ui pattern: the classes know nothing about QML):
//
//   QrShare      creatable — QrShare { host: Workspace; plainText: …; withTitle: … }: a note's text
//                and its codes, the one shown
//   QrCodeView   creatable — QrCodeView { text: share.partText }: one code drawn
#pragma once

#include "qrcodeview.h"
#include "qrshare.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::qrview {

struct QrShareForeign {
    Q_GADGET
    QML_FOREIGN(brulion::qrview::QrShare)
    QML_NAMED_ELEMENT(QrShare)
};

struct QrCodeViewForeign {
    Q_GADGET
    QML_FOREIGN(brulion::qrview::QrCodeView)
    QML_NAMED_ELEMENT(QrCodeView)
};

} // namespace brulion::qrview
