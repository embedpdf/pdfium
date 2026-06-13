// Copyright 2024 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/fpdfapi/parser/cpdf_revision_provider.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "core/fpdfapi/parser/cpdf_array.h"
#include "core/fpdfapi/parser/cpdf_dictionary.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_parser.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fxcrt/fx_safe_types.h"
#include "core/fxcrt/numerics/safe_conversions.h"
#include "core/fxcrt/span.h"

using ObjectType = CPDF_CrossRefTable::ObjectType;
using ObjectInfo = CPDF_CrossRefTable::ObjectInfo;

namespace {

struct XRefStreamIndexEntry {
  uint32_t start_obj_num;
  uint32_t obj_count;
};

constexpr size_t kMinFieldCount = 3;

uint32_t GetVarInt(pdfium::span<const uint8_t> input) {
  uint32_t result = 0;
  for (uint8_t c : input) {
    result = result * 256 + c;
  }
  return result;
}

std::optional<ObjectType> GetObjectTypeFromStreamType(uint32_t type) {
  switch (type) {
    case 0:
      return ObjectType::kFree;
    case 1:
      return ObjectType::kNormal;
    case 2:
      return ObjectType::kCompressed;
    default:
      return std::nullopt;
  }
}

std::vector<XRefStreamIndexEntry> GetStreamIndices(const CPDF_Array* array,
                                                   uint32_t size) {
  std::vector<XRefStreamIndexEntry> indices;
  if (array) {
    for (size_t i = 0; i < array->size() / 2; i++) {
      RetainPtr<const CPDF_Number> start_num = array->GetNumberAt(i * 2);
      if (!start_num)
        continue;
      RetainPtr<const CPDF_Number> count_obj = array->GetNumberAt(i * 2 + 1);
      if (!count_obj)
        continue;
      int nStartNum = start_num->GetInteger();
      int nCount = count_obj->GetInteger();
      if (nStartNum < 0 || nCount <= 0)
        continue;
      indices.push_back(
          {static_cast<uint32_t>(nStartNum), static_cast<uint32_t>(nCount)});
    }
  }
  if (indices.empty())
    indices.push_back({0, size});
  return indices;
}

std::vector<uint32_t> GetFieldWidths(const CPDF_Array* array) {
  std::vector<uint32_t> results;
  if (!array)
    return results;
  CPDF_ArrayLocker locker(array);
  for (const auto& obj : locker)
    results.push_back(obj->GetInteger());
  return results;
}

// Decode one xref stream entry and insert into the output map.
void DecodeOneEntry(pdfium::span<const uint8_t> entry_span,
                    pdfium::span<const uint32_t> field_widths,
                    uint32_t obj_num,
                    CPDF_RevisionProvider::ObjectMap* out) {
  ObjectType type;
  if (field_widths[0]) {
    uint32_t raw_type = GetVarInt(entry_span.first(field_widths[0]));
    std::optional<ObjectType> maybe_type = GetObjectTypeFromStreamType(raw_type);
    if (!maybe_type.has_value())
      return;
    type = maybe_type.value();
  } else {
    type = ObjectType::kNormal;
  }

  uint32_t second =
      GetVarInt(entry_span.subspan(field_widths[0], field_widths[1]));
  uint32_t third = GetVarInt(
      entry_span.subspan(field_widths[0] + field_widths[1], field_widths[2]));

  ObjectInfo info;
  info.type = type;

  if (type == ObjectType::kFree) {
    if (pdfium::IsValueInRangeForNumericType<uint16_t>(third)) {
      info.gennum = static_cast<uint16_t>(third);
      (*out)[obj_num] = info;
    }
    return;
  }

  if (type == ObjectType::kNormal) {
    if (pdfium::IsValueInRangeForNumericType<FX_FILESIZE>(second) &&
        pdfium::IsValueInRangeForNumericType<uint16_t>(third)) {
      info.pos = static_cast<FX_FILESIZE>(second);
      info.gennum = static_cast<uint16_t>(third);
      (*out)[obj_num] = info;
    }
    return;
  }

  // kCompressed
  if (obj_num <= CPDF_Parser::kMaxObjectNumber) {
    info.archive.obj_num = second;
    info.archive.obj_index = third;
    (*out)[obj_num] = info;
  }
}

// Decode all xref stream entries from a stream object into an ObjectMap.
CPDF_RevisionProvider::ObjectMap DecodeXRefStreamEntries(
    const CPDF_Stream* stream) {
  CPDF_RevisionProvider::ObjectMap result;
  if (!stream)
    return result;

  RetainPtr<const CPDF_Dictionary> dict = stream->GetDict();
  if (!dict)
    return result;

  const int32_t size = dict->GetIntegerFor("Size");
  if (size < 0)
    return result;

  std::vector<XRefStreamIndexEntry> indices =
      GetStreamIndices(dict->GetArrayFor("Index").Get(),
                       static_cast<uint32_t>(size));

  std::vector<uint32_t> field_widths =
      GetFieldWidths(dict->GetArrayFor("W").Get());
  if (field_widths.size() < kMinFieldCount)
    return result;

  FX_SAFE_UINT32 dwAccWidth;
  for (uint32_t width : field_widths)
    dwAccWidth += width;
  if (!dwAccWidth.IsValid())
    return result;

  uint32_t total_width = dwAccWidth.ValueOrDie();

  auto pAcc = pdfium::MakeRetain<CPDF_StreamAcc>(
      pdfium::WrapRetain(const_cast<CPDF_Stream*>(stream)));
  pAcc->LoadAllDataFiltered();

  pdfium::span<const uint8_t> data_span = pAcc->GetSpan();
  uint32_t segindex = 0;
  for (const auto& index : indices) {
    FX_SAFE_UINT32 seg_end = segindex;
    seg_end += index.obj_count;
    seg_end *= total_width;
    if (!seg_end.IsValid() || seg_end.ValueOrDie() > data_span.size())
      continue;

    pdfium::span<const uint8_t> seg_span = data_span.subspan(
        segindex * total_width, index.obj_count * total_width);

    for (uint32_t i = 0; i < index.obj_count; ++i) {
      const uint32_t obj_num = index.start_obj_num + i;
      if (obj_num > CPDF_Parser::kMaxObjectNumber)
        break;
      DecodeOneEntry(seg_span.subspan(i * total_width, total_width),
                     field_widths, obj_num, &result);
    }
    segindex += index.obj_count;
  }
  return result;
}

}  // namespace

CPDF_RevisionProvider::CPDF_RevisionProvider() = default;
CPDF_RevisionProvider::~CPDF_RevisionProvider() = default;

bool CPDF_RevisionProvider::Build(
    CPDF_Parser* parser,
    const std::vector<FX_FILESIZE>& xref_list,
    const std::vector<FX_FILESIZE>& xref_stream_list) {
  if (built_)
    return true;

  if (!parser || xref_list.size() != xref_stream_list.size())
    return false;

  std::vector<unsigned int> trailer_ends = parser->GetTrailerEnds();

  const size_t count = xref_list.size();
  layers_.resize(count);

  for (size_t i = 0; i < count; ++i) {
    RevisionLayer& layer = layers_[i];
    layer.xref_offset = std::max(xref_list[i], xref_stream_list[i]);

    // Map to trailer_ends by index. trailer_ends are ordered by file position
    // (earliest %%EOF first), matching oldest-first ordering of xref_list
    // after FindAllCrossReferenceTablesAndStream prepends to the vectors.
    if (i < trailer_ends.size()) {
      layer.revision_end = static_cast<FX_FILESIZE>(trailer_ends[i]);
    } else if (!trailer_ends.empty()) {
      layer.revision_end =
          static_cast<FX_FILESIZE>(trailer_ends.back());
    } else {
      layer.revision_end = parser->GetDocumentSize();
    }

    // Extract xref stream entries for this layer.
    if (xref_stream_list[i] > 0) {
      RetainPtr<CPDF_Object> obj =
          parser->ParseIndirectObjectAtForTesting(xref_stream_list[i]);
      if (const CPDF_Stream* stream = obj ? obj->AsStream() : nullptr) {
        layer.layer_objects = DecodeXRefStreamEntries(stream);
      }
    }

    // Extract classic xref table entries for this layer. Table entries
    // take precedence over stream entries per ISO 32000-1 7.5.8.4,
    // so they are applied after stream entries to overwrite conflicts.
    if (xref_list[i] > 0) {
      CPDF_Parser::ObjectMap table_objects;
      if (parser->ExtractCrossRefTableEntriesAt(xref_list[i],
                                                &table_objects)) {
        for (const auto& [obj_num, info] : table_objects) {
          layer.layer_objects[obj_num] = info;
        }
      }
    }
  }

  built_ = true;
  return true;
}

size_t CPDF_RevisionProvider::GetRevisionCount() const {
  return layers_.size();
}

const CPDF_RevisionProvider::RevisionLayer&
CPDF_RevisionProvider::GetLayer(size_t index) const {
  return layers_[index];
}

CPDF_RevisionProvider::ObjectMap
CPDF_RevisionProvider::GetVisibleObjectsAtRevision(
    size_t revision_index) const {
  ObjectMap merged;
  for (size_t i = 0; i <= revision_index && i < layers_.size(); ++i) {
    for (const auto& [obj_num, info] : layers_[i].layer_objects) {
      merged[obj_num] = info;
    }
  }
  return merged;
}
