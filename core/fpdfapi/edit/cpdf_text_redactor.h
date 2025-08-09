// Copyright 2025
// Use of this source code is governed by a BSD-style license.

#ifndef CORE_FPDFAPI_EDIT_CPDF_TEXT_REDACTOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_TEXT_REDACTOR_H_

#include "core/fxcrt/span.h"
#include "core/fxcrt/fx_coordinates.h"

class CPDF_Page;

// Redacts (removes) glyphs from text objects that intersect the given rect(s).
// Inputs are in PAGE USER SPACE (same space as highlights).
// If `recurse_forms` is true, contents of Form XObjects used on the page
// are also scanned and redacted. Edits inside a form regenerate that form’s
// content stream immediately. The page stream is NOT regenerated here.
//
// Returns true if anything changed.
bool RedactTextInRect(CPDF_Page* page,
                      const CFX_FloatRect& page_space_rect,
                      bool recurse_forms);

bool RedactTextInRects(CPDF_Page* page,
                       pdfium::span<const CFX_FloatRect> page_space_rects,
                       bool recurse_forms);

#endif  // CORE_FPDFAPI_EDIT_CPDF_TEXT_REDACTOR_H_