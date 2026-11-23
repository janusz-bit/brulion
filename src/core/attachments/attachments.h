// brulion attachments module — the text of PDFs, pictures and documents in the whole-notebook search
// (docs/UI.md "Search", "Searching attachments"; docs/ARCHITECTURE.md). GUI-free and pure: what
// a PDF page or a picture says is read elsewhere (the app's AttachmentIndexer: the PDF's text layer
// through src/platform/pdfpages.h, OCR through src/platform/ocr.h, on a worker thread); the text of
// documents (DOCX, PPTX, ODT, EPUB) is read here, from their ZIP files of XML; this module
// keeps it on the device, indexes it and searches it. Include this header to get the whole module
// (namespace brulion::attachments).
//
// API overview:
//
//   documenttext.h extractDocumentText(device, format, limits, cancelled) -> DocumentText{status,
//                     pages} — the paragraphs of a DOCX, PPTX, ODT or EPUB in reading order, bounded
//                     (entries, parts unpacked, text) and safe (no external entity, no "zip bomb");
//                   documentRecord(format, text, extractor) — the Record of it
//   record.h        Record — what one file says: its kind (PDF, picture, document), its pages, each with its
//                     text, where the text came from (PageSource: the PDF's text layer, OCR — with
//                     its languages and the words' boxes — or nothing yet) and, for OCR, the words;
//                   encodeRecord(record) / decodeRecord(bytes) — the device cache's file of one
//                     content hash (bounds-checked, checksummed, a malformed file is no record);
//                   decodeRecordPage(bytes, page) / readRecordPage(device, page) — one page
//                     without decoding the others (excerpts, the viewer's highlights);
//                   readRecordInfo(device) — kind, extractor version and the pages' sources;
//                   ocrCharBoxes(page) — a box for every UTF-16 unit of an OCR page's text (the
//                     PDF viewer and the picture view highlight a hit from them)
//   manifest.h      Manifest — the notebook's attachments by path: size, modification time, the
//                     content hash naming their record (encodeManifest / decodeManifest);
//                   hashFile(device, cancelled) — SHA-256 of a file read in chunks
//   attachmentsearch.h
//                   AttachmentSearch — a SearchIndex (index/searchindex.h: BM25+, prefix, fuzzy,
//                     diacritic-insensitive) of the attachments' pages, one document per page and
//                     one for the file's name: prepare(path, record) (pure, any thread), set(),
//                     setPage(), remove(), rename(), search(query, limit, kinds, pageText) ->
//                     Hit{path, kind, page, matchedPages, score, literal, ocr, excerpt, find} — one
//                     hit per file at its best page, with an excerpt around the hit and the text
//                     the PDF viewer searches for to show it
//   searchfilters.h parseSearchFilters(query) -> SearchFilters{text, scopes} — the search field's
//                     `in:` filters (in:attachments, in:pdf, in:pictures, in:notes, in:pages,
//                     in:journals, in:documents; also type: and the Polish w: / typ:, values in English or
//                     Polish, diacritics optional)
#pragma once

#include "attachments/attachmentsearch.h"
#include "attachments/documenttext.h"
#include "attachments/manifest.h"
#include "attachments/record.h"
#include "attachments/searchfilters.h"
