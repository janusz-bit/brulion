# Tests of the ai module (brulion_core_ai): b.ai's requests, the HTTP requests to Anthropic's and
# OpenAI-compatible APIs, their answers and errors — against a fake transport, no network.
brulion_add_test(tst_ai SOURCES tst_ai.cpp LIBS brulion_core_ai)
