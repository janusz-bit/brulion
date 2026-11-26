# Text (docs/ATRAMENT.md "Text", D3, D7, D8): fonts and fallback, segmentation, shaping with
# HarfBuzz, paragraph layout and the glyph rasterizer. HarfBuzz is its only dependency outside
# Atrament.
brulion_add_atrament_module(text
    SOURCES
        fonts.cpp fonts.h
        segmentation.cpp segmentation.h segmentation_p.h
        textlayout.cpp textlayout.h
        textsystem.cpp textsystem.h
        unicode.cpp unicode.h
    DEPS scene
    LIBS brulion_harfbuzz
)
