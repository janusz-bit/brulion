#pragma once
// What the chat formats of ai/local.h share (local.cpp: LFM3's; localformats.cpp: the formats of a
// model of the user's own — ChatML, Llama 3, Gemma … — with the Hermes convention of tool calls).
// Not part of the module's interface.

#include "ai/local.h"

#include <QJsonValue>
#include <QList>
#include <QString>

#include <utility>

namespace brulion::ai::local::detail {

/// A JSON string literal of `text` (non-ASCII letters as they are).
QString jsonText(const QString &text);
/// The text of a JSON string literal.
QString unquoted(QStringView literal);
/// JSON as a model's template writes it (Python's json.dumps: ", " and ": ").
QString spacedJson(const QJsonValue &value);
/// The members of a JSON object's text in the order written, each name with its value as the
/// model writes a tool's parameter: a string's text, anything else as it stands.
QList<std::pair<QString, QString>> orderedMembers(const QString &json);

/// A tool call of an assistant's turn: its parameters in the order written, as the model's text,
/// and its arguments' JSON text as given (a format that writes them back as JSON).
struct Call {
    QString id;
    QString name;
    QList<std::pair<QString, QString>> parameters;
    QString arguments;
};

/// An assistant's turn as a prompt writes it.
struct Turn {
    bool thought = false; ///< it has thoughts (possibly none written)
    QString thoughts;
    QString content;
    QList<Call> calls;
};

/// A message's turn: from a reply's raw (an OpenAI-style message — this module's own, or an
/// OpenAI-compatible provider's), from another provider's blocks (their text), or its plain text.
Turn turnOf(const Message &message);

/// The type a tool's schema gives a parameter ("string", "integer" …; "" when it gives none).
QString parameterType(const QList<Tool> &tools, const QString &tool, const QString &parameter);
/// A parameter's text as a JSON value of the type its tool's schema names (local.cpp).
QString valueJson(const QString &text, const QString &type);

/// A prompt's pieces as they are added: texts next to each other are one piece (a model's
/// tokenizer reads the text between two control tokens as a whole).
class Writer {
public:
    void tag(const QString &token) { m_pieces.append(Piece{token, true, {}}); }
    /// A tag that is one token in some vocabularies only (Piece::loose).
    void looseTag(const QString &token)
    {
        Piece piece{token, true, {}};
        piece.loose = true;
        m_pieces.append(piece);
    }
    void text(const QString &text)
    {
        if (text.isEmpty())
            return;
        if (!m_split && !m_pieces.isEmpty() && !m_pieces.last().special && m_pieces.last().image.isEmpty())
            m_pieces.last().text += text;
        else
            m_pieces.append(Piece{text, false, {}});
        m_split = false;
    }
    /// Ends the text being written: the next text is a piece of its own (the end of a prompt's
    /// stable part inside a turn).
    void split() { m_split = true; }
    void image(const QByteArray &data) { m_pieces.append(Piece{QString(), false, data}); }
    qsizetype size() const { return m_pieces.size(); }
    QList<Piece> pieces() const { return m_pieces; }

private:
    QList<Piece> m_pieces;
    bool m_split = false;
};

/// The last user message that is not tool results (-1: none): a model's thoughts stay only in the
/// turns after it.
qsizetype lastQuestion(const Request &request);

// The formats of localformats.cpp.
bool knowsOtherFormat(const QString &format);
Prompt otherPrompt(const QString &format, const Request &request);
Reply otherReply(const QString &format, const QList<Piece> &generated, const Request &request, bool complete);
Partial otherPartial(const QString &format, const QList<Piece> &generated, const Request &request);
Grammar otherGrammar(const QString &format, const QList<Tool> &tools);

/// GBNF of a tool's arguments as a JSON object: its schema's keys where it names them, each with a
/// value of its type where that is simple ("string", "integer", "number", "boolean", an enum of
/// strings), any JSON otherwise. Rules are added to `rules` under `prefix`; returns the rule's name.
QString argumentsRule(const Tool &tool, const QString &prefix, QStringList *rules);
/// The rules of JSON values every grammar shares (value, object, array, string, number, ws).
QStringList jsonRules();
/// A GBNF string literal of `text`.
QString gbnfLiteral(const QString &text);
/// The rule name of a tool (letters, digits and '-' only).
QString ruleName(const QString &tool);

} // namespace brulion::ai::local::detail
