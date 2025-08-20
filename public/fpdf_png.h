#ifndef PUBLIC_FPDF_PNG_H_
#define PUBLIC_FPDF_PNG_H_

#include "fpdfview.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Compress RGBA to PNG. Returns byte size, or 0 on error.
// compression: zlib level [0..9].
// Caller MUST free the returned buffer with the module's free().
FPDF_EXPORT size_t FPDF_CALLCONV
EPDF_PNG_EncodeRGBA(uint8_t* rgba,
                    int width,
                    int height,
                    int stride,
                    int compression,
                    uint8_t** out_ptr);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PUBLIC_FPDF_PNG_H_