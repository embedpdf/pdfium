// Copyright 2025
// Use of this source code is governed by a BSD-style license.

#include "core/fpdfapi/edit/cpdf_text_redactor.h"

#include <cmath>
#include <sstream>
#include <utility>
#include <vector>
#include <algorithm>

#include "core/fpdfapi/edit/cpdf_contentstream_write_utils.h"
#include "core/fpdfapi/edit/cpdf_pagecontentgenerator.h"
#include "core/fpdfapi/edit/cpdf_pagecontentmanager.h"
#include "core/fpdfapi/font/cpdf_cidfont.h"
#include "core/fpdfapi/font/cpdf_font.h"
#include "core/fpdfapi/page/cpdf_form.h"
#include "core/fpdfapi/page/cpdf_formobject.h"
#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/page/cpdf_pageobject.h"
#include "core/fpdfapi/page/cpdf_pageobjectholder.h"
#include "core/fpdfapi/page/cpdf_textobject.h"
#include "core/fpdfapi/page/cpdf_pathobject.h"
#include "core/fpdfapi/page/cpdf_image.h"
#include "core/fpdfapi/page/cpdf_imageobject.h"
#include "core/fpdfapi/parser/cpdf_document.h"
#include "core/fpdfapi/parser/cpdf_name.h"
#include "core/fpdfapi/parser/cpdf_number.h"
#include "core/fpdfapi/parser/cpdf_stream.h"
#include "core/fpdfapi/parser/cpdf_reference.h"
#include "core/fpdfapi/parser/cpdf_stream_acc.h"
#include "core/fxge/dib/cfx_dibitmap.h"
#include "core/fxge/dib/fx_dib.h"
#include "core/fxcrt/check.h"
#include "core/fxcrt/span.h"

namespace {

static void AddBlackOverlayPaths(CPDF_Page* page,
                                 pdfium::span<const CFX_FloatRect> rects_page_space) {
  if (!page || rects_page_space.empty())
    return;

  for (const auto& r : rects_page_space) {
    auto po = std::make_unique<CPDF_PathObject>();
    po->set_stroke(false);
    po->set_filltype(CFX_FillRenderOptions::FillType::kWinding);
    po->path().AppendFloatRect(r);        // left/bottom/right/top in PAGE USER SPACE
    po->SetPathMatrix(CFX_Matrix());      // identity
    po->CalcBoundingBox();
    po->SetDirty(true);
    page->AppendPageObject(std::move(po));  // appended last => paints on top
  }
}

enum class RedactOutcome { kUnchanged, kModified, kRemovedAll };

inline bool Intersects(const CFX_FloatRect& a, const CFX_FloatRect& b) {
  return a.right > b.left && a.left < b.right && a.top > b.bottom &&
         a.bottom < b.top;
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
// Note: CPDF_TextObject::GetItemInfo() origin_ is already adjusted for vertical
// writing, so we do not apply any extra vertical origin shift here.
CFX_FloatRect GlyphBBoxInPage(const CPDF_TextObject* to,
                              CPDF_Font* font,
                              uint32_t code,
                              const CPDF_TextObject::Item& it,
                              const CFX_Matrix& parent_to_page) {
  FX_RECT r_font_units = font->GetCharBBox(code);
  const float fs = to->GetFontSize();

  CFX_FloatRect glyph_box(
      r_font_units.left * fs / 1000.0f, r_font_units.bottom * fs / 1000.0f,
      r_font_units.right * fs / 1000.0f, r_font_units.top * fs / 1000.0f);

  // Position inside the text object’s local space.
  glyph_box.left += it.origin_.x;
  glyph_box.right += it.origin_.x;
  glyph_box.bottom += it.origin_.y;
  glyph_box.top += it.origin_.y;

  // Text matrix to page space (for this text object), then parent to page.
  const CFX_Matrix tm = to->GetTextMatrix();
  glyph_box = tm.TransformRect(glyph_box);
  return parent_to_page.TransformRect(glyph_box);
}

// Advance in thousandths for a single code, matching how PDFium applies widths
// and char/word spacing during layout.
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

  // Apply word space only for ASCII space in typical (non-special) cases.
  if (code == ' ') {
    const CPDF_CIDFont* cid = font->AsCIDFont();
    if (!cid || cid->GetCharSize(' ') == 1)
      w_th += to->GetWordSpace() * 1000.0f / fs;
  }

  // Always apply char space.
  w_th += to->GetCharSpace() * 1000.0f / fs;
  return w_th;
}

// Round to nearest integer thousandth for stable TJ outputs.
inline int32_t RoundThousandths(float v) {
  return v >= 0 ? static_cast<int32_t>(v + 0.5f)
                : static_cast<int32_t>(v - 0.5f);
}

// Small deadband to tame float fuzz when synthesizing TJ from origins.
constexpr float kTJDeadband = 0.25f;  // thousandths

// State for building a TJ array from kept glyph runs.
struct RedactionState {
  CPDF_Font* font = nullptr;

  // Output buffers for SetSegments(): strings[i] followed by kernings[i] between
  // strings[i] and strings[i+1].
  std::vector<ByteString> strings;
  std::vector<float> kernings;

  // Accumulates original file TJ numbers and removal advances between kept runs.
  float kerning_accumulator = 0.0f;
  bool has_explicit_kerning = false;

  // For synthesized kerning using origins when no explicit TJ exists.
  CFX_PointF prev_glyph_origin{};
  uint32_t prev_glyph_code = 0;

  void ResetBetweenRuns() {
    kerning_accumulator = 0.0f;
    has_explicit_kerning = false;
  }

  void AppendKeptGlyph(const CPDF_TextObject::Item& item) {
    DCHECK(font);
    DCHECK(!strings.empty());
    font->AppendChar(&strings.back(), item.char_code_);
    prev_glyph_origin = item.origin_;
    prev_glyph_code = item.char_code_;
  }
};

// Push a kerning (integer thousandths) and open a new (initially empty) run.
void FlushSegment(RedactionState* s, float kerning_mth) {
  const int32_t rounded = RoundThousandths(kerning_mth);
  if (rounded == 0)
    return;
  s->kernings.push_back(static_cast<float>(rounded));
  s->strings.push_back(ByteString());  // next glyphs will fill this
}

RedactOutcome RedactTextObjectMulti(CPDF_TextObject* to,
                                    pdfium::span<const CFX_FloatRect> page_rects,
                                    const CFX_Matrix& parent_to_page) {
  CPDF_Font* font = to->GetFont();
  if (!font)
    return RedactOutcome::kUnchanged;

  const CPDF_CIDFont* cid = font->AsCIDFont();
  const bool is_vert = cid && cid->IsVertWriting();
  const float fs = to->GetFontSize();

  bool any_kept = false;
  bool any_removed = false;

  RedactionState st;
  st.font = font;
  st.strings.push_back(ByteString());  // start first run

  const size_t n = to->CountItems();
  for (size_t i = 0; i < n; ++i) {
    const CPDF_TextObject::Item it = to->GetItemInfo(i);

    // Original file kerning separator inside TJ.
    if (it.char_code_ == CPDF_Font::kInvalidCharCode) {
      float adj = 0.0f;
      if (to->GetSeparatorAdjustment(i, &adj)) {
        st.kerning_accumulator += adj;  // keep sign; PDF TJ semantics
        st.has_explicit_kerning = true;
      }
      continue;
    }

    // Decide keep/remove by intersection.
    const CFX_FloatRect gbox =
        GlyphBBoxInPage(to, font, it.char_code_, it, parent_to_page);
    const bool hit = IntersectsAny(gbox, page_rects);

    if (hit) {
      // Merge the removed glyph's advance into the pending kerning pool.
      st.kerning_accumulator -= AdvanceThousandths(to, font, it.char_code_);
      any_removed = true;
      continue;
    }

    // First kept glyph in the object.
    if (!any_kept) {
      float leading_offset_user = 0.0f;

      if (st.kerning_accumulator != 0.0f) {
        // Remove pre-run spacing by shifting the text matrix (TJ cannot lead).
        leading_offset_user = -st.kerning_accumulator * fs / 1000.0f;
        st.kerning_accumulator = 0.0f;
        st.has_explicit_kerning = false;
      } else {
        // If no pending spacing, align the run's origin to the first kept glyph.
        leading_offset_user = is_vert ? it.origin_.y : it.origin_.x;
      }

      if (leading_offset_user != 0.0f) {
        CFX_Matrix tm = to->GetTextMatrix();
        // Move along the text X axis in user space (handles rotation).
        tm.e += leading_offset_user * tm.a;
        tm.f += leading_offset_user * tm.b;
        to->SetTextMatrix(tm);
      }
    } else {
      // Between kept runs: emit an inter-run kerning.
      if (st.has_explicit_kerning) {
        float k = st.kerning_accumulator;
        if (std::fabs(k) < kTJDeadband)
          k = 0.0f;
        FlushSegment(&st, k);
      } else {
        // Infer kerning from origins of consecutive kept glyphs.
        const float delta_user = is_vert
                                     ? (it.origin_.y - st.prev_glyph_origin.y)
                                     : (it.origin_.x - st.prev_glyph_origin.x);
        const float delta_mth = delta_user * 1000.0f / fs;
        const float nominal_advance_mth =
            AdvanceThousandths(to, font, st.prev_glyph_code);
        float kerning_mth = nominal_advance_mth - delta_mth;
        if (std::fabs(kerning_mth) < kTJDeadband)
          kerning_mth = 0.0f;
        FlushSegment(&st, kerning_mth);
      }
    }

    // Keep this glyph.
    st.AppendKeptGlyph(it);
    st.ResetBetweenRuns();
    any_kept = true;
  }

  if (!any_kept)
    return any_removed ? RedactOutcome::kRemovedAll : RedactOutcome::kUnchanged;

  // If the last operation opened a new (empty) run by flushing a kerning,
  // drop the dangling run and its paired kerning so we keep the invariant
  // kernings.size() == strings.size() - 1.
  if (!st.strings.empty() && st.strings.back().IsEmpty()) {
    st.strings.pop_back();
    if (!st.kernings.empty())
      st.kernings.pop_back();
  }

  CHECK(st.kernings.size() + 1 == st.strings.size());

  to->SetSegments(pdfium::span(st.strings), pdfium::span(st.kernings));
  to->SetDirty(true);
  // Re-assert Tm to ensure downstream writers notice a change even when the
  // numeric value is identical after float ops.
  CFX_Matrix tm = to->GetTextMatrix();
  to->SetTextMatrix(tm);

  return any_removed ? RedactOutcome::kModified : RedactOutcome::kUnchanged;
}

// Map page-space rects into the image's sample grid (image-local).
static void PageRectsToImageGrid(const CFX_Matrix& image_to_page,
                                 int img_w, int img_h,
                                 pdfium::span<const CFX_FloatRect> page_rects,
                                 std::vector<CFX_FloatRect>* out_image_rects) {
  out_image_rects->clear();
  if (img_w <= 0 || img_h <= 0 || page_rects.empty())
    return;

  // Step 1: page -> unit image space
  const CFX_Matrix page_to_unit = image_to_page.GetInverse();

  out_image_rects->reserve(page_rects.size());
  for (const auto& pr : page_rects) {
    // Page -> unit
    CFX_FloatRect ur = page_to_unit.TransformRect(pr);
    ur.Normalize();

    // Step 2: unit -> pixel
    CFX_FloatRect ir(ur.left   * img_w,
                     ur.bottom * img_h,
                     ur.right  * img_w,
                     ur.top    * img_h);
    ir.Normalize();

    // Clamp to pixel bounds
    ir.left   = std::clamp(ir.left,   0.0f, static_cast<float>(img_w));
    ir.right  = std::clamp(ir.right,  0.0f, static_cast<float>(img_w));
    ir.bottom = std::clamp(ir.bottom, 0.0f, static_cast<float>(img_h));
    ir.top    = std::clamp(ir.top,    0.0f, static_cast<float>(img_h));

    if (ir.right > ir.left && ir.top > ir.bottom)
      out_image_rects->push_back(ir);
  }
}

// Returns true if the image stream was overwritten.
// Returns true if the image stream was overwritten.
// Returns true if the image stream was overwritten.
static bool RedactImageObject(CPDF_Page* page,
                              CPDF_ImageObject* iobj,
                              pdfium::span<const CFX_FloatRect> page_rects,
                              const CFX_Matrix& parent_to_page,
                              bool fill_black) {
  if (!iobj)
    return false;
  CPDF_Image* image = iobj->GetImage();
  if (!image)
    return false;

  CPDF_Document* doc = page->GetDocument();
  const int W = image->GetPixelWidth();
  const int H = image->GetPixelHeight();
  if (W <= 0 || H <= 0)
    return false;

  // Object -> page for this placement.
  const CFX_Matrix img_to_page = parent_to_page * iobj->matrix();

  // Quick reject using unit bbox in page space.
  const CFX_FloatRect img_bbox_page =
      img_to_page.TransformRect(CFX_FloatRect(0, 0, 1.0f, 1.0f));
  bool touches = false;
  for (const auto& r : page_rects) {
    if (img_bbox_page.right > r.left && img_bbox_page.left < r.right &&
        img_bbox_page.top > r.bottom && img_bbox_page.bottom < r.top) {
      touches = true;
      break;
    }
  }
  if (!touches)
    return false;

  // Decode source.
  RetainPtr<CFX_DIBBase> dib = image->LoadDIBBase();
  if (!dib)
    return false;

  const int bpp        = dib->GetBPP();
  const bool is_mask   = dib->IsMaskFormat();
  const bool has_alpha = dib->IsAlphaFormat();

  const bool is_gray8  = (bpp == 8)  && !is_mask;   // may be true for real gray OR indexed
  const bool is_rgb24  = (bpp == 24);
  const bool is_bgra32 = (bpp == 32) &&  has_alpha;
  const bool is_bgrx32 = (bpp == 32) && !has_alpha;

  // Palette detection for indexed-8 images (PNG paletted path).
  auto palette = dib->GetPaletteSpan();             // span<const uint32_t> ARGB (0xAARRGGBB)
  const bool is_indexed8 = is_gray8 && !palette.empty();

  bool palette_has_alpha = false;
  if (is_indexed8) {
    for (uint32_t c : palette) {
      if ((c >> 24) != 0xFF) { palette_has_alpha = true; break; }
    }
  }

  if (!(is_gray8 || is_rgb24 || is_bgra32 || is_bgrx32)) {
    // Unsupported source format.
    return false;
  }

  // If the image has an SMask, keep it so we preserve transparency.
  RetainPtr<const CPDF_Stream> orig_smask_stream;
  if (image->GetStream()) {
    RetainPtr<const CPDF_Dictionary> idict = image->GetStream()->GetDict();
    if (idict) {
      RetainPtr<const CPDF_Object> smask_obj = idict->GetDirectObjectFor("SMask");
      if (smask_obj && smask_obj->AsStream())
        orig_smask_stream = pdfium::WrapRetain(smask_obj->AsStream());
    }
  }

  // Map page-space rects into image pixel space (bottom-up).
  std::vector<CFX_FloatRect> img_rects;
  PageRectsToImageGrid(img_to_page, W, H, page_rects, &img_rects);
  if (img_rects.empty())
    return false;

  struct IRect { int x0, y0, x1, y1; };
  std::vector<IRect> boxes;
  boxes.reserve(img_rects.size());
  for (const auto& r : img_rects) {
    IRect b;
    b.x0 = std::max(0, std::min(W, static_cast<int>(std::floor(r.left))));
    b.x1 = std::max(0, std::min(W, static_cast<int>(std::ceil (r.right))));
    b.y0 = std::max(0, std::min(H, static_cast<int>(std::floor(r.bottom))));
    b.y1 = std::max(0, std::min(H, static_cast<int>(std::ceil (r.top))));
    if (b.x1 > b.x0 && b.y1 > b.y0)
      boxes.push_back(b);
  }
  if (boxes.empty())
    return false;

  const uint8_t fill_val = fill_black ? 0x00 : 0xFF;

  // Build new decoded buffers.
  DataVector<uint8_t> out_rgb(static_cast<size_t>(W) * static_cast<size_t>(H) * 3u);
  DataVector<uint8_t> out_a;  // only used if we have/keep alpha

  // We need an alpha plane if: original was BGRA32, or there was an SMask, or
  // palette carries alpha (PNG paletted transparency).
  bool process_alpha = is_bgra32 || !!orig_smask_stream || (is_indexed8 && palette_has_alpha);

  if (process_alpha) {
    out_a.resize(static_cast<size_t>(W) * static_cast<size_t>(H));
    if (orig_smask_stream && !is_bgra32) {
      auto acc = pdfium::MakeRetain<CPDF_StreamAcc>(orig_smask_stream);
      acc->LoadAllDataFiltered();
      pdfium::span<const uint8_t> span = acc->GetSpan();
      if (span.size() >= out_a.size())
        memcpy(out_a.data(), span.data(), out_a.size());
      else {
        memcpy(out_a.data(), span.data(), span.size());
        std::fill(out_a.begin() + static_cast<ptrdiff_t>(span.size()),
                  out_a.end(), 0xFF);
      }
    } else {
      // Default opaque; specific formats will overwrite per pixel below.
      std::fill(out_a.begin(), out_a.end(), 0xFF);
    }
  }

  size_t total_redacted_px = 0;

  for (int row_top = 0; row_top < H; ++row_top) {
    const int y_img = H - 1 - row_top;  // convert to bottom-up index
    const pdfium::span<const uint8_t> sline = dib->GetScanline(row_top);
    uint8_t* drow_rgb = out_rgb.data() + static_cast<size_t>(row_top) * static_cast<size_t>(W) * 3u;
    uint8_t* arow     = process_alpha ? (out_a.data() + static_cast<size_t>(row_top) * static_cast<size_t>(W)) : nullptr;

    if (sline.empty()) {
      // Defensive: fill whole row as redacted.
      std::fill(drow_rgb, drow_rgb + static_cast<size_t>(W) * 3u, fill_val);
      if (process_alpha)
        std::fill(arow, arow + static_cast<size_t>(W), 0xFF);
      total_redacted_px += static_cast<size_t>(W);
      continue;
    }

    for (int x = 0; x < W; ++x) {
      const bool red = IntersectsAny(
          {static_cast<float>(x), static_cast<float>(y_img),
           static_cast<float>(x + 1), static_cast<float>(y_img + 1)}, img_rects);

      if (red) {
        drow_rgb[3*x + 0] = fill_val;
        drow_rgb[3*x + 1] = fill_val;
        drow_rgb[3*x + 2] = fill_val;
        if (process_alpha)
          arow[x] = 0xFF;  // paint on top => force opaque under the box
        ++total_redacted_px;
        continue;
      }

      if (is_indexed8) {
        // Expand palette index -> RGB (palette entries are ARGB 0xAARRGGBB).
        const uint8_t idx  = sline[x];
        const uint32_t argb = palette[idx];
        drow_rgb[3*x + 0] = static_cast<uint8_t>((argb >> 16) & 0xFF);  // R
        drow_rgb[3*x + 1] = static_cast<uint8_t>((argb >>  8) & 0xFF);  // G
        drow_rgb[3*x + 2] = static_cast<uint8_t>( argb        & 0xFF);  // B
        if (process_alpha && !orig_smask_stream && !is_bgra32 && palette_has_alpha)
          arow[x] = static_cast<uint8_t>((argb >> 24) & 0xFF);
      } else if (is_gray8) {
        const uint8_t v = sline[x];
        drow_rgb[3*x + 0] = v;
        drow_rgb[3*x + 1] = v;
        drow_rgb[3*x + 2] = v;
        // alpha already handled via SMask if present
      } else if (is_rgb24) {
        drow_rgb[3*x + 0] = sline[3*x + 2];
        drow_rgb[3*x + 1] = sline[3*x + 1];
        drow_rgb[3*x + 2] = sline[3*x + 0];
      } else {  // 32-bpp BGRA/BGRx
        drow_rgb[3*x + 0] = sline[4*x + 2];
        drow_rgb[3*x + 1] = sline[4*x + 1];
        drow_rgb[3*x + 2] = sline[4*x + 0];
        if (process_alpha && is_bgra32)
          arow[x] = sline[4*x + 3];
      }
    }
  }

  if (total_redacted_px == 0)
    return false;

  // Ensure redaction regions are fully opaque in the SMask/alpha plane.
  if (process_alpha) {
    for (const auto& box : boxes) {
      for (int y = box.y0; y < box.y1; ++y) {
        const int row_top = H - 1 - y;
        uint8_t* row_ptr = out_a.data() + static_cast<size_t>(row_top) * static_cast<size_t>(W);
        std::fill(row_ptr + box.x0, row_ptr + box.x1, 0xFF);
      }
    }
  }

  // Build main image dict (decoded RGB).
  RetainPtr<CPDF_Dictionary> ndict = doc->New<CPDF_Dictionary>();
  ndict->SetNewFor<CPDF_Name>("Type", "XObject");
  ndict->SetNewFor<CPDF_Name>("Subtype", "Image");
  ndict->SetNewFor<CPDF_Number>("Width", W);
  ndict->SetNewFor<CPDF_Number>("Height", H);
  ndict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceRGB");
  ndict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);

  // If we have/kept alpha, attach a soft mask.
  if (process_alpha) {
    RetainPtr<CPDF_Dictionary> smask_dict = doc->New<CPDF_Dictionary>();
    smask_dict->SetNewFor<CPDF_Name>("Type", "XObject");
    smask_dict->SetNewFor<CPDF_Name>("Subtype", "Image");
    smask_dict->SetNewFor<CPDF_Number>("Width", W);
    smask_dict->SetNewFor<CPDF_Number>("Height", H);
    smask_dict->SetNewFor<CPDF_Name>("ColorSpace", "DeviceGray");
    smask_dict->SetNewFor<CPDF_Number>("BitsPerComponent", 8);

    RetainPtr<CPDF_Stream> smask_stream =
        pdfium::MakeRetain<CPDF_Stream>(std::move(out_a), std::move(smask_dict));
    const uint32_t smask_objnum = doc->AddIndirectObject(smask_stream);
    ndict->SetFor("SMask", pdfium::MakeRetain<CPDF_Reference>(doc, smask_objnum));
  }

  const bool ok = image->OverwriteStreamInPlace(std::move(out_rgb), std::move(ndict),
                                                /*data_is_decoded=*/true);
  if (ok) {
    image->ResetCache(page);
    page->ClearRenderContext();
    iobj->SetDirty(true);
  }
  return ok;
}

// Redact all text objects inside a holder (page or form). If `recurse_forms` is
// true, also descends into nested Form XObjects via their placement matrices.
//
// `to_page` transforms holder-local space to PAGE USER SPACE.
// Redact all page objects inside a holder (page or form).
bool RedactHolder(CPDF_Page* page_for_cache,
                  CPDF_PageObjectHolder* holder,
                  pdfium::span<const CFX_FloatRect> page_rects,
                  const CFX_Matrix& to_page,
                  bool recurse_forms,
                  bool fill_black) {
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

    if (CPDF_ImageObject* io = po->AsImage()) {
      if (RedactImageObject(page_for_cache, io, page_rects, to_page, fill_black)) {
        changed = true;
      }
      continue;
    }

    if (CPDF_PathObject* path = po->AsPath()) {
      // Get the path's bounding box and transform it to page coordinates.
      CFX_Matrix total_transform = to_page * path->matrix();
      CFX_FloatRect path_bbox_page = total_transform.TransformRect(path->path().GetBoundingBox());
      path_bbox_page.Normalize();

      // Check if the path's bounding box is completely inside any redaction rect.
      for (const auto& redact_rect : page_rects) {
        if (path_bbox_page.left >= redact_rect.left &&
            path_bbox_page.right <= redact_rect.right &&
            path_bbox_page.bottom >= redact_rect.bottom &&
            path_bbox_page.top <= redact_rect.top) {
          
          to_remove.push_back(path);
          changed = true;
          break;
        }
      }
      continue;
    }

    if (recurse_forms) {
      if (CPDF_FormObject* fo = po->AsForm()) {
        CPDF_Form* form = fo->form();
        if (!form)
          continue;

        const CFX_Matrix placement = fo->form_matrix();
        const CFX_Matrix next_to_page = to_page * placement;
        const bool form_changed = RedactHolder(page_for_cache, form, page_rects, next_to_page, true, fill_black);

        if (form_changed) {
          CPDF_PageContentGenerator form_gen(form);
          form_gen.GenerateContent();
          changed = true;
        }
      }
    }
  }

  // Physically remove fully emptied text and path objects.
  if (!to_remove.empty()) {
    for (CPDF_PageObject* obj : to_remove) {
      holder->RemovePageObject(obj);
    }
    changed = true;
  }

  return changed;
}

}  // namespace

bool RedactTextInRect(CPDF_Page* page,
                      const CFX_FloatRect& page_space_rect_in,
                      bool recurse_forms,
                      bool draw_black_boxes) {
  if (!page)
    return false;

  CFX_FloatRect r = page_space_rect_in;
  r.Normalize();
  const CFX_Matrix identity;

  const CFX_FloatRect rects[] = {r};
  const bool changed =
      RedactHolder(page, page, pdfium::span(rects), identity, recurse_forms,
                   /*fill_black=*/draw_black_boxes);

  if (draw_black_boxes) {
    AddBlackOverlayPaths(page, pdfium::span(rects));  // paint on top
  }

  // Adding a stream is a change; reflect that.
  return changed || draw_black_boxes;
}

bool RedactTextInRects(CPDF_Page* page,
                       pdfium::span<const CFX_FloatRect> page_space_rects_in,
                       bool recurse_forms,
                       bool draw_black_boxes) {
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
  const bool changed =
      RedactHolder(page, page, pdfium::span(rects), identity, recurse_forms,
                   /*fill_black=*/draw_black_boxes);

  if (draw_black_boxes) {
    AddBlackOverlayPaths(page, pdfium::span(rects));  // paint on top
  }

  return changed || draw_black_boxes;
}