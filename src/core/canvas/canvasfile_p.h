// What a canvas remembers of its file (internal): each node's and edge's text and members as
// written, the arrays' whitespace, the file around them.
#pragma once

#include "canvas/canvas.h"

#include <QList>
#include <QString>

namespace brulion::canvas::detail {

/// One member of an object as written.
struct Member {
    QString lead;    ///< whitespace before the key (after `{` or `,`)
    QString keyText; ///< the key token, quotes included
    QString key;     ///< decoded
    QString sep;     ///< from the key token to the value (`:`, `: `)
    QString value;   ///< the value as written
};

/// A node or an edge as read.
struct Origin {
    QString source; ///< the object as written
    QList<Member> members;
    QString close; ///< whitespace before its `}`
    Node node;     ///< the node as read (origin null), for a node
    Edge edge;     ///< the edge as read, for an edge
};

/// An array of the root (`nodes`, `edges`) as written.
struct ArraySpan {
    bool present = false;
    qsizetype from = 0; ///< its `[`
    qsizetype to = 0;   ///< after its `]`
    QStringList leads;  ///< the whitespace before each item
    QString close;      ///< before `]`
};

struct Layout {
    QString text; ///< the file
    ArraySpan nodes;
    ArraySpan edges;
    qsizetype rootCloseFrom = 0; ///< the whitespace before the root's `}`
    bool rootHasMembers = false;
    QString memberLead; ///< the root's members' leading whitespace (the last one's)
    QString memberSep;  ///< `:` as the root writes it
};

} // namespace brulion::canvas::detail
