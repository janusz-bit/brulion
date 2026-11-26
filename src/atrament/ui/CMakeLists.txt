brulion_add_atrament_module(ui
    SOURCES
        app.cpp app.h
        atrament.h
        div.cpp div.h
        element.cpp element.h
        events.cpp events.h
        keymap.cpp keymap.h
        list.cpp list.h
        platform.h
        style.cpp style.h
        testplatform.cpp testplatform.h
        text.cpp text.h
        textinput.cpp textinput.h
        window.cpp window.h
    DEPS raster text layout scene base
)
