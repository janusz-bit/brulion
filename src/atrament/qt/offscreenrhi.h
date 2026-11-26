#pragma once

// A QRhi without a window, for tests and tools (tst_atrament_qt, atrament-gallery --screenshot
// --renderer gpu): the GPU renderer draws into an offscreen texture that is read back.
//
// Vulkan works under any Qt platform plugin, `offscreen` included: when the plugin has no Vulkan
// of its own, the instance is a plain one loaded from libvulkan (no surface extensions — nothing
// is presented). OpenGL needs a platform plugin with OpenGL (xcb, wayland; offscreen only with
// GLX on an X display).

#include "base/color.h"
#include "qrhirenderer.h"

#include <QString>

#include <memory>
#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QOffscreenSurface;
class QRhiRenderPassDescriptor;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QVulkanInstance;
QT_END_NAMESPACE

namespace brulion::atrament {

class OffscreenRhi {
public:
    enum class Backend { Vulkan, OpenGL };

    // nullptr (and why, in `error`) when the backend cannot be created here.
    static std::unique_ptr<OffscreenRhi> create(Backend backend, QString *error = nullptr);
    ~OffscreenRhi();

    QRhi *rhi() const { return m_rhi.get(); }
    QVulkanInstance *vulkanInstance() const { return m_vulkan.get(); }
    // "Vulkan: llvmpipe (LLVM 21.1.8, 256 bits)".
    QString description() const;

    // Draws the renderer's frame (its size) into an RGBA8 texture cleared to `clear`, every colour ×
    // `opacity`, waits for the GPU and returns the pixels, top row first, premultiplied —
    // RasterTarget's layout (none without `readBack`: timing the frame alone). nullopt when the
    // renderer cannot draw on this QRhi.
    std::optional<std::vector<Pixel>> render(QRhiRenderer &renderer, Color clear, bool readBack = true,
                                             float opacity = 1);

private:
    OffscreenRhi();

    std::unique_ptr<QVulkanInstance> m_vulkan;
    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QRhi> m_rhi;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::unique_ptr<QRhiTextureRenderTarget> m_target;
};

} // namespace brulion::atrament
