// QML registration of the PDF viewer's C++ (library brulion_pdfview, pdf/CMakeLists.txt) in the app's
// QML module "Brulion": PdfViewModel is a creatable type (qml/content/ContentPdfView.qml). The image
// provider "pdfview" is added by main.cpp where a renderer exists.
#pragma once

#include "pdfviewmodel.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace brulion::pdfview {

struct PdfViewModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::pdfview::PdfViewModel)
    QML_NAMED_ELEMENT(PdfViewModel)
};

} // namespace brulion::pdfview
