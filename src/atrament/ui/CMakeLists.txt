brulion_add_atrament_module(ui
    SOURCES
        accessibility.cpp accessibility.h
        app.cpp app.h
        atrament.h
        div.cpp div.h
        edgescroll.cpp edgescroll.h
        element.cpp element.h
        events.cpp events.h
        keymap.cpp keymap.h
        list.cpp list.h
        measuredlist.cpp measuredlist.h
        platform.h
        style.cpp style.h
        testplatform.cpp testplatform.h
        text.cpp text.h
        textinput.cpp textinput.h
        touch.cpp touch.h
        window.cpp window.h
    DEPS raster text layout scene base
)
