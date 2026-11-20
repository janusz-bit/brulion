# Tests of the cloudimport module (brulion_core_cloudimport): the OneNote, Todoist, Airtable and
# CSV-attachment downloads against fake servers (no network), their results fed to the io
# module's importers.
brulion_add_test(tst_cloudimport SOURCES tst_cloudimport.cpp LIBS brulion_core_cloudimport brulion_core_io)
