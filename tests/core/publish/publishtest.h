// Shared helpers of the publish module's tests: a notebook built in code (an index over Polish
// pages, their bytes), running a Publisher operation to its end, targets of each kind.
#pragma once

#include "index/vaultindex.h"
#include "publish/publish.h"
#include "testtiming.h"

#include <QHash>
#include <QSignalSpy>
#include <QTest>

#include <optional>

namespace publishtest {

using namespace Qt::StringLiterals;

/// A notebook in memory: files by path, and the index over them.
struct Notebook {
    QHash<QString, QByteArray> files;
    std::unique_ptr<brulion::VaultIndex> index = std::make_unique<brulion::VaultIndex>();

    void write(const QString &path, const QByteArray &data)
    {
        files.insert(path, data);
        index->update(path, brulion::VaultIndex::readsText(path) ? QString::fromUtf8(data) : QString());
    }
    void write(const QString &path, const QString &text) { write(path, text.toUtf8()); }
    void remove(const QString &path)
    {
        files.remove(path);
        index->remove(path);
    }
    brulion::publish::ReadFile reader() const
    {
        return [this](const brulion::VaultPath &path) -> std::optional<QByteArray> {
            const auto it = files.constFind(path);
            return it == files.constEnd() ? std::nullopt : std::optional<QByteArray>(*it);
        };
    }
    /// The site of `target` as the command line builds it.
    brulion::publish::SiteBuild site(const brulion::publish::PublishTarget &target,
                                     const QDateTime &now = QDateTime(QDate(2026, 11, 20), QTime(18, 30))) const
    {
        return brulion::publish::buildSite(
                brulion::publish::prepareSite(*index, target, reader(), u"Zeszyt"_s, now).input);
    }
};

/// A small blog: two published pages linking to each other and to a picture, a private page,
/// an extension page, a conversation with AI, a page with an encrypted fragment.
inline void writeBlog(Notebook &nb)
{
    nb.write(u"Blog/Pierwszy wpis.md"_s, u"---\ntags: blog\n---\n# Pierwszy wpis\n\nZażółć gęślą jaźń — zobacz "
                                         u"[[Drugi wpis]] i [[Szkic]].\n\n![[kot.png]]\n"_s);
    nb.write(u"Blog/Drugi wpis.md"_s, u"Drugi tekst o łąkach, link do [[Pierwszy wpis]] i [[Notatki prywatne]].\n"_s);
    nb.write(u"Blog/Szkic.md"_s, u"---\npublish: false\n---\nJeszcze nie gotowe.\n"_s);
    nb.write(u"Notatki prywatne.md"_s, u"Hasło do wifi: tajne.\n"_s);
    nb.write(u"assets/kot.png"_s, QByteArray("\x89PNG\r\n\x1a\nkot", 11));
}

/// Run `start` (which calls publisher.preview / publish / unpublish with a callback that stores
/// the report) until the report arrives.
template<typename Start>
brulion::publish::PublishReport run(Start start)
{
    std::optional<brulion::publish::PublishReport> out;
    start([&out](const brulion::publish::PublishReport &r) { out = r; });
    if (!QTest::qWaitFor([&out] { return out.has_value(); }, brulion::test::kRoomyMs)) {
        brulion::publish::PublishReport none;
        none.errorCode = u"test-timeout"_s;
        return none;
    }
    return *out;
}

} // namespace publishtest
