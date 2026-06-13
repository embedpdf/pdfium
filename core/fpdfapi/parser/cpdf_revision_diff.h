// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_REVISION_DIFF_H_
#define CORE_FPDFAPI_PARSER_CPDF_REVISION_DIFF_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "core/fpdfapi/parser/cpdf_cross_ref_table.h"

enum class RevisionDiffCategory : uint8_t {
  kAdded = 0,
  kModified = 1,
  kFreed = 2,
};

struct RevisionDiffEntry {
  uint32_t obj_num;
  RevisionDiffCategory category;
};

class CPDF_RevisionDiff {
 public:
  using ObjectInfo = CPDF_CrossRefTable::ObjectInfo;
  using ObjectMap = std::map<uint32_t, ObjectInfo>;

  static std::vector<RevisionDiffEntry> ComputeDiff(const ObjectMap& older,
                                                    const ObjectMap& newer);
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_REVISION_DIFF_H_
