# Code mode's texts written by the AI (brulion_core_codeai): the questions for a commit message, a
# pull request, a worktree's branch name and a thread's title (what each sends, bounded), and the
# answers read — fences, JSON, labels, quotes, prefixes, sentences instead of names, malformed and
# empty answers; a thread's messages chosen for its title.
brulion_add_test(tst_codeai SOURCES tst_codeai.cpp LIBS brulion_core_codeai)
