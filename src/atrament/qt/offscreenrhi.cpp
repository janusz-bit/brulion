#include "offscreenrhi.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <private/qguiapplication_p.h>
#include <qpa/qplatformintegration.h>
#include <rhi/qrhi.h>
#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#include <private/qbasicvulkanplatforminstance_p.h>
#include <private/qvulkaninstance_p.h>
#endif

#include <algorithm>
#include <cstring>

namespace brulion::atrament {

#if QT_CONFIG(vulkan)
namespace {

// A Vulkan instance straight from the loader, without the platform plugin: no surface extensions
// (nothing is presented), so it works under `offscreen` and in a test without a display.
class HeadlessVulkanInstance final : public QBasicPlatformVulkanInstance {
public:
    explicit HeadlessVulkanInstance(QVulkanInstance *instance) : m_instance(instance)
    {
#ifdef Q_OS_WIN
        loadVulkanLibrary(QStringLiteral("vulkan-1"));
#else
        loadVulkanLibrary(QStringLiteral("vulkan"), 1);
#endif
    }
    void createOrAdoptInstance() override { initInstance(m_instance, {}); }
    bool supportsPresent(VkPhysicalDevice, uint32_t, QWindow *) override { return false; }

private:
    QVulkanInstance *m_instance;
};

} // namespace
#endif

OffscreenRhi::OffscreenRhi() = default;

std::unique_ptr<OffscreenRhi> OffscreenRhi::create(Backend backend, QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return nullptr;
    };
    std::unique_ptr<OffscreenRhi> r(new OffscreenRhi);
    if (backend == Backend::Vulkan) {
#if QT_CONFIG(vulkan)
        r->m_vulkan = std::make_unique<QVulkanInstance>();
        QVulkanInstancePrivate::get(r->m_vulkan.get())
                ->platformInst.reset(new HeadlessVulkanInstance(r->m_vulkan.get()));
        const QVersionNumber supported = r->m_vulkan->supportedApiVersion();
        if (supported >= QVersionNumber(1, 2))
            r->m_vulkan->setApiVersion(QVersionNumber(1, 2));
        else if (supported >= QVersionNumber(1, 1))
            r->m_vulkan->setApiVersion(QVersionNumber(1, 1));
        r->m_vulkan->setExtensions(QRhiVulkanInitParams::preferredInstanceExtensions());
        if (!r->m_vulkan->create())
            return fail(QStringLiteral("no Vulkan instance (error %1)").arg(int(r->m_vulkan->errorCode())));
        QRhiVulkanInitParams params;
        params.inst = r->m_vulkan.get();
        r->m_rhi.reset(QRhi::create(QRhi::Vulkan, &params));
        if (!r->m_rhi)
            return fail(QStringLiteral("no Vulkan device"));
        return r;
#else
        // The browser's Qt (WebGL only).
        return fail(QStringLiteral("this Qt has no Vulkan"));
#endif
    }
    if (!QGuiApplicationPrivate::platformIntegration()->hasCapability(QPlatformIntegration::OpenGL))
        return fail(QStringLiteral("the %1 platform plugin has no OpenGL").arg(QGuiApplication::platformName()));
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    r->m_surface.reset(QRhiGles2InitParams::newFallbackSurface(format));
    QRhiGles2InitParams params;
    params.format = format;
    params.fallbackSurface = r->m_surface.get();
    r->m_rhi.reset(QRhi::create(QRhi::OpenGLES2, &params));
    if (!r->m_rhi)
        return fail(QStringLiteral("no OpenGL context"));
    return r;
}

OffscreenRhi::~OffscreenRhi()
{
    m_target.reset();
    m_renderPass.reset();
    m_texture.reset();
    m_rhi.reset();
    m_surface.reset();
#if QT_CONFIG(vulkan)
    if (m_vulkan)
        m_vulkan->destroy();
#endif
}

QString OffscreenRhi::description() const
{
    if (!m_rhi)
        return {};
    return QStringLiteral("%1: %2").arg(QLatin1String(m_rhi->backendName()),
                                        QString::fromUtf8(m_rhi->driverInfo().deviceName));
}

std::optional<std::vector<Pixel>> OffscreenRhi::render(QRhiRenderer &renderer, Color clear, bool readBack,
                                                       float opacity)
{
    const int w = renderer.frame().width;
    const int h = renderer.frame().height;
    if (!m_rhi || w <= 0 || h <= 0)
        return std::nullopt;
    if (!m_texture || m_texture->pixelSize() != QSize(w, h)) {
        m_target.reset();
        m_renderPass.reset();
        m_texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1,
                                          QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!m_texture->create())
            return std::nullopt;
        m_target.reset(m_rhi->newTextureRenderTarget({m_texture.get()}));
        m_renderPass.reset(m_target->newCompatibleRenderPassDescriptor());
        m_target->setRenderPassDescriptor(m_renderPass.get());
        if (!m_target->create())
            return std::nullopt;
    }
    QRhiCommandBuffer *cb = nullptr;
    if (m_rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return std::nullopt;
    // Device pixels, y down, to clip space (clipSpaceCorrMatrix: the backend's conventions).
    QMatrix4x4 transform = m_rhi->clipSpaceCorrMatrix();
    transform.ortho(0, float(w), float(h), 0, -1, 1);
    if (!renderer.prepare(m_rhi.get(), cb, m_renderPass.get(), 1, transform, opacity)) {
        m_rhi->endOffscreenFrame();
        return std::nullopt;
    }
    // The target holds premultiplied colour, as the CPU renderer's.
    const float a = std::clamp(clear.a, 0.0f, 1.0f);
    const auto c = [a](float v) { return std::clamp(v, 0.0f, 1.0f) * a; };
    cb->beginPass(m_target.get(), QColor::fromRgbF(c(clear.r), c(clear.g), c(clear.b), a), {1.0f, 0});
    renderer.render(cb, QSize(w, h));
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch *readback = nullptr;
    if (readBack) {
        readback = m_rhi->nextResourceUpdateBatch();
        readback->readBackTexture({m_texture.get()}, &result);
    }
    cb->endPass(readback);
    // Offscreen frames are synchronous: the GPU has finished when this returns.
    m_rhi->endOffscreenFrame();
    std::vector<Pixel> pixels;
    if (!readBack)
        return pixels;
    if (result.data.size() < qsizetype(std::size_t(w) * std::size_t(h) * 4))
        return std::nullopt;
    pixels.resize(std::size_t(w) * std::size_t(h));
    const bool flip = m_rhi->isYUpInFramebuffer();
    for (int y = 0; y < h; ++y) {
        const int from = flip ? h - 1 - y : y;
        std::memcpy(pixels.data() + std::size_t(y) * std::size_t(w),
                    result.data.constData() + std::size_t(from) * std::size_t(w) * 4, std::size_t(w) * 4);
    }
    return pixels;
}

} // namespace brulion::atrament
