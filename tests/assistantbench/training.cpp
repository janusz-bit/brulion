#include "training.h"

#include "ai/local.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::assistantbench {

namespace {

QJsonObject segment(const ai::local::Piece &piece, const QList<int> &tokens, bool train)
{
    QJsonArray ids;
    for (const int t : tokens)
        ids.append(t);
    return QJsonObject{{u"text"_s, piece.text}, {u"special"_s, piece.special}, {u"train"_s, train}, {u"tokens"_s, ids}};
}

} // namespace

QList<QJsonArray> trainingSequences(const QList<Turn> &turns, const QString &endText, int endToken)
{
    QList<QJsonArray> sequences;
    QJsonArray current;
    QList<int> read; // the tokens of `current`
    for (const Turn &turn : turns) {
        const QList<int> prompt = turn.prompt.all();
        // Where the prompt goes on from the sequence: after a whole piece.
        qsizetype from = -1;
        if (!current.isEmpty() && prompt.size() > read.size() && std::equal(read.cbegin(), read.cend(), prompt.cbegin())) {
            qsizetype tokens = 0;
            qsizetype piece = 0;
            while (piece < turn.prompt.tokens.size() && tokens < read.size())
                tokens += turn.prompt.tokens.at(piece++).size();
            if (tokens == read.size())
                from = piece;
        }
        if (from < 0) {
            if (!current.isEmpty())
                sequences.append(current);
            current = QJsonArray();
            read.clear();
            from = 0;
        }
        for (qsizetype i = from; i < turn.prompt.pieces.size(); ++i)
            current.append(segment(turn.prompt.pieces.at(i), turn.prompt.tokens.at(i), false));
        read = prompt;

        const bool learn = turn.complete && turn.learnt;
        for (qsizetype i = 0; i < turn.answer.pieces.size(); ++i) {
            current.append(segment(turn.answer.pieces.at(i), turn.answer.tokens.at(i), learn));
            read += turn.answer.tokens.at(i);
        }
        if (turn.complete) {
            current.append(segment(ai::local::Piece{endText, true, {}}, {endToken}, learn));
            read.append(endToken);
        }
    }
    if (!current.isEmpty())
        sequences.append(current);
    return sequences;
}

std::optional<QList<ai::local::Piece>> writtenAnswer(const QString &format, const ai::Request &request,
                                                      const ai::Reply &reply, const QString &endText, QString *error)
{
    const QList<ai::local::Piece> asked = ai::local::prompt(format, request).pieces;
    ai::Request next = request;
    ai::Message turn;
    turn.role = u"assistant"_s;
    turn.content = reply.text;
    turn.raw = reply.raw;
    next.messages.append(turn);
    const QList<ai::local::Piece> after = ai::local::prompt(format, next).pieces;
    if (after.size() <= asked.size() || !std::equal(asked.cbegin(), asked.cend(), after.cbegin())) {
        *error = u"the turn written back does not follow the prompt"_s;
        return std::nullopt;
    }
    QList<ai::local::Piece> written;
    qsizetype i = asked.size();
    for (; i < after.size() && !(after.at(i).special && after.at(i).text == endText); ++i)
        written.append(after.at(i));
    if (i == after.size()) {
        *error = u"the turn written back has no end (%1)"_s.arg(endText);
        return std::nullopt;
    }

    // Read back as the model's answer, it is the same reply.
    const ai::Reply read = ai::local::reply(format, written, request, true);
    const auto inputOf = [](const ai::ToolCall &call) {
        return QJsonDocument(QJsonObject::fromVariantMap(call.input)).toJson(QJsonDocument::Compact);
    };
    bool same = read.text.trimmed() == reply.text.trimmed() && read.toolCalls.size() == reply.toolCalls.size();
    for (qsizetype c = 0; same && c < read.toolCalls.size(); ++c) {
        same = read.toolCalls.at(c).name == reply.toolCalls.at(c).name
               && inputOf(read.toolCalls.at(c)) == inputOf(reply.toolCalls.at(c));
    }
    if (!same) {
        *error = u"the turn written back reads as another answer: %1"_s.arg(QString::fromUtf8(read.raw));
        return std::nullopt;
    }
    return written;
}

} // namespace brulion::assistantbench
