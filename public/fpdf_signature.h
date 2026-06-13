// Copyright 2020 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PUBLIC_FPDF_SIGNATURE_H_
#define PUBLIC_FPDF_SIGNATURE_H_

// NOLINTNEXTLINE(build/include)
#include "fpdfview.h"

// NOLINTNEXTLINE(build/include)
#include "fpdf_revision.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Experimental API.
// Function: FPDF_GetSignatureCount
//          Get total number of signatures in the document.
// Parameters:
//          document    -   Handle to document. Returned by FPDF_LoadDocument().
// Return value:
//          Total number of signatures in the document on success, -1 on error.
FPDF_EXPORT int FPDF_CALLCONV FPDF_GetSignatureCount(FPDF_DOCUMENT document);

// Experimental API.
// Function: FPDF_GetSignatureObject
//          Get the Nth signature of the document.
// Parameters:
//          document    -   Handle to document. Returned by FPDF_LoadDocument().
//          index       -   Index into the array of signatures of the document.
// Return value:
//          Returns the handle to the signature, or NULL on failure. The caller
//          does not take ownership of the returned FPDF_SIGNATURE. Instead, it
//          remains valid until FPDF_CloseDocument() is called for the document.
FPDF_EXPORT FPDF_SIGNATURE FPDF_CALLCONV
FPDF_GetSignatureObject(FPDF_DOCUMENT document, int index);

// Experimental API.
// Function: FPDFSignatureObj_GetContents
//          Get the contents of a signature object.
// Parameters:
//          signature   -   Handle to the signature object. Returned by
//                          FPDF_GetSignatureObject().
//          buffer      -   The address of a buffer that receives the contents.
//          length      -   The size, in bytes, of |buffer|.
// Return value:
//          Returns the number of bytes in the contents on success, 0 on error.
//
// For public-key signatures, |buffer| is either a DER-encoded PKCS#1 binary or
// a DER-encoded PKCS#7 binary. If |length| is less than the returned length, or
// |buffer| is NULL, |buffer| will not be modified.
FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetContents(FPDF_SIGNATURE signature,
                             void* buffer,
                             unsigned long length);

// Experimental API.
// Function: FPDFSignatureObj_GetByteRange
//          Get the byte range of a signature object.
// Parameters:
//          signature   -   Handle to the signature object. Returned by
//                          FPDF_GetSignatureObject().
//          buffer      -   The address of a buffer that receives the
//                          byte range.
//          length      -   The size, in ints, of |buffer|.
// Return value:
//          Returns the number of ints in the byte range on
//          success, 0 on error.
//
// |buffer| is an array of pairs of integers (starting byte offset,
// length in bytes) that describes the exact byte range for the digest
// calculation. If |length| is less than the returned length, or
// |buffer| is NULL, |buffer| will not be modified.
FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetByteRange(FPDF_SIGNATURE signature,
                              int* buffer,
                              unsigned long length);

// Experimental API.
// Function: FPDFSignatureObj_GetSubFilter
//          Get the encoding of the value of a signature object.
// Parameters:
//          signature   -   Handle to the signature object. Returned by
//                          FPDF_GetSignatureObject().
//          buffer      -   The address of a buffer that receives the encoding.
//          length      -   The size, in bytes, of |buffer|.
// Return value:
//          Returns the number of bytes in the encoding name (including the
//          trailing NUL character) on success, 0 on error.
//
// The |buffer| is always encoded in 7-bit ASCII. If |length| is less than the
// returned length, or |buffer| is NULL, |buffer| will not be modified.
FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetSubFilter(FPDF_SIGNATURE signature,
                              char* buffer,
                              unsigned long length);

// Experimental API.
// Function: FPDFSignatureObj_GetReason
//          Get the reason (comment) of the signature object.
// Parameters:
//          signature   -   Handle to the signature object. Returned by
//                          FPDF_GetSignatureObject().
//          buffer      -   The address of a buffer that receives the reason.
//          length      -   The size, in bytes, of |buffer|.
// Return value:
//          Returns the number of bytes in the reason on success, 0 on error.
//
// Regardless of the platform, the |buffer| is always in UTF-16LE encoding. The
// string is terminated by a UTF16 NUL character. If |length| is less than the
// returned length, or |buffer| is NULL, |buffer| will not be modified.
FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetReason(FPDF_SIGNATURE signature,
                           void* buffer,
                           unsigned long length);

// Experimental API.
// Function: FPDFSignatureObj_GetTime
//          Get the time of signing of a signature object.
// Parameters:
//          signature   -   Handle to the signature object. Returned by
//                          FPDF_GetSignatureObject().
//          buffer      -   The address of a buffer that receives the time.
//          length      -   The size, in bytes, of |buffer|.
// Return value:
//          Returns the number of bytes in the encoding name (including the
//          trailing NUL character) on success, 0 on error.
//
// The |buffer| is always encoded in 7-bit ASCII. If |length| is less than the
// returned length, or |buffer| is NULL, |buffer| will not be modified.
//
// The format of time is expected to be D:YYYYMMDDHHMMSS+XX'YY', i.e. it's
// percision is seconds, with timezone information. This value should be used
// only when the time of signing is not available in the (PKCS#7 binary)
// signature.
FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetTime(FPDF_SIGNATURE signature,
                         char* buffer,
                         unsigned long length);

// Experimental API.
// Function: FPDFSignatureObj_GetDocMDPPermission
//          Get the DocMDP permission of a signature object.
// Parameters:
//          signature   -   Handle to the signature object. Returned by
//                          FPDF_GetSignatureObject().
// Return value:
//          Returns the permission (1, 2 or 3) on success, 0 on error.
FPDF_EXPORT unsigned int FPDF_CALLCONV
FPDFSignatureObj_GetDocMDPPermission(FPDF_SIGNATURE signature);

// SubFilter values for digital signature dictionaries.
//
// These map to the /SubFilter name in a signature value dictionary (/V).
// The choice of SubFilter determines the signature format and validation
// rules that PDF processors must follow.
typedef enum EPDF_SIG_SUBFILTER {
  // /adbe.pkcs7.detached -- Standard PKCS#7 detached signatures.
  // Used for approval and certification signatures.
  // Note: deprecated in PDF 2.0 (ISO 32000-2) in favor of
  // ETSI.CAdES.detached, but widely supported for compatibility.
  EPDF_SIG_SUBFILTER_ADBE_PKCS7_DETACHED = 0,

  // /ETSI.CAdES.detached -- CAdES signatures per ETSI TS 102 778 / EN 319 142.
  // Preferred for PAdES profiles (B-B, B-T, B-LT, B-LTA) and eIDAS compliance.
  // Used for approval and certification signatures.
  EPDF_SIG_SUBFILTER_ETSI_CADES_DETACHED = 1,

  // /ETSI.RFC3161 -- Document timestamp signatures per RFC 3161.
  // NOT for signer identity signatures. This is exclusively for document
  // timestamps, typically added as the final revision in a PAdES B-LTA flow.
  // When used, EPDFSig_PrepareSignatureDict sets /Type /DocTimeStamp
  // instead of /Type /Sig.
  EPDF_SIG_SUBFILTER_ETSI_RFC3161 = 2,
} EPDF_SIG_SUBFILTER;

// Experimental EmbedPDF Extension API.
// Prepare a signature value dictionary (/V) on a Sig field widget.
//
// Creates the /V dict as a new indirect object on the field's parent
// dictionary. The /V dict contains:
//   /Type       -- /Sig (or /DocTimeStamp when sub_filter is ETSI_RFC3161)
//   /Filter     -- /Adobe.PPKLite
//   /SubFilter  -- per |sub_filter| enum
//   /ByteRange  -- [0 0000000000 0000000000 0000000000] (placeholder)
//   /Contents   -- <0000...0000> (hex placeholder, 2 * contents_size chars)
//
// Intended lifecycle:
//   1. Call this function to prepare the /V dict in the document model.
//   2. Optionally call EPDFSig_SetReason(), EPDFSig_SetLocation(), etc.
//   3. Save the document incrementally (FPDF_INCREMENTAL flag).
//   4. In the saved bytes, locate the /ByteRange and /Contents placeholders.
//   5. Compute actual ByteRange offsets and patch them in-place.
//   6. Hash the byte spans described by ByteRange.
//   7. Generate a CMS/PKCS#7 blob (or RFC 3161 timestamp token).
//   8. Hex-encode the blob and patch it into the /Contents placeholder.
//
//   annot          - handle to a Sig field widget annotation (from
//                    EPDFPage_CreateFormField with FPDF_FORMFIELD_SIGNATURE,
//                    or an existing unsigned Sig widget).
//   sub_filter     - one of EPDF_SIG_SUBFILTER_*.
//   contents_size  - placeholder size for /Contents in bytes.
//                    The hex string in the PDF will be 2x this length.
//                    Recommended: 8192 for plain PKCS#7 signatures,
//                    16384 for PAdES B-T (signature + timestamp),
//                    32768 for PAdES B-LTA with revocation data.
//
// Returns true on success, false if annot is not a Sig widget, the field
// already has a /V dict, or on any other error.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_PrepareSignatureDict(FPDF_ANNOTATION annot,
                             EPDF_SIG_SUBFILTER sub_filter,
                             unsigned long contents_size);

// Experimental EmbedPDF Extension API.
// Set the /Reason string on an already-prepared signature value dict.
// Must be called after EPDFSig_PrepareSignatureDict and before save.
//
//   annot  - handle to a Sig field widget annotation.
//   reason - the reason string (UTF-16LE). Pass NULL to remove.
//
// Returns true on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetReason(FPDF_ANNOTATION annot, FPDF_WIDESTRING reason);

// Experimental EmbedPDF Extension API.
// Set the /Location string on an already-prepared signature value dict.
// Must be called after EPDFSig_PrepareSignatureDict and before save.
//
//   annot    - handle to a Sig field widget annotation.
//   location - the location string (UTF-16LE). Pass NULL to remove.
//
// Returns true on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetLocation(FPDF_ANNOTATION annot, FPDF_WIDESTRING location);

// Experimental EmbedPDF Extension API.
// Set the /ContactInfo string on an already-prepared signature value dict.
// Must be called after EPDFSig_PrepareSignatureDict and before save.
//
//   annot        - handle to a Sig field widget annotation.
//   contact_info - the contact info string (UTF-16LE). Pass NULL to remove.
//
// Returns true on success.
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetContactInfo(FPDF_ANNOTATION annot, FPDF_WIDESTRING contact_info);

// Experimental EmbedPDF Extension API.
// Make a signature a certification (DocMDP) signature.
//
// This performs two actions required by the PDF spec (ISO 32000-2, 12.8.2.2):
//
//   1. Adds a /Reference entry on the signature's /V dict:
//      /Reference [ << /TransformMethod /DocMDP
//                      /Type /SigRef
//                      /TransformParams << /P |permission|
//                                         /V /1.2
//                                         /Type /TransformParams >> >> ]
//
//   2. Sets the document catalog's /Perms/DocMDP entry to point at
//      the signature value dictionary, so PDF processors can locate
//      the certification signature from the catalog.
//
// Only one certification signature is allowed per document (per spec).
// This function checks the catalog /Perms/DocMDP entry and returns false
// if a certification signature already exists in the document, regardless
// of which field it is on.
//
//   document    - handle to the document (needed to access the catalog).
//   annot       - handle to a Sig field widget whose /V dict has been
//                 prepared via EPDFSig_PrepareSignatureDict.
//   permission  - DocMDP permission level, must be 1, 2, or 3:
//                   1 = no changes allowed (except DSS/timestamps)
//                   2 = form filling, signing, and page templates
//                   3 = same as 2, plus annotation create/delete/modify
//
// Returns true on success, false if:
//   - annot is not a prepared Sig widget
//   - permission is not 1, 2, or 3
//   - a certification signature already exists in the document catalog
FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetDocMDP(FPDF_DOCUMENT document,
                   FPDF_ANNOTATION annot,
                   int permission);

// Experimental EmbedPDF Extension API.
// Get /Location from a signature object's /V dict.
//
//   signature - handle to the signature object.
//   buffer    - the address of a buffer that receives the location.
//   length    - the size, in bytes, of |buffer|.
//
// Returns the number of bytes in the location on success, 0 on error.
// The |buffer| is always in UTF-16LE encoding, terminated by a UTF16 NUL.
// If |length| is less than the returned length, or |buffer| is NULL,
// |buffer| will not be modified.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFSig_GetLocation(FPDF_SIGNATURE signature,
                     void* buffer,
                     unsigned long length);

// Experimental EmbedPDF Extension API.
// Get /ContactInfo from a signature object's /V dict.
//
//   signature - handle to the signature object.
//   buffer    - the address of a buffer that receives the contact info.
//   length    - the size, in bytes, of |buffer|.
//
// Returns the number of bytes in the contact info on success, 0 on error.
// The |buffer| is always in UTF-16LE encoding, terminated by a UTF16 NUL.
// If |length| is less than the returned length, or |buffer| is NULL,
// |buffer| will not be modified.
FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFSig_GetContactInfo(FPDF_SIGNATURE signature,
                        void* buffer,
                        unsigned long length);

// Experimental EmbedPDF Extension API.
// Get the FPDF_SIGNATURE handle for a Sig field widget annotation.
//
// Bridges the annotation world to the FPDFSignatureObj_Get* reader family.
// The returned handle can be passed to FPDFSignatureObj_GetContents,
// FPDFSignatureObj_GetReason, EPDFSig_GetLocation, etc.
//
//   annot - handle to an annotation. Must be a Sig field widget
//           (merged field/widget or child widget with Sig parent).
//
// Returns the FPDF_SIGNATURE handle on success, or NULL if the annotation
// is not a Sig field widget. The caller does not take ownership; the handle
// remains valid until FPDF_CloseDocument() is called.
FPDF_EXPORT FPDF_SIGNATURE FPDF_CALLCONV
EPDFSig_GetAnnotSignatureHandle(FPDF_ANNOTATION annot);

// ---- Signature-Revision Bridge APIs ----
// These functions cross both the signature and revision domains.
// Pure revision APIs are in fpdf_revision.h.

// Experimental EmbedPDF Extension API.
// Get the signature associated with a revision, if any.
// Requires the document handle to enumerate signature fields.
// If multiple signatures map to the same revision, returns the first match.
// Returns NULL if the revision has no signature.
FPDF_EXPORT FPDF_SIGNATURE FPDF_CALLCONV
EPDFRevision_GetSignature(FPDF_DOCUMENT document, EPDF_REVISION revision);

// Experimental EmbedPDF Extension API.
// Get the revision index that a signature belongs to.
// Returns -1 on error or if unmappable.
FPDF_EXPORT int FPDF_CALLCONV
EPDFSig_GetSignatureRevision(FPDF_DOCUMENT document,
                             FPDF_SIGNATURE signature);

// DocMDP compliance status values.
#define EPDF_DOCMDP_COMPLIANT 0
#define EPDF_DOCMDP_VIOLATED 1
#define EPDF_DOCMDP_NOT_APPLICABLE 2
#define EPDF_DOCMDP_UNSUPPORTED 3
#define EPDF_DOCMDP_INDETERMINATE 4

// Experimental EmbedPDF Extension API.
// Check DocMDP compliance between the certified revision and a later revision.
// Automatically finds the certification signature and its permission from
// catalog /Perms/DocMDP.
//
//   document        - document handle.
//   check_revision  - revision to check (-1 means current/latest).
//
// Returns one of the EPDF_DOCMDP_* status values.
FPDF_EXPORT int FPDF_CALLCONV
EPDFSig_CheckDocMDPCompliance(FPDF_DOCUMENT document,
                              int check_revision);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // PUBLIC_FPDF_SIGNATURE_H_
