# gitcli — git and the GitHub CLI for the "Code" mode (docs/CODE-GIT.md). gitcli.h: the pure
# parsers of git's machine-readable output (status --porcelain=v2, diff --numstat / --raw,
# worktree list --porcelain, branch --format, the URL gh pr create prints) and the rules shared
# with the app (branch names, checkpoint ids, safe paths). client.h: Client, which runs git and gh
# with QProcess, asynchronously and without a shell — status with lines added / deleted, a file's
# two texts for a diff, commit / push / pull, branches, worktrees, pull requests — and
# checkpoints (checkpoints.cpp): snapshots of the work tree through a temporary index, kept by
# refs/brulion/checkpoints/<id>, the changes since one, the work tree put back to one.
brulion_add_core_module(gitcli
    SOURCES
        gitcli.h parse.cpp
        client.h client.cpp
        checkpoints.cpp
        messages_p.h)
