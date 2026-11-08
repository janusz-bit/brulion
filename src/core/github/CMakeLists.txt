# github — a notebook kept in a GitHub repository (docs/UI.md "Notebooks from GitHub"): repository
# addresses as people paste them, Git's object ids, the GitHub REST API client (repositories,
# branches, trees, blobs, commits, references, forks, pull requests; raw.githubusercontent.com for
# public files without a token) and RepoSync — pulling the newest version of a branch's folder into
# the vault with three-way merges, committing the notebook's changes, proposing them as a pull
# request from a branch (of a fork when the user may not push). HTTP only through
# sync::HttpTransport (tests: an in-memory GitHub, tests/core/github/fakegithub.h); the token
# lives in memory, registered with log::redact.
# Public headers: github.h (overview), address.h, gitobjects.h, client.h, reposync.h.
brulion_add_core_module(github
    SOURCES
        github.h
        address.h address.cpp
        gitobjects.h gitobjects.cpp
        client.h client.cpp
        reposync.h reposync.cpp reposync_commit.cpp
        reposync_p.h
    DEPS util text vault log sync drawing
    QT Network)
