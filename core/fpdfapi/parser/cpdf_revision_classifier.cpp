// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_revision_classifier.h"

#include <map>
#include <queue>
#include <set>

#include "constants/annotation_common.h"
#include "constants/form_fields.h"
#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream.h"

namespace {

bool ShouldSkipSupportKey(const ByteString& key) {
  return key == "P" || key == "Parent" || key == "Prev" || key == "First";
}

// BFS that follows CPDF_Reference into their targets using
// doc->GetIndirectObject(). This matches the ObjectTreeTraverser pattern
// but is scoped to a specific support root.
void CollectReferencesRecursive(CPDF_Document* doc,
                                const CPDF_Object* root,
                                std::set<uint32_t>* visited) {
  std::queue<RetainPtr<const CPDF_Object>> queue;
  queue.push(RetainPtr<const CPDF_Object>(root));

  while (!queue.empty()) {
    RetainPtr<const CPDF_Object> obj = std::move(queue.front());
    queue.pop();

    if (!obj)
      continue;

    if (obj->IsReference()) {
      uint32_t ref_num = obj->AsReference()->GetRefObjNum();
      if (!ref_num || !visited->insert(ref_num).second)
        continue;
      RetainPtr<const CPDF_Object> resolved =
          doc->GetIndirectObject(ref_num);
      if (resolved)
        queue.push(std::move(resolved));
      continue;
    }

    if (const CPDF_Dictionary* dict = obj->AsDictionary()) {
      CPDF_DictionaryLocker locker(dict);
      for (const auto& [key, value] : locker) {
        if (ShouldSkipSupportKey(key))
          continue;
        queue.push(value);
      }
    } else if (const CPDF_Array* arr = obj->AsArray()) {
      CPDF_ArrayLocker locker(arr);
      for (const auto& elem : locker)
        queue.push(elem);
    } else if (const CPDF_Stream* stream = obj->AsStream()) {
      RetainPtr<const CPDF_Dictionary> stream_dict = stream->GetDict();
      if (stream_dict) {
        CPDF_DictionaryLocker locker(stream_dict.Get());
        for (const auto& [key, value] : locker) {
          if (ShouldSkipSupportKey(key))
            continue;
          queue.push(value);
        }
      }
    }
  }
}

// Enumerate all indirect objects from the /AP dictionary.
// Covers all modes (N/R/D) and all states (Yes/Off/etc.).
//
// GetDirectObjectFor() resolves references, so directly-embedded (non-indirect)
// sub-dicts or streams will have GetObjNum() == 0 and contribute no object
// number to the result set. Only their indirect descendants (found via
// CollectReferencesRecursive) will be collected. This is intentional: the
// revision diff only tracks indirect objects, so inline objects are invisible
// to the diff and need not be classified.
std::set<uint32_t> CollectAPObjectNumbers(CPDF_Document* doc,
                                          const CPDF_Dictionary* owner_dict) {
  std::set<uint32_t> result;
  RetainPtr<const CPDF_Dictionary> ap_dict =
      owner_dict->GetDictFor(pdfium::annotation::kAP);
  if (!ap_dict)
    return result;

  for (const char* mode_key : {"N", "R", "D"}) {
    RetainPtr<const CPDF_Object> mode_obj =
        ap_dict->GetDirectObjectFor(mode_key);
    if (!mode_obj)
      continue;

    if (mode_obj->IsStream()) {
      if (uint32_t num = mode_obj->GetObjNum(); num)
        result.insert(num);
      CollectReferencesRecursive(doc, mode_obj.Get(), &result);
    } else if (const CPDF_Dictionary* state_dict = mode_obj->AsDictionary()) {
      CPDF_DictionaryLocker locker(state_dict);
      for (const auto& [key, value] : locker) {
        if (uint32_t num = value->GetObjNum(); num)
          result.insert(num);
        CollectReferencesRecursive(doc, value.Get(), &result);
      }
    }
  }
  return result;
}

std::set<uint32_t> CollectValueObjectNumbers(CPDF_Document* doc,
                                             const CPDF_Dictionary* owner_dict) {
  std::set<uint32_t> result;
  const CPDF_Object* value = owner_dict->GetObjectFor("V");
  if (!value)
    return result;

  if (value->IsReference())
    result.insert(value->AsReference()->GetRefObjNum());
  CollectReferencesRecursive(doc, value, &result);
  return result;
}

void CollectSupportRoot(CPDF_Document* doc,
                        const CPDF_Dictionary* owner_dict,
                        const char* key,
                        std::set<uint32_t>* result) {
  const CPDF_Object* root = owner_dict->GetObjectFor(key);
  if (!root)
    return;

  if (root->IsReference())
    result->insert(root->AsReference()->GetRefObjNum());
  CollectReferencesRecursive(doc, root, result);
}

std::set<uint32_t> CollectAnnotationSupportObjectNumbers(
    CPDF_Document* doc,
    const CPDF_Dictionary* owner_dict) {
  std::set<uint32_t> result = CollectAPObjectNumbers(doc, owner_dict);

  for (const char* key : {pdfium::annotation::kPopup, pdfium::annotation::kA,
                          pdfium::annotation::kAA, pdfium::annotation::kBS,
                          pdfium::annotation::kBE, pdfium::annotation::kMK,
                          pdfium::annotation::kOC}) {
    CollectSupportRoot(doc, owner_dict, key, &result);
  }

  return result;
}

uint32_t GetAcroFormObjNum(const CPDF_Dictionary* root) {
  if (!root)
    return 0;
  const CPDF_Object* acroform = root->GetObjectFor("AcroForm");
  if (!acroform)
    return 0;
  if (acroform->IsReference())
    return acroform->AsReference()->GetRefObjNum();
  return 0;
}

struct DirectClassification {
  uint32_t target_obj_num = 0;
  uint32_t page_obj_num = 0;
  SemanticChangeType semantic_type = SemanticChangeType::kOther;
  SupportOwnerKind owner_kind = SupportOwnerKind::kNone;
};

uint32_t GetPageObjectNum(const CPDF_Dictionary* dict) {
  if (!dict)
    return 0;

  const CPDF_Object* page = dict->GetObjectFor("P");
  if (page) {
    if (page->IsReference())
      return page->AsReference()->GetRefObjNum();

    if (const CPDF_Dictionary* page_dict = page->AsDictionary())
      return page_dict->GetObjNum();
  }

  // Follow the /Parent owner chain until a /P page reference is found.
  // This is not page-tree inheritance; it simply walks the owner chain.
  // Depth cap prevents infinite loops from malformed circular references.
  const CPDF_Dictionary* cur = dict;
  for (int depth = 0; depth < 4; ++depth) {
    const CPDF_Object* parent_ref = cur->GetObjectFor("Parent");
    if (!parent_ref)
      break;

    const CPDF_Object* parent_obj = parent_ref->GetDirect();
    const CPDF_Dictionary* parent_dict =
        parent_obj ? parent_obj->AsDictionary() : nullptr;
    if (!parent_dict)
      break;

    const CPDF_Object* parent_page = parent_dict->GetObjectFor("P");
    if (parent_page) {
      if (parent_page->IsReference())
        return parent_page->AsReference()->GetRefObjNum();

      if (const CPDF_Dictionary* parent_page_dict = parent_page->AsDictionary())
        return parent_page_dict->GetObjNum();
    }

    cur = parent_dict;
  }

  return 0;
}

// LIMITATION: Classification uses the latest parse context
// (doc->GetMutableIndirectObject), not a historical snapshot. Objects that were
// freed or replaced in later revisions are resolved to their current-revision
// state, which may differ from their state at the time the change was made.
// This is acceptable for the first implementation because freed objects are
// uncommon in typical form-fill and annotation workflows, and the conservative
// promotion rules in Pass 2 mitigate most misclassification risk.
DirectClassification ClassifyObject(CPDF_Document* doc,
                                    uint32_t obj_num,
                                    const CPDF_Dictionary* root,
                                    uint32_t acroform_obj_num,
                                    const std::set<uint32_t>& dss_obj_nums) {
  if (dss_obj_nums.count(obj_num))
    return {obj_num, 0, SemanticChangeType::kDSS, SupportOwnerKind::kNone};

  RetainPtr<CPDF_Object> obj = doc->GetMutableIndirectObject(obj_num);
  if (!obj)
    return {};

  CPDF_Dictionary* dict = obj->AsMutableDictionary();
  if (!dict) {
    if (CPDF_Stream* stream = obj->AsMutableStream())
      dict = stream->GetMutableDict();
    if (!dict)
      return {};
  }

  ByteString type = dict->GetNameFor("Type");

  if (type == "DocTimeStamp")
    return {obj_num, 0, SemanticChangeType::kDocumentTimestamp,
            SupportOwnerKind::kNone};

  if (type == "Sig")
    return {obj_num, 0, SemanticChangeType::kSignature,
            SupportOwnerKind::kNone};

  ByteString subtype = dict->GetNameFor("Subtype");
  if (subtype == "Widget")
    return {obj_num, GetPageObjectNum(dict), SemanticChangeType::kFormStateChange,
            SupportOwnerKind::kForm};

  if (acroform_obj_num != 0 && obj_num == acroform_obj_num)
    return {obj_num, 0, SemanticChangeType::kFormStateChange,
            SupportOwnerKind::kForm};

  if (!subtype.IsEmpty() && dict->KeyExist("Rect")) {
    uint32_t target_obj_num = obj_num;
    if (subtype == "Popup") {
      const CPDF_Object* parent_ref = dict->GetObjectFor("Parent");
      if (parent_ref) {
        if (parent_ref->IsReference()) {
          target_obj_num = parent_ref->AsReference()->GetRefObjNum();
        } else if (const CPDF_Dictionary* parent_dict =
                       parent_ref->AsDictionary()) {
          if (parent_dict->GetObjNum())
            target_obj_num = parent_dict->GetObjNum();
        }
      }
    }

    return {target_obj_num, GetPageObjectNum(dict),
            SemanticChangeType::kAnnotation, SupportOwnerKind::kAnnotation};
  }

  if (dict->KeyExist("FT") || dict->KeyExist("V") ||
      dict->KeyExist("Parent")) {
    return {obj_num, GetPageObjectNum(dict),
            SemanticChangeType::kFormStateChange, SupportOwnerKind::kForm};
  }

  if (type == "Page" || type == "Pages")
    return {obj_num, type == "Page" ? obj_num : 0, SemanticChangeType::kPage,
            SupportOwnerKind::kNone};

  if (type == "Catalog")
    return {obj_num, 0, SemanticChangeType::kCatalog,
            SupportOwnerKind::kNone};

  return {};
}

}  // namespace

std::set<uint32_t> CollectDSSObjectNumbers(CPDF_Document* doc) {
  std::set<uint32_t> result;
  const CPDF_Dictionary* root = doc->GetRoot();
  if (!root)
    return result;

  const CPDF_Object* dss_obj = root->GetObjectFor("DSS");
  if (!dss_obj)
    return result;

  if (dss_obj->IsReference()) {
    uint32_t dss_num = dss_obj->AsReference()->GetRefObjNum();
    result.insert(dss_num);
    RetainPtr<const CPDF_Object> resolved = doc->GetIndirectObject(dss_num);
    if (resolved)
      CollectReferencesRecursive(doc, resolved.Get(), &result);
  } else if (dss_obj->IsDictionary()) {
    CollectReferencesRecursive(doc, dss_obj, &result);
  }
  return result;
}

std::set<uint32_t> CollectSupportObjectNumbers(
    CPDF_Document* doc,
    const CPDF_Dictionary* owner_dict,
    const SupportCollectionPolicy& policy) {
  std::set<uint32_t> result;

  if (policy.include_ap)
    result.merge(CollectAPObjectNumbers(doc, owner_dict));

  if (policy.include_value)
    result.merge(CollectValueObjectNumbers(doc, owner_dict));

  if (policy.include_annotation_support)
    result.merge(CollectAnnotationSupportObjectNumbers(doc, owner_dict));

  return result;
}

SupportPromotionDecision DecideSupportPromotion(
    uint32_t /*obj_num*/,
    RevisionDiffCategory /*diff_category*/,
    const std::set<uint32_t>& form_owner_hits,
    const std::set<uint32_t>& annotation_owner_hits,
    bool has_multiple_references_in_document) {
  const bool from_form = !form_owner_hits.empty();
  const bool from_annot = !annotation_owner_hits.empty();

  if (!from_form && !from_annot)
    return SupportPromotionDecision::kNotLocal;

  if (from_form && from_annot)
    return SupportPromotionDecision::kAmbiguous;

  if (form_owner_hits.size() > 1 || annotation_owner_hits.size() > 1)
    return SupportPromotionDecision::kAmbiguous;

  if (has_multiple_references_in_document)
    return SupportPromotionDecision::kShared;

  return from_form ? SupportPromotionDecision::kPromoteToForm
                   : SupportPromotionDecision::kPromoteToAnnotation;
}

// LIMITATION: All object access uses the current (latest) document parse
// state. See ClassifyObject comment for details on freed/replaced object
// classification accuracy.
std::vector<ResolvedSemanticChange> ClassifyChanges(
    CPDF_Document* doc,
    const std::vector<RevisionDiffEntry>& raw_diff,
    const std::set<uint32_t>& multi_ref_set) {
  const CPDF_Dictionary* root = doc->GetRoot();
  const uint32_t acroform_obj_num = GetAcroFormObjNum(root);

  std::set<uint32_t> dss_obj_nums = CollectDSSObjectNumbers(doc);

  const std::set<uint32_t> changed_obj_nums = [&raw_diff]() {
    std::set<uint32_t> changed;
    for (const auto& entry : raw_diff)
      changed.insert(entry.obj_num);
    return changed;
  }();

  // --- Pass 1: direct-owner classification ---
  std::vector<ResolvedSemanticChange> result;
  result.reserve(raw_diff.size());

  std::map<uint32_t, SupportOwnerKind> direct_owners;

  for (const auto& entry : raw_diff) {
    ResolvedSemanticChange change;
    change.changed_obj_num = entry.obj_num;
    change.target_obj_num = entry.obj_num;
    change.page_obj_num = 0;
    change.diff_category = entry.category;
    DirectClassification direct =
        ClassifyObject(doc, entry.obj_num, root, acroform_obj_num,
                       dss_obj_nums);
    change.target_obj_num = direct.target_obj_num ? direct.target_obj_num
                                                  : entry.obj_num;
    change.page_obj_num = direct.page_obj_num;
    change.semantic_type = direct.semantic_type;
    result.push_back(change);

    if (direct.owner_kind != SupportOwnerKind::kNone)
      direct_owners[entry.obj_num] = direct.owner_kind;
  }

  // --- Pass 2: support-object promotion ---
  std::map<uint32_t, std::set<uint32_t>> form_support_hits;
  std::map<uint32_t, std::set<uint32_t>> annotation_support_hits;

  for (const auto& [owner_obj_num, owner_kind] : direct_owners) {
    RetainPtr<CPDF_Object> obj = doc->GetMutableIndirectObject(owner_obj_num);
    CPDF_Dictionary* owner_dict = obj ? obj->AsMutableDictionary() : nullptr;
    if (!owner_dict) {
      if (CPDF_Stream* stream = obj ? obj->AsMutableStream() : nullptr)
        owner_dict = stream->GetMutableDict();
    }
    if (!owner_dict)
      continue;

    SupportCollectionPolicy policy;
    if (owner_kind == SupportOwnerKind::kForm) {
      policy.include_ap = true;
      policy.include_value = true;
    } else if (owner_kind == SupportOwnerKind::kAnnotation) {
      policy.include_annotation_support = true;
    }

    std::set<uint32_t> owned =
        CollectSupportObjectNumbers(doc, owner_dict, policy);

    for (uint32_t support_obj_num : owned) {
      if (!changed_obj_nums.count(support_obj_num))
        continue;
      if (owner_kind == SupportOwnerKind::kForm) {
        form_support_hits[support_obj_num].insert(owner_obj_num);
      } else if (owner_kind == SupportOwnerKind::kAnnotation) {
        annotation_support_hits[support_obj_num].insert(owner_obj_num);
      }
    }
  }

  for (auto& change : result) {
    if ((change.semantic_type == SemanticChangeType::kSignature ||
         change.semantic_type == SemanticChangeType::kDocumentTimestamp) &&
        form_support_hits[change.changed_obj_num].size() == 1 &&
        annotation_support_hits[change.changed_obj_num].empty() &&
        !multi_ref_set.count(change.changed_obj_num)) {
      uint32_t owner_obj_num = *form_support_hits[change.changed_obj_num].begin();
      change.target_obj_num = owner_obj_num;
      RetainPtr<CPDF_Object> owner_obj =
          doc->GetMutableIndirectObject(owner_obj_num);
      CPDF_Dictionary* owner_dict =
          owner_obj ? owner_obj->AsMutableDictionary() : nullptr;
      if (!owner_dict) {
        if (CPDF_Stream* owner_stream =
                owner_obj ? owner_obj->AsMutableStream() : nullptr) {
          owner_dict = owner_stream->GetMutableDict();
        }
      }
      change.page_obj_num = GetPageObjectNum(owner_dict);
      continue;
    }

    if (change.semantic_type != SemanticChangeType::kOther)
      continue;

    SupportPromotionDecision decision = DecideSupportPromotion(
        change.changed_obj_num, change.diff_category,
        form_support_hits[change.changed_obj_num],
        annotation_support_hits[change.changed_obj_num],
        multi_ref_set.count(change.changed_obj_num));

    switch (decision) {
      case SupportPromotionDecision::kPromoteToForm:
        change.semantic_type = SemanticChangeType::kFormStateChange;
        if (!form_support_hits[change.changed_obj_num].empty()) {
          uint32_t owner_obj_num = *form_support_hits[change.changed_obj_num].begin();
          change.target_obj_num = owner_obj_num;
          RetainPtr<CPDF_Object> owner_obj = doc->GetMutableIndirectObject(owner_obj_num);
          CPDF_Dictionary* owner_dict =
              owner_obj ? owner_obj->AsMutableDictionary() : nullptr;
          if (!owner_dict) {
            if (CPDF_Stream* owner_stream = owner_obj ? owner_obj->AsMutableStream() : nullptr)
              owner_dict = owner_stream->GetMutableDict();
          }
          change.page_obj_num = GetPageObjectNum(owner_dict);
        }
        break;
      case SupportPromotionDecision::kPromoteToAnnotation:
        change.semantic_type = SemanticChangeType::kAnnotation;
        if (!annotation_support_hits[change.changed_obj_num].empty()) {
          uint32_t owner_obj_num =
              *annotation_support_hits[change.changed_obj_num].begin();
          change.target_obj_num = owner_obj_num;
          RetainPtr<CPDF_Object> owner_obj = doc->GetMutableIndirectObject(owner_obj_num);
          CPDF_Dictionary* owner_dict =
              owner_obj ? owner_obj->AsMutableDictionary() : nullptr;
          if (!owner_dict) {
            if (CPDF_Stream* owner_stream = owner_obj ? owner_obj->AsMutableStream() : nullptr)
              owner_dict = owner_stream->GetMutableDict();
          }
          change.page_obj_num = GetPageObjectNum(owner_dict);
        }
        break;
      case SupportPromotionDecision::kAmbiguous:
      case SupportPromotionDecision::kShared:
      case SupportPromotionDecision::kNotLocal:
        break;
    }
  }

  return result;
}
