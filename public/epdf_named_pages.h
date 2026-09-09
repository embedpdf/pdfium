// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef PUBLIC_EPDF_NAMED_PAGES_H_
#define PUBLIC_EPDF_NAMED_PAGES_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental EmbedPDF Extension API.
//
// The catalog's /Names /Pages and /Names /Templates name trees
// (ISO 32000-2, 7.7.4 and 12.7.7): registrations of a text key to a page
// object. /Pages keys name VISIBLE pages that also sit in the page tree;
// /Templates keys name invisible template pages (/Type /Template, no
// /Parent) that live outside the page tree and can never be loaded as a
// page.
//
// This API never interprets key text. Acrobat's stamp-library convention
// ("identifier=label") is a consumer concern. Keys are exchanged as UTF-16LE
// (FPDF_WIDESTRING in, FPDF_WCHAR buffers out), decoded from and encoded to
// PDF text strings by the runtime; a key is identified by its decoded text.
//
// Writes touch /Names /Pages only, store the value as an INDIRECT reference
// to the page dictionary, and go through the mutable catalog path so layer
// documents capture the change.

// Which tree an entry belongs to.
#define EPDF_NAMED_PAGE_TREE_PAGES 0
#define EPDF_NAMED_PAGE_TREE_TEMPLATES 1

// What an entry's value resolves to.
// A page dictionary in this document's page tree.
#define EPDF_NAMED_PAGE_KIND_PAGE 0
// A /Type /Template dictionary outside the page tree.
#define EPDF_NAMED_PAGE_KIND_TEMPLATE 1
// Null, missing, not a dictionary, or a dictionary that is neither.
#define EPDF_NAMED_PAGE_KIND_DANGLING 2

// Number of entries in |tree|, in tree (key-sorted) order.
//
//   document - handle to the document.
//   tree     - EPDF_NAMED_PAGE_TREE_PAGES or EPDF_NAMED_PAGE_TREE_TEMPLATES.
//
// Returns the entry count (0 when the tree is absent), or -1 on error.
FPDF_EXPORT int FPDF_CALLCONV EPDFDoc_GetNamedPageCount(FPDF_DOCUMENT document,
                                                        int tree);

// Read the entry at |index| of |tree|.
//
//   document - handle to the document.
//   tree     - EPDF_NAMED_PAGE_TREE_PAGES or EPDF_NAMED_PAGE_TREE_TEMPLATES.
//   index    - entry index, 0 <= index < EPDFDoc_GetNamedPageCount().
//   buffer   - receives the decoded key as UTF-16LE with a NUL terminator;
//              may be NULL to query the required size.
//   buflen   - size of |buffer| in bytes.
//   obj_num  - receives the value's object number (0 when dangling); may be
//              NULL.
//   kind     - receives one of EPDF_NAMED_PAGE_KIND_*; may be NULL.
//
// Returns the number of bytes needed for the key including the terminator
// (the key is copied only when |buflen| is large enough), or 0 on error.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFDoc_GetNamedPageAt(FPDF_DOCUMENT document,
                       int tree,
                       int index,
                       FPDF_WCHAR* buffer,
                       unsigned long buflen,
                       unsigned int* obj_num,
                       int* kind);

// Create or replace the /Names /Pages registration |key| -> page.
//
//   document     - handle to the document.
//   key          - UTF-16LE zero-terminated key text; must be non-empty.
//   page_obj_num - object number of a page in this document's page tree.
//
// An existing registration with the same decoded key is replaced (its old
// value dropped), so a set is idempotent. Fails when |page_obj_num| is not a
// page in the page tree (templates cannot be registered here).
//
// Returns true on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_SetNamedPage(FPDF_DOCUMENT document,
                     FPDF_WIDESTRING key,
                     unsigned int page_obj_num);

// Remove the /Names /Pages registration |key|. The page itself is untouched.
//
//   document - handle to the document.
//   key      - UTF-16LE zero-terminated key text.
//
// Returns true when a registration was removed, false when none matched or
// on error.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFDoc_RemoveNamedPage(FPDF_DOCUMENT document, FPDF_WIDESTRING key);

// Remove every /Names /Pages registration whose value is |page_obj_num|.
// Called by EPDFDoc_DeletePageByObjectNumber() before the page leaves the
// tree, so a deleted page never leaves a dangling registration behind.
//
//   document     - handle to the document.
//   page_obj_num - object number of the page.
//
// Returns the number of registrations removed, or -1 on error.
FPDF_EXPORT int FPDF_CALLCONV
EPDFDoc_RemoveNamedPagesForPage(FPDF_DOCUMENT document,
                                unsigned int page_obj_num);

#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // PUBLIC_EPDF_NAMED_PAGES_H_
