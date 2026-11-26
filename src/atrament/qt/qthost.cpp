#include "qthost.h"

#include "qrhirenderer.h"
#include "qtaccessibility.h"
#include "text/fonts.h"
#include "ui/app.h"
#include "ui/window.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRenderNode>
#include <QSGRendererInterface>
#include <QStyleHints>
#include <QTimer>
#include <QWheelEvent>
#include <rhi/qrhi.h>

namespace brulion::atrament {

namespace {

// The GPU renderer inside Qt Quick's render pass: the frame and the atlas pages come in the sync
// (updatePaintNode, the GUI thread waiting), uploads are recorded in prepare() before the pass,
// the draws in render(). The node is the item's size and draws nothing outside it; it uses only
// QRhi (no native API calls) and no depth buffer — Qt Quick then draws the window back to front.
class AtramentRenderNode final : public QSGRenderNode {
public:
    template<typename Shared>
    explicit AtramentRenderNode(std::shared_ptr<Shared> shared)
        : m_failed(std::shared_ptr<std::atomic<bool>>(shared, &shared->failed))
        , m_lostTextures(std::shared_ptr<std::atomic<bool>>(shared, &shared->lostTextures))
    { }

    QRhiRenderer renderer;
    QRectF bounds;
    float scale = 1;

    void prepare() override
    {
        QRhiRenderTarget *rt = renderTarget();
        QRhiCommandBuffer *cb = commandBuffer();
        m_ready = false;
        if (!rt || !cb)
            return;
        // The frame's device pixels → the item's logical pixels → Qt Quick's clip space.
        QMatrix4x4 transform = *projectionMatrix() * *matrix();
        transform.scale(1 / scale, 1 / scale);
        m_ready = renderer.prepare(rt->rhi(), cb, rt->renderPassDescriptor(), rt->sampleCount(), transform,
                                   float(inheritedOpacity()));
        if (!m_ready)
            m_failed->store(true);
    }

    void render(const RenderState *state) override
    {
        if (!m_ready)
            return;
        std::optional<std::array<int, 4>> scissor;
        if (state->scissorEnabled()) {
            const QRect r = state->scissorRect();
            scissor = std::array<int, 4>{r.x(), r.y(), r.width(), r.height()};
        }
        renderer.render(commandBuffer(), renderTarget()->pixelSize(), scissor);
    }

    void releaseResources() override
    {
        renderer.releaseResources();
        m_lostTextures->store(true);
    }

    StateFlags changedStates() const override { return ViewportState | ScissorState | BlendState; }
    RenderingFlags flags() const override { return BoundedRectRendering | NoExternalRendering; }
    QRectF rect() const override { return bounds; }

private:
    std::shared_ptr<std::atomic<bool>> m_failed;
    std::shared_ptr<std::atomic<bool>> m_lostTextures;
    bool m_ready = false;
};

std::u16string toU16(const QString &s)
{
    return std::u16string(reinterpret_cast<const char16_t *>(s.utf16()), std::size_t(s.size()));
}

QString toQString(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

// A field as Qt's input methods see it (qthost.h): the text without the composition, the caret
// and anchor where the composition is (it sits at the caret), offsets in that text.
struct QtField {
    std::u16string text;
    int cursor = 0;
    int anchor = 0;
    bool composing = false;
};

QtField qtField(InputHandler &h)
{
    QtField f;
    f.text = h.textForRange({0, h.textLength()});
    const InputHandler::Selection sel = h.selection().value_or(InputHandler::Selection{});
    const std::optional<Range> marked = h.markedRange();
    if (marked && !marked->isEmpty()) {
        const int n = int(f.text.size());
        const int a = std::clamp(marked->start, 0, n);
        const int b = std::clamp(marked->end, a, n);
        f.text.erase(std::size_t(a), std::size_t(b - a));
        f.cursor = f.anchor = a;
        f.composing = true;
    } else {
        f.cursor = sel.focus;
        f.anchor = sel.anchor;
    }
    return f;
}

// A QInputMethodEvent applied as Qt's own text controls apply it (QQuickTextInput): the preedit
// they show is not in their text, so the old one goes first; the commit replaces the range at
// replacementStart / replacementLength around the caret (or the selection, which typing deletes);
// a Selection attribute then sets the caret and anchor; the new preedit is shown at the caret,
// with the Cursor attribute's position inside it. Here the preedit is the field's marked text,
// which is part of its text: the common cases map onto one call each (so that a composition stays
// one undo step), the rest takes the composition out first.
void applyInputMethodEvent(InputHandler &h, const QInputMethodEvent &e)
{
    const std::u16string commit = toU16(e.commitString());
    const std::u16string preedit = toU16(e.preeditString());
    const int rs = e.replacementStart();
    const int rl = std::max(0, e.replacementLength());
    std::optional<Range> preeditCursor;
    std::optional<InputHandler::Selection> selection;
    for (const QInputMethodEvent::Attribute &a : e.attributes()) {
        if (a.type == QInputMethodEvent::Cursor) {
            const int at = std::clamp(a.start, 0, int(preedit.size()));
            preeditCursor = Range{at, at};
        } else if (a.type == QInputMethodEvent::Selection) {
            selection = InputHandler::Selection{a.start, a.start + a.length};
        }
    }
    const bool replaces = !commit.empty() || rl > 0;
    bool committed = false;
    if (h.markedRange()) {
        if (!replaces && !selection && !preedit.empty()) {
            // The composition changes (typing in it, moving its caret).
            h.replaceAndMarkText(std::nullopt, preedit, preeditCursor);
            return;
        }
        if (!commit.empty() && rl == 0 && rs == 0) {
            // Committed where it was.
            h.replaceText(std::nullopt, commit);
            committed = true;
        } else {
            // Qt's text never had it: out it goes (with its undo step), the rest acts on that text.
            h.replaceAndMarkText(std::nullopt, u"", std::nullopt);
        }
    }
    if (replaces && !committed) {
        const InputHandler::Selection sel = h.selection().value_or(InputHandler::Selection{});
        const Range selected = sel.range();
        if (!selected.isEmpty() && rl == 0 && rs == 0) {
            h.replaceText(selected, commit); // typing over a selection
        } else {
            int caret = sel.focus;
            if (!selected.isEmpty()) {
                h.replaceText(selected, u"");
                caret = selected.start;
            }
            const int now = h.textLength();
            const int start = std::clamp(caret + rs, 0, now);
            const Range range{start, std::clamp(start + rl, start, now)};
            if (commit.empty() && !preedit.empty() && !selection) {
                // A range of the text becomes the composition (Android's setComposingRegion, then
                // the composing text), in one step.
                h.replaceAndMarkText(range, preedit, preeditCursor);
                return;
            }
            h.replaceText(range, commit);
            // Where Qt leaves the caret: after what was committed, else where it was less what
            // went before it.
            const int qtCaret = std::clamp(!commit.empty() ? start + int(commit.size())
                                           : rs <= 0       ? caret - std::min(-rs, rl)
                                                           : caret,
                                           0, h.textLength());
            const auto after = h.selection();
            if (after && (after->focus != qtCaret || after->anchor != qtCaret))
                h.setSelection({qtCaret, qtCaret});
        }
    }
    if (selection) {
        const int n = h.textLength();
        h.setSelection({std::clamp(selection->anchor, 0, n), std::clamp(selection->focus, 0, n)});
    }
    if (!preedit.empty())
        h.replaceAndMarkText(std::nullopt, preedit, preeditCursor);
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
    m_accessibility = std::make_unique<AtramentAccessibility>(this);
}

AtramentItem::~AtramentItem()
{
    m_accessibility.reset(); // its interfaces ask the window
    if (m_app && m_window)
        m_app->closeWindow(*m_window);
}

void AtramentItem::attach(App &app, const AnyView &root)
{
    m_app = &app;
    m_window = &app.openWindow(*this, root);
    m_accessibility->setWindow(m_window);
}

void AtramentItem::accessibilityChanged(const std::vector<AccessibilityEvent> &events)
{
    if (m_accessibility)
        m_accessibility->treeChanged(events);
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

bool AtramentItem::gpuAvailable() const
{
    static const bool shaders = atramentGpuShadersBuilt();
    if (!shaders || !atramentGpuRendererWanted() || m_gpuShared->failed.load())
        return false;
    QQuickWindow *w = window();
    return w && QSGRendererInterface::isApiRhiBased(w->rendererInterface()->graphicsApi());
}

void AtramentItem::checkGpuRenderer()
{
    if (!m_gpu)
        return;
    if (m_gpuShared->failed.load()) {
        qWarning("Atrament: the GPU renderer cannot draw here; the CPU renderer takes over");
        requestFrame();
    } else if (m_gpuShared->lostTextures.exchange(false)) {
        update(); // a sync, which copies the atlas pages again
    }
}

void AtramentItem::present(const Scene &scene, Atlas &atlas)
{
    const float scale = scaleFactor();
    const int w = std::max(1, int(std::ceil(width() * scale)));
    const int h = std::max(1, int(std::ceil(height() * scale)));
    m_gpu = gpuAvailable();
    if (m_gpu) {
        // Built here, from the scene this thread owns; the render node takes it in the next sync.
        m_gpuFrame = std::make_shared<const GpuFrame>(m_gpuBuilder.build(scene, atlas, w, h));
        m_gpuFrameNew = true;
        m_atlas = &atlas;
        if (!m_image.isNull()) {
            m_image = QImage();
            m_target = RasterTarget{};
        }
        update();
        return;
    }
    m_gpuFrame.reset();
    m_atlas = nullptr;
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
    if (!window()) {
        delete old;
        return nullptr;
    }
    if (m_gpu && m_gpuFrame) {
        auto *node = old && old->type() == QSGNode::RenderNodeType ? static_cast<AtramentRenderNode *>(old) : nullptr;
        if (!node) {
            delete old;
            node = new AtramentRenderNode(m_gpuShared);
            m_gpuFrameNew = true;
        }
        if (m_gpuFrameNew) {
            node->renderer.setFrame(m_gpuFrame);
            m_gpuFrameNew = false;
        }
        // The GUI thread waits: the atlases can be read (and marked clean) here.
        if (m_atlas)
            node->renderer.syncAtlas(QRhiRenderer::AtlasSlot::Atlas, *m_atlas);
        node->renderer.syncAtlas(QRhiRenderer::AtlasSlot::Paths, m_gpuBuilder.pathAtlas());
        node->bounds = QRectF(0, 0, width(), height());
        node->scale = scaleFactor();
        node->markDirty(QSGNode::DirtyMaterial);
        return node;
    }
    if (m_image.isNull()) {
        delete old;
        return nullptr;
    }
    if (old && old->type() == QSGNode::RenderNodeType) {
        delete old;
        old = nullptr;
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
    if (change == ItemSceneChange) {
        QObject::disconnect(m_frameSwapped);
        if (value.window)
            m_frameSwapped = connect(value.window, &QQuickWindow::frameSwapped, this, &AtramentItem::checkGpuRenderer,
                                     Qt::QueuedConnection);
    }
    if ((change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange) && m_callbacks.resized)
        m_callbacks.resized();
}

void AtramentItem::setCursor(CursorStyle cursor)
{
    QQuickItem::setCursor(QCursor(cursorToQt(cursor)));
}

InputHandler *AtramentItem::inputHandler() const
{
    return m_callbacks.inputHandler ? m_callbacks.inputHandler() : nullptr;
}

void AtramentItem::inputStateChanged()
{
    // Between a change of focus and its frame the new element's handler is not there yet: the
    // frame asks again (no keyboard hidden and shown again for a move from field to field).
    if (m_window && !m_window->focusDrawn())
        return;
    InputHandler *h = inputHandler();
    setFlag(ItemAcceptsInputMethod, h != nullptr);
    const std::uint64_t field = h && m_window ? m_window->focusedId() : 0;
    if (!hasActiveFocus()) {
        // Qt's focus is elsewhere: the input method follows that item; this one starts afresh.
        m_imeFocus = field;
        m_imeState = {};
        m_imeComposing = false;
        return;
    }
    QInputMethod *im = QGuiApplication::inputMethod();
    ImeState state;
    if (h) {
        state.text = h->textForRange({0, h->textLength()});
        state.selection = h->selection().value_or(InputHandler::Selection{});
        state.marked = h->markedRange();
        state.bounds = h->inputBounds();
    }
    // A composition that ended without the input method (the field lost the focus, a press in it,
    // undo): it forgets its own, which must not reach the text again.
    const bool forget = field != m_imeFocus || (m_imeComposing && !state.marked);
    if (forget) {
        m_imeForgetting = true;
        im->reset();
        m_imeForgetting = false;
        m_imeComposing = false;
    }
    if (state.bounds) {
        // Where a touch edits the field (Android shows its handles for a touch there) — Qt Quick
        // gives the whole item.
        const Rect &b = *state.bounds;
        const QRectF rect(b.left(), b.top(), b.width(), b.height());
        if (im->inputItemRectangle() != rect)
            im->setInputItemRectangle(rect);
    }
    if (field != m_imeFocus) {
        const bool hadField = m_imeFocus != 0;
        m_imeFocus = field;
        m_imeState = state;
        Q_EMIT inputMethodHintsChanged();
        im->update(Qt::ImQueryAll);
        Q_EMIT cursorPositionChanged();
        // The virtual keyboard for the field that took the focus (again with its hints: one line
        // or many), away when none has it.
        if (h && !m_accessibilityFocusing)
            im->show();
        else if (!h && hadField)
            im->hide();
        return;
    }
    if (h && state != m_imeState) {
        m_imeState = std::move(state);
        im->update(Qt::ImQueryInput | Qt::ImCursorRectangle | Qt::ImAnchorRectangle | Qt::ImInputItemClipRectangle);
        Q_EMIT cursorPositionChanged();
    }
}

Qt::InputMethodHints AtramentItem::inputMethodHints() const
{
    InputHandler *h = inputHandler();
    if (!h)
        return Qt::ImhNone;
    switch (h->purpose()) {
    case InputHandler::Purpose::Url: return Qt::ImhUrlCharactersOnly;
    case InputHandler::Purpose::Email: return Qt::ImhEmailCharactersOnly;
    case InputHandler::Purpose::Number: return Qt::ImhFormattedNumbersOnly;
    case InputHandler::Purpose::Password: return Qt::ImhHiddenText | Qt::ImhSensitiveData | Qt::ImhNoPredictiveText;
    case InputHandler::Purpose::Multiline: return Qt::ImhMultiLine;
    case InputHandler::Purpose::Text: break;
    }
    return Qt::ImhNone;
}

void AtramentItem::showInputPanel()
{
    if (inputHandler() && hasActiveFocus())
        QGuiApplication::inputMethod()->show();
}

void AtramentItem::showInputPanelAt(Point p)
{
    // A tap in the focused field brings the keyboard back (after the user put it away).
    InputHandler *h = inputHandler();
    if (!h || !hasActiveFocus())
        return;
    const std::optional<Rect> bounds = h->inputBounds();
    if (!bounds || bounds->contains(p))
        QGuiApplication::inputMethod()->show();
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
    // A press ends a composition: the input method commits it first, as Qt's own fields have it
    // (Android does on its own at a touch elsewhere).
    if (InputHandler *h = inputHandler(); h && h->markedRange())
        QGuiApplication::inputMethod()->commit();
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
    if (e.button == MouseButton::Left)
        showInputPanelAt(e.position);
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
    InputHandler *h = inputHandler();
    if (!h || m_imeForgetting) {
        // Nothing takes text, or the input method drops a composition the field already ended.
        if (m_imeForgetting)
            event->accept();
        else
            event->ignore();
        return;
    }
    m_app->update([&] { applyInputMethodEvent(*h, *event); });
    // Whatever the field marks now is the input method's composition.
    if (InputHandler *now = inputHandler())
        m_imeComposing = now->markedRange().has_value();
    event->accept();
}

QVariant AtramentItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return inputMethodQuery(query, QVariant());
}

QVariant AtramentItem::inputMethodQuery(Qt::InputMethodQuery query, const QVariant &argument) const
{
    InputHandler *h = inputHandler();
    if (query == Qt::ImEnabled)
        return h != nullptr;
    if (!h)
        return QQuickItem::inputMethodQuery(query);
    const InputHandler::Selection sel = h->selection().value_or(InputHandler::Selection{});
    switch (query) {
    case Qt::ImCursorRectangle:
    case Qt::ImAnchorRectangle: {
        // The caret on screen (inside the composition while there is one).
        const int at = query == Qt::ImCursorRectangle ? sel.focus : sel.anchor;
        const std::optional<Rect> r = h->boundsForRange({ at, at });
        return r ? QRectF(r->left(), r->top(), std::max(1.0f, r->width()), r->height()) : QRectF();
    }
    case Qt::ImCursorPosition:
        if (argument.typeId() == QMetaType::QPointF || argument.typeId() == QMetaType::QPoint) {
            // The offset under a point, in the input method's own coordinates (the composition
            // included, which are the field's).
            const QPointF p = argument.toPointF();
            const std::optional<int> at = h->offsetForPoint({float(p.x()), float(p.y())});
            return at ? QVariant(*at) : QVariant();
        }
        return qtField(*h).cursor;
    case Qt::ImAnchorPosition: return qtField(*h).anchor;
    case Qt::ImAbsolutePosition: // one block: the whole field
        return qtField(*h).cursor;
    case Qt::ImSurroundingText: return toQString(qtField(*h).text);
    case Qt::ImCurrentSelection: {
        const QtField f = qtField(*h);
        const int a = std::min(f.cursor, f.anchor);
        const int b = std::max(f.cursor, f.anchor);
        return toQString(std::u16string_view(f.text).substr(std::size_t(a), std::size_t(b - a)));
    }
    case Qt::ImTextBeforeCursor:
    case Qt::ImTextAfterCursor: {
        // Without a length: all of it (QQuickTextInput's answer).
        const QtField f = qtField(*h);
        const int n = int(f.text.size());
        int want = n;
        if (argument.isValid()) {
            bool ok = false;
            const int length = argument.toInt(&ok);
            if (ok)
                want = std::clamp(length, 0, n);
        }
        const int c = std::clamp(f.cursor, 0, n);
        const std::u16string_view text(f.text);
        if (query == Qt::ImTextBeforeCursor) {
            const int from = std::max(0, c - want);
            return toQString(text.substr(std::size_t(from), std::size_t(c - from)));
        }
        return toQString(text.substr(std::size_t(c), std::size_t(std::min(n - c, want))));
    }
    case Qt::ImReadOnly: return false; // Android shows the caret's handle only for a field that says so
    case Qt::ImHints: return int(inputMethodHints());
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}

void AtramentItem::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    // The input method meets this item afresh (Qt reset it for the item it now follows).
    m_imeFocus = 0;
    m_imeState = {};
    m_imeComposing = false;
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
