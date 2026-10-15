// Shared helpers of the planning tests: an in-memory VaultIndex from (path, content) pairs.
#pragma once

#include "index/vaultindex.h"

#include <QList>
#include <QString>

#include <memory>
#include <utility>

namespace brulion::planning::test {

using Files = QList<std::pair<QString, QString>>;

inline std::unique_ptr<VaultIndex> makeIndex(const Files &files, const VaultConfig &config = VaultConfig())
{
    auto index = std::make_unique<VaultIndex>(config);
    {
        VaultIndex::Batch batch(*index);
        for (const auto &[path, content] : files)
            index->update(path, content);
    }
    return index;
}

} // namespace brulion::planning::test
