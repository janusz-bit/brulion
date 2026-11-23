#include "markdown/noteoptions.h"

#include "markdown/tasks.h"

#include <QMetaType>

using namespace Qt::StringLiterals;

namespace brulion::markdown {

namespace {

/// A value as text: a list by its first item, quotes of a Logseq line dropped, trimmed.
QString valueText(const QVariant &value)
{
    const int type = value.metaType().id();
    QString text = type == QMetaType::QVariantList || type == QMetaType::QStringList ? value.toStringList().value(0)
                                                                                     : value.toString();
    text = text.trimmed();
    if (text.size() >= 2
        && ((text.front() == u'"' && text.back() == u'"') || (text.front() == u'\'' && text.back() == u'\'')))
        text = text.sliced(1, text.size() - 2).trimmed();
    return text;
}

/// Lower case without the Polish diacritics ("Pełna" → "pelna").
QString folded(const QString &text)
{
    QString out = text.toLower();
    static constexpr struct {
        char16_t from;
        char16_t to;
    } map[] = {{u'ą', u'a'}, {u'ć', u'c'}, {u'ę', u'e'}, {u'ł', u'l'}, {u'ń', u'n'},
               {u'ó', u'o'}, {u'ś', u's'}, {u'ź', u'z'}, {u'ż', u'z'}};
    for (QChar &c : out) {
        for (const auto &m : map) {
            if (c == QChar(m.from))
                c = QChar(m.to);
        }
    }
    return out;
}

} // namespace

QString noteOptionName(NoteOption option)
{
    switch (option) {
    case NoteOption::LineNumbers: return u"lineNumbers"_s;
    case NoteOption::PictureFrames: return u"pictureFrames"_s;
    case NoteOption::TextWidth: return u"textWidth"_s;
    case NoteOption::Tasks: return u"tasks"_s;
    }
    return {};
}

std::optional<NoteOption> parseNoteOptionName(QStringView name)
{
    for (const NoteOption o :
         {NoteOption::LineNumbers, NoteOption::PictureFrames, NoteOption::TextWidth, NoteOption::Tasks}) {
        if (name == noteOptionName(o))
            return o;
    }
    return std::nullopt;
}

QStringList noteOptionKeys(NoteOption option)
{
    switch (option) {
    case NoteOption::LineNumbers: return {u"line-numbers"_s, u"numery-linii"_s};
    case NoteOption::PictureFrames: return {u"picture-frames"_s, u"ramki"_s};
    case NoteOption::TextWidth: return {u"text-width"_s, u"szerokosc-tekstu"_s};
    case NoteOption::Tasks: return {u"tasks"_s, u"zadania"_s};
    }
    return {};
}

QString noteOptionKey(NoteOption option, bool polish)
{
    return noteOptionKeys(option).value(polish ? 1 : 0);
}

std::optional<bool> parseNoteFlag(const QVariant &value)
{
    switch (value.metaType().id()) {
    case QMetaType::Bool: return value.toBool();
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Double: {
        const double n = value.toDouble();
        if (n == 1)
            return true;
        if (n == 0)
            return false;
        return std::nullopt;
    }
    default: break;
    }
    const QString v = valueText(value).toLower();
    for (const QStringView yes : {u"true", u"yes", u"tak", u"on", u"1"}) {
        if (v == yes)
            return true;
    }
    for (const QStringView no : {u"false", u"no", u"nie", u"off", u"0"}) {
        if (v == no)
            return false;
    }
    return std::nullopt;
}

QString noteTextWidthName(NoteTextWidth width)
{
    switch (width) {
    case NoteTextWidth::Narrow: return u"narrow"_s;
    case NoteTextWidth::Normal: return u"normal"_s;
    case NoteTextWidth::Wide: return u"wide"_s;
    case NoteTextWidth::Full: return u"full"_s;
    }
    return {};
}

std::optional<NoteTextWidth> noteTextWidthFromName(QStringView name)
{
    for (const NoteTextWidth w : {NoteTextWidth::Narrow, NoteTextWidth::Normal, NoteTextWidth::Wide, NoteTextWidth::Full}) {
        if (name == noteTextWidthName(w))
            return w;
    }
    return std::nullopt;
}

QString noteTextWidthValue(NoteTextWidth width, bool polish)
{
    if (!polish)
        return noteTextWidthName(width);
    switch (width) {
    case NoteTextWidth::Narrow: return u"wąska"_s;
    case NoteTextWidth::Normal: return u"normalna"_s;
    case NoteTextWidth::Wide: return u"szeroka"_s;
    case NoteTextWidth::Full: return u"pełna"_s;
    }
    return {};
}

std::optional<NoteTextWidth> parseNoteTextWidth(const QVariant &value)
{
    const QString v = folded(valueText(value));
    for (const NoteTextWidth w : {NoteTextWidth::Narrow, NoteTextWidth::Normal, NoteTextWidth::Wide, NoteTextWidth::Full}) {
        if (v == noteTextWidthName(w) || v == folded(noteTextWidthValue(w, true)))
            return w;
    }
    return std::nullopt;
}

int noteTextWidthPixels(NoteTextWidth width)
{
    switch (width) {
    case NoteTextWidth::Narrow: return 600;
    case NoteTextWidth::Normal: return 680;
    case NoteTextWidth::Wide: return 960;
    case NoteTextWidth::Full: return 0;
    }
    return 0;
}

NoteOptions noteOptions(const Properties &properties)
{
    /// The value of the first of the option's keys the note has (invalid: none).
    const auto valueOf = [&](NoteOption option) {
        for (const QString &key : noteOptionKeys(option)) {
            if (const auto it = properties.constFind(key); it != properties.cend())
                return *it;
        }
        return QVariant();
    };
    NoteOptions out;
    out.lineNumbers = parseNoteFlag(valueOf(NoteOption::LineNumbers));
    out.pictureFrames = parseNoteFlag(valueOf(NoteOption::PictureFrames));
    out.textWidth = parseNoteTextWidth(valueOf(NoteOption::TextWidth));
    out.tasksIgnored = tasksIgnored(properties);
    return out;
}

} // namespace brulion::markdown
