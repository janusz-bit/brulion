// FakeEditorHost — an in-memory EditorHost for the editor playground (`brulion --editor`) and
// the editor tests: files in a hash, suggestions/link resolution/block ids computed by parsing
// those files with the markdown module (simple, not the real index), assets as generated
// images, navigation recorded and announced by signals.
//
//   FakeEditorHost { id: host; Component.onCompleted: populateSample() }
//   PageController { host: host; path: host.samplePath }
#pragma once

#include "editorhost.h"

#include <QHash>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

namespace brulion::editor {

class FakeEditorHost : public EditorHost {
    Q_OBJECT
    /// Number of write() calls (tests).
    Q_PROPERTY(int writeCount READ writeCount NOTIFY written FINAL)
    /// The rich Polish sample page created by populateSample().
    Q_PROPERTY(QString samplePath READ samplePath CONSTANT FINAL)
    /// Last route passed to navigate().
    Q_PROPERTY(QVariantMap lastRoute READ lastRoute NOTIFY navigated FINAL)

public:
    explicit FakeEditorHost(QObject *parent = nullptr);
    ~FakeEditorHost() override;

    // EditorHost
    QString read(const QString &path, bool *ok = nullptr) override;
    bool write(const QString &path, const QString &content) override;
    QVariantList suggestPages(const QString &prefix, int limit = 20) const override;
    QVariantList suggestBlocks(const QString &text, int limit = 20) const override;
    QVariantList suggestTags(const QString &prefix, int limit = 20) const override;
    QString resolveLink(const QString &target, const QString &fromPath) const override;
    QString blockText(const QString &blockId) const override;
    QString ensureBlockId(const QString &path, int line) override;
    QVariantList templates() const override;
    QUrl assetUrl(const QString &path) const override;
    QString saveAttachment(const QString &fileName, const QByteArray &data) override;
    QVariantMap vaultConfig() const override;
    void navigate(const QVariantMap &route, bool newPane = false) override;
    void openUrl(const QUrl &url) override;

    /// Put a file without any signal (setting up a test or the playground).
    Q_INVOKABLE void setFile(const QString &path, const QString &content);
    /// Change a file "outside the editor": stores it and emits fileChanged(path).
    Q_INVOKABLE void externalWrite(const QString &path, const QString &content);
    Q_INVOKABLE void removeFile(const QString &path);
    Q_INVOKABLE QString file(const QString &path) const;
    Q_INVOKABLE QStringList files() const;
    /// Fill the vault with sample pages (Polish), see samplePath.
    Q_INVOKABLE void populateSample();
    /// Make write() fail (tests).
    Q_INVOKABLE void setFailWrites(bool fail) { m_failWrites = fail; }

    int writeCount() const { return m_writeCount; }
    QString samplePath() const;
    QVariantMap lastRoute() const { return m_lastRoute; }
    /// The text of the sample page.
    static QString sampleNote();

Q_SIGNALS:
    void written(const QString &path);
    void navigated(const QVariantMap &route, bool newPane);
    void urlOpened(const QUrl &url);

private:
    QHash<QString, QString> m_files;
    QHash<QString, QByteArray> m_assets;
    mutable QHash<QString, QUrl> m_assetUrls;
    mutable std::unique_ptr<QTemporaryDir> m_assetDir;
    int m_writeCount = 0;
    bool m_failWrites = false;
    QVariantMap m_lastRoute;
};

} // namespace brulion::editor
