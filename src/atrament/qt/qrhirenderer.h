#pragma once

// The GPU renderer of a Scene on Qt's QRhi (docs/ATRAMENT.md D12, M2): Vulkan, Metal, Direct3D or
// OpenGL ES 3 / OpenGL 3.3, whatever Qt Quick runs on. It draws a GpuFrame (gpuframe.h) — one
// instanced draw per batch, painter's order, premultiplied source-over (ONE, ONE_MINUS_SRC_ALPHA)
// on sRGB components as the CPU renderer — into a render pass someone else began: Qt Quick's
// (AtramentItem's render node) or an offscreen texture's (tests, offscreenrhi.h).
//
// Atlas pages are mirrored as textures (A8 → R8, colour → RGBA8): syncAtlas() copies what changed
// since this renderer's last copy — the dirty rectangle, or the whole page when another renderer
// cleaned it since or its size changed — and marks the page clean. Pictures get a texture of
// their own the first time they are drawn, kept while frames use them.
//
// Threads: setFrame() and syncAtlas() copy everything they need, so they may run where the scene
// and the atlas live (Qt Quick's render thread while the GUI thread waits in its sync, for
// AtramentItem); prepare() and render() run where the QRhi does — never at the same time.

#include "gpuframe.h"

#include <QMatrix4x4>
#include <QSize>

#include <QByteArray>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

QT_BEGIN_NAMESPACE
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QShader;
QT_END_NAMESPACE

namespace brulion::atrament {

class QRhiRenderer {
public:
    // Whether this QRhi has what the renderer needs (instancing, texelFetch, R8 textures).
    static bool supports(QRhi *rhi);
    enum class AtlasSlot : std::uint8_t { Atlas, Paths };

    QRhiRenderer();
    ~QRhiRenderer();
    QRhiRenderer(const QRhiRenderer &) = delete;
    QRhiRenderer &operator=(const QRhiRenderer &) = delete;

    // The frame to draw from now on (shared: Qt Quick may make a new renderer for the same frame).
    void setFrame(std::shared_ptr<const GpuFrame> frame);
    const GpuFrame &frame() const;
    // Copies what changed in `atlas` since the last call for this slot, then marks it clean.
    void syncAtlas(AtlasSlot slot, Atlas &atlas);

    // Creates what is missing for `rhi` and records the uploads of this frame (outside a render
    // pass). `transform` maps the frame's device pixels to clip space, `opacity` multiplies every
    // colour. Returns false when the renderer cannot draw on this QRhi (no instancing, no R8
    // textures, a shader or pipeline that does not build): the caller falls back to the CPU.
    bool prepare(QRhi *rhi, QRhiCommandBuffer *cb, QRhiRenderPassDescriptor *renderPass, int sampleCount,
                 const QMatrix4x4 &transform, float opacity);
    // Records the draws into the pass begun on `cb`. The viewport covers `viewportSize` pixels of
    // the render target; `scissor` (x, y, width, height, QRhi's bottom-left origin) limits them.
    void render(QRhiCommandBuffer *cb, QSize viewportSize, std::optional<std::array<int, 4>> scissor = std::nullopt);

    // Drops every QRhi resource (the QRhi goes away, or Qt Quick releases its resources); the
    // next prepare() starts again and syncAtlas() copies whole pages again.
    void releaseResources();

    struct Stats {
        std::size_t batches = 0; // draw calls of the last render()
        std::size_t instances = 0;
        std::size_t uploadedBytes = 0; // texture bytes the last prepare() uploaded
        std::size_t textures = 0;      // atlas pages and pictures mirrored
    };
    const Stats &stats() const { return m_stats; }

private:
    // A page's texture (prepare(), render()).
    struct PageMirror {
        QRhiTexture *texture = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        int width = 0;
        int height = 0;
        AtlasKind kind = AtlasKind::Alpha;
    };
    // What syncAtlas() copied of a page last.
    struct PageCopy {
        bool copied = false;
        std::uint64_t revision = 0;
        int width = 0;
        int height = 0;
        AtlasKind kind = AtlasKind::Alpha;
    };
    struct PictureMirror {
        QRhiTexture *texture = nullptr;
        QRhiShaderResourceBindings *bindings = nullptr;
        std::uint64_t lastUsed = 0;
        bool failed = false;
    };
    struct Upload {
        AtlasSlot slot = AtlasSlot::Atlas;
        std::uint32_t page = 0;
        AtlasKind kind = AtlasKind::Alpha;
        int pageWidth = 0; // the page's size at the copy: the texture is made (again) at this size
        int pageHeight = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        QByteArray bytes; // tightly packed rows
    };

    bool create(QRhi *rhi);
    bool createPipelines(QRhiRenderPassDescriptor *renderPass, int sampleCount);
    void destroyPipelines();
    QRhiShaderResourceBindings *bindingsFor(QRhiTexture *texture);
    void applyUploads(QRhiResourceUpdateBatch *batch);
    void uploadPictures(QRhiResourceUpdateBatch *batch);
    QRhiShaderResourceBindings *bindingsOf(const GpuTexture &texture) const;

    std::shared_ptr<const GpuFrame> m_frame;
    std::vector<Upload> m_uploads;
    std::vector<PageCopy> m_copies[2]; // by AtlasSlot
    std::vector<PageMirror> m_pages[2];
    std::unordered_map<std::uint64_t, PictureMirror> m_pictures;
    std::uint64_t m_frameNumber = 0;
    bool m_frameUploaded = false; // the instances of m_frame are in m_instances

    QRhi *m_rhi = nullptr;
    bool m_failed = false;
    QRhiBuffer *m_corners = nullptr; // the unit square, a triangle strip
    QRhiBuffer *m_instances = nullptr;
    QRhiBuffer *m_globals = nullptr;
    QRhiSampler *m_sampler = nullptr;
    QRhiTexture *m_emptyAlpha = nullptr; // for a batch whose texture is missing, and pipelines' layouts
    QRhiTexture *m_emptyColor = nullptr;
    QRhiShaderResourceBindings *m_plainBindings = nullptr;
    QRhiShaderResourceBindings *m_emptyAlphaBindings = nullptr;
    QRhiShaderResourceBindings *m_emptyColorBindings = nullptr;
    QRhiRenderPassDescriptor *m_renderPass = nullptr; // a compatible copy the pipelines were made for
    int m_sampleCount = 0;
    QRhiGraphicsPipeline *m_pipelines[5] = {};
    bool m_cornersUploaded = false;
    std::array<float, 20> m_lastGlobals{}; // the uniform block of the last prepare()
    Stats m_stats;
};

// Whether AtramentItem should draw with the GPU renderer: not when BRULION_ATRAMENT_RENDERER=cpu.
bool atramentGpuRendererWanted();
// Whether the shaders were built in (a Qt without Shader Tools builds the renderer without them).
bool atramentGpuShadersBuilt();

} // namespace brulion::atrament
