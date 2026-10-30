#include "wasmfiles.h"

#include <private/qwasmlocalfileaccess_p.h>

#include <memory>
#include <string>

using namespace Qt::StringLiterals;

namespace brulion::platform::wasm {

void openFiles(const QStringList &nameFilters, bool multiple, qint64 maxBytes,
               std::function<void(const QList<PickedFile> &files)> done)
{
    // Shared by Qt's three callbacks, which it keeps (copies) for as long as the pick runs.
    auto collector = std::make_shared<PickCollector>(maxBytes, std::move(done));
    // Qt's filter list: entries separated by ";;" (QWasmLocalFileAccess makeFilterList).
    QStringList filters;
    for (const QString &f : nameFilters) {
        if (!f.trimmed().isEmpty())
            filters.append(f);
    }
    QWasmLocalFileAccess::openFiles(
            filters.join(u";;"_s).toStdString(),
            multiple ? QWasmLocalFileAccess::FileSelectMode::MultipleFiles
                     : QWasmLocalFileAccess::FileSelectMode::SingleFile,
            [collector](int count) { collector->closed(count); },
            [collector](uint64_t size, const std::string &name) -> char * {
                return collector->accept(size, QString::fromStdString(name));
            },
            [collector]() { collector->ready(); });
}

void saveFile(const QByteArray &data, const QString &suggestedName)
{
    QWasmLocalFileAccess::saveFile(data, suggestedName.toStdString());
}

} // namespace brulion::platform::wasm
