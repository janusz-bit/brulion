#include "qrshare.h"

#include "qr/qrtext.h"

#include <algorithm>
#include <climits>

using namespace Qt::StringLiterals;

namespace brulion::qrview {

QrShare::QrShare(QObject *parent) : QObject(parent) { }

void QrShare::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    m_host = host;
    Q_EMIT hostChanged();
    if (m_plainText)
        update(); // block references read again
}

void QrShare::setMarkdown(const QString &markdown)
{
    if (m_markdown == markdown)
        return;
    m_markdown = markdown;
    Q_EMIT markdownChanged();
    update();
}

void QrShare::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    Q_EMIT titleChanged();
    update();
}

void QrShare::setPlainText(bool plainText)
{
    if (m_plainText == plainText)
        return;
    m_plainText = plainText;
    Q_EMIT plainTextChanged();
    update();
}

void QrShare::setWithTitle(bool withTitle)
{
    if (m_withTitle == withTitle)
        return;
    m_withTitle = withTitle;
    Q_EMIT withTitleChanged();
    update();
}

QString QrShare::state() const
{
    if (!m_parts.isEmpty())
        return u"ok"_s;
    return m_codesNeeded > 0 ? u"tooLong"_s : u"empty"_s;
}

int QrShare::maxCodes()
{
    return qr::MaxCodes;
}

void QrShare::setIndex(int index)
{
    const int shown = m_parts.isEmpty() ? 0 : std::clamp(index, 0, int(m_parts.size()) - 1);
    if (shown == m_index)
        return;
    m_index = shown;
    Q_EMIT partChanged();
}

QString QrShare::partText() const
{
    return m_index >= 0 && m_index < m_parts.size() ? m_parts.at(m_index) : QString();
}

void QrShare::next()
{
    setIndex(m_index + 1);
}

void QrShare::previous()
{
    setIndex(m_index - 1);
}

void QrShare::update()
{
    qr::NoteTextOptions options;
    options.format = m_plainText ? qr::TextFormat::PlainText : qr::TextFormat::Markdown;
    options.title = m_withTitle ? m_title : QString();
    if (m_plainText && m_host) {
        const QPointer<EditorHost> host = m_host;
        options.blockText = [host](const QString &id) { return host ? host->blockText(id) : QString(); };
    }
    const QString text = qr::noteText(m_markdown, options);
    const bool titleOptional = !m_title.trimmed().isEmpty() && !qr::hasOwnTitle(m_markdown);
    const qsizetype bytes = qr::utf8Size(text);
    // A text too long is never cut just to be counted: its size tells how many codes it needs.
    qsizetype needed = (bytes + qr::MaxBytes - 1) / qr::MaxBytes;
    QStringList parts;
    if (needed > 0 && needed <= qr::MaxCodes) {
        parts = qr::splitText(text);
        needed = parts.size(); // (one more where a character crosses a limit)
        if (needed > qr::MaxCodes)
            parts.clear();
    }

    const bool textChanges = text != m_text || titleOptional != m_titleOptional || parts != m_parts;
    const int index = text == m_text ? std::clamp(m_index, 0, std::max(0, int(parts.size()) - 1)) : 0;
    const bool partChanges = textChanges || index != m_index;
    m_text = text;
    m_bytes = int(std::min<qsizetype>(bytes, INT_MAX));
    m_codesNeeded = int(std::min<qsizetype>(needed, INT_MAX));
    m_parts = parts;
    m_titleOptional = titleOptional;
    m_index = index;
    if (textChanges)
        Q_EMIT textChanged();
    if (partChanges)
        Q_EMIT partChanged();
}

} // namespace brulion::qrview
