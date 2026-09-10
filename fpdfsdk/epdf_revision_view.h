// Copyright 2026 CloudPDF LTD
// SPDX-License-Identifier: Apache-2.0

#ifndef FPDFSDK_EPDF_REVISION_VIEW_H_
#define FPDFSDK_EPDF_REVISION_VIEW_H_

#include <stdint.h>

#include <memory>

#include "core/fxcrt/retain_ptr.h"
#include "core/fxcrt/unowned_ptr.h"

class CPDF_Document;
class CPDF_Object;
class CPDF_ParseOnlyHolder;
class CPDF_Parser;
class IFX_SeekableReadStream;

namespace epdf {

// The immutable bytes a document was loaded from, with a parser over exactly
// those bytes. Revision analysis - revision boundaries, signature coverage,
// digests, the cross-revision object diff - reads these bytes and nothing
// else: an edit that has not been saved is not part of any revision.
//
// An ordinary document is its own view: its parser holds the loaded file.
// A layer document is not - its parser is the BASE parser, and the bytes it
// was loaded from are the base file followed by the delta it ingested. The
// view parses that concatenation privately, so a layer reports the same
// revisions, coverage, digests and object values as the same bytes opened as
// a plain document, and two layers over one base with different deltas no
// longer compare as identical.
class RevisionView {
 public:
  // Null when the document has no parser or its loaded bytes do not parse.
  static std::unique_ptr<RevisionView> Create(CPDF_Document* document);
  ~RevisionView();

  CPDF_Parser* parser() const { return parser_; }
  RetainPtr<IFX_SeekableReadStream> file() const;

  // Parses |objnum| from the loaded bytes. Never consults a document's
  // object cache, so a value changed in memory is not mistaken for the
  // value a revision holds.
  RetainPtr<CPDF_Object> ParseObject(uint32_t objnum) const;

 private:
  RevisionView();

  // Layers only: a private parser over base + delta. Declared before
  // |parser_| and destroyed after it; the parser must not outlive the holder.
  std::unique_ptr<CPDF_ParseOnlyHolder> holder_;
  std::unique_ptr<CPDF_Parser> owned_parser_;
  UnownedPtr<CPDF_Parser> parser_;
};

}  // namespace epdf

#endif  // FPDFSDK_EPDF_REVISION_VIEW_H_
