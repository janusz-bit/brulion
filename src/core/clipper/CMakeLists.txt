# clipper — the Web Clipper's side in Brulion (docs/CLIPPER.md): the native messaging frames the
# browser extension (clipper/) and brulion-cli's host mode exchange, a clip request read and checked
# (page content is hostile: sizes bounded, paths refused), the clip's templates (built-in ones,
# variables, YAML-safe frontmatter), a page's HTML as Markdown with its pictures, the pictures'
# download (limits, http(s) only), and the native messaging host manifests for each browser and
# system. The command line (src/cli/commands_clip.cpp) writes the clip into the notebook.
brulion_add_core_module(clipper
    SOURCES
        clipper.h
        nativemessaging.h nativemessaging.cpp
        request.h request.cpp
        cliptemplate.h cliptemplate.cpp
        article.h article.cpp
        pictures.h pictures.cpp
        nativehost.h nativehost.cpp
        hostinstall.h hostinstall.cpp
    DEPS util text dates markdown io webpicture
    QT Network)
