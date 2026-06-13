// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CORE_FPDFAPI_PARSER_CPDF_REVISION_PROVIDER_H_
#define CORE_FPDFAPI_PARSER_CPDF_REVISION_PROVIDER_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "core/fpdfapi/parser/cpdf_cross_ref_table.h"
#include "core/fxcrt/fx_types.h"

class CPDF_Parser;

class CPDF_RevisionProvider {
 public:
  using ObjectInfo = CPDF_CrossRefTable::ObjectInfo;
  using ObjectMap = std::map<uint32_t, ObjectInfo>;

  struct RevisionLayer {
    FX_FILESIZE xref_offset = 0;
    // Byte offset after %%EOF for this revision's incremental save.
    // WARNING: Derived from GetTrailerEnds() which returns unsigned int,
    // truncating FX_FILESIZE for files > 4 GB. This is an upstream PDFium
    // limitation.
    FX_FILESIZE revision_end = 0;
    ObjectMap layer_objects;
  };

  CPDF_RevisionProvider();
  ~CPDF_RevisionProvider();

  // Build layers by re-parsing xref sections from stored offsets.
  // Uses parser's syntax_ to re-read xref tables/streams at known positions.
  bool Build(CPDF_Parser* parser,
             const std::vector<FX_FILESIZE>& xref_list,
             const std::vector<FX_FILESIZE>& xref_stream_list);

  bool is_built() const { return built_; }
  size_t GetRevisionCount() const;
  const RevisionLayer& GetLayer(size_t index) const;

  // Build merged object map representing visible xref state at revision N.
  // Merges layers 0..revision_index inclusive.
  //
  // Returns xref-level visibility: which object numbers exist and where
  // they live (offset or compressed archive ref). Does NOT parse or return
  // actual object values.
  ObjectMap GetVisibleObjectsAtRevision(size_t revision_index) const;

 private:
  std::vector<RevisionLayer> layers_;
  bool built_ = false;
};

#endif  // CORE_FPDFAPI_PARSER_CPDF_REVISION_PROVIDER_H_
