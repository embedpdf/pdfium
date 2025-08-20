#include <stddef.h>
#include <stdint.h>

#include "public/fpdf_webp.h"
#include "webp/encode.h"

// Clamp helper to keep inputs sane.
static inline float ClampQuality(float q) {
  if (q < 0.0f) return 0.0f;
  if (q > 100.0f) return 100.0f;
  return q;
}

extern "C" size_t EPDF_WebP_EncodeRGBA(uint8_t* rgba,
                                       int width,
                                       int height,
                                       int stride,
                                       float quality,
                                       uint8_t** out_ptr) {
  if (!rgba || !out_ptr || width <= 0 || height <= 0 || stride <= 0)
    return 0;

  *out_ptr = nullptr;
  const float q = ClampQuality(quality);
  // Lossy encoder (fast, good for docs). Returns 0 on failure.
  return WebPEncodeRGBA(rgba, width, height, stride, q, out_ptr);
}

extern "C" void EPDF_WebP_Free(void* p) {
  if (p)
    WebPFree(p);
}