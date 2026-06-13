// Copyright 2020 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdf_signature.h"

#include <vector>

#include "constants/form_fields.h"
#include "core/fpdfapi/page/cpdf_annotcontext.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_revision_classifier.h"
#include "core/fpdfapi/parser/cpdf_revision_diff.h"
#include "core/fpdfapi/parser/cpdf_revision_provider.h"
#include "core/fpdfapi/parser/cpdf_string.h"
#include "core/fpdfapi/parser/object_tree_traversal_util.h"
#include "core/fxcrt/compiler_specific.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/span.h"
#include "core/fxcrt/span_util.h"
#include "core/fxcrt/stl_util.h"
#include "fpdfsdk/cpdfsdk_helpers.h"

namespace {

// Fixed-width sentinel for /ByteRange placeholder slots 1-3.
// INT_MAX serializes as exactly 10 decimal digits ("2147483647"), ensuring the
// signing orchestrator can patch real values in-place without changing file
// length. Each patched value must be left-aligned, right-padded with spaces to
// exactly 10 characters.
constexpr int kByteRangePlaceholder = 2147483647;

RetainPtr<CPDF_Array> CreateByteRangePlaceholderArray(
    CPDF_Dictionary* pSigDict) {
  auto pByteRange = pSigDict->SetNewFor<CPDF_Array>("ByteRange");
  pByteRange->AppendNew<CPDF_Number>(0);
  pByteRange->AppendNew<CPDF_Number>(kByteRangePlaceholder);
  pByteRange->AppendNew<CPDF_Number>(kByteRangePlaceholder);
  pByteRange->AppendNew<CPDF_Number>(kByteRangePlaceholder);
  return pByteRange;
}

}  // namespace

static std::vector<RetainPtr<const CPDF_Dictionary>> CollectSignatures(
    CPDF_Document* doc) {
  std::vector<RetainPtr<const CPDF_Dictionary>> signatures;
  const CPDF_Dictionary* root = doc->GetRoot();
  if (!root) {
    return signatures;
  }

  RetainPtr<const CPDF_Dictionary> acro_form = root->GetDictFor("AcroForm");
  if (!acro_form) {
    return signatures;
  }

  RetainPtr<const CPDF_Array> fields = acro_form->GetArrayFor("Fields");
  if (!fields) {
    return signatures;
  }

  CPDF_ArrayLocker locker(std::move(fields));
  for (auto& field : locker) {
    RetainPtr<const CPDF_Dictionary> field_dict = field->GetDict();
    if (field_dict && field_dict->GetNameFor(pdfium::form_fields::kFT) ==
                          pdfium::form_fields::kSig) {
      signatures.push_back(std::move(field_dict));
    }
  }
  return signatures;
}

namespace {

RetainPtr<CPDF_Dictionary> GetSigFieldDict(FPDF_ANNOTATION annot) {
  CPDF_AnnotContext* pCtx = CPDFAnnotContextFromFPDFAnnotation(annot);
  if (!pCtx)
    return nullptr;
  RetainPtr<CPDF_Dictionary> pAnnotDict = pCtx->GetMutableAnnotDict();
  if (!pAnnotDict)
    return nullptr;

  // Merged field/widget: FT is directly on the annotation dict.
  if (pAnnotDict->GetNameFor(pdfium::form_fields::kFT) ==
      pdfium::form_fields::kSig) {
    return pAnnotDict;
  }

  // Separate field + widget: walk to Parent.
  RetainPtr<CPDF_Dictionary> pParent =
      pAnnotDict->GetMutableDictFor("Parent");
  if (!pParent)
    return nullptr;
  if (pParent->GetNameFor(pdfium::form_fields::kFT) !=
      pdfium::form_fields::kSig) {
    return nullptr;
  }
  return pParent;
}

RetainPtr<CPDF_Dictionary> GetSigValueDict(FPDF_ANNOTATION annot) {
  RetainPtr<CPDF_Dictionary> pField = GetSigFieldDict(annot);
  if (!pField)
    return nullptr;
  return pField->GetMutableDictFor(pdfium::form_fields::kV);
}

}  // namespace

FPDF_EXPORT int FPDF_CALLCONV FPDF_GetSignatureCount(FPDF_DOCUMENT document) {
  auto* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc) {
    return -1;
  }

  return fxcrt::CollectionSize<int>(CollectSignatures(doc));
}

FPDF_EXPORT FPDF_SIGNATURE FPDF_CALLCONV
FPDF_GetSignatureObject(FPDF_DOCUMENT document, int index) {
  auto* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc) {
    return nullptr;
  }

  std::vector<RetainPtr<const CPDF_Dictionary>> signatures =
      CollectSignatures(doc);
  if (!fxcrt::IndexInBounds(signatures, index)) {
    return nullptr;
  }

  return FPDFSignatureFromCPDFDictionary(signatures[index].Get());
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetContents(FPDF_SIGNATURE signature,
                             void* buffer,
                             unsigned long length) {
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict) {
    return 0;
  }
  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict) {
    return 0;
  }
  // SAFETY: required from caller.
  auto result_span = UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, length));
  ByteString contents = value_dict->GetByteStringFor("Contents");
  fxcrt::try_spancpy(result_span, contents.span());
  return pdfium::checked_cast<unsigned long>(contents.span().size());
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetByteRange(FPDF_SIGNATURE signature,
                              int* buffer,
                              unsigned long length) {
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict) {
    return 0;
  }

  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict) {
    return 0;
  }

  RetainPtr<const CPDF_Array> byte_range = value_dict->GetArrayFor("ByteRange");
  if (!byte_range) {
    return 0;
  }

  const unsigned long byte_range_len =
      fxcrt::CollectionSize<unsigned long>(*byte_range);
  if (buffer && length >= byte_range_len) {
    // SAFETY: required from caller.
    auto buffer_span = UNSAFE_BUFFERS(pdfium::span(buffer, length));
    for (size_t i = 0; i < byte_range_len; ++i) {
      buffer_span[i] = byte_range->GetIntegerAt(i);
    }
  }
  return byte_range_len;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetSubFilter(FPDF_SIGNATURE signature,
                              char* buffer,
                              unsigned long length) {
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict) {
    return 0;
  }

  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict || !value_dict->KeyExist("SubFilter")) {
    return 0;
  }

  ByteString sub_filter = value_dict->GetNameFor("SubFilter");

  // SAFETY: required from caller.
  return NulTerminateMaybeCopyAndReturnLength(
      sub_filter, UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, length)));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetReason(FPDF_SIGNATURE signature,
                           void* buffer,
                           unsigned long length) {
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict) {
    return 0;
  }

  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict) {
    return 0;
  }

  RetainPtr<const CPDF_Object> obj = value_dict->GetObjectFor("Reason");
  if (!obj || !obj->IsString()) {
    return 0;
  }

  // SAFETY: required from caller.
  return Utf16EncodeMaybeCopyAndReturnLength(
      obj->GetUnicodeText(),
      UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, length)));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
FPDFSignatureObj_GetTime(FPDF_SIGNATURE signature,
                         char* buffer,
                         unsigned long length) {
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict) {
    return 0;
  }

  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict) {
    return 0;
  }

  RetainPtr<const CPDF_Object> obj = value_dict->GetObjectFor("M");
  if (!obj || !obj->IsString()) {
    return 0;
  }

  // SAFETY: required from caller.
  return NulTerminateMaybeCopyAndReturnLength(
      obj->GetString(), UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, length)));
}

FPDF_EXPORT unsigned int FPDF_CALLCONV
FPDFSignatureObj_GetDocMDPPermission(FPDF_SIGNATURE signature) {
  int permission = 0;
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict) {
    return permission;
  }

  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict) {
    return permission;
  }

  RetainPtr<const CPDF_Array> references = value_dict->GetArrayFor("Reference");
  if (!references) {
    return permission;
  }

  CPDF_ArrayLocker locker(std::move(references));
  for (auto& reference : locker) {
    RetainPtr<const CPDF_Dictionary> reference_dict = reference->GetDict();
    if (!reference_dict) {
      continue;
    }

    ByteString transform_method = reference_dict->GetNameFor("TransformMethod");
    if (transform_method != "DocMDP") {
      continue;
    }

    RetainPtr<const CPDF_Dictionary> transform_params =
        reference_dict->GetDictFor("TransformParams");
    if (!transform_params) {
      continue;
    }

    // Valid values are 1, 2 and 3; 2 is the default.
    permission = transform_params->GetIntegerFor("P", 2);
    if (permission < 1 || permission > 3) {
      permission = 0;
    }

    return permission;
  }

  return permission;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_PrepareSignatureDict(FPDF_ANNOTATION annot,
                             EPDF_SIG_SUBFILTER sub_filter,
                             unsigned long contents_size) {
  RetainPtr<CPDF_Dictionary> pField = GetSigFieldDict(annot);
  if (!pField)
    return false;

  if (pField->GetDictFor(pdfium::form_fields::kV))
    return false;

  if (contents_size == 0)
    return false;

  CPDF_AnnotContext* pCtx = CPDFAnnotContextFromFPDFAnnotation(annot);
  if (!pCtx || !pCtx->GetPage())
    return false;

  CPDF_Page* pPage = pCtx->GetPage()->AsPDFPage();
  if (!pPage)
    return false;

  CPDF_Document* pDoc = pPage->GetDocument();
  if (!pDoc)
    return false;

  ByteString type_value;
  ByteString sub_filter_value;
  switch (sub_filter) {
    case EPDF_SIG_SUBFILTER_ADBE_PKCS7_DETACHED:
      type_value = "Sig";
      sub_filter_value = "adbe.pkcs7.detached";
      break;
    case EPDF_SIG_SUBFILTER_ETSI_CADES_DETACHED:
      type_value = "Sig";
      sub_filter_value = "ETSI.CAdES.detached";
      break;
    case EPDF_SIG_SUBFILTER_ETSI_RFC3161:
      type_value = "DocTimeStamp";
      sub_filter_value = "ETSI.RFC3161";
      break;
    default:
      return false;
  }

  RetainPtr<CPDF_Dictionary> pSigDict =
      pDoc->NewIndirect<CPDF_Dictionary>();

  pSigDict->SetNewFor<CPDF_Name>("Type", type_value);
  pSigDict->SetNewFor<CPDF_Name>("Filter", "Adobe.PPKLite");
  pSigDict->SetNewFor<CPDF_Name>("SubFilter", sub_filter_value);

  CreateByteRangePlaceholderArray(pSigDict.Get());

  // /Contents placeholder: zero-filled byte buffer of |contents_size| bytes.
  // PDFium serializes this as a hex string <00...00> of 2*contents_size chars.
  std::vector<uint8_t> contents_placeholder(contents_size, 0);
  pSigDict->SetNewFor<CPDF_String>(
      "Contents",
      pdfium::span<const uint8_t>(contents_placeholder),
      CPDF_String::DataType::kIsHex);

  pField->SetNewFor<CPDF_Reference>(pdfium::form_fields::kV, pDoc,
                                     pSigDict->GetObjNum());
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetReason(FPDF_ANNOTATION annot, FPDF_WIDESTRING reason) {
  RetainPtr<CPDF_Dictionary> pSigDict = GetSigValueDict(annot);
  if (!pSigDict)
    return false;

  if (!reason) {
    pSigDict->RemoveFor("Reason");
    return true;
  }

  // SAFETY: caller guarantees NUL-terminated FPDF_WIDESTRING.
  WideString ws = UNSAFE_BUFFERS(WideStringFromFPDFWideString(reason));
  pSigDict->SetNewFor<CPDF_String>("Reason", ws.AsStringView());
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetLocation(FPDF_ANNOTATION annot, FPDF_WIDESTRING location) {
  RetainPtr<CPDF_Dictionary> pSigDict = GetSigValueDict(annot);
  if (!pSigDict)
    return false;

  if (!location) {
    pSigDict->RemoveFor("Location");
    return true;
  }

  // SAFETY: caller guarantees NUL-terminated FPDF_WIDESTRING.
  WideString ws = UNSAFE_BUFFERS(WideStringFromFPDFWideString(location));
  pSigDict->SetNewFor<CPDF_String>("Location", ws.AsStringView());
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetContactInfo(FPDF_ANNOTATION annot, FPDF_WIDESTRING contact_info) {
  RetainPtr<CPDF_Dictionary> pSigDict = GetSigValueDict(annot);
  if (!pSigDict)
    return false;

  if (!contact_info) {
    pSigDict->RemoveFor("ContactInfo");
    return true;
  }

  // SAFETY: caller guarantees NUL-terminated FPDF_WIDESTRING.
  WideString ws = UNSAFE_BUFFERS(WideStringFromFPDFWideString(contact_info));
  pSigDict->SetNewFor<CPDF_String>("ContactInfo", ws.AsStringView());
  return true;
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFSig_SetDocMDP(FPDF_DOCUMENT document,
                   FPDF_ANNOTATION annot,
                   int permission) {
  if (permission < 1 || permission > 3)
    return false;

  CPDF_Document* pDoc = CPDFDocumentFromFPDFDocument(document);
  if (!pDoc)
    return false;

  RetainPtr<CPDF_Dictionary> pSigDict = GetSigValueDict(annot);
  if (!pSigDict)
    return false;

  // Certification signatures must be /Type /Sig, not /Type /DocTimeStamp.
  if (pSigDict->GetNameFor("Type") == "DocTimeStamp")
    return false;

  RetainPtr<CPDF_Dictionary> pRoot = pDoc->GetMutableRoot();
  if (!pRoot)
    return false;

  // Enforce single certification per document at catalog level.
  RetainPtr<const CPDF_Dictionary> pPerms = pRoot->GetDictFor("Perms");
  if (pPerms && pPerms->GetDictFor("DocMDP"))
    return false;

  // 1. Add /Reference entry on the signature's /V dict.
  RetainPtr<CPDF_Dictionary> pTransformParams =
      pDoc->NewIndirect<CPDF_Dictionary>();
  pTransformParams->SetNewFor<CPDF_Name>("Type", "TransformParams");
  pTransformParams->SetNewFor<CPDF_Name>("V", "1.2");
  pTransformParams->SetNewFor<CPDF_Number>("P", permission);

  RetainPtr<CPDF_Dictionary> pSigRef =
      pDoc->NewIndirect<CPDF_Dictionary>();
  pSigRef->SetNewFor<CPDF_Name>("Type", "SigRef");
  pSigRef->SetNewFor<CPDF_Name>("TransformMethod", "DocMDP");
  pSigRef->SetNewFor<CPDF_Reference>("TransformParams", pDoc,
                                      pTransformParams->GetObjNum());

  RetainPtr<CPDF_Array> pRefArray =
      pSigDict->SetNewFor<CPDF_Array>("Reference");
  pRefArray->AppendNew<CPDF_Reference>(pDoc, pSigRef->GetObjNum());

  // 2. Wire catalog /Perms/DocMDP to point at the signature value dict.
  RetainPtr<CPDF_Dictionary> pPermsDict =
      pRoot->GetOrCreateDictFor("Perms");
  pPermsDict->SetNewFor<CPDF_Reference>("DocMDP", pDoc,
                                         pSigDict->GetObjNum());
  return true;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFSig_GetLocation(FPDF_SIGNATURE signature,
                     void* buffer,
                     unsigned long length) {
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict)
    return 0;

  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict)
    return 0;

  RetainPtr<const CPDF_Object> obj = value_dict->GetObjectFor("Location");
  if (!obj || !obj->IsString())
    return 0;

  // SAFETY: required from caller.
  return Utf16EncodeMaybeCopyAndReturnLength(
      obj->GetUnicodeText(),
      UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, length)));
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFSig_GetContactInfo(FPDF_SIGNATURE signature,
                        void* buffer,
                        unsigned long length) {
  const CPDF_Dictionary* signature_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!signature_dict)
    return 0;

  RetainPtr<const CPDF_Dictionary> value_dict =
      signature_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict)
    return 0;

  RetainPtr<const CPDF_Object> obj = value_dict->GetObjectFor("ContactInfo");
  if (!obj || !obj->IsString())
    return 0;

  // SAFETY: required from caller.
  return Utf16EncodeMaybeCopyAndReturnLength(
      obj->GetUnicodeText(),
      UNSAFE_BUFFERS(SpanFromFPDFApiArgs(buffer, length)));
}

FPDF_EXPORT FPDF_SIGNATURE FPDF_CALLCONV
EPDFSig_GetAnnotSignatureHandle(FPDF_ANNOTATION annot) {
  RetainPtr<CPDF_Dictionary> pField = GetSigFieldDict(annot);
  if (!pField)
    return nullptr;
  return FPDFSignatureFromCPDFDictionary(pField.Get());
}

// ---- Signature-Revision Bridge Implementations ----

namespace {

int MapSignatureToRevision(const CPDF_RevisionProvider* provider,
                           const CPDF_Dictionary* sig_field_dict) {
  RetainPtr<const CPDF_Dictionary> value_dict =
      sig_field_dict->GetDictFor(pdfium::form_fields::kV);
  if (!value_dict)
    return -1;

  RetainPtr<const CPDF_Array> byte_range =
      value_dict->GetArrayFor("ByteRange");
  if (!byte_range || byte_range->size() < 4)
    return -1;

  const int64_t signed_end =
      byte_range->GetIntegerAt(2) + byte_range->GetIntegerAt(3);

  for (size_t i = 0; i < provider->GetRevisionCount(); ++i) {
    if (provider->GetLayer(i).revision_end == signed_end)
      return static_cast<int>(i);
  }

  int best = -1;
  for (size_t i = 0; i < provider->GetRevisionCount(); ++i) {
    if (provider->GetLayer(i).revision_end <= signed_end)
      best = static_cast<int>(i);
  }
  return best;
}

}  // namespace

FPDF_EXPORT FPDF_SIGNATURE FPDF_CALLCONV
EPDFRevision_GetSignature(FPDF_DOCUMENT document, EPDF_REVISION revision) {
  if (!revision || !document)
    return nullptr;

  auto* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc)
    return nullptr;

  const CPDF_RevisionProvider* provider =
      GetRevisionProviderFromDocument(document);
  if (!provider || provider->GetRevisionCount() == 0)
    return nullptr;

  const auto* target_layer =
      reinterpret_cast<const CPDF_RevisionProvider::RevisionLayer*>(revision);

  int target_index = -1;
  for (size_t i = 0; i < provider->GetRevisionCount(); ++i) {
    if (&provider->GetLayer(i) == target_layer) {
      target_index = static_cast<int>(i);
      break;
    }
  }
  if (target_index < 0)
    return nullptr;

  std::vector<RetainPtr<const CPDF_Dictionary>> signatures =
      CollectSignatures(doc);
  for (const auto& sig : signatures) {
    if (MapSignatureToRevision(provider, sig.Get()) == target_index)
      return FPDFSignatureFromCPDFDictionary(sig.Get());
  }
  return nullptr;
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFSig_GetSignatureRevision(FPDF_DOCUMENT document,
                             FPDF_SIGNATURE signature) {
  auto* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc)
    return -1;

  const CPDF_RevisionProvider* provider =
      GetRevisionProviderFromDocument(document);
  if (!provider)
    return -1;

  const CPDF_Dictionary* sig_dict =
      CPDFDictionaryFromFPDFSignature(signature);
  if (!sig_dict)
    return -1;

  return MapSignatureToRevision(provider, sig_dict);
}

FPDF_EXPORT int FPDF_CALLCONV
EPDFSig_CheckDocMDPCompliance(FPDF_DOCUMENT document,
                              int check_revision) {
  auto* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc)
    return EPDF_DOCMDP_UNSUPPORTED;

  CPDF_Parser* parser = doc->GetParser();
  if (!parser)
    return EPDF_DOCMDP_UNSUPPORTED;

  const CPDF_RevisionProvider* provider = parser->GetRevisionProvider();
  if (!provider || provider->GetRevisionCount() == 0)
    return EPDF_DOCMDP_UNSUPPORTED;

  // Find the certification signature via catalog /Perms/DocMDP.
  const CPDF_Dictionary* root = doc->GetRoot();
  if (!root)
    return EPDF_DOCMDP_NOT_APPLICABLE;

  RetainPtr<const CPDF_Dictionary> perms = root->GetDictFor("Perms");
  if (!perms)
    return EPDF_DOCMDP_NOT_APPLICABLE;

  RetainPtr<const CPDF_Dictionary> docmdp_sig = perms->GetDictFor("DocMDP");
  if (!docmdp_sig)
    return EPDF_DOCMDP_NOT_APPLICABLE;

  // Get permission level from the /Reference TransformParams.
  int permission = 2;  // Default per ISO 32000.
  RetainPtr<const CPDF_Array> reference = docmdp_sig->GetArrayFor("Reference");
  if (reference) {
    for (size_t i = 0; i < reference->size(); ++i) {
      RetainPtr<const CPDF_Dictionary> ref_dict = reference->GetDictAt(i);
      if (!ref_dict)
        continue;
      if (ref_dict->GetNameFor("TransformMethod") != "DocMDP")
        continue;
      RetainPtr<const CPDF_Dictionary> params =
          ref_dict->GetDictFor("TransformParams");
      if (params) {
        int p = params->GetIntegerFor("P");
        if (p >= 1 && p <= 3)
          permission = p;
      }
      break;
    }
  }

  // Determine the certification signature's revision.
  int cert_revision = -1;
  std::vector<RetainPtr<const CPDF_Dictionary>> signatures =
      CollectSignatures(doc);
  for (const auto& sig : signatures) {
    RetainPtr<const CPDF_Dictionary> v = sig->GetDictFor(pdfium::form_fields::kV);
    if (v.Get() == docmdp_sig.Get()) {
      cert_revision = MapSignatureToRevision(provider, sig.Get());
      break;
    }
  }
  if (cert_revision < 0)
    return EPDF_DOCMDP_INDETERMINATE;

  // Determine the check revision.
  int target_revision = check_revision;
  if (target_revision < 0)
    target_revision = static_cast<int>(provider->GetRevisionCount()) - 1;

  if (target_revision <= cert_revision)
    return EPDF_DOCMDP_COMPLIANT;

  // Compute diff between certified revision and check revision.
  auto cert_map =
      provider->GetVisibleObjectsAtRevision(cert_revision);
  auto check_map =
      provider->GetVisibleObjectsAtRevision(target_revision);

  std::vector<RevisionDiffEntry> raw_diff =
      CPDF_RevisionDiff::ComputeDiff(cert_map, check_map);

  if (raw_diff.empty())
    return EPDF_DOCMDP_COMPLIANT;

  // TODO: GetObjectsWithMultipleReferences is a full-document BFS. If this
  // function is called repeatedly for the same document, consider caching the
  // result on a per-document handle or a dedicated context object.
  std::set<uint32_t> multi_ref_set =
      GetObjectsWithMultipleReferences(doc);
  std::vector<ResolvedSemanticChange> changes =
      ClassifyChanges(doc, raw_diff, multi_ref_set);

  // Check compliance.
  if (permission < 1 || permission > 3)
    return EPDF_DOCMDP_UNSUPPORTED;

  for (const auto& change : changes) {
    if (change.semantic_type == SemanticChangeType::kDSS ||
        change.semantic_type == SemanticChangeType::kDocumentTimestamp) {
      continue;
    }

    switch (permission) {
      case 1:
        return EPDF_DOCMDP_VIOLATED;

      case 2:
        if (change.semantic_type != SemanticChangeType::kFormStateChange &&
            change.semantic_type != SemanticChangeType::kSignature) {
          return EPDF_DOCMDP_VIOLATED;
        }
        break;

      case 3:
        if (change.semantic_type != SemanticChangeType::kFormStateChange &&
            change.semantic_type != SemanticChangeType::kSignature &&
            change.semantic_type != SemanticChangeType::kAnnotation) {
          return EPDF_DOCMDP_VIOLATED;
        }
        break;
    }
  }

  return EPDF_DOCMDP_COMPLIANT;
}
