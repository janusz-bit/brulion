# Zstandard 1.5.7's decoder (https://github.com/facebook/zstd, tag v1.5.7; BSD, see LICENSE — zstd is
# dual-licensed BSD or GPLv2, Brulion takes the BSD licence) — the compressed Codex rollouts
# (`rollout-*.jsonl.zst`) the usage history of src/core/agentusage reads. Vendored unmodified:
# lib/zstd.h, lib/zstd_errors.h and LICENSE as released, and zstddeclib.c — zstd's own single-file
# decoder, generated from that release's sources with its script:
#
#   python3 build/single_file_libs/combine.py -r lib -x legacy/zstd_legacy.h \
#       -o zstddeclib.c build/single_file_libs/zstddeclib-in.c
#
# (the sources as nixpkgs fetches them: `nix build nixpkgs#zstd.src`, NAR hash
# sha256-tNFWIT9ydfozB8dWcmTMuZLCQmQudTFJIkSr0aG7S44=). Decompression only, no legacy formats, no
# assembly (ZSTD_DISABLE_ASM, zstddeclib-in.c), error strings stripped; no system zstd is needed on
# any platform (Android's and the browser's Qt have none).
#
# C, not C++: the project enables both languages. Warnings are disabled for this third-party code;
# Brulion's -Werror flags never apply to it.
add_library(brulion_zstd STATIC zstddeclib.c zstd.h zstd_errors.h)
set_target_properties(brulion_zstd PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
target_include_directories(brulion_zstd SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(brulion_zstd PRIVATE $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-w> $<$<C_COMPILER_ID:MSVC>:/w>)
