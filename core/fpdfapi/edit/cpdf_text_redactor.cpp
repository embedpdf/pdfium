// Copyright 2025
// Use of this source code is governed by a BSD-style license.

#include "core/fpdfapi/edit/cpdf_text_redactor.h"

#include <utility>
#include <vector>

#include "core/fpdfapi/edit/cpdf_pagecontentgenerator.h"
#include "core/fpdfapi/font/cpdf_cidfont.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_pageobjectholder.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fxcrt/check.h"

namespace {

inline bool Intersects(const CFX_FloatRect& a, const CFX_FloatRect& b) {
  return a.right > b.left && a.left < b.right &&
         a.top > b.bottom && a.bottom < b.top;
}

inline bool IntersectsAny(const CFX_FloatRect& box,
                          pdfium::span<const CFX_FloatRect> rects) {
  for (const auto& r : rects) {
    if (Intersects(box, r))
      return true;
  }
  return false;
}

// Compute a glyph's bbox in PAGE USER SPACE.
//
// Important: CPDF_TextObject::GetItemInfo() already adjusts `origin_` for
// vertical writing (includes the vertical origin shift), so we do NOT apply
// that offset again here.
CFX_FloatRect GlyphBBoxInPage(const CPDF_TextObject* to,
                              CPDF_Font* font,
                              uint32_t code,
                              const CPDF_TextObject::Item& it,
                              const CFX_Matrix& parent_to_page) {
  // Glyph bbox in font units.
  FX_RECT r_font_units = font->GetCharBBox(code);

  const float fs = to->GetFontSize();
  // Scale from 1/1000 em to user units.
  CFX_FloatRect glyph_box(
      r_font_units.left * fs / 1000.0f,   r_font_units.bottom * fs / 1000.0f,
      r_font_units.right * fs / 1000.0f,  r_font_units.top * fs / 1000.0f);

  // Position within the text object’s local space.
  glyph_box.left   += it.origin_.x;
  glyph_box.right  += it.origin_.x;
  glyph_box.bottom += it.origin_.y;
  glyph_box.top    += it.origin_.y;

  // Text matrix to page space (for this text object).
  const CFX_Matrix tm = to->GetTextMatrix();
  glyph_box = tm.TransformRect(glyph_box);

  // Parent transform (e.g., Form placement) to page space.
  return parent_to_page.TransformRect(glyph_box);
}

// Advance in thousandths for a single code, matching CalcPositionDataInternal().
// - Width portion is already thousandths.
// - char/word spaces are user-space, convert to thousandths via 1000/fs.
float AdvanceThousandths(const CPDF_TextObject* to,
                         CPDF_Font* font,
                         uint32_t code) {
  float w_th = 0.0f;
  if (const CPDF_CIDFont* cid = font->AsCIDFont(); cid && cid->IsVertWriting()) {
    const uint16_t c = cid->CIDFromCharCode(code);
    w_th = static_cast<float>(cid->GetVertWidth(c));
  } else {
    w_th = static_cast<float>(font->GetCharWidthF(code));
  }

  const float fs = to->GetFontSize();
  // Word space applies to ASCII space in non-vertical, non-special cases.
  if (code == ' ') {
    const CPDF_CIDFont* cid = font->AsCIDFont();
    if (!cid || cid->GetCharSize(' ') == 1)
      w_th += to->GetWordSpace() * 1000.0f / fs;
  }
  w_th += to->GetCharSpace() * 1000.0f / fs;
  return w_th;
}

enum class RedactOutcome { kUnchanged, kModified, kRemovedAll };

// Rewrites `to` so glyphs intersecting ANY rect in `page_rects` are dropped
// and spacing is preserved via TJ. Returns outcome.
RedactOutcome RedactTextObjectMulti(CPDF_TextObject* to,
                                    pdfium::span<const CFX_FloatRect> page_rects,
                                    const CFX_Matrix& parent_to_page) {
  CPDF_Font* font = to->GetFont();
  if (!font)
    return RedactOutcome::kUnchanged;

  ByteString run;                    // current kept hex run
  std::vector<ByteString> strings;   // segments for SetSegments()
  std::vector<float> kernings;       // thousandths between segments
  float pending_tj = 0.0f;           // accumulated removal + original TJ
  bool any_kept = false;
  bool any_removed = false;

  const size_t n = to->CountItems();
  for (size_t i = 0; i < n; ++i) {
    CPDF_TextObject::Item it = to->GetItemInfo(i);

    if (it.char_code_ == CPDF_Font::kInvalidCharCode) {
      float original_adj = 0.0f;
      if (to->GetSeparatorAdjustment(i, &original_adj)) {
        // Merge original TJ into the pending pool. It will be emitted (with
        // sign preserved) when we flush the next kept run.
        pending_tj += original_adj;
      }
      continue;
    }

    const CFX_FloatRect gbox =
        GlyphBBoxInPage(to, font, it.char_code_, it, parent_to_page);
    const bool hit = IntersectsAny(gbox, page_rects);

    if (hit) {
      any_removed = true;
      pending_tj -= AdvanceThousandths(to, font, it.char_code_);
      continue;
    }

    // Keep this glyph.
    if (!run.IsEmpty() && pending_tj != 0.0f) {
      strings.push_back(run);
      kernings.push_back(pending_tj);
      run.clear();
      pending_tj = 0.0f;
    } else if (run.IsEmpty() && pending_tj != 0.0f) {
      // We have removal/TJ before the first kept glyph: create an empty segment
      // so we can attach the kerning in between segments.
      strings.emplace_back(ByteString());
      kernings.push_back(pending_tj);
      pending_tj = 0.0f;
    }

    font->AppendChar(&run, it.char_code_);
    any_kept = true;
  }

  if (!run.IsEmpty())
    strings.push_back(run);

  if (!any_kept)
    return any_removed ? RedactOutcome::kRemovedAll : RedactOutcome::kUnchanged;

  // `kernings.size()` must be exactly `strings.size() - 1`.
  CHECK(kernings.size() + 1 == strings.size());

  // Rebuild the text object.
  to->SetSegments(pdfium::span(strings), pdfium::span(kernings));
  to->SetDirty(true);
  CFX_Matrix tm = to->GetTextMatrix();
  to->SetTextMatrix(tm); 
  return any_removed ? RedactOutcome::kModified : RedactOutcome::kUnchanged;
}

// Redact all text objects inside a holder (page or form). If `recurse_forms` is
// true, also descends into nested form XObjects using their placement matrices.
//
// `to_page` is the transform from holder-local space to PAGE USER SPACE.
//
// Returns true if anything changed in this holder.
bool RedactHolder(CPDF_PageObjectHolder* holder,
                  pdfium::span<const CFX_FloatRect> page_rects,
                  const CFX_Matrix& to_page,
                  bool recurse_forms) {
  bool changed = false;
  std::vector<CPDF_PageObject*> to_remove;

  for (auto it = holder->begin(); it != holder->end(); ++it) {
    CPDF_PageObject* po = it->get();
    if (!po->IsActive())
      continue;

    if (CPDF_TextObject* to = po->AsText()) {
      const RedactOutcome out = RedactTextObjectMulti(to, page_rects, to_page);
      if (out == RedactOutcome::kRemovedAll) {
        to_remove.push_back(po);
        changed = true;
      } else if (out == RedactOutcome::kModified) {
        changed = true;
      }
      continue;
    }

    if (recurse_forms) {
      if (CPDF_FormObject* fo = po->AsForm()) {
        CPDF_Form* form = fo->form();
        if (!form)
          continue;

        // Placement matrix (object space -> parent space).
        const CFX_Matrix placement = fo->form_matrix();
        const CFX_Matrix next_to_page = to_page * placement;

        // Recurse into the form’s own holder space.
        const bool form_changed =
            RedactHolder(form, page_rects, next_to_page, /*recurse_forms=*/true);

        if (form_changed) {
          // Regenerate the form XObject stream immediately so changes are
          // persisted and visible to the page; there's no public API to do
          // this later from the embedder.
          CPDF_PageContentGenerator form_gen(form);
          form_gen.GenerateContent();
          changed = true;
        }
      }
    }
  }

  // Physically remove any fully-emptied text objects.
  if (!to_remove.empty()) {
    for (CPDF_PageObject* obj : to_remove) {
      std::unique_ptr<CPDF_PageObject> unused = holder->RemovePageObject(obj);
      (void)unused;
    }
    changed = true;
  }

  return changed;
}

}  // namespace

bool RedactTextInRect(CPDF_Page* page,
                      const CFX_FloatRect& page_space_rect_in,
                      bool recurse_forms) {
  if (!page)
    return false;

  CFX_FloatRect r = page_space_rect_in;
  r.Normalize();
  const CFX_Matrix identity;

  const CFX_FloatRect rects[] = {r};
  return RedactHolder(page, pdfium::span(rects), identity, recurse_forms);
}

bool RedactTextInRects(CPDF_Page* page,
                       pdfium::span<const CFX_FloatRect> page_space_rects_in,
                       bool recurse_forms) {
  if (!page || page_space_rects_in.empty())
    return false;

  // Normalize copies.
  std::vector<CFX_FloatRect> rects;
  rects.reserve(page_space_rects_in.size());
  for (const auto& rr : page_space_rects_in) {
    CFX_FloatRect r = rr;
    r.Normalize();
    rects.push_back(r);
  }

  const CFX_Matrix identity;
  return RedactHolder(page, pdfium::span(rects), identity, recurse_forms);
}