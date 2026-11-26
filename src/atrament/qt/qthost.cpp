#include "qthost.h"

#include "text/fonts.h"
#include "ui/app.h"
#include "ui/window.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QStyleHints>
#include <QTimer>
#include <QWheelEvent>

namespace brulion::atrament {

namespace {

std::u16string toU16(const QString &s)
{
    return std::u16string(reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size()));
}

QString toQString(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

Modifiers modifiersFromQt(Qt::KeyboardModifiers m)
{
    Modifiers out;
    out.control = m.testFlag(Qt::ControlModifier);
    out.alt = m.testFlag(Qt::AltModifier);
    out.shift = m.testFlag(Qt::ShiftModifier);
    out.platform = m.testFlag(Qt::MetaModifier);
    return out;
}

std::optional<MouseButton> buttonFromQt(Qt::MouseButton b)
{
    switch (b) {
    case Qt::LeftButton:
        return MouseButton::Left;
    case Qt::RightButton:
        return MouseButton::Right;
    case Qt::MiddleButton:
        return MouseButton::Middle;
    case Qt::BackButton:
        return MouseButton::Back;
    case Qt::ForwardButton:
        return MouseButton::Forward;
    default:
        return std::nullopt;
    }
}

PointerKind pointerFromQt(const QPointerEvent *e)
{
    switch (e->pointerType()) {
    case QPointingDevice::PointerType::Finger:
        return PointerKind::Touch;
    case QPointingDevice::PointerType::Pen:
    case QPointingDevice::PointerType::Eraser:
        return PointerKind::Pen;
    default:
        return PointerKind::Mouse;
    }
}

Qt::CursorShape cursorToQt(CursorStyle c)
{
    switch (c) {
    case CursorStyle::Arrow:
        return Qt::ArrowCursor;
    case CursorStyle::IBeam:
        return Qt::IBeamCursor;
    case CursorStyle::PointingHand:
        return Qt::PointingHandCursor;
    case CursorStyle::Crosshair:
        return Qt::CrossCursor;
    case CursorStyle::OpenHand:
        return Qt::OpenHandCursor;
    case CursorStyle::ClosedHand:
        return Qt::ClosedHandCursor;
    case CursorStyle::ResizeLeftRight:
        return Qt::SizeHorCursor;
    case CursorStyle::ResizeUpDown:
        return Qt::SizeVerCursor;
    case CursorStyle::ResizeColumn:
        return Qt::SplitHCursor;
    case CursorStyle::ResizeRow:
        return Qt::SplitVCursor;
    case CursorStyle::NotAllowed:
        return Qt::ForbiddenCursor;
    case CursorStyle::Wait:
        return Qt::WaitCursor;
    case CursorStyle::None:
        return Qt::BlankCursor;
    }
    return Qt::ArrowCursor;
}

struct KeyName {
    int key;
    const char16_t *name;
};
constexpr KeyName kKeyNames[] = {
    { Qt::Key_Return, u"enter" },
    { Qt::Key_Enter, u"enter" },
    { Qt::Key_Escape, u"escape" },
    { Qt::Key_Tab, u"tab" },
    { Qt::Key_Backtab, u"tab" },
    { Qt::Key_Backspace, u"backspace" },
    { Qt::Key_Delete, u"delete" },
    { Qt::Key_Insert, u"insert" },
    { Qt::Key_Home, u"home" },
    { Qt::Key_End, u"end" },
    { Qt::Key_PageUp, u"pageup" },
    { Qt::Key_PageDown, u"pagedown" },
    { Qt::Key_Left, u"left" },
    { Qt::Key_Right, u"right" },
    { Qt::Key_Up, u"up" },
    { Qt::Key_Down, u"down" },
    { Qt::Key_Space, u"space" },
    { Qt::Key_Menu, u"menu" },
    { Qt::Key_Back, u"back" },
};

} // namespace

Keystroke keystrokeFromQt(int key, Qt::KeyboardModifiers modifiers, const QString &text)
{
    Keystroke k;
    k.modifiers = modifiersFromQt(modifiers);
    if (key == Qt::Key_Backtab)
        k.modifiers.shift = true;
    for (const KeyName &n : kKeyNames) {
        if (n.key == key) {
            k.key = n.name;
            break;
        }
    }
    if (k.key.empty() && key >= Qt::Key_F1 && key <= Qt::Key_F35)
        k.key = u"f" + toU16(QString::number(key - Qt::Key_F1 + 1));
    if (k.key.empty() && key >= Qt::Key_A && key <= Qt::Key_Z)
        k.key = std::u16string(1, char16_t(u'a' + (key - Qt::Key_A)));
    if (k.key.empty() && key > 0 && key < 0x110000 && QChar::isPrint(char32_t(key)))
        k.key = toU16(QString(QChar::fromUcs4(char32_t(key))).toLower());
    // Text that the key types: never control characters, never with Ctrl or Super.
    if (!text.isEmpty() && !k.modifiers.control && !k.modifiers.platform) {
        bool printable = true;
        for (QChar c : text)
            printable = printable && (c.isPrint() || c.isSurrogate());
        if (printable)
            k.keyChar = toU16(text);
    }
    if (k.key == u"space" && k.keyChar.empty() && !k.modifiers.control && !k.modifiers.platform)
        k.keyChar = u" ";
    return k;
}

// --- QtPlatform

QtPlatform::QtPlatform()
    : m_context(new QObject)
{
}

QtPlatform::~QtPlatform()
{
    delete m_context; // its timers with it
}

std::unique_ptr<PlatformWindow> QtPlatform::openWindow(const WindowOptions &)
{
    return nullptr; // hosted: the item is the window
}

void QtPlatform::post(std::function<void()> task)
{
    QMetaObject::invokeMethod(m_context, std::move(task), Qt::QueuedConnection);
}

TimerId QtPlatform::postDelayed(std::chrono::milliseconds delay, std::function<void()> task)
{
    const TimerId id = m_nextTimer++;
    auto *timer = new QTimer(m_context);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    QObject::connect(timer, &QTimer::timeout, m_context, [this, id, timer, task = std::move(task)] {
        m_timers.erase(id);
        timer->deleteLater();
        task();
    });
    m_timers[id] = timer;
    timer->start(delay);
    return id;
}

void QtPlatform::cancel(TimerId id)
{
    auto it = m_timers.find(id);
    if (it == m_timers.end())
        return;
    if (it->second) {
        it->second->stop();
        it->second->deleteLater();
    }
    m_timers.erase(it);
}

void QtPlatform::writeClipboard(std::u16string_view text)
{
    if (QClipboard *c = QGuiApplication::clipboard())
        c->setText(toQString(text));
}

std::optional<std::u16string> QtPlatform::readClipboard()
{
    QClipboard *c = QGuiApplication::clipboard();
    if (!c)
        return std::nullopt;
    const QString text = c->text();
    if (text.isNull())
        return std::nullopt;
    return toU16(text);
}

std::chrono::milliseconds QtPlatform::doubleClickInterval() const
{
    return std::chrono::milliseconds(QGuiApplication::styleHints()->mouseDoubleClickInterval());
}

int addQtFonts(FontCollection &fonts, const QStringList &paths)
{
    int added = 0;
    for (const QString &path : paths) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = f.readAll();
        auto data = std::make_shared<std::vector<std::uint8_t>>(bytes.begin(), bytes.end());
        added += int(fonts.addFontData(std::move(data)).size());
    }
    return added;
}

// --- AtramentItem

AtramentItem::AtramentItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setFlag(ItemIsFocusScope);
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::AllButtons);
    setActiveFocusOnTab(true);
}

AtramentItem::~AtramentItem()
{
    if (m_app && m_window)
        m_app->closeWindow(*m_window);
}

void AtramentItem::attach(App &app, const AnyView &root)
{
    m_app = &app;
    m_window = &app.openWindow(*this, root);
}

Size AtramentItem::contentSize() const
{
    return { float(width()), float(height()) };
}

float AtramentItem::scaleFactor() const
{
    return window() ? float(window()->effectiveDevicePixelRatio()) : 1.0f;
}

Modifiers AtramentItem::modifiers() const
{
    return modifiersFromQt(QGuiApplication::queryKeyboardModifiers());
}

bool AtramentItem::isActive() const
{
    return window() && window()->isActive();
}

void AtramentItem::requestFrame()
{
    // The frame is built in updatePolish, on this (the GUI) thread, before the scene graph syncs.
    polish();
}

void AtramentItem::updatePolish()
{
    if (m_callbacks.frame)
        m_callbacks.frame();
}

void AtramentItem::present(const Scene &scene, const Atlas &atlas)
{
    const float scale = scaleFactor();
    const int w = std::max(1, int(std::ceil(width() * scale)));
    const int h = std::max(1, int(std::ceil(height() * scale)));
    if (m_target.width != w || m_target.height != h)
        m_target.resize(w, h);
    m_renderer.render(scene, atlas, m_target, transparent());
    // The pixels are RGBA8 premultiplied in memory order, as QImage's RGBA8888_Premultiplied.
    m_image = QImage(reinterpret_cast<const uchar *>(m_target.pixels.data()), w, h, w * 4,
            QImage::Format_RGBA8888_Premultiplied)
                      .copy();
    m_image.setDevicePixelRatio(scale);
    m_imageDirty = true;
    update();
}

QSGNode *AtramentItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    if (m_image.isNull() || !window()) {
        delete old;
        return nullptr;
    }
    auto *node = static_cast<QSGImageNode *>(old);
    if (!node) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
    }
    if (m_imageDirty || !node->texture()) {
        node->setTexture(window()->createTextureFromImage(m_image, QQuickWindow::TextureHasAlphaChannel));
        m_imageDirty = false;
    }
    node->setRect(QRectF(0, 0, width(), height()));
    node->setFiltering(QSGTexture::Nearest);
    return node;
}

void AtramentItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size() && m_callbacks.resized)
        m_callbacks.resized();
}

void AtramentItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if ((change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange) && m_callbacks.resized)
        m_callbacks.resized();
}

void AtramentItem::setCursor(CursorStyle cursor)
{
    QQuickItem::setCursor(QCursor(cursorToQt(cursor)));
}

void AtramentItem::inputStateChanged()
{
    InputHandler *handler = m_callbacks.inputHandler ? m_callbacks.inputHandler() : nullptr;
    setFlag(ItemAcceptsInputMethod, handler != nullptr);
    if (hasActiveFocus())
        QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
}

bool AtramentItem::send(const PlatformInput &input)
{
    return m_callbacks.input ? m_callbacks.input(input) : false;
}

int AtramentItem::clickCountFor(Point p, MouseButton button)
{
    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(QGuiApplication::styleHints()->mouseDoubleClickInterval());
    const float distance = std::abs(p.x - m_lastClickPos.x) + std::abs(p.y - m_lastClickPos.y);
    if (button == m_lastClickButton && now - m_lastClick <= interval && distance < 6)
        ++m_clickCount;
    else
        m_clickCount = 1;
    m_lastClick = now;
    m_lastClickPos = p;
    m_lastClickButton = button;
    return m_clickCount;
}

void AtramentItem::mousePressEvent(QMouseEvent *event)
{
    const auto button = buttonFromQt(event->button());
    if (!button) {
        event->ignore();
        return;
    }
    if (!hasActiveFocus())
        forceActiveFocus(Qt::MouseFocusReason);
    MouseDownEvent e;
    e.button = *button;
    e.position = { float(event->position().x()), float(event->position().y()) };
    e.modifiers = modifiersFromQt(event->modifiers());
    e.clickCount = clickCountFor(e.position, *button);
    e.pointer = pointerFromQt(event);
    m_mouse = e.position;
    send(e);
    event->accept(); // keep the grab, so the release comes here
}

void AtramentItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    // clickCountFor() counts from the presses; Qt's double-click event is a second press.
    mousePressEvent(event);
}

void AtramentItem::mouseReleaseEvent(QMouseEvent *event)
{
    const auto button = buttonFromQt(event->button());
    if (!button)
        return;
    MouseUpEvent e;
    e.button = *button;
    e.position = { float(event->position().x()), float(event->position().y()) };
    e.modifiers = modifiersFromQt(event->modifiers());
    e.clickCount = m_clickCount;
    e.pointer = pointerFromQt(event);
    m_mouse = e.position;
    send(e);
}

void AtramentItem::mouseMoveEvent(QMouseEvent *event)
{
    MouseMoveEvent e;
    e.position = { float(event->position().x()), float(event->position().y()) };
    e.modifiers = modifiersFromQt(event->modifiers());
    if (event->buttons() & Qt::LeftButton)
        e.pressedButton = MouseButton::Left;
    else if (event->buttons() & Qt::RightButton)
        e.pressedButton = MouseButton::Right;
    e.pointer = pointerFromQt(event);
    m_mouse = e.position;
    send(e);
}

void AtramentItem::hoverMoveEvent(QHoverEvent *event)
{
    MouseMoveEvent e;
    e.position = { float(event->position().x()), float(event->position().y()) };
    e.modifiers = modifiersFromQt(event->modifiers());
    m_mouse = e.position;
    send(e);
}

void AtramentItem::hoverLeaveEvent(QHoverEvent *event)
{
    MouseExitEvent e;
    e.position = { float(event->position().x()), float(event->position().y()) };
    m_mouse = { -1e9f, -1e9f };
    send(e);
}

void AtramentItem::wheelEvent(QWheelEvent *event)
{
    ScrollWheelEvent e;
    e.position = { float(event->position().x()), float(event->position().y()) };
    e.modifiers = modifiersFromQt(event->modifiers());
    const QPoint pixels = event->pixelDelta();
    if (!pixels.isNull()) {
        e.precise = true;
        e.delta = { -float(pixels.x()), -float(pixels.y()) };
    } else {
        // 120 per notch; three lines of about 16 px each, as most desktops scroll.
        const QPoint angle = event->angleDelta();
        e.delta = { -float(angle.x()) / 120.0f * 48.0f, -float(angle.y()) / 120.0f * 48.0f };
    }
    switch (event->phase()) {
    case Qt::ScrollBegin:
        e.phase = TouchPhase::Started;
        break;
    case Qt::ScrollEnd:
        e.phase = TouchPhase::Ended;
        break;
    default:
        e.phase = TouchPhase::Moved;
        break;
    }
    if (send(e))
        event->accept();
    else
        event->ignore();
}

void AtramentItem::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Control || event->key() == Qt::Key_Alt
            || event->key() == Qt::Key_Meta) {
        send(ModifiersChangedEvent { modifiersFromQt(event->modifiers()) });
        event->ignore();
        return;
    }
    KeyDownEvent e;
    e.keystroke = keystrokeFromQt(event->key(), event->modifiers(), event->text());
    e.isRepeat = event->isAutoRepeat();
    if (send(e))
        event->accept();
    else
        event->ignore();
}

void AtramentItem::keyReleaseEvent(QKeyEvent *event)
{
    KeyUpEvent e;
    e.keystroke = keystrokeFromQt(event->key(), event->modifiers(), event->text());
    send(e);
}

void AtramentItem::inputMethodEvent(QInputMethodEvent *event)
{
    InputHandler *h = m_callbacks.inputHandler ? m_callbacks.inputHandler() : nullptr;
    if (!h) {
        event->ignore();
        return;
    }
    m_app->update([&] {
        // Qt: the commit replaces the preedit (or the selection), the replacement range is
        // relative to the caret; then the preedit becomes the new marked text.
        const std::optional<Range> marked = h->markedRange();
        if (!event->commitString().isEmpty() || event->replacementLength() > 0) {
            std::optional<Range> range = marked;
            if (event->replacementLength() > 0 || event->replacementStart() != 0) {
                const auto sel = h->selection();
                const int caret = sel ? sel->focus : 0;
                const int start = std::max(0, caret + event->replacementStart());
                range = Range { start, std::min(h->textLength(), start + event->replacementLength()) };
            }
            h->replaceText(range, toU16(event->commitString()));
        }
        const QString preedit = event->preeditString();
        if (!preedit.isEmpty()) {
            std::optional<Range> cursor;
            for (const QInputMethodEvent::Attribute &a : event->attributes()) {
                if (a.type == QInputMethodEvent::Cursor)
                    cursor = Range { a.start, a.start };
            }
            h->replaceAndMarkText(std::nullopt, toU16(preedit), cursor);
        } else if (h->markedRange() && event->commitString().isEmpty()) {
            h->replaceText(std::nullopt, u""); // the composition was cancelled
        }
    });
    event->accept();
}

QVariant AtramentItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    InputHandler *h = m_callbacks.inputHandler ? m_callbacks.inputHandler() : nullptr;
    if (query == Qt::ImEnabled)
        return h != nullptr;
    if (!h)
        return QQuickItem::inputMethodQuery(query);
    const auto sel = h->selection();
    const int focus = sel ? sel->focus : 0;
    const int anchor = sel ? sel->anchor : 0;
    switch (query) {
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle: {
        const int at = query == Qt::ImCursorRectangle ? focus : anchor;
        const std::optional<Rect> r = h->boundsForRange({ at, at });
        return r ? QRectF(r->left(), r->top(), std::max(1.0f, r->width()), r->height()) : QRectF();
    }
    case Qt::ImSurroundingText:
        return toQString(h->textForRange({ 0, h->textLength() }));
    case Qt::ImCursorPosition:
        return focus;
    case Qt::ImAnchorPosition:
        return anchor;
    case Qt::ImCurrentSelection:
        return sel ? toQString(h->textForRange(sel->range())) : QString();
    case Qt::ImTextBeforeCursor:
        return toQString(h->textForRange({ std::max(0, focus - 256), focus }));
    case Qt::ImTextAfterCursor:
        return toQString(h->textForRange({ focus, std::min(h->textLength(), focus + 256) }));
    case Qt::ImHints: {
        Qt::InputMethodHints hints = Qt::ImhNone;
        switch (h->purpose()) {
        case InputHandler::Purpose::Url:
            hints = Qt::ImhUrlCharactersOnly;
            break;
        case InputHandler::Purpose::Email:
            hints = Qt::ImhEmailCharactersOnly;
            break;
        case InputHandler::Purpose::Number:
            hints = Qt::ImhFormattedNumbersOnly;
            break;
        case InputHandler::Purpose::Password:
            hints = Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText;
            break;
        case InputHandler::Purpose::Multiline:
            hints = Qt::ImhMultiLine;
            break;
        case InputHandler::Purpose::Text:
            break;
        }
        return int(hints);
    }
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void AtramentItem::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    inputStateChanged();
    if (m_callbacks.activeChanged)
        m_callbacks.activeChanged(true);
}

void AtramentItem::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    if (m_callbacks.activeChanged)
        m_callbacks.activeChanged(false);
}

} // namespace brulion::atrament
