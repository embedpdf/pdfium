#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"
#include "public/fpdf_png.h"

namespace {

struct MemBuf {
  uint8_t* data = nullptr;
  size_t size = 0;
  size_t cap = 0;
};

static inline int ClampCompression(int c) {
  return c < 0 ? 0 : (c > 9 ? 9 : c);
}

static void EnsureCap(png_structp png_ptr, MemBuf* m, size_t need) {
  if (m->size + need <= m->cap) return;
  size_t new_cap = m->cap ? m->cap : 8192;
  while (new_cap < m->size + need) new_cap *= 2;
  uint8_t* p = static_cast<uint8_t*>(realloc(m->data, new_cap));  // <-- Emscripten heap
  if (!p) png_error(png_ptr, "EPDF_PNG: OOM");
  m->data = p;
  m->cap = new_cap;
}

static void PngWrite(png_structp png_ptr, png_bytep data, png_size_t len) {
  MemBuf* m = static_cast<MemBuf*>(png_get_io_ptr(png_ptr));
  EnsureCap(png_ptr, m, len);
  memcpy(m->data + m->size, data, len);
  m->size += len;
}

}  // namespace

extern "C" size_t EPDF_PNG_EncodeRGBA(uint8_t* rgba,
                                      int width,
                                      int height,
                                      int stride,
                                      int compression,
                                      uint8_t** out_ptr) {
  if (!rgba || !out_ptr || width <= 0 || height <= 0 || stride <= 0)
    return 0;
  *out_ptr = nullptr;

  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) return 0;

  png_infop info = png_create_info_struct(png);
  if (!info) { png_destroy_write_struct(&png, nullptr); return 0; }

  if (setjmp(png_jmpbuf(png))) {            // libpng longjmp path
    png_destroy_write_struct(&png, &info);
    // m.data (if any) is malloc'ed; we intentionally leak nothing here
    // because we only assign *out_ptr if success below.
    return 0;
  }

  MemBuf m;
  png_set_write_fn(png, &m, PngWrite, nullptr);

  png_set_IHDR(png, info,
               static_cast<png_uint_32>(width),
               static_cast<png_uint_32>(height),
               8,
               PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_DEFAULT,
               PNG_FILTER_TYPE_DEFAULT);

  png_set_compression_level(png, ClampCompression(compression));
  png_write_info(png, info);

  // Build row pointers into caller's RGBA buffer (stride may exceed width*4).
  png_bytep* rows = static_cast<png_bytep*>(malloc(sizeof(png_bytep) * height));
  if (!rows) { png_destroy_write_struct(&png, &info); return 0; }
  for (int y = 0; y < height; ++y)
    rows[y] = rgba + static_cast<size_t>(y) * static_cast<size_t>(stride);

  png_write_image(png, rows);
  png_write_end(png, info);

  free(rows);
  png_destroy_write_struct(&png, &info);

  if (m.size == 0) { free(m.data); return 0; }
  *out_ptr = m.data;          // <-- Caller frees with wasmExports.free(ptr)
  return m.size;
} 