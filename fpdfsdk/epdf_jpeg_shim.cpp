#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <setjmp.h>

// jpeglib.h may require stdio.h to be included first.
#include <stdio.h>
#include "jpeglib.h"
#include "jerror.h" // Explicitly include for JERR_* macros.

#include "public/fpdf_jpeg.h"

namespace {

// A buffer for holding the output JPEG data.
// It's managed by libjpeg's destination manager callbacks.
struct MemDestMgr {
  // Public part of the manager. MUST be the first field.
  jpeg_destination_mgr pub;
  uint8_t* data = nullptr;
  size_t size = 0;
  size_t cap = 0;
};

// Custom error handler for libjpeg to use with setjmp/longjmp.
struct JpegErrorMgr {
  // Public part. MUST be the first field.
  jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
};

// Custom error exit routine for libjpeg.
void JpegErrorExit(j_common_ptr cinfo) {
  JpegErrorMgr* err = reinterpret_cast<JpegErrorMgr*>(cinfo->err);
  // Return control to the setjmp point.
  longjmp(err->setjmp_buffer, 1);
}

// The following three functions are callbacks for our custom destination manager.

// 1. Called before compression starts to prepare the buffer.
void InitDestination(j_compress_ptr cinfo) {
  MemDestMgr* dest = reinterpret_cast<MemDestMgr*>(cinfo->dest);
  dest->cap = 8192; // Initial buffer capacity.
  dest->data = static_cast<uint8_t*>(malloc(dest->cap));
  if (!dest->data) {
    ERREXIT(cinfo, JERR_OUT_OF_MEMORY);
  }
  dest->pub.next_output_byte = dest->data;
  dest->pub.free_in_buffer = dest->cap;
  dest->size = 0;
}

// 2. Called by libjpeg when its output buffer is full.
boolean EmptyOutputBuffer(j_compress_ptr cinfo) {
  MemDestMgr* dest = reinterpret_cast<MemDestMgr*>(cinfo->dest);
  size_t old_cap = dest->cap;
  size_t new_cap = old_cap * 2; // Double the buffer size.
  uint8_t* new_data = static_cast<uint8_t*>(realloc(dest->data, new_cap));
  if (!new_data) {
    ERREXIT(cinfo, JERR_OUT_OF_MEMORY);
  }
  dest->data = new_data;
  dest->cap = new_cap;
  dest->pub.next_output_byte = dest->data + old_cap;
  dest->pub.free_in_buffer = dest->cap - old_cap;
  return TRUE;
}

// 3. Called after compression is finished to finalize the buffer.
void TermDestination(j_compress_ptr cinfo) {
  MemDestMgr* dest = reinterpret_cast<MemDestMgr*>(cinfo->dest);
  dest->size = dest->cap - dest->pub.free_in_buffer;
}

// Helper to attach our custom destination manager to libjpeg.
void JpegMemDest(j_compress_ptr cinfo, MemDestMgr* dest) {
  cinfo->dest = reinterpret_cast<jpeg_destination_mgr*>(dest);
  dest->pub.init_destination = InitDestination;
  dest->pub.empty_output_buffer = EmptyOutputBuffer;
  dest->pub.term_destination = TermDestination;
}

// Clamp helper to keep quality value within the valid [0, 100] range.
static inline int ClampQuality(int q) {
  return q < 0 ? 0 : (q > 100 ? 100 : q);
}

}  // namespace

extern "C" size_t EPDF_JPEG_EncodeRGBA(uint8_t* rgba,
                                       int width,
                                       int height,
                                       int stride,
                                       int quality,
                                       uint8_t** out_ptr) {
  if (!rgba || !out_ptr || width <= 0 || height <= 0 || stride <= 0)
    return 0;
  *out_ptr = nullptr;

  jpeg_compress_struct cinfo;
  JpegErrorMgr jerr;
  MemDestMgr dest_mgr;
  uint8_t* rgb_row = nullptr;

  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = JpegErrorExit;

  // Establish the setjmp return context for handling errors.
  if (setjmp(jerr.setjmp_buffer)) {
    jpeg_destroy_compress(&cinfo);
    free(dest_mgr.data);
    free(rgb_row); // Also free the temp row buffer on error.
    return 0;
  }

  jpeg_create_compress(&cinfo);
  JpegMemDest(&cinfo, &dest_mgr);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;      // 3 for RGB.
  cinfo.in_color_space = JCS_RGB;  // Use standard RGB color space.

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, ClampQuality(quality), TRUE);

  jpeg_start_compress(&cinfo, TRUE);

  // Allocate a temporary buffer to hold one row of RGB data.
  size_t rgb_row_size = width * 3;
  rgb_row = static_cast<uint8_t*>(malloc(rgb_row_size));
  if (!rgb_row) {
    ERREXIT(&cinfo, JERR_OUT_OF_MEMORY);
  }

  // Feed rows of pixel data to the compressor.
  while (cinfo.next_scanline < cinfo.image_height) {
    uint8_t* rgba_row_pointer = rgba + cinfo.next_scanline * stride;
    // Convert this row from RGBA to RGB, skipping the alpha channel.
    for (int x = 0; x < width; ++x) {
      rgb_row[x * 3 + 0] = rgba_row_pointer[x * 4 + 0]; // R
      rgb_row[x * 3 + 1] = rgba_row_pointer[x * 4 + 1]; // G
      rgb_row[x * 3 + 2] = rgba_row_pointer[x * 4 + 2]; // B
    }
    jpeg_write_scanlines(&cinfo, &rgb_row, 1);
  }

  free(rgb_row);  // Clean up the temporary buffer.
  rgb_row = nullptr; // Avoid double-free in error handler.

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  if (dest_mgr.size == 0) {
    free(dest_mgr.data);
    return 0;
  }

  // Success!
  *out_ptr = dest_mgr.data; // Pass ownership to the caller.
  return dest_mgr.size;
}