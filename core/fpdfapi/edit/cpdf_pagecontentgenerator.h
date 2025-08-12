// Copyright 2016 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original code copyright 2014 Foxit Software Inc. http://www.foxitsoftware.com

#ifndef CORE_FPDFAPI_EDIT_CPDF_PAGECONTENTGENERATOR_H_
#define CORE_FPDFAPI_EDIT_CPDF_PAGECONTENTGENERATOR_H_

#include <stdint.h>

#include <map>
#include <vector>

#include "core/fxcrt/bytestring.h"
#include "core/fxcrt/fx_string_wrappers.h"
#include "core/fxcrt/unowned_ptr.h"

class CPDF_ContentMarks;
class CPDF_Document;
class CPDF_FormObject;
class CPDF_ImageObject;
class CPDF_Object;
class CPDF_PageObject;
class CPDF_PageObjectHolder;
class CPDF_Path;
class CPDF_PathObject;
class CPDF_TextObject;
class CPDF_Color; 
class CPDF_ColorSpace;
class CPDF_ColorState;

class CPDF_PageContentGenerator {
 public:
  explicit CPDF_PageContentGenerator(CPDF_PageObjectHolder* pObjHolder);
  ~CPDF_PageContentGenerator();

  void GenerateContent();
  bool ProcessPageObjects(fxcrt::ostringstream* buf);

 private:
  friend class CPDFPageContentGeneratorTest;

  void ProcessPageObject(fxcrt::ostringstream* buf, CPDF_PageObject* pPageObj);
  void ProcessPathPoints(fxcrt::ostringstream* buf, CPDF_Path* pPath);
  void ProcessPath(fxcrt::ostringstream* buf, CPDF_PathObject* pPathObj);
  void ProcessForm(fxcrt::ostringstream* buf, CPDF_FormObject* pFormObj);
  void ProcessImage(fxcrt::ostringstream* buf, CPDF_ImageObject* pImageObj);
  void ProcessGraphics(fxcrt::ostringstream* buf, CPDF_PageObject* pPageObj);
  void ProcessDefaultGraphics(fxcrt::ostringstream* buf);
  void ProcessText(fxcrt::ostringstream* buf, CPDF_TextObject* pTextObj);
  bool EmitColor(fxcrt::ostringstream& buf,
    const CPDF_Color* color,
    bool is_stroke,
    CPDF_PageObject* owner);
  ByteString GetOrCreateDefaultGraphics() const;
  ByteString RealizeColorSpaceObject(const CPDF_ColorSpace* cs);
  ByteString RealizeResource(const CPDF_Object* pResource,
                             ByteStringView type) const;
  const CPDF_ContentMarks* ProcessContentMarks(fxcrt::ostringstream* buf,
                                               const CPDF_PageObject* pPageObj,
                                               const CPDF_ContentMarks* pPrev);
  void FinishMarks(fxcrt::ostringstream* buf,
                   const CPDF_ContentMarks* pContentMarks);

  // Returns a map from content stream index to new stream data. Unmodified
  // streams are not touched.
  std::map<int32_t, fxcrt::ostringstream> GenerateModifiedStreams();

  // For each entry in `new_stream_data`, adds the string buffer to the page's
  // content stream.
  void UpdateContentStreams(
      std::map<int32_t, fxcrt::ostringstream>&& new_stream_data);

  // Sets the stream index of all page objects with stream index ==
  // |CPDF_PageObject::kNoContentStream|. These are new objects that had not
  // been parsed from or written to any content stream yet.
  void UpdateStreamlessPageObjects(int new_content_stream_index);

  // Updates the resource dictionary for `obj_holder_` to account for all the
  // changes.
  void UpdateResourcesDict();

  UnownedPtr<CPDF_PageObjectHolder> const obj_holder_;
  UnownedPtr<CPDF_Document> const document_;
  std::vector<UnownedPtr<CPDF_PageObject>> page_objects_;
  ByteString default_graphics_name_;
};

#endif  // CORE_FPDFAPI_EDIT_CPDF_PAGECONTENTGENERATOR_H_
