// A clipped page's HTML as Markdown (docs/CLIPPER.md "What a clip becomes"): io::htmlToMarkdown()
// (QTextDocument parses it, nothing is ever loaded or run) with the page's own address as the base of
// its relative links and pictures, links only to http(s), mailto: and the page's own fragments,
// embedded players as links, and every picture a token that the clip replaces once it knows where
// the picture ended up (saved into the notebook, or left on the internet). Part of the clipper
// module — see clipper/clipper.h.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>
#include <QUrl>

#include <functional>

namespace brulion::clipper {

/// The largest number of pictures one clip keeps track of (more are dropped from the text).
inline constexpr int MaxPictures = 200;

/// A picture of a clip.
struct ClipPicture {
    QString source; ///< an http(s) address, or a data: URL
    QString alt;    ///< its description (one line)
};

/// The pictures of one clip (its content, selection, page picture and screenshot share them).
class PictureList {
public:
    /// The token standing for `source` in Markdown; the same source gets the same token. "" when
    /// the list is full (MaxPictures) or `source` is neither http(s) nor a data: picture.
    QString add(const QString &source, const QString &alt);
    const QList<ClipPicture> &pictures() const { return m_pictures; }
    bool isEmpty() const { return m_pictures.isEmpty(); }

private:
    QList<ClipPicture> m_pictures;
};

/// The token of the picture `index` (private-use characters around its number).
QString pictureToken(int index);
/// `text` without the characters tokens are made of (input from a page never carries a token).
QString withoutTokenChars(QString text);

/// `markdown` with every picture token replaced by `embed(index, inLink)`; `inLink` tells that
/// the token is a link's whole label (`[TOKEN](href)`, a picture that links somewhere): the
/// embed may then return the Markdown for the whole link. An unknown index becomes "".
QString replacePictureTokens(const QString &markdown, const std::function<QString(int index, bool inLink)> &embed);

struct ConvertedHtml {
    QString markdown;  ///< with picture tokens; ends with "\n" unless empty
    QString plainText; ///< the text alone (for `{{content:text}}`)
};

/// `html` as Markdown, its relative addresses resolved against `base` (the page's address);
/// pictures added to `pictures` (as tokens in the Markdown). Links to the page itself (`#notes`)
/// keep the page's address; links to anything but http(s), mailto: and the page keep only their
/// text. A link around a picture to the same picture (or to any picture file) is the picture alone.
/// `plainText`: also its text alone (a second parse; only templates with `:text` need it).
ConvertedHtml convertHtml(const QString &html, const QUrl &base, PictureList &pictures, bool plainText = false);

/// `url` resolved against `base` (a relative or protocol-relative address); invalid when the
/// result is not http(s).
QUrl resolveWebUrl(QStringView url, const QUrl &base);

} // namespace brulion::clipper
