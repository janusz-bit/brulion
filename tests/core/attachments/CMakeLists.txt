# Tests of the attachments module (brulion_core_attachments): records, the manifest, the attachment
# search and the search field's filters.
brulion_add_test(tst_attachments SOURCES tst_attachments.cpp LIBS brulion_core_attachments)
# The text of DOCX, PPTX, ODT and EPUB (documenttext.h): fixtures built in the test, hostile files.
brulion_add_test(tst_documenttext SOURCES tst_documenttext.cpp LIBS brulion_core_attachments brulion_core_io)
