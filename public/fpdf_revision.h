// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_FPDF_REVISION_H_
#define PUBLIC_FPDF_REVISION_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Opaque handle to a document revision. Valid until FPDF_CloseDocument().
typedef const struct epdf_revision_t__* EPDF_REVISION;

// Opaque handle to a revision diff result. Caller must manage lifetime.
typedef const struct epdf_revision_diff_t__* EPDF_REVISION_DIFF;

// Experimental EmbedPDF Extension API.
// Get the number of incremental revisions in the document.
// Index 0 = oldest (original document).
// Returns -1 on error.
FPDF_EXPORT int FPDF_CALLCONV
EPDFRevision_GetCount(FPDF_DOCUMENT document);

// Experimental EmbedPDF Extension API.
// Get a revision handle by index. Index 0 = oldest.
// Returns NULL on error.
FPDF_EXPORT EPDF_REVISION FPDF_CALLCONV
EPDFRevision_Get(FPDF_DOCUMENT document, int index);

// Experimental EmbedPDF Extension API.
// Get the effective file end offset for a revision (64-bit out-param).
// The offset is the byte position immediately after the %%EOF marker
// for this revision's incremental save.
// WARNING: actual values may be truncated to 32 bits due to upstream
// GetTrailerEnds() limitation for files > 4 GB.
// Returns TRUE on success, FALSE on error.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFRevision_GetFileEnd(EPDF_REVISION revision,
                        unsigned long long* out_file_end);

// Experimental EmbedPDF Extension API.
// Compare two revisions and produce a diff of changed objects.
// Returns NULL on error. Caller takes ownership; free with
// EPDFRevisionDiff_Close().
FPDF_EXPORT EPDF_REVISION_DIFF FPDF_CALLCONV
EPDFRevision_Compare(FPDF_DOCUMENT document,
                     int older_revision,
                     int newer_revision);

// Experimental EmbedPDF Extension API.
// Close a diff handle returned by EPDFRevision_Compare.
FPDF_EXPORT void FPDF_CALLCONV
EPDFRevisionDiff_Close(EPDF_REVISION_DIFF diff);

// Experimental EmbedPDF Extension API.
// Get the number of changed objects in a diff.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFRevisionDiff_GetEntryCount(EPDF_REVISION_DIFF diff);

// Experimental EmbedPDF Extension API.
// Get a specific diff entry.
//   out_obj_num  - receives the object number.
//   out_category - receives 0=added, 1=modified, 2=freed.
// Returns TRUE on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFRevisionDiff_GetEntry(EPDF_REVISION_DIFF diff,
                          unsigned long index,
                          unsigned int* out_obj_num,
                          int* out_category);

// Semantic change category values returned by
// EPDFRevisionDiff_GetSemanticCategoryCounts and
// EPDFRevisionDiff_GetSemanticEntry.
#define EPDF_SEMANTIC_FORM_STATE_CHANGE 0
#define EPDF_SEMANTIC_ANNOTATION 1
#define EPDF_SEMANTIC_SIGNATURE 2
#define EPDF_SEMANTIC_DOCUMENT_TIMESTAMP 3
#define EPDF_SEMANTIC_DSS 4
#define EPDF_SEMANTIC_PAGE 5
#define EPDF_SEMANTIC_CATALOG 6
#define EPDF_SEMANTIC_OTHER 7

// Experimental EmbedPDF Extension API.
// Get semantic category counts for a diff. Lazily computes semantic
// classification on the first call for a given diff handle and caches the
// result. Requires the document handle for object-graph access during
// classification.
//
//   document       - document handle (needed for semantic classification).
//   diff           - diff handle from EPDFRevision_Compare().
//   category_buffer - receives SemanticChangeType values.
//   count_buffer    - receives the count for each category.
//   buffer_length   - number of slots in category_buffer and count_buffer.
//
// Returns the number of distinct non-zero categories.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFRevisionDiff_GetSemanticCategoryCounts(
    FPDF_DOCUMENT document,
    EPDF_REVISION_DIFF diff,
    int* category_buffer,
    unsigned long* count_buffer,
    unsigned long buffer_length);

// Experimental EmbedPDF Extension API.
// Get the number of resolved semantic entries in a diff.
// Lazily computes semantic classification on the first call and caches
// the result (same cache as GetSemanticCategoryCounts).
//
//   document - document handle (needed for semantic classification).
//   diff     - diff handle from EPDFRevision_Compare().
//
// Returns the number of resolved entries, or 0 on error.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFRevisionDiff_GetResolvedEntryCount(FPDF_DOCUMENT document,
                                       EPDF_REVISION_DIFF diff);

// Experimental EmbedPDF Extension API.
// Get a specific resolved semantic entry.
// Lazily computes semantic classification on the first call and caches
// the result (same cache as GetSemanticCategoryCounts).
//
//   document            - document handle (needed for semantic classification).
//   diff                - diff handle from EPDFRevision_Compare().
//   index               - zero-based index into the resolved entries.
//   out_changed_obj_num - receives the actual changed indirect object number.
//   out_target_obj_num  - receives the resolved logical target object number,
//                         or 0 if unavailable.
//   out_page_obj_num    - receives the owning page dictionary object number,
//                         or 0 if unavailable.
//   out_diff_category   - receives 0=added, 1=modified, 2=freed.
//   out_semantic_type   - receives one of the EPDF_SEMANTIC_* values.
//
// Returns TRUE on success, FALSE on error or out-of-range index.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFRevisionDiff_GetResolvedEntry(FPDF_DOCUMENT document,
                                  EPDF_REVISION_DIFF diff,
                                  unsigned long index,
                                  unsigned int* out_changed_obj_num,
                                  unsigned int* out_target_obj_num,
                                  unsigned int* out_page_obj_num,
                                  int* out_diff_category,
                                  int* out_semantic_type);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_FPDF_REVISION_H_
