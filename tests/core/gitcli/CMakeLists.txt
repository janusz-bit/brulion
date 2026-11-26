# git for the "Code" mode (brulion_core_gitcli). tst_gitcli: the parsers without git — status
# --porcelain=v2 (branch headers, every kind of entry, paths with spaces, newlines and Polish
# letters), diff --numstat / --raw, worktree list, branch --format, gh's pull request URL, the
# rules for names, ids and paths; garbage and truncated records. tst_gitcli_repo: Client with real
# git in temporary repositories (skipped without git on PATH; package.nix's nativeCheckInputs has
# it): status, a file's texts, commit, push / pull with a bare remote, branches, worktrees, pull
# requests through a fake gh (fake_gh.cpp), checkpoints (the index, HEAD and stash untouched; restore of
# modified, added, deleted, renamed files, folders, modes, links; ignored files left), time limits.
brulion_add_test(tst_gitcli SOURCES tst_gitcli.cpp LIBS brulion_core_gitcli)
brulion_add_test(tst_gitcli_repo SOURCES tst_gitcli_repo.cpp LIBS brulion_core_gitcli)
set_tests_properties(tst_gitcli_repo PROPERTIES TIMEOUT 300)
# The GitHub CLI as the tests need it (and a git that never finishes): a program on every platform.
qt_add_executable(fake_gh fake_gh.cpp)
target_link_libraries(fake_gh PRIVATE Qt6::Core)
brulion_set_warnings(fake_gh)
add_dependencies(tst_gitcli_repo fake_gh)
target_compile_definitions(tst_gitcli_repo PRIVATE FAKE_GH="$<TARGET_FILE:fake_gh>")
