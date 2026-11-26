#pragma once

// The Qt host (docs/ATRAMENT.md D12): Atrament inside today's Qt Quick shell, one view at a time.
//
//   QtPlatform      Atrament's Platform over Qt: the event loop's queue, QTimer, the clipboard
//   AtramentItem    a QQuickItem that is an Atrament window: it draws the window's frame and
//                   turns Qt's mouse, wheel, key, hover and input-method events into Atrament's,
//                   answering the input method's queries from the focused element's InputHandler.
//
// The input method sees a field as Qt's own controls show it: the composition (Atrament's marked
// text, which is part of the text) is not in the text Qt is told about, it sits at the caret, and
// Qt's events (a commit with a replacement range, a selection, a new preedit) act on that text.
// The item converts both ways, keeps the input method told about every change of the caret, the
// selection and the text (Android: InputMethodManager.updateSelection, through the
// cursorPositionChanged() signal), makes it forget a composition that ended without it, shows the
// virtual keyboard for a field and hides it when no field has the focus.
//
// Frames are built on the GUI thread (in updatePolish), never on Qt Quick's render thread. They are
// drawn by the GPU renderer (qrhirenderer.h) in a QSGRenderNode, inside Qt Quick's own render pass,
// where Qt Quick runs on QRhi; by the CPU renderer into a texture where it does not (the software
// backend), where the GPU renderer fails on the device, or with BRULION_ATRAMENT_RENDERER=cpu.

#include "gpuframe.h"
#include "raster/raster.h"
#include "ui/platform.h"

#include <QImage>
#include <QPointer>
#include <QQuickItem>

#include <atomic>
#include <functional>
#include <map>
#include <memory>

class QTimer;

namespace brulion::atrament {

class App;
class AtramentAccessibility;
class FontCollection;
class Window;
struct AnyView;

class QtPlatform final : public Platform {
public:
    QtPlatform();
    ~QtPlatform() override;
    std::unique_ptr<PlatformWindow> openWindow(const WindowOptions &options) override;
    void post(std::function<void()> task) override;
    TimerId postDelayed(std::chrono::milliseconds delay, std::function<void()> task) override;
    void cancel(TimerId timer) override;
    std::chrono::steady_clock::time_point now() const override { return std::chrono::steady_clock::now(); }
    void writeClipboard(std::u16string_view text) override;
    std::optional<std::u16string> readClipboard() override;
    std::chrono::milliseconds doubleClickInterval() const override;

private:
    QObject *m_context;
    std::map<TimerId, QPointer<QTimer>> m_timers;
    TimerId m_nextTimer = 1;
};

// Loads font files from Qt resources or files into a collection (":/fonts/ui/BrulionInter-Regular.ttf").
int addQtFonts(FontCollection &fonts, const QStringList &paths);

class AtramentItem : public QQuickItem, public PlatformWindow {
    Q_OBJECT
    // The focused field's hints. Qt's Android input context talks only to a focus object that has
    // this property (as Qt Quick's text fields do): without it every call of the keyboard's
    // InputConnection was dropped.
    Q_PROPERTY(Qt::InputMethodHints inputMethodHints READ inputMethodHints NOTIFY inputMethodHintsChanged FINAL)
public:
    explicit AtramentItem(QQuickItem *parent = nullptr);
    ~AtramentItem() override;

    // Attaches an Atrament window showing `root` (call once; the item detaches it when destroyed).
    void attach(App &app, const AnyView &root);
    Window *atramentWindow() const { return m_window; }

    // PlatformWindow
    Size contentSize() const override;
    float scaleFactor() const override;
    Point mousePosition() const override { return m_mouse; }
    Modifiers modifiers() const override;
    bool isActive() const override;
    void requestFrame() override;
    void present(const Scene &scene, Atlas &atlas) override;
    void setCursor(CursorStyle cursor) override;
    void inputStateChanged() override;
    void accessibilityChanged(const std::vector<AccessibilityEvent> &events) override;

    // The accessibility bridge (qtaccessibility.h): the window's semantic tree for Qt's.
    AtramentAccessibility *accessibility() const { return m_accessibility.get(); }

    // The last frame's pixels when the CPU renderer draws (tests, screenshots).
    const QImage &frameImage() const { return m_image; }
    // Whether frames go to the GPU renderer (decided at each frame from the window's graphics API).
    bool usesGpuRenderer() const { return m_gpu; }

    Qt::InputMethodHints inputMethodHints() const;
    // The virtual keyboard for the focused field, if there is one and the item has Qt's focus.
    void showInputPanel();
    // While an assistive technology moves the focus (not a deliberate activation): a field that
    // takes it opens no keyboard (the accessibility bridge sets it around its Focus action).
    void setAccessibilityFocusing(bool on) { m_accessibilityFocusing = on; }

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;

public:
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    // The queries with an argument (QInputMethod::queryFocusObject): Qt::ImCursorPosition at a
    // point (Android's selection handles, a tap while composing), Qt::ImTextBeforeCursor /
    // Qt::ImTextAfterCursor of a length.
    Q_INVOKABLE QVariant inputMethodQuery(Qt::InputMethodQuery query, const QVariant &argument) const;

Q_SIGNALS:
    // The focused field's caret, selection, composition or text changed (Qt's Android input context
    // connects to a signal of this name and tells the input method).
    void cursorPositionChanged();
    void inputMethodHintsChanged();

protected:
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    bool send(const PlatformInput &input);
    InputHandler *inputHandler() const;
    void showInputPanelAt(Point p);
    int clickCountFor(Point p, MouseButton button);
    bool gpuAvailable() const;
    void checkGpuRenderer();

    App *m_app = nullptr;
    Window *m_window = nullptr;
    Point m_mouse { -1e9f, -1e9f };
    // The CPU path.
    RasterRenderer m_renderer;
    RasterTarget m_target;
    QImage m_image;
    bool m_imageDirty = false;
    // The GPU path: the frame waiting for the next sync, the atlas it refers to, and a flag the
    // render thread sets when the renderer cannot draw (then the CPU renderer takes over).
    struct GpuShared {
        std::atomic<bool> failed{false};       // the renderer cannot draw here: the CPU takes over
        std::atomic<bool> lostTextures{false}; // Qt Quick released them: copy the atlas again
    };
    bool m_gpu = false;
    GpuFrameBuilder m_gpuBuilder;
    std::shared_ptr<const GpuFrame> m_gpuFrame;
    bool m_gpuFrameNew = false;
    Atlas *m_atlas = nullptr;
    std::shared_ptr<GpuShared> m_gpuShared = std::make_shared<GpuShared>();
    QMetaObject::Connection m_frameSwapped;
    // Double and triple clicks: Qt reports only doubles.
    std::chrono::steady_clock::time_point m_lastClick;
    Point m_lastClickPos;
    MouseButton m_lastClickButton = MouseButton::Left;
    int m_clickCount = 0;
    std::unique_ptr<AtramentAccessibility> m_accessibility;
    // The input method's view of the focused field, as last told (inputStateChanged).
    struct ImeState {
        std::u16string text;
        InputHandler::Selection selection;
        std::optional<Range> marked;
        std::optional<Rect> bounds;
        bool operator==(const ImeState &) const = default;
    };
    std::uint64_t m_imeFocus = 0; // the focused field the input method knows (Window::focusedId)
    ImeState m_imeState;
    bool m_imeComposing = false;  // the field's marked text is the input method's composition
    bool m_imeForgetting = false; // its events change nothing while it drops a composition
    bool m_accessibilityFocusing = false;
};

// A key event as Atrament's keystroke ("ctrl-shift-a", keyChar the text it types).
Keystroke keystrokeFromQt(int key, Qt::KeyboardModifiers modifiers, const QString &text);

} // namespace brulion::atrament
