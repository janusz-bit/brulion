// brulion::cloudimport — downloads for the import from web services (docs/IMPORT.md "OneNote",
// "Todoist", "CSV and Airtable"). GUI-free; HTTP through sync::HttpTransport only
// (NetworkHttpTransport in the app, fake servers in tests), single-threaded. What a download
// gets becomes input files for the io module's importers, in the shapes they read from disk.
//
// API overview:
//
//   apiclient.h  class ApiClient — one service's requests: the token as a Bearer header to the
//                  hosts allowed for it (https only), retries after 429 / 503 / 504 and dropped
//                  connections with Retry-After, a body size limit; isHttpsHost(), isHttpsDomain()
//   downloads.h  class Download (progress, finished(files, warnings), failed(code, message);
//                  FileBudget: per file, total, count)
//                class OneNoteDownload — Microsoft Graph (a token of the mstodo module's
//                  DeviceCodeLogin with scope Notes.Read): listNotebooks(), start(token,
//                  notebookIds) -> OneNote/onenote.json + pages/*.html + resources/*
//                class TodoistDownload — the Sync API with a personal token -> todoist.json +
//                  _attachments/… (comment files)
//                class AirtableDownload — the REST API with a personal access token:
//                  listBases(), listTables(), start(token, base, name, tableIds) ->
//                  airtable.json + _attachments/…
//                class UrlFilesDownload — the files of a CSV's attachment cells -> _attachments/…
//
// Security: tokens stay in memory (registered with log::redact while in use) and are never
// written, logged or put into a message; each service's requests go only to its own hosts
// (graph.microsoft.com; api.todoist.com and other *.todoist.com for comment files;
// api.airtable.com, and *.airtableusercontent.com / dl.airtable.com without the token); the
// files of CSV cells go to any https server, never with credentials.
#pragma once

#include "cloudimport/apiclient.h"
#include "cloudimport/downloads.h"
