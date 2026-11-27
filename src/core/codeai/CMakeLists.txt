# codeai — Code mode's texts written by the AI (docs/CODE.md, docs/CODE-GIT.md), as T3 Code writes
# them: the questions for a commit message, a pull request's title and description, a new
# worktree's branch name and a thread's title, and their answers read. GUI-free and pure; the app
# sends the requests (AiAsk, WorkspaceExtensions::sendAi).
brulion_add_core_module(codeai
    SOURCES
        codeai.h codeai.cpp
    DEPS util ai chat)
