// brulion::github — a notebook kept in a GitHub repository (docs/UI.md "Notebooks from GitHub"):
// Markdown notes in a folder of a branch opened as a notebook on every platform, the browser too
// (api.github.com answers the browser's CORS requests; there is no git there), the newest version
// pulled, the notebook's changes committed or proposed as a pull request. GUI-free; HTTP through
// sync::HttpTransport only (NetworkHttpTransport in the app, an in-memory GitHub in tests),
// single-threaded (the thread that owns the objects). Module "github"; deps: util, text, vault,
// log, sync, drawing; QtCore + QtNetwork.
//
// Why the REST API and not Git: one implementation for the desktop, Android and the browser, no
// native library to build for each, and only the notebook's folder travels — never the history.
// Other Git hosts are not supported (docs/UI.md says what to use instead).
//
// API overview
//
//   address.h     parseRepoAddress(text) -> RepoAddress{owner, name, refPath, file} — "owner/name",
//                 github.com URLs (…/tree/<branch>/<folder>, …/blob/…), git@ and ssh:// remotes,
//                 raw.githubusercontent.com; splitRefPath(refPath, branches) -> {branch, folder}
//                 (a branch name may hold "/": the longest existing branch wins);
//                 isValidOwner / isValidRepoName / isValidBranchName, normalizeFolder; webUrl,
//                 commitUrl, fileUrl, newTokenUrl (a classic token with the `repo` scope), encodePath
//   gitobjects.h  blobSha(bytes) — Git's blob id (what a tree lists: a file's version known without
//                 downloading it), isSha, shortSha, isLfsPointer; the tree entry modes
//   client.h      GitHubClient(transport): setToken (a personal access token, masked in logs;
//                 none: public repositories, 60 requests an hour), setBrowser (no User-Agent);
//                 user, repository (permissions), branch (If-None-Match), branchNames, tree,
//                 folder (contents API), blob, rawFile, createBlob, createTree, createCommit,
//                 updateBranch (never forced), createBranch, createFork, createPullRequest,
//                 pullRequest, putFile (an empty repository's first commit); cancel();
//                 GitHubError{code Auth | Forbidden | RateLimited | NotFound | EmptyRepository |
//                 Conflict | NotFastForward | Validation | Server | Network | Timeout | Parse |
//                 Integrity | TooLarge | Cancelled, status, message, resetAt, retryAfterSeconds},
//                 errorCodeName(); rateLimit()
//   reposync.h    RepoLink{repo, branch, folder}; isNotebookPath(path) (what of a folder is the
//                 notebook's: no hidden files, no .github, .git, …); RepoSync(vault, client, store):
//                 setLink, pull(done) -> PullReport, commit(message, paths, done) -> CommitReport,
//                 propose(title, body, paths, done) -> CommitReport with the Proposal (a pull
//                 request from a branch of the repository or of the user's fork; proposing again
//                 updates it while it is open), refreshProposal, cancel; localChanges() ->
//                 [LocalChange{path, kind added | modified | deleted, proposed}], headCommit,
//                 lastPullMs, proposal, repository, resetState; signals progress, stateChanged.
//                 Bookkeeping in the vault's LocalStore ("github/…"); merges as Nextcloud's sync
//                 (sync::merge3, mergeJson3, drawing::mergeDrawingFiles); conflict copies
//                 "Name.conflict-GitHub-<stamp>.ext", never committed
//
// Security: requests go to api.github.com and raw.githubusercontent.com only, with the token in an
// Authorization header to the API alone (the transport follows no redirect to another host); a
// downloaded file is written only when its bytes are the blob the tree names; notes from GitHub
// are notes like any other (rendered escaped, never executed).
#pragma once

#include "github/address.h"
#include "github/client.h"
#include "github/gitobjects.h"
#include "github/reposync.h"
