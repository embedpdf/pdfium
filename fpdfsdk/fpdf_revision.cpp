// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "public/fpdf_revision.h"

#include <map>
#include <set>
#include <vector>

#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/parser/cpdf_revision_classifier.h"
#include "core/fpdfapi/parser/cpdf_revision_diff.h"
#include "core/fpdfapi/parser/cpdf_revision_provider.h"
#include "core/fpdfapi/parser/object_tree_traversal_util.h"
#include "fpdfsdk/cpdfsdk_helpers.h"

namespace {

struct RevisionDiffResult {
  std::vector<RevisionDiffEntry> entries;
  std::vector<ResolvedSemanticChange> semantic_changes;
  std::set<uint32_t> multi_ref_set;
  bool semantic_computed = false;
};

void EnsureSemanticComputed(FPDF_DOCUMENT document,
                            RevisionDiffResult* result) {
  if (result->semantic_computed)
    return;

  auto* doc = CPDFDocumentFromFPDFDocument(document);
  if (!doc)
    return;

  result->multi_ref_set = GetObjectsWithMultipleReferences(doc);
  result->semantic_changes =
      ClassifyChanges(doc, result->entries, result->multi_ref_set);
  result->semantic_computed = true;
}

}  // namespace

FPDF_EXPORT int FPDF_CALLCONV
EPDFRevision_GetCount(FPDF_DOCUMENT document) {
  const CPDF_RevisionProvider* provider =
      GetRevisionProviderFromDocument(document);
  if (!provider)
    return -1;
  return static_cast<int>(provider->GetRevisionCount());
}

FPDF_EXPORT EPDF_REVISION FPDF_CALLCONV
EPDFRevision_Get(FPDF_DOCUMENT document, int index) {
  const CPDF_RevisionProvider* provider =
      GetRevisionProviderFromDocument(document);
  if (!provider || index < 0 ||
      static_cast<size_t>(index) >= provider->GetRevisionCount()) {
    return nullptr;
  }
  return reinterpret_cast<EPDF_REVISION>(&provider->GetLayer(index));
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFRevision_GetFileEnd(EPDF_REVISION revision,
                        unsigned long long* out_file_end) {
  if (!revision || !out_file_end)
    return false;
  const auto* layer =
      reinterpret_cast<const CPDF_RevisionProvider::RevisionLayer*>(revision);
  *out_file_end = static_cast<unsigned long long>(layer->revision_end);
  return true;
}

FPDF_EXPORT EPDF_REVISION_DIFF FPDF_CALLCONV
EPDFRevision_Compare(FPDF_DOCUMENT document,
                     int older_revision,
                     int newer_revision) {
  const CPDF_RevisionProvider* provider =
      GetRevisionProviderFromDocument(document);
  if (!provider)
    return nullptr;

  const size_t count = provider->GetRevisionCount();
  if (older_revision < 0 || static_cast<size_t>(older_revision) >= count ||
      newer_revision < 0 || static_cast<size_t>(newer_revision) >= count ||
      older_revision >= newer_revision) {
    return nullptr;
  }

  auto older_map =
      provider->GetVisibleObjectsAtRevision(older_revision);
  auto newer_map =
      provider->GetVisibleObjectsAtRevision(newer_revision);

  auto* result = new RevisionDiffResult();
  result->entries = CPDF_RevisionDiff::ComputeDiff(older_map, newer_map);
  return reinterpret_cast<EPDF_REVISION_DIFF>(result);
}

FPDF_EXPORT void FPDF_CALLCONV
EPDFRevisionDiff_Close(EPDF_REVISION_DIFF diff) {
  delete reinterpret_cast<const RevisionDiffResult*>(diff);
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFRevisionDiff_GetEntryCount(EPDF_REVISION_DIFF diff) {
  if (!diff)
    return 0;
  const auto* result = reinterpret_cast<const RevisionDiffResult*>(diff);
  return static_cast<unsigned long>(result->entries.size());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFRevisionDiff_GetEntry(EPDF_REVISION_DIFF diff,
                          unsigned long index,
                          unsigned int* out_obj_num,
                          int* out_category) {
  if (!diff || !out_obj_num || !out_category)
    return false;

  const auto* result = reinterpret_cast<const RevisionDiffResult*>(diff);
  if (index >= result->entries.size())
    return false;

  const RevisionDiffEntry& entry = result->entries[index];
  *out_obj_num = entry.obj_num;
  *out_category = static_cast<int>(entry.category);
  return true;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFRevisionDiff_GetSemanticCategoryCounts(FPDF_DOCUMENT document,
                                           EPDF_REVISION_DIFF diff,
                                           int* category_buffer,
                                           unsigned long* count_buffer,
                                           unsigned long buffer_length) {
  if (!diff || !document)
    return 0;

  auto* result =
      const_cast<RevisionDiffResult*>(
          reinterpret_cast<const RevisionDiffResult*>(diff));

  EnsureSemanticComputed(document, result);

  std::map<int, unsigned long> counts;
  for (const auto& change : result->semantic_changes) {
    counts[static_cast<int>(change.semantic_type)]++;
  }

  unsigned long filled = 0;
  for (const auto& [cat, cnt] : counts) {
    if (filled < buffer_length) {
      if (category_buffer)
        category_buffer[filled] = cat;
      if (count_buffer)
        count_buffer[filled] = cnt;
      filled++;
    }
  }
  return filled;
}

FPDF_EXPORT unsigned long FPDF_CALLCONV
EPDFRevisionDiff_GetResolvedEntryCount(FPDF_DOCUMENT document,
                                       EPDF_REVISION_DIFF diff) {
  if (!diff || !document)
    return 0;

  auto* result =
      const_cast<RevisionDiffResult*>(
          reinterpret_cast<const RevisionDiffResult*>(diff));

  EnsureSemanticComputed(document, result);

  return static_cast<unsigned long>(result->semantic_changes.size());
}

FPDF_EXPORT FPDF_BOOL FPDF_CALLCONV
EPDFRevisionDiff_GetResolvedEntry(FPDF_DOCUMENT document,
                                  EPDF_REVISION_DIFF diff,
                                  unsigned long index,
                                  unsigned int* out_changed_obj_num,
                                  unsigned int* out_target_obj_num,
                                  unsigned int* out_page_obj_num,
                                  int* out_diff_category,
                                  int* out_semantic_type) {
  if (!diff || !document || !out_changed_obj_num || !out_target_obj_num ||
      !out_page_obj_num || !out_diff_category ||
      !out_semantic_type) {
    return false;
  }

  auto* result =
      const_cast<RevisionDiffResult*>(
          reinterpret_cast<const RevisionDiffResult*>(diff));

  EnsureSemanticComputed(document, result);

  if (index >= result->semantic_changes.size())
    return false;

  const ResolvedSemanticChange& change = result->semantic_changes[index];
  *out_changed_obj_num = change.changed_obj_num;
  *out_target_obj_num = change.target_obj_num;
  *out_page_obj_num = change.page_obj_num;
  *out_diff_category = static_cast<int>(change.diff_category);
  *out_semantic_type = static_cast<int>(change.semantic_type);
  return true;
}
