// brulion-cli — Brulion's command line (src/cli/cli.h, docs/CLI.md).
#include "cli.h"

int main(int argc, char *argv[])
{
    return brulion::cli::run(argc, argv);
}
