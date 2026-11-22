# A report of an AI response (brulion_core_aireport): the reasons, the report's title and text (the
# user's and the AI's texts fenced literally, the question only when added), the GitHub issue's and
# the email's links (percent-encoding of Polish letters, "+", "&", "#"), long texts cut to fit the
# link (never inside a surrogate pair, said under each), addresses that are no plain email, where
# the response was shown and the model the settings name.
brulion_add_test(tst_aireport SOURCES tst_aireport.cpp LIBS brulion_core_aireport)
