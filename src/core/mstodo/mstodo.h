// brulion::mstodo — Microsoft To Do download for the import (docs/IMPORT.md "Microsoft To Do").
// GUI-free; HTTP through sync::HttpTransport only (NetworkHttpTransport in the app, a fake
// Microsoft in tests), single-threaded (the thread that owns the objects).
//
// API overview:
//
//   deviceauth.h   validateClientId(id) / validateAuthority(authority) -> "" or a translated error
//                  (client id: a GUID; authority: consumers | organizations | common | a tenant
//                  GUID or domain; DefaultAuthority = "consumers")
//                  describeAuthError(error, description, codes) -> { code, message } — the
//                  identity platform's answers explained (AADSTS7000218 public client flows off,
//                  AADSTS700016 unknown client id, AADSTS50059 / 9002331 wrong account type, …)
//                  class DeviceCodeLogin { start(clientId, authority), cancel() — signals
//                    codeReady(userCode, verificationUri, expiresInSeconds), signedIn(token),
//                    failed(code, message) } — POST …/{authority}/oauth2/v2.0/devicecode with
//                    scope Tasks.Read, then polls …/token every `interval` s (slow_down: +5 s)
//   download.h     class TodoDownload { start(accessToken), cancel() — signals progress(lists
//                    done, lists, tasks), finished(bundle JSON, warnings), failed(code, message) }
//                  — GET /v1.0/me/todo/lists, every list's tasks ($expand=checklistItems,
//                  linkedResources; per-task requests when Graph refuses the expansion), the
//                  attachments of tasks that have some (size budget), @odata.nextLink paging,
//                  Retry-After on 429 / 503; the bundle: {"brulion": "mstodo-export",
//                  "version": 1, "exported", "lists": [{list…, "tasks": [{task…,
//                  "checklistItems", "linkedResources", "attachments"}]}]}
//
// Security: the access token stays in memory (registered with log::redact while in use) and is
// never written, logged or put into a message; only login.microsoftonline.com and
// graph.microsoft.com are ever contacted (paging links to other hosts are not followed).
#pragma once

#include "mstodo/deviceauth.h"
#include "mstodo/download.h"
