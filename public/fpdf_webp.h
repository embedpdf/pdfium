#ifndef PUBLIC_FPDF_WEBP_H_
#define PUBLIC_FPDF_WEBP_H_

#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns number of bytes in output, or 0 on error.
FPDF_EXPORT size_t FPDF_CALLCONV
EPDF_WebP_EncodeRGBA(uint8_t* rgba,
                     int width,
                     int height,
                     int stride,
                     float quality,
                     uint8_t** out_ptr);

#ifdef __cplusplus
}
#endif

#endif        