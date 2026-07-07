#pragma once

// DMA-BUF import into the main process's GL context (step 4 of
// docs/plans/client-server-refactor.md). Requires the GLFW window to have
// been created with the EGL context API — with GLX there is no EGLDisplay
// to import into, dmabufAvailable() returns false and the transport
// negotiation falls back to shm.

#include "ipc/Protocol.h"

namespace texture_import {

// Probe the *current* GL context: EGL with EGL_EXT_image_dma_buf_import
// and GL_OES_EGL_image? Call once after context creation.
bool dmabufAvailable();

// Wrap announced dma-buf planes in a GL texture (zero-copy, kernel-
// refcounted: the texture stays valid after the exporting process dies).
// Does not close the fds. Returns 0 on failure.
unsigned importDmabuf(const ipc::TextureAnnounceMsg &ann,
                      const int *fds, uint32_t num_fds);

// Can this context GPU-wait on native-fence FDs? (EGL_ANDROID_native_fence_sync
// + EGL_KHR_wait_sync on the current display.) Call once after context
// creation; advertised to the worker as ipc::kCapFenceSync.
bool fenceSyncAvailable();

// Queue a server-side wait for the fence carried by a FrameDone: subsequent
// GL commands (sampling the imported textures) execute only after the
// worker's rendering completed. Never blocks the CPU. Takes ownership of fd.
void waitFence(int fd);

} // namespace texture_import
