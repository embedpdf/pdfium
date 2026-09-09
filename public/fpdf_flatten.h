// Copyright 2014 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef PUBLIC_FPDF_FLATTEN_H_
#define PUBLIC_FPDF_FLATTEN_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

// Flatten operation failed.
#define FLATTEN_FAIL 0
// Flatten operation succeed.
#define FLATTEN_SUCCESS 1
// Nothing to be flattened.
#define FLATTEN_NOTHINGTODO 2

// Flatten for normal display.
#define FLAT_NORMALDISPLAY 0
// Flatten for print.
#define FLAT_PRINT 1

// Per-annotation outcome of EPDFPage_FlattenAnnotations().
// Painted into the page content and removed from /Annots.
#define EPDF_FLATTEN_STATUS_APPLIED 0
// Belongs to the page but was left in place: hidden for the usage, a Popup,
// or without a usable normal appearance.
#define EPDF_FLATTEN_STATUS_SKIPPED 1
// Not an annotation of |page| (or a null handle) — the call fails whole.
#define EPDF_FLATTEN_STATUS_NOT_ON_PAGE 2

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Flatten annotations and form fields into the page contents.
//
//   page  - handle to the page.
//   nFlag - One of the |FLAT_*| values denoting the page usage.
//
// Returns one of the |FLATTEN_*| values.
//
// Currently, all failures return |FLATTEN_FAIL| with no indication of the
// cause.
FPDF_EXPORT int FPDF_CALLCONV FPDFPage_Flatten(FPDF_PAGE page, int nFlag);

// Experimental EmbedPDF Extension API.
// Flatten every eligible annotation appearance on a page. This is the
// layer-safe counterpart to FPDFPage_Flatten(): the page dictionary is
// promoted before any mutation.
//
// Only annotations whose normal appearance was successfully added to page
// content are removed. Hidden, usage-ineligible, popup, malformed, and
// appearance-less annotations remain in /Annots. Flattened widgets are also
// detached from the AcroForm field tree; a separate logical field remains as
// an unplaced field.
//
//   page  - handle to the page.
//   usage - exactly one of the |FLAT_*| values.
//
// Returns FLATTEN_SUCCESS if at least one annotation was flattened,
// FLATTEN_NOTHINGTODO if the page has no eligible usable appearances, or
// FLATTEN_FAIL for invalid arguments.
FPDF_EXPORT int FPDF_CALLCONV EPDFPage_Flatten(FPDF_PAGE page, int usage);

// Experimental EmbedPDF Extension API.
// Flatten a chosen SET of annotations of a page — EPDFPage_Flatten() with a
// scope. Eligibility, appearance resolution, widget detachment, and layer
// promotion are identical: annotations that are hidden for |usage|, Popups,
// or have no usable normal appearance are left in /Annots and reported
// EPDF_FLATTEN_STATUS_SKIPPED, so a caller can say "2 of 3 flattened".
//
// All-or-nothing on validity: when any handle is null or not an annotation
// of |page|, nothing changes, that entry reports
// EPDF_FLATTEN_STATUS_NOT_ON_PAGE, and the call returns FLATTEN_FAIL.
//
// The caller must close every handle with FPDFPage_CloseAnnot() after this
// call; a flattened annotation is gone from the page, so its handle must not
// be used again before it is closed.
//
//   page        - handle to the page owning the annotations.
//   annots      - handles of the annotations to flatten.
//   annot_count - number of entries; > 0.
//   usage       - exactly one of the |FLAT_*| values.
//   statuses    - receives one EPDF_FLATTEN_STATUS_* per entry; may be NULL.
//
// Returns FLATTEN_SUCCESS if at least one annotation was flattened,
// FLATTEN_NOTHINGTODO if every entry was skipped, or FLATTEN_FAIL for invalid
// arguments (including an entry that is not on |page|).
FPDF_EXPORT int FPDF_CALLCONV
EPDFPage_FlattenAnnotations(FPDF_PAGE page,
                            FPDF_ANNOTATION* annots,
                            int annot_count,
                            int usage,
                            int* statuses);

// Experimental EmbedPDF Extension API.
// Flatten the normal appearances of a chosen set of annotations of |page|
// into a NEW single-page document — the same placement as
// EPDFPage_FlattenAnnotations() (ISO 32000-2 12.5.5 algorithm 8.1, /Matrix
// honored), aimed at a fresh page instead of the source page. The page is
// the union of the annotations' /Rect with its lower-left at the origin;
// every appearance lands exactly where the source page shows it. Forms and
// their resources are deep-cloned once; resources shared between
// annotations are deduplicated. The source document is untouched.
//
// Eligibility follows FLAT_NORMALDISPLAY. All-or-nothing: if any entry is
// null, not on |page|, hidden, a Popup, or without a usable normal
// appearance, no document is produced — a stamp silently missing one of its
// parts would be worse than an error.
//
//   page        - handle to the page owning the annotations.
//   annots      - handles of the annotations to export.
//   annot_count - number of entries; > 0.
//
// Returns a new document (the caller closes it with FPDF_CloseDocument()),
// or NULL.
FPDF_EXPORT FPDF_DOCUMENT FPDF_CALLCONV
EPDFPage_ExportAnnotationsAsDocument(FPDF_PAGE page,
                                     FPDF_ANNOTATION* annots,
                                     int annot_count);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_FPDF_FLATTEN_H_
