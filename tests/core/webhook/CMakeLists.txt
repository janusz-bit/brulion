# Webhooks (brulion_core_webhook): tokens (shape, randomness, constant-time comparison, the one
# found among many), paths with a proxy's prefix and malformed ones, bodies (JSON, text, forms,
# empty, Polish text, cut at a character, too large, not UTF-8, another charset, binary), the
# message with its fence and its sentence in both languages, the rate limits.
brulion_add_test(tst_webhook SOURCES tst_webhook.cpp LIBS brulion_core_webhook)
