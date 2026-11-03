// Sequence diagrams (`sequenceDiagram`): the model, the parser and the scene. Internal; the
// tests read the model.
#pragma once

#include "diagram/common_p.h"
#include "diagram/diagram.h"

namespace brulion::diagram::detail {

struct SeqParticipant {
    QString id;
    QString label;
    bool actor = false; ///< drawn as a stick figure
    int box = -1;       ///< the `box` it is in
    int line = 0;
};

struct SeqBox {
    QString title;
    QString color; ///< validated, "" for the theme's
    int line = 0;
};

struct SeqEvent {
    enum class Type : quint8 { Message, Note, Activate, Deactivate, BlockStart, BlockSection, BlockEnd };
    enum class NotePlace : quint8 { LeftOf, RightOf, Over };
    Type type = Type::Message;
    int from = -1, to = -1; ///< Message: participants; Note: the first and last (Over A,B); Activate…: `from`
    QString text;
    // Message
    bool dotted = false;
    Head startHead = Head::None, endHead = Head::None;
    bool activateTarget = false;   ///< `->>+`
    bool deactivateSource = false; ///< `->>-`
    int number = 0;                ///< autonumber (0: none)
    // Note
    NotePlace place = NotePlace::RightOf;
    // Block: `loop`, `alt`, `opt`, `par`, `critical`, `break`, `rect` (BlockStart), `else`, `and`,
    // `option` (BlockSection)
    QString keyword;
    QString color; ///< `rect`'s colour (validated)
    int line = 0;
};

struct SequenceDiagram {
    QList<SeqParticipant> participants;
    QList<SeqBox> boxes;
    QList<SeqEvent> events;
    QString title;
};

Outcome<SequenceDiagram> parseSequence(QStringView source);
Scene drawSequence(const SequenceDiagram &diagram, const Metrics &metrics);

} // namespace brulion::diagram::detail
