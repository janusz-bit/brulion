# Qt6::Gui only for QImage: a failing test writes its frame as a PNG (the renderer never sees Qt).
brulion_add_test(tst_atrament_raster SOURCES tst_atrament_raster.cpp LIBS brulion_atrament_raster Qt6::Gui)
