// AtramentView — an Atrament view inside the app's QML (docs/ATRAMENT.md D12, M2): the way views move
// from QML to Atrament one at a time. `AtramentView { view: "gallery" }` shows the named view of
// the process's one Atrament App (its fonts: the app's bundled ones, ":/fonts/ui" and ":/fonts").
// Built where Atrament is (BRULION_ATRAMENT: with the app on every platform); `brulion --atrament`
// opens qml/dev/AtramentDemo.qml (developer QML: desktop builds, and debuggable APKs — `adb shell am
// start … --es applicationArguments --atrament`).
#pragma once

#include "qthost.h"

#include <QColor>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>

#include <memory>

namespace brulion::atrament {
class App;
}

class AtramentView : public brulion::atrament::AtramentItem {
    Q_OBJECT
    QML_ELEMENT
    // The view to show: "gallery" (the framework's gallery) or "page" (the blocks of `pageModel`
    // drawn by Atrament: editor/atrament/pageview.h, AppSettings.editorAtrament). Set once.
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged FINAL)
    // "page": the editor's BlockListModel (a PageController's `blocks`), set before the view.
    Q_PROPERTY(QObject *pageModel READ pageModel WRITE setPageModel NOTIFY pageModelChanged FINAL)
    // "page": the editor's look (BlockEditor's metrics and Theme colours).
    Q_PROPERTY(qreal fontSize MEMBER m_fontSize NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal lineHeight MEMBER m_lineHeight NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal indentStep MEMBER m_indentStep NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal gutter MEMBER m_gutter NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal bulletColumn MEMBER m_bulletColumn NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool documentMode MEMBER m_documentMode NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal listIndent MEMBER m_listIndent NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal contentX MEMBER m_contentX NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal contentWidth MEMBER m_contentWidth NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor mutedColor MEMBER m_mutedColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor linkColor MEMBER m_linkColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor guideColor MEMBER m_guideColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY styleChanged FINAL)

    // "page": the rows Atrament does not draw yet (foreign rows: code, tables, math, pictures,
    // queries, the properties…) are `delegate` (the editor's BlockDelegate) placed over them, and
    // `header` / `footer` are the items above the blocks and below them (the title, the
    // properties, the zoom's breadcrumb; the room to tap under the last block) — the list's rows
    // too, scrolling with it (editor/atrament/foreignhost.h; qthost.h "Hosted children").
    Q_PROPERTY(QQmlComponent *delegate READ delegate WRITE setDelegate NOTIFY delegateChanged FINAL)
    Q_PROPERTY(QQuickItem *header READ header WRITE setHeader NOTIFY headerChanged FINAL)
    Q_PROPERTY(QQuickItem *footer READ footer WRITE setFooter NOTIFY footerChanged FINAL)

public:
    explicit AtramentView(QQuickItem *parent = nullptr);
    ~AtramentView() override;

    QString view() const { return m_view; }
    void setView(const QString &view);
    QObject *pageModel() const { return m_pageModel.data(); }
    void setPageModel(QObject *model);

    // The process's Atrament App, made on first use.
    static brulion::atrament::App &atramentApp();

    QQmlComponent *delegate() const { return m_delegate.data(); }
    void setDelegate(QQmlComponent *delegate);
    QQuickItem *header() const { return m_header.data(); }
    void setHeader(QQuickItem *item);
    QQuickItem *footer() const { return m_footer.data(); }
    void setFooter(QQuickItem *item);
    // "page": the delegate over model row `row` (null: the row is Atrament's, or not near the view).
    Q_INVOKABLE QQuickItem *rowItem(int row) const;

Q_SIGNALS:
    void viewChanged();
    void pageModelChanged();
    // "page": a link of row `row` clicked or tapped (BlockEditor.activateLink), a task's box.
    void linkActivated(int row, const QString &link);
    void taskToggled(int row);
    void styleChanged();
    void delegateChanged();
    void headerChanged();
    void footerChanged();

protected:
    void componentComplete() override;
    void placeHostedChildren() override;

private:
    void attachView();
    void applyStyle();

    QString m_view;
    bool m_attached = false;
    QPointer<QObject> m_pageModel;
    // The "page" view's entity (editor/atrament/pageview.h), released after the window closes.
    struct Page;
    std::unique_ptr<Page> m_page;
    qreal m_fontSize = 16;
    qreal m_lineHeight = 24;
    qreal m_indentStep = 26;
    qreal m_gutter = 16;
    qreal m_bulletColumn = 20;
    bool m_documentMode = false;
    qreal m_listIndent = 27;
    qreal m_contentX = 0;
    qreal m_contentWidth = 0;
    QColor m_textColor{0x1f, 0x23, 0x28};
    QColor m_mutedColor{0x6e, 0x77, 0x81};
    QColor m_linkColor{0x3b, 0x5b, 0xdb};
    QColor m_guideColor{0x8c, 0x95, 0x9f, 0x59};
    QColor m_backgroundColor{Qt::white};
    QPointer<QQmlComponent> m_delegate;
    QPointer<QQuickItem> m_header;
    QPointer<QQuickItem> m_footer;
};
