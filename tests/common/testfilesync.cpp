// Linked into a test by brulion_add_test() where BRULION_TEST_FILE_SYNC is on (tests/app): the
// vault's own writes sync their data, stamp their folder and rename as in the app, which installs
// the platform's file calls in main() (src/platform/filesync.h) — not QSaveFile's fallback that
// only brulion-cli takes. (tests/core/vault does the same in vaulttest.h.)
#include "filesync.h"

namespace {
[[maybe_unused]] const bool kInstalled = (brulion::platform::installFileSync(), true);
} // namespace
