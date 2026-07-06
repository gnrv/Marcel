#include "TextureImport.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace texture_import {

namespace {

typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOES)(GLenum, void *);

PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;
PFNGLEGLIMAGETARGETTEXTURE2DOES p_glEGLImageTargetTexture2DOES;
bool has_modifier_ext = false;

bool hasEglExtension(EGLDisplay dpy, const char *name)
{
    const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
    return exts && strstr(exts, name);
}

bool hasGlExtension(const char *name)
{
    // Compatibility-profile query (the main window is GL 3.0 compat).
    const char *exts = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
    return exts && strstr(exts, name);
}

} // namespace

bool dmabufAvailable()
{
    EGLDisplay dpy = eglGetCurrentDisplay();
    if (dpy == EGL_NO_DISPLAY)
        return false; // GLX context: no import possible
    if (!hasEglExtension(dpy, "EGL_KHR_image_base") ||
        !hasEglExtension(dpy, "EGL_EXT_image_dma_buf_import"))
        return false;
    if (!hasGlExtension("GL_OES_EGL_image") && !hasGlExtension("GL_EXT_EGL_image_storage"))
        return false;
    has_modifier_ext = hasEglExtension(dpy, "EGL_EXT_image_dma_buf_import_modifiers");
    p_eglCreateImageKHR = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    p_eglDestroyImageKHR = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    p_glEGLImageTargetTexture2DOES = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOES>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    return p_eglCreateImageKHR && p_eglDestroyImageKHR && p_glEGLImageTargetTexture2DOES;
}

unsigned importDmabuf(const ipc::TextureAnnounceMsg &ann,
                      const int *fds, uint32_t num_fds)
{
    EGLDisplay dpy = eglGetCurrentDisplay();
    if (dpy == EGL_NO_DISPLAY || !p_eglCreateImageKHR ||
        num_fds < ann.num_planes || ann.num_planes == 0 || ann.num_planes > 4)
        return 0;

    static const EGLint fd_attr[4] = {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE1_FD_EXT,
                                      EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE3_FD_EXT};
    static const EGLint off_attr[4] = {EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
                                       EGL_DMA_BUF_PLANE2_OFFSET_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT};
    static const EGLint pitch_attr[4] = {EGL_DMA_BUF_PLANE0_PITCH_EXT, EGL_DMA_BUF_PLANE1_PITCH_EXT,
                                         EGL_DMA_BUF_PLANE2_PITCH_EXT, EGL_DMA_BUF_PLANE3_PITCH_EXT};
    static const EGLint mod_lo_attr[4] = {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
                                          EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
    static const EGLint mod_hi_attr[4] = {EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
                                          EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT, EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};

    std::vector<EGLint> attribs = {
        EGL_WIDTH, static_cast<EGLint>(ann.width),
        EGL_HEIGHT, static_cast<EGLint>(ann.height),
        EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(ann.fourcc),
    };
    for (uint32_t i = 0; i < ann.num_planes; ++i) {
        attribs.insert(attribs.end(), {fd_attr[i], fds[i],
                                       off_attr[i], static_cast<EGLint>(ann.offset[i]),
                                       pitch_attr[i], static_cast<EGLint>(ann.stride[i])});
        if (has_modifier_ext) {
            attribs.insert(attribs.end(),
                           {mod_lo_attr[i], static_cast<EGLint>(ann.modifier & 0xffffffffu),
                            mod_hi_attr[i], static_cast<EGLint>(ann.modifier >> 32)});
        }
    }
    attribs.push_back(EGL_NONE);

    EGLImageKHR image = p_eglCreateImageKHR(dpy, EGL_NO_CONTEXT,
                                            EGL_LINUX_DMA_BUF_EXT, nullptr,
                                            attribs.data());
    if (image == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "TextureImport: eglCreateImageKHR failed (0x%x)\n", eglGetError());
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    while (glGetError() != GL_NO_ERROR) {} // clear stale errors
    p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    GLenum err = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);
    p_eglDestroyImageKHR(dpy, image); // the texture keeps the buffer alive
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "TextureImport: glEGLImageTargetTexture2DOES failed (0x%x)\n", err);
        glDeleteTextures(1, &tex);
        return 0;
    }
    return tex;
}

} // namespace texture_import
