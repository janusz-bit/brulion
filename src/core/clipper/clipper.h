// brulion::clipper — the Web Clipper's side in Brulion (docs/CLIPPER.md): what the browser extension
// (clipper/ at the repository's top) sends, read and checked, and turned into Markdown for a page,
// today's journal or a heading's section. Include this header to get the whole module. GUI-free:
// the HTML is parsed by io::htmlToMarkdown() (QTextDocument, which needs no QGuiApplication and
// never loads anything), pictures come through webpicture's fetcher. The command line
// (src/cli/commands_clip.cpp, `brulion-cli clip …` and the native messaging host) writes into the
// notebook with its usual edits (lock, compare-and-swap, history).
//
// API overview:
//
//   nativemessaging.h  FrameDecoder (feed() bytes, next() frames: a JSON object or FrameError —
//                        TooLarge ends the stream, Truncated at the end), encodeFrame(), the
//                        limits MaxIncomingBytes (64 MiB) / MaxOutgoingBytes (1 MiB)
//   request.h          ClipRequest (kind, url, title, html, selection, metadata, screenshot,
//                        picture / link, tags, ClipPlace {page | journal | heading}, TemplateChoice,
//                        downloadPictures, vault), parseClipRequest() (sizes bounded, paths with "..",
//                        hidden or absolute parts refused), cleanLine(), checkedFolder(),
//                        checkedPagePath(), cleanTags(), tagText(), checkedLinkUrl()
//   cliptemplate.h     builtinTemplates() (brulion, obsidian, joplin, link), fillClipTemplate()
//                        (ClipValues + the variables of every template; YAML-safe frontmatter,
//                        `key::` lines, Markdown text), frontmatterAsBlockProperties(),
//                        parsePublished(), asQuote()
//   article.h          convertHtml() (HTML → Markdown with picture tokens, relative addresses
//                        resolved, links kept to http(s) / mailto: / the page), PictureList,
//                        replacePictureTokens(), resolveWebUrl()
//   pictures.h         PictureBatch (the download within PictureLimits; results per picture),
//                        decodeDataPicture(), pictureFileName()
//   nativehost.h       hostName(), the extension ids, hostManifest(), hostLocation() per Browser /
//                        Os / Scope, isNativeMessagingLaunch()
//   hostinstall.h      hostState() / installHost() / uninstallHost() (files; Windows' registry),
//                        currentEnvironment(), defaultHostProgram(), isNixOs()
#pragma once

#include "clipper/article.h"
#include "clipper/cliptemplate.h"
#include "clipper/hostinstall.h"
#include "clipper/nativehost.h"
#include "clipper/nativemessaging.h"
#include "clipper/pictures.h"
#include "clipper/request.h"
