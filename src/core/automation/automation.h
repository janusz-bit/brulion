// brulion::automation — brulion:// addresses, Android's automation intents and brulion-cli's
// `uri` commands: what a link, a launcher, Tasker or a script asks of a notebook, how much it may do
// without asking, and doing it (docs/UI.md "Automation", docs/ANDROID.md "Automation (Tasker …)",
// docs/CLI.md "uri"). GUI-free; never reads the clock — callers pass `now`.
//
//   uri.h     parseUri(text) -> ParsedUri{Request{action, params, vault, key, source, id,
//             x-success/x-error/x-cancel}, error}; buildUri(request); isAutomationUri(text);
//             redactedUri() (the key masked for logs); withQuery() (callback addresses); the limits
//             (MaxUriLength, MaxValueLength, MaxParameters)
//   policy.h  accessOf(request) -> Open | Write | Run; decide(access, policy ask|key|off, key
//             state, canAsk) -> Allow | Confirm | Refuse; mayCallBack(); generateKey(),
//             keyMatches() (constant time); KeyStore (secrets/automation-keys.json, a key per
//             notebook and device)
//   edits.h   insertBlock(content, {block, End|Start, heading, blockId}) -> {content, line} (pure,
//             untouched bytes kept); buildTask({text, due, scheduled, start, remind, alarm, every,
//             priority}, now) -> "- [ ] … ⏰ …"; dayOf(phrase, now)
//   plan.h    plan(request, vault, index, context) -> Plan (route | write | run, or an error code);
//             planned(plan, current) (the text a write leaves: previews); execute(plan, Writer) ->
//             Outcome{ok, error, path, line, block, reminder, route} (toJson(), callbackParams());
//             NotebookWriter (a headless notebook); matchNotebook(name or folder, known)
#pragma once

#include "automation/edits.h"
#include "automation/plan.h"
#include "automation/policy.h"
#include "automation/uri.h"
