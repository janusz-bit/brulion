# gitcli — git and the GitHub CLI for the "Code" mode (docs/CODE-GIT.md). gitcli.h: the pure
# parsers of git's machine-readable output (status --porcelain=v2, diff --numstat / --raw,
# worktree list --porcelain, branch --format, log, stash list, remote -v, unified diffs, the URL
# gh pr create prints, gh pr view's JSON) and the rules shared with the app (branch names,
# checkpoint ids, safe paths and revisions). client.h: Client, which runs git and gh with QProcess,
# asynchronously and without a shell — status with lines added / deleted, a file's two texts for a
# diff, commit (all, the staged files, chosen files) / push / pull, branches, worktrees, pull
# requests, history, stashes, merges and rebases in progress, single files and hunks staged or
# thrown away — and checkpoints (checkpoints.cpp): snapshots of the work tree through a temporary
# index, kept by refs/brulion/checkpoints/<id>, the changes since one, the work tree put back to one.
brulion_add_core_module(gitcli
    SOURCES
        gitcli.h parse.cpp history.cpp patch.cpp
        client.h client.cpp client_history.cpp client_files.cpp client_operations.cpp
        checkpoints.cpp
        messages_p.h)
