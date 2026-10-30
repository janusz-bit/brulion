#include "devtools.h"

#include "dateshelper.h"
#include "layoutdump.h"
#include "scrollprobe.h"
#include "themepalette.h"

#include <QCoreApplication>
#include <QImage>
#include <QJSEngine>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>
#include <QTranslator>

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

DevTools *g_instance = nullptr;

QString valueAfter(const QStringList &args, const QString &option)
{
    const qsizetype i = args.indexOf(option);
    if (i >= 0 && i + 1 < args.size())
        return args.at(i + 1);
    // Also accept --option=value.
    const QString prefix = option + u'=';
    for (const QString &arg : args) {
        if (arg.startsWith(prefix))
            return arg.mid(prefix.size());
    }
    return {};
}

/// Number, size and text formatting (QLocale(), Qt.locale()) follows the UI language, not the
/// system locale: "1,5 MB" and "1 234" in Polish, "1.5 MB" in English.
void applyDefaultLocale(const QString &language)
{
    QLocale::setDefault(language == u"en" ? QLocale(QLocale::English, QLocale::UnitedStates)
                                          : QLocale(QLocale::Polish, QLocale::Poland));
}

} // namespace

void DevTools::applyEarlyArguments(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char *value = nullptr;
        if (std::strcmp(arg, "--open") == 0)
            break; // files from here on (main.cpp, notebook::splitOpenArguments)
        if (std::strcmp(arg, "--scale") == 0 && i + 1 < argc)
            value = argv[i + 1];
        else if (std::strncmp(arg, "--scale=", 8) == 0)
            value = arg + 8;
        if (value) {
            const QByteArray factor(value);
            bool ok = false;
            const double f = factor.toDouble(&ok);
            if (ok && f >= 0.5 && f <= 6.0)
                qputenv("QT_SCALE_FACTOR", factor);
            else
                std::fprintf(stderr, "brulion: ignoring invalid --scale %s\n", value);
        }
    }
}

DevTools::DevTools(const QStringList &arguments, QObject *parent) : QObject(parent)
{
    m_gallery = arguments.contains(u"--gallery"_s);
    m_section = valueAfter(arguments, u"--section"_s);
    m_editor = arguments.contains(u"--editor"_s);
    m_editorScript = valueAfter(arguments, u"--editor-script"_s);
    m_screenshot = valueAfter(arguments, u"--screenshot"_s);
    m_dumpLayout = valueAfter(arguments, u"--dump-layout"_s);
    m_touch = arguments.contains(u"--touch"_s);
    m_reducedMotion = arguments.contains(u"--reduced-motion"_s);
    m_testSlowdown = std::max(1, qEnvironmentVariableIntValue("BRULION_TEST_SLOWDOWN"));
    if (std::any_of(arguments.cbegin(), arguments.cend(),
                    [](const QString &arg) { return arg.startsWith(u"--scroll-probe"_s); }))
        m_probeArguments = arguments;

    const QString theme = valueAfter(arguments, u"--theme"_s);
    if (theme == u"light" || theme == u"dark")
        m_theme = theme;
    const QString accent = valueAfter(arguments, u"--accent"_s).trimmed().toLower();
    if (accentNames().contains(accent)) {
        m_accentName = accent;
    } else if (const auto color = parseColor(accent)) {
        m_accentName = u"custom"_s;
        m_customAccent = toHex(*color);
    }
    const QString surface = valueAfter(arguments, u"--surface"_s);
    if (surfaceNames(false).contains(surface))
        m_lightSurface = surface;
    else if (surfaceNames(true).contains(surface))
        m_darkSurface = surface;
    const QString density = valueAfter(arguments, u"--density"_s);
    if (density == u"compact" || density == u"comfortable")
        m_density = density;

    const QString locale = valueAfter(arguments, u"--locale"_s);
    if (locale == u"pl" || locale == u"en")
        m_language = locale;
    else
        m_language = QLocale().name().startsWith(u"en") ? u"en"_s : u"pl"_s;
    applyDefaultLocale(m_language);

    bool ok = false;
    const int delay = valueAfter(arguments, u"--delay"_s).toInt(&ok);
    if (ok && delay >= 0)
        m_delayMs = delay;
    const int keyboard = valueAfter(arguments, u"--keyboard"_s).toInt(&ok);
    if (ok && keyboard >= 0 && keyboard <= 4000)
        m_keyboard = keyboard;

    static const QRegularExpression sizeRe(u"^(\\d{2,5})x(\\d{2,5})$"_s);
    const auto m = sizeRe.match(valueAfter(arguments, u"--size"_s));
    if (m.hasMatch())
        m_size = QSize(m.captured(1).toInt(), m.captured(2).toInt());

    DatesHelper::setDefaults(m_language, valueAfter(arguments, u"--now"_s));
    if (!g_instance)
        g_instance = this;
}

DevTools::~DevTools()
{
    if (g_instance == this)
        g_instance = nullptr;
}

DevTools *DevTools::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine);
    if (g_instance) {
        QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
        return g_instance;
    }
    // No instance from main() (e.g. QML tooling): defaults, owned by this engine only.
    auto *tools = new DevTools(QStringList{});
    g_instance = nullptr;
    QJSEngine::setObjectOwnership(tools, QJSEngine::JavaScriptOwnership);
    Q_UNUSED(jsEngine);
    return tools;
}

void DevTools::setTranslator(QTranslator *translator)
{
    m_translator = translator;
    m_translatorInstalled = translator && !translator->isEmpty();
    m_qtTranslations.setLanguage(m_language);
}

QString DevTools::rootComponent() const
{
    if (m_editor)
        return u"EditorPlayground"_s;
    return m_gallery ? u"Gallery"_s : u"Main"_s;
}

void DevTools::attach(QQmlApplicationEngine *engine)
{
    m_engine = engine;
    connect(engine, &QQmlApplicationEngine::objectCreated, this, [this](QObject *object, const QUrl &) {
        auto *window = qobject_cast<QQuickWindow *>(object);
        if (!window)
            return;
        if (m_size.isValid())
            window->resize(m_size);
        if (!m_screenshot.isEmpty()) {
            // In front, as a user sees it: an X server without a window manager (Xvfb) never
            // activates a window by itself, and an inactive app pauses the graph's layout.
            window->requestActivate();
            QTimer::singleShot(m_delayMs, this, [this, object] { takeScreenshot(object); });
        }
    });
    // --scroll-probe: frame-by-frame measurements of scrolling (scrollprobe.h).
    startScrollProbe(engine, m_probeArguments);
}

void DevTools::takeScreenshot(QObject *root)
{
    auto *window = qobject_cast<QQuickWindow *>(root);
    if (!window) {
        std::fprintf(stderr, "brulion: --screenshot: no window\n");
        QCoreApplication::exit(1);
        return;
    }
    const QImage image = window->grabWindow();
    if (image.isNull() || !image.save(m_screenshot)) {
        std::fprintf(stderr, "brulion: --screenshot: could not save %s\n", qPrintable(m_screenshot));
        QCoreApplication::exit(1);
        return;
    }
    // The layout of the same frame (the UI benchmark, layoutdump.h).
    if (!m_dumpLayout.isEmpty()) {
        const QObject *theme = m_engine ? m_engine->singletonInstance<QObject *>("Brulion", "Theme") : nullptr;
        QString error;
        if (!writeLayoutDump(window, m_dumpLayout, theme, &error)) {
            std::fprintf(stderr, "brulion: --dump-layout: %s\n", qPrintable(error));
            QCoreApplication::exit(1);
            return;
        }
    }
    std::fprintf(stderr, "brulion: screenshot %s (%dx%d)\n", qPrintable(m_screenshot), image.width(), image.height());
    QCoreApplication::exit(0);
}

void DevTools::setLanguage(const QString &language)
{
    const QString lang = language == u"en" ? u"en"_s : u"pl"_s;
    if (lang == m_language)
        return;
    m_language = lang;
    applyDefaultLocale(lang);
    if (m_translator) {
        // brulion_pl.qm (translation) or brulion_en.qm (plural forms of the English sources).
        if (m_translatorInstalled) {
            QCoreApplication::removeTranslator(m_translator);
            m_translatorInstalled = false;
        }
        if (m_translator->load(u":/i18n/brulion_"_s + lang + u".qm"_s))
            m_translatorInstalled = QCoreApplication::installTranslator(m_translator);
    }
    m_qtTranslations.setLanguage(lang); // "Anuluj", "Otwórz" of Qt's own dialogs and buttons
    if (m_engine)
        m_engine->retranslate();
    Q_EMIT languageChanged();
}

} // namespace brulion::ui
