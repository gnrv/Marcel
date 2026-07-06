#include "TextureExport.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdio>
#include <cstring>

namespace texture_export {

namespace {

bool hasExtension(EGLDisplay dpy, const char *name)
{
    const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
    return exts && strstr(exts, name);
}

PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;
PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC p_eglExportDMABUFImageQueryMESA;
PFNEGLEXPORTDMABUFIMAGEMESAPROC p_eglExportDMABUFImageMESA;

bool loadFunctions()
{
    if (p_eglExportDMABUFImageMESA)
        return true;
    p_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    p_eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    p_eglExportDMABUFImageQueryMESA = reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC>(
        eglGetProcAddress("eglExportDMABUFImageQueryMESA"));
    p_eglExportDMABUFImageMESA = reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEMESAPROC>(
        eglGetProcAddress("eglExportDMABUFImageMESA"));
    return p_eglCreateImageKHR && p_eglDestroyImageKHR &&
           p_eglExportDMABUFImageQueryMESA && p_eglExportDMABUFImageMESA;
}

} // namespace

bool dmabufAvailable(void *egl_display)
{
    EGLDisplay dpy = static_cast<EGLDisplay>(egl_display);
    return dpy != EGL_NO_DISPLAY &&
           hasExtension(dpy, "EGL_KHR_gl_texture_2D_image") &&
           hasExtension(dpy, "EGL_MESA_image_dma_buf_export") && loadFunctions();
}

int exportDmabuf(void *egl_display, void *egl_context, unsigned tex,
                 ipc::TextureAnnounceMsg &ann, int fds[ipc::kMaxFds])
{
    EGLDisplay dpy = static_cast<EGLDisplay>(egl_display);
    EGLContext ctx = static_cast<EGLContext>(egl_context);
    if (!loadFunctions())
        return 0;

    EGLImageKHR image = p_eglCreateImageKHR(
        dpy, ctx, EGL_GL_TEXTURE_2D_KHR,
        reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(tex)), nullptr);
    if (image == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "TextureExport: eglCreateImageKHR failed (0x%x)\n", eglGetError());
        return 0;
    }

    int fourcc = 0, num_planes = 0;
    EGLuint64KHR modifier = 0;
    EGLint strides[ipc::kMaxFds] = {}, offsets[ipc::kMaxFds] = {};
    int raw_fds[ipc::kMaxFds] = {-1, -1, -1, -1};
    bool ok = p_eglExportDMABUFImageQueryMESA(dpy, image, &fourcc, &num_planes, &modifier) &&
              num_planes > 0 && num_planes <= static_cast<int>(ipc::kMaxFds) &&
              p_eglExportDMABUFImageMESA(dpy, image, raw_fds, strides, offsets);
    // The exported fds are independent kernel references; the image (and
    // even the texture) can go away without invalidating them.
    p_eglDestroyImageKHR(dpy, image);
    if (!ok) {
        fprintf(stderr, "TextureExport: dma-buf export failed (0x%x)\n", eglGetError());
        return 0;
    }

    ann.transport = ipc::kTransportDmabuf;
    ann.fourcc = static_cast<uint32_t>(fourcc);
    ann.modifier = modifier;
    ann.num_planes = static_cast<uint32_t>(num_planes);
    for (int i = 0; i < num_planes; ++i) {
        ann.stride[i] = static_cast<uint32_t>(strides[i]);
        ann.offset[i] = static_cast<uint32_t>(offsets[i]);
        fds[i] = raw_fds[i];
    }
    return num_planes;
}

} // namespace texture_export
