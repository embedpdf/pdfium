// Copyright 2025 The PDFium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONSTANTS_METADATA_H_
#define CONSTANTS_METADATA_H_

namespace pdfium {
namespace metadata {

// ISO 32000-1:2008, 14.3.3 Trapped entry in the document Information dictionary.
inline constexpr char kInfoTrapped[] = "Trapped";

// Common /Info dictionary keys (Document Information Dictionary).
inline constexpr char kInfoTitle[] = "Title";
inline constexpr char kInfoAuthor[] = "Author";
inline constexpr char kInfoSubject[] = "Subject";
inline constexpr char kInfoKeywords[] = "Keywords";
inline constexpr char kInfoProducer[] = "Producer";
inline constexpr char kInfoCreator[] = "Creator";
inline constexpr char kInfoCreationDate[] = "CreationDate";
inline constexpr char kInfoModDate[] = "ModDate";

// Allowed name values for /Trapped.
inline constexpr char kNameTrue[] = "True";
inline constexpr char kNameFalse[] = "False";
inline constexpr char kNameUnknown[] = "Unknown";

}  // namespace metadata
}  // namespace pdfium

#endif  // CONSTANTS_METADATA_H_
