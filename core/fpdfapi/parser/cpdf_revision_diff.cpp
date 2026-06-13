// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_revision_diff.h"

std::vector<RevisionDiffEntry> CPDF_RevisionDiff::ComputeDiff(
    const ObjectMap& older,
    const ObjectMap& newer) {
  std::vector<RevisionDiffEntry> result;

  for (const auto& [obj_num, new_info] : newer) {
    auto it = older.find(obj_num);

    if (it == older.end()) {
      if (new_info.type != CPDF_CrossRefTable::ObjectType::kFree) {
        result.push_back({obj_num, RevisionDiffCategory::kAdded});
      }
      continue;
    }

    const auto& old_info = it->second;

    if (new_info.type == CPDF_CrossRefTable::ObjectType::kFree &&
        old_info.type != CPDF_CrossRefTable::ObjectType::kFree) {
      result.push_back({obj_num, RevisionDiffCategory::kFreed});
      continue;
    }

    if (new_info.type != old_info.type) {
      result.push_back({obj_num, RevisionDiffCategory::kModified});
      continue;
    }

    if (new_info.type == CPDF_CrossRefTable::ObjectType::kNormal) {
      if (new_info.pos != old_info.pos ||
          new_info.gennum != old_info.gennum) {
        result.push_back({obj_num, RevisionDiffCategory::kModified});
      }
    } else if (new_info.type == CPDF_CrossRefTable::ObjectType::kCompressed) {
      if (new_info.archive.obj_num != old_info.archive.obj_num ||
          new_info.archive.obj_index != old_info.archive.obj_index) {
        result.push_back({obj_num, RevisionDiffCategory::kModified});
      }
    }
  }

  // Objects present in older but not in newer were freed.
  for (const auto& [obj_num, old_info] : older) {
    if (old_info.type != CPDF_CrossRefTable::ObjectType::kFree &&
        newer.find(obj_num) == newer.end()) {
      result.push_back({obj_num, RevisionDiffCategory::kFreed});
    }
  }

  return result;
}
