#pragma once

// DMA-BUF export of the worker's render-target textures (step 4 of
// docs/plans/client-server-refactor.md): wrap a GL texture in an EGLImage
// and export it as dma-buf plane fds via EGL_MESA_image_dma_buf_export.
// The fds are kernel-refcounted references to the GPU buffer — the main
// process imports them once per ring buffer and samples zero-copy.

#include "ipc/Protocol.h"

namespace texture_export {

// Both EGL_MESA_image_dma_buf_export and EGL_KHR_gl_texture_2D_image
// available on this display?
bool dmabufAvailable(void *egl_display);

// Export `tex` (GL_TEXTURE_2D, already allocated). Fills the plane info of
// `ann` (fourcc, modifier, num_planes, stride[], offset[]) and up to
// ipc::kMaxFds fds. Returns the fd count, or 0 on failure. The caller owns
// the fds (close after sending).
int exportDmabuf(void *egl_display, void *egl_context, unsigned tex,
                 ipc::TextureAnnounceMsg &ann, int fds[ipc::kMaxFds]);

// EGL_ANDROID_native_fence_sync available on this display?
bool fenceAvailable(void *egl_display);

// Inserts a fence after all GL commands issued so far (flushing them) and
// returns its FD for the importer to wait on, or -1 on failure — the
// caller must then fall back to glFinish. Caller owns the fd.
int createFenceFd(void *egl_display);

} // namespace texture_export
