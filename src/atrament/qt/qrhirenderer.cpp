#include "qrhirenderer.h"

#include <QFile>
#include <QLoggingCategory>
#include <QPoint>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

Q_STATIC_LOGGING_CATEGORY(lcRenderer, "brulion.atrament.gpu", QtWarningMsg)

namespace brulion::atrament {

namespace {

constexpr int kKinds = 5;

// The uniform block of every shader (std140): mat4 transform; float opacity.
constexpr quint32 kGlobalsSize = 80;

// Pictures not drawn for this many frames lose their textures.
constexpr std::uint64_t kPictureIdleFrames = 120;

QShader loadShader(const char *name)
{
    QFile f(QStringLiteral(":/atrament/shaders/%1.qsb").arg(QLatin1String(name)));
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcRenderer, "shader %s missing", name);
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

struct KindInfo {
    const char *vertex;
    const char *fragment;
    quint32 stride;
    int vec4s; // per-instance attributes, locations 1…
    bool textured;
};

const KindInfo &kindInfo(GpuKind kind)
{
    static const KindInfo kinds[kKinds] = {
            {"quad.vert", "quad.frag", sizeof(GpuQuadInstance), int(sizeof(GpuQuadInstance) / 16), false},
            {"shadow.vert", "shadow.frag", sizeof(GpuShadowInstance), int(sizeof(GpuShadowInstance) / 16), false},
            {"sprite.vert", "sprite.frag", sizeof(GpuSpriteInstance), int(sizeof(GpuSpriteInstance) / 16), true},
            {"image.vert", "image.frag", sizeof(GpuImageInstance), int(sizeof(GpuImageInstance) / 16), true},
            {"underline.vert", "underline.frag", sizeof(GpuUnderlineInstance), int(sizeof(GpuUnderlineInstance) / 16),
             false},
    };
    return kinds[int(kind)];
}

template<typename T>
void destroy(T *&resource)
{
    delete resource; // QRhi resources release their native objects when deleted (deferred where in use)
    resource = nullptr;
}

} // namespace

bool atramentGpuRendererWanted()
{
    return qEnvironmentVariable("BRULION_ATRAMENT_RENDERER").compare(QLatin1String("cpu"), Qt::CaseInsensitive) != 0;
}

bool atramentGpuShadersBuilt()
{
    return QFile::exists(QStringLiteral(":/atrament/shaders/quad.frag.qsb"));
}

QRhiRenderer::QRhiRenderer() = default;

QRhiRenderer::~QRhiRenderer()
{
    releaseResources();
}

void QRhiRenderer::setFrame(std::shared_ptr<const GpuFrame> frame)
{
    m_frame = std::move(frame);
    m_frameUploaded = false;
}

const GpuFrame &QRhiRenderer::frame() const
{
    static const GpuFrame empty;
    return m_frame ? *m_frame : empty;
}

void QRhiRenderer::syncAtlas(AtlasSlot slot, Atlas &atlas)
{
    std::vector<PageCopy> &copies = m_copies[int(slot)];
    const std::vector<AtlasPage> &pages = atlas.pages();
    if (copies.size() < pages.size())
        copies.resize(pages.size());
    for (std::uint32_t i = 0; i < pages.size(); ++i) {
        const AtlasPage &page = pages[i];
        PageCopy &copy = copies[i];
        if (copy.copied && copy.revision == page.revision && copy.width == page.width && copy.height == page.height)
            continue;
        // The dirty rectangle holds every change since the page was last cleaned: enough when that
        // was this renderer's own copy, and the page kept its size.
        const bool whole = !copy.copied || copy.width != page.width || copy.height != page.height
                           || copy.kind != page.kind || copy.revision < page.cleanRevision;
        Upload up;
        up.slot = slot;
        up.page = i;
        up.kind = page.kind;
        up.pageWidth = page.width;
        up.pageHeight = page.height;
        if (whole) {
            up.width = page.width;
            up.height = page.height;
        } else {
            const auto clampTo = [](float v, int hi) { return std::clamp(int(std::floor(v)), 0, hi); };
            up.x = clampTo(page.dirty.left(), page.width);
            up.y = clampTo(page.dirty.top(), page.height);
            up.width = std::clamp(int(std::ceil(page.dirty.right())), up.x, page.width) - up.x;
            up.height = std::clamp(int(std::ceil(page.dirty.bottom())), up.y, page.height) - up.y;
        }
        const int bpp = page.bytesPerPixel();
        const std::size_t rowBytes = std::size_t(up.width) * std::size_t(bpp);
        if (up.width > 0 && up.height > 0
            && page.pixels.size() >= std::size_t(page.width) * std::size_t(page.height) * std::size_t(bpp)) {
            up.bytes.resize(qsizetype(rowBytes * std::size_t(up.height)));
            for (int y = 0; y < up.height; ++y)
                std::memcpy(up.bytes.data() + std::size_t(y) * rowBytes,
                            page.row(up.y + y) + std::size_t(up.x) * std::size_t(bpp), rowBytes);
        } else {
            up.width = up.height = 0;
        }
        if (whole || up.width > 0)
            m_uploads.push_back(std::move(up));
        copy.copied = true;
        copy.revision = page.revision;
        copy.width = page.width;
        copy.height = page.height;
        copy.kind = page.kind;
        atlas.markClean(i);
    }
}

bool QRhiRenderer::create(QRhi *rhi)
{
    m_rhi = rhi;
    if (!rhi->isFeatureSupported(QRhi::Instancing) || !rhi->isFeatureSupported(QRhi::TexelFetch)
        || !rhi->isTextureFormatSupported(QRhiTexture::R8)) {
        qCWarning(lcRenderer, "%s lacks instancing, texelFetch or R8 textures", rhi->backendName());
        return false;
    }
    m_corners = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, 8 * sizeof(float));
    m_globals = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kGlobalsSize);
    m_sampler = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge,
                                QRhiSampler::ClampToEdge);
    m_emptyAlpha = rhi->newTexture(QRhiTexture::R8, QSize(1, 1));
    m_emptyColor = rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1));
    if (!m_corners->create() || !m_globals->create() || !m_sampler->create() || !m_emptyAlpha->create()
        || !m_emptyColor->create())
        return false;
    m_cornersUploaded = false;
    m_plainBindings = rhi->newShaderResourceBindings();
    m_plainBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_globals)});
    if (!m_plainBindings->create())
        return false;
    m_emptyAlphaBindings = bindingsFor(m_emptyAlpha);
    m_emptyColorBindings = bindingsFor(m_emptyColor);
    return m_emptyAlphaBindings && m_emptyColorBindings;
}

QRhiShaderResourceBindings *QRhiRenderer::bindingsFor(QRhiTexture *texture)
{
    QRhiShaderResourceBindings *srb = m_rhi->newShaderResourceBindings();
    srb->setBindings(
            {QRhiShaderResourceBinding::uniformBuffer(
                     0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_globals),
             QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, texture,
                                                       m_sampler)});
    if (!srb->create()) {
        delete srb;
        return nullptr;
    }
    return srb;
}

bool QRhiRenderer::createPipelines(QRhiRenderPassDescriptor *renderPass, int sampleCount)
{
    m_renderPass = renderPass->newCompatibleRenderPassDescriptor();
    m_sampleCount = sampleCount;
    // Premultiplied source-over.
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    for (int k = 0; k < kKinds; ++k) {
        const KindInfo &info = kindInfo(GpuKind(k));
        const QShader vs = loadShader(info.vertex);
        const QShader fs = loadShader(info.fragment);
        if (!vs.isValid() || !fs.isValid())
            return false;
        QRhiVertexInputLayout layout;
        layout.setBindings({QRhiVertexInputBinding(2 * sizeof(float)),
                            QRhiVertexInputBinding(info.stride, QRhiVertexInputBinding::PerInstance)});
        std::vector<QRhiVertexInputAttribute> attributes;
        attributes.emplace_back(0, 0, QRhiVertexInputAttribute::Float2, 0);
        for (int i = 0; i < info.vec4s; ++i)
            attributes.emplace_back(1, i + 1, QRhiVertexInputAttribute::Float4, quint32(i * 16));
        layout.setAttributes(attributes.begin(), attributes.end());

        QRhiGraphicsPipeline *p = m_rhi->newGraphicsPipeline();
        p->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        p->setFlags(QRhiGraphicsPipeline::UsesScissor);
        p->setTargetBlends({blend});
        p->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        p->setVertexInputLayout(layout);
        p->setShaderResourceBindings(!info.textured                  ? m_plainBindings
                                     : GpuKind(k) == GpuKind::Sprite ? m_emptyAlphaBindings
                                                                     : m_emptyColorBindings);
        p->setRenderPassDescriptor(m_renderPass);
        p->setSampleCount(sampleCount);
        m_pipelines[k] = p;
        if (!p->create()) {
            qCWarning(lcRenderer, "pipeline %s / %s does not build on %s", info.vertex, info.fragment,
                      m_rhi->backendName());
            return false;
        }
    }
    return true;
}

void QRhiRenderer::destroyPipelines()
{
    for (QRhiGraphicsPipeline *&p : m_pipelines)
        destroy(p);
    destroy(m_renderPass);
    m_sampleCount = 0;
}

void QRhiRenderer::releaseResources()
{
    destroyPipelines();
    for (std::vector<PageMirror> &pages : m_pages) {
        for (PageMirror &m : pages) {
            destroy(m.bindings);
            destroy(m.texture);
        }
        pages.clear();
    }
    for (auto &[id, picture] : m_pictures) {
        destroy(picture.bindings);
        destroy(picture.texture);
    }
    m_pictures.clear();
    destroy(m_plainBindings);
    destroy(m_emptyAlphaBindings);
    destroy(m_emptyColorBindings);
    destroy(m_emptyAlpha);
    destroy(m_emptyColor);
    destroy(m_sampler);
    destroy(m_globals);
    destroy(m_instances);
    destroy(m_corners);
    m_rhi = nullptr;
    m_failed = false;
    m_frameUploaded = false;
    // The textures are gone: the next sync copies whole pages.
    m_uploads.clear();
    for (std::vector<PageCopy> &copies : m_copies)
        copies.clear();
}

void QRhiRenderer::applyUploads(QRhiResourceUpdateBatch *batch)
{
    for (Upload &up : m_uploads) {
        std::vector<PageMirror> &pages = m_pages[int(up.slot)];
        if (pages.size() <= up.page)
            pages.resize(std::size_t(up.page) + 1);
        PageMirror &m = pages[up.page];
        if (!m.texture || m.width != up.pageWidth || m.height != up.pageHeight || m.kind != up.kind) {
            destroy(m.bindings);
            destroy(m.texture);
            m.width = m.height = 0;
            if (up.pageWidth > 0 && up.pageHeight > 0) {
                m.texture = m_rhi->newTexture(up.kind == AtlasKind::Alpha ? QRhiTexture::R8 : QRhiTexture::RGBA8,
                                              QSize(up.pageWidth, up.pageHeight));
                if (m.texture->create()) {
                    m.bindings = bindingsFor(m.texture);
                    m.width = up.pageWidth;
                    m.height = up.pageHeight;
                    m.kind = up.kind;
                } else {
                    qCWarning(lcRenderer, "no %d×%d atlas texture", up.pageWidth, up.pageHeight);
                    destroy(m.texture);
                }
            }
        }
        if (!m.texture || up.width <= 0 || up.height <= 0)
            continue;
        QRhiTextureSubresourceUploadDescription sub(up.bytes);
        sub.setDestinationTopLeft(QPoint(up.x, up.y));
        sub.setSourceSize(QSize(up.width, up.height));
        batch->uploadTexture(m.texture, QRhiTextureUploadDescription({0, 0, sub}));
        m_stats.uploadedBytes += std::size_t(up.bytes.size());
    }
    m_uploads.clear();
}

void QRhiRenderer::uploadPictures(QRhiResourceUpdateBatch *batch)
{
    for (const std::shared_ptr<const ImageData> &image : frame().pictures) {
        PictureMirror &m = m_pictures[image->id];
        m.lastUsed = m_frameNumber;
        if (m.texture || m.failed)
            continue;
        m.texture = m_rhi->newTexture(QRhiTexture::RGBA8, QSize(image->width, image->height));
        if (!m.texture->create()) {
            // Larger than the device's textures, say: not drawn (and not tried again).
            qCWarning(lcRenderer, "no %d×%d texture for a picture", image->width, image->height);
            destroy(m.texture);
            m.failed = true;
            continue;
        }
        m.bindings = bindingsFor(m.texture);
        const auto bytes = quint32(std::size_t(image->width) * std::size_t(image->height) * 4);
        QRhiTextureSubresourceUploadDescription sub(image->pixels.data(), bytes);
        batch->uploadTexture(m.texture, QRhiTextureUploadDescription({0, 0, sub}));
        m_stats.uploadedBytes += bytes;
    }
    for (auto it = m_pictures.begin(); it != m_pictures.end();) {
        if (m_frameNumber - it->second.lastUsed > kPictureIdleFrames) {
            destroy(it->second.bindings);
            destroy(it->second.texture);
            it = m_pictures.erase(it);
        } else {
            ++it;
        }
    }
}

bool QRhiRenderer::prepare(QRhi *rhi, QRhiCommandBuffer *cb, QRhiRenderPassDescriptor *renderPass, int sampleCount,
                           const QMatrix4x4 &transform, float opacity)
{
    if (!rhi || !cb || !renderPass)
        return false;
    if (m_rhi && m_rhi != rhi)
        releaseResources(); // another QRhi: start again (the next sync copies whole pages)
    if (!m_rhi) {
        if (!create(rhi)) {
            m_failed = true;
            return false;
        }
    }
    if (m_failed)
        return false;
    if (!m_renderPass || !m_renderPass->isCompatible(renderPass) || m_sampleCount != sampleCount) {
        destroyPipelines();
        if (!createPipelines(renderPass, sampleCount)) {
            m_failed = true;
            return false;
        }
    }
    ++m_frameNumber;
    m_stats.uploadedBytes = 0;
    QRhiResourceUpdateBatch *batch = rhi->nextResourceUpdateBatch();
    if (!m_cornersUploaded) {
        static const float corners[8] = {0, 0, 1, 0, 0, 1, 1, 1};
        batch->uploadStaticBuffer(m_corners, corners);
        static const std::uint8_t zero[4] = {};
        batch->uploadTexture(m_emptyAlpha, QRhiTextureUploadDescription({0, 0, {zero, 1}}));
        batch->uploadTexture(m_emptyColor, QRhiTextureUploadDescription({0, 0, {zero, 4}}));
        m_cornersUploaded = true;
    }
    applyUploads(batch);
    uploadPictures(batch);
    const GpuFrame &f = frame();
    const auto size = quint32(f.instances.size());
    if (size > 0 && (!m_instances || m_instances->size() < size)) {
        destroy(m_instances);
        // Room to grow: a frame with more glyphs than the last should not make a new buffer.
        const quint32 capacity = std::max<quint32>(64 * 1024, size + size / 2);
        m_instances = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, capacity);
        if (!m_instances->create()) {
            destroy(m_instances);
            m_failed = true;
            batch->release();
            return false;
        }
        m_frameUploaded = false;
    }
    if (size > 0 && !m_frameUploaded) {
        batch->updateDynamicBuffer(m_instances, 0, size, f.instances.data());
        m_frameUploaded = true;
    }
    std::array<float, kGlobalsSize / sizeof(float)> globals{};
    std::memcpy(globals.data(), transform.constData(), 16 * sizeof(float));
    globals[16] = opacity;
    batch->updateDynamicBuffer(m_globals, 0, kGlobalsSize, globals.data());
    // QRhi's OpenGL backend emulates uniform buffers with glUniform calls, which it skips for a
    // program whose bindings it believes unchanged since the frame before — and offscreen frames
    // (QQuickWidget, QQuickRenderControl, the tests) do not count as frames for it: a new transform
    // or opacity would reach the first pipeline only. A new generation of every binding set has
    // them set again.
    if (rhi->backend() == QRhi::OpenGLES2 && globals != m_lastGlobals) {
        for (QRhiShaderResourceBindings *srb : {m_plainBindings, m_emptyAlphaBindings, m_emptyColorBindings})
            srb->updateResources();
        for (const std::vector<PageMirror> &pages : m_pages)
            for (const PageMirror &m : pages)
                if (m.bindings)
                    m.bindings->updateResources();
        for (const auto &[id, picture] : m_pictures)
            if (picture.bindings)
                picture.bindings->updateResources();
    }
    m_lastGlobals = globals;
    cb->resourceUpdate(batch);
    m_stats.textures = m_pictures.size();
    for (const std::vector<PageMirror> &pages : m_pages)
        for (const PageMirror &m : pages)
            m_stats.textures += m.texture ? 1 : 0;
    return true;
}

QRhiShaderResourceBindings *QRhiRenderer::bindingsOf(const GpuTexture &texture) const
{
    QRhiShaderResourceBindings *srb = nullptr;
    switch (texture.source) {
    case GpuTexture::Source::None: return m_plainBindings;
    case GpuTexture::Source::Atlas:
    case GpuTexture::Source::PathAtlas: {
        const std::vector<PageMirror> &pages =
                m_pages[int(texture.source == GpuTexture::Source::Atlas ? AtlasSlot::Atlas : AtlasSlot::Paths)];
        if (texture.page < pages.size())
            srb = pages[texture.page].bindings;
        break;
    }
    case GpuTexture::Source::Picture: {
        auto it = m_pictures.find(texture.image);
        if (it != m_pictures.end())
            srb = it->second.bindings;
        break;
    }
    }
    return srb;
}

void QRhiRenderer::render(QRhiCommandBuffer *cb, QSize viewportSize, std::optional<std::array<int, 4>> scissor)
{
    m_stats.batches = 0;
    m_stats.instances = 0;
    const GpuFrame &f = frame();
    if (m_failed || !m_pipelines[0] || !m_instances || f.batches.empty())
        return;
    const QRhiViewport viewport(0, 0, float(viewportSize.width()), float(viewportSize.height()));
    const QRhiScissor clip = scissor ? QRhiScissor((*scissor)[0], (*scissor)[1], (*scissor)[2], (*scissor)[3])
                                     : QRhiScissor(0, 0, viewportSize.width(), viewportSize.height());
    int current = -1;
    QRhiShaderResourceBindings *bound = nullptr;
    for (const GpuBatch &batch : f.batches) {
        QRhiShaderResourceBindings *srb = bindingsOf(batch.texture);
        if (!srb) {
            // A texture that could not be made: nothing to sample (glyphs of that page stay away).
            srb = batch.kind == GpuKind::Sprite ? m_emptyAlphaBindings : m_emptyColorBindings;
        }
        if (int(batch.kind) != current) {
            current = int(batch.kind);
            cb->setGraphicsPipeline(m_pipelines[current]);
            cb->setViewport(viewport);
            cb->setScissor(clip);
            bound = nullptr;
        }
        if (srb != bound) {
            cb->setShaderResources(srb);
            bound = srb;
        }
        const QRhiCommandBuffer::VertexInput inputs[2] = {{m_corners, 0}, {m_instances, quint32(batch.offset)}};
        cb->setVertexInput(0, 2, inputs);
        cb->draw(4, batch.count);
        ++m_stats.batches;
        m_stats.instances += batch.count;
    }
}

} // namespace brulion::atrament
