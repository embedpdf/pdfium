// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_REVISION_CLASSIFIER_H_
#define CORE_FPDFAPI_PARSER_CPDF_REVISION_CLASSIFIER_H_

#include <stdint.h>

#include <set>
#include <vector>

#include "core/fpdfapi/parser/cpdf_revision_diff.h"

class CPDF_Document;
class CPDF_Dictionary;

enum class SemanticChangeType : uint8_t {
  kFormStateChange = 0,
  kAnnotation = 1,
  kSignature = 2,
  kDocumentTimestamp = 3,
  kDSS = 4,
  kPage = 5,
  kCatalog = 6,
  kOther = 7,
};

struct ResolvedSemanticChange {
  uint32_t changed_obj_num;
  uint32_t target_obj_num;
  uint32_t page_obj_num;
  RevisionDiffCategory diff_category;
  SemanticChangeType semantic_type;
};

enum class SupportOwnerKind : uint8_t {
  kNone = 0,
  kForm,
  kAnnotation,
};

struct SupportCollectionPolicy {
  bool include_ap = false;
  bool include_value = false;
  bool include_annotation_support = false;
};

enum class SupportPromotionDecision : uint8_t {
  kPromoteToForm,
  kPromoteToAnnotation,
  kAmbiguous,
  kShared,
  kNotLocal,
};

// Collect all indirect object numbers reachable from the /DSS entry in the
// document catalog. Returns an empty set if no DSS exists.
std::set<uint32_t> CollectDSSObjectNumbers(CPDF_Document* doc);

// Collect all indirect object numbers reachable from the support roots
// of an annotation/widget owner dictionary, per the given policy.
std::set<uint32_t> CollectSupportObjectNumbers(
    CPDF_Document* doc,
    const CPDF_Dictionary* owner_dict,
    const SupportCollectionPolicy& policy);

// Decide whether a changed support object should be promoted.
SupportPromotionDecision DecideSupportPromotion(
    uint32_t obj_num,
    RevisionDiffCategory diff_category,
    const std::set<uint32_t>& form_owner_hits,
    const std::set<uint32_t>& annotation_owner_hits,
    bool has_multiple_references_in_document);

// Two-pass semantic classification of raw diff entries.
// multi_ref_set should be precomputed via GetObjectsWithMultipleReferences().
std::vector<ResolvedSemanticChange> ClassifyChanges(
    CPDF_Document* doc,
    const std::vector<RevisionDiffEntry>& raw_diff,
    const std::set<uint32_t>& multi_ref_set);

#endif  // CORE_FPDFAPI_PARSER_CPDF_REVISION_CLASSIFIER_H_
