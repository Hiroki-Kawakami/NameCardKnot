// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Binary container constants for the NameCardKnot PDF format.
// Single source of truth shared by the TS writer and (mirrored in) the C++
// parser. See docs/namecard_pdf.md.

export const FOOTER_MAGIC = "NCK1"; // footer header magic (4 bytes)
export const FOOTER_END_MAGIC = "NCKEND01"; // trailer end magic (8 bytes)
export const FOOTER_VERSION = 1;

export const FOOTER_HEADER_SIZE = 12; // magic(4)+version(2)+flags(2)+count(2)+reserved(2)
export const FOOTER_ENTRY_SIZE = 16; // type+flags+subtype+reserved + offset(4)+length(4)+crc(4)
export const FOOTER_TRAILER_SIZE = 24; // base_total_length(4)+footer_offset(4)+crc(4)+reserved(4)+magic(8)

// Asset types (footer entry `type`).
export enum AssetType {
  Name = 0,
  Url = 1,
  Message = 2,
  DisplayJpeg = 3,
  ShareJpeg = 4,
  NameGlyphs = 5,
}

// Asset encodings (footer entry `subtype`).
export enum AssetSubtype {
  Jpeg = 0, // also: strings, name_glyphs blob
  Png = 1,
}

// Footer entry `flags` bits.
export const FLAG_STRIP_ON_SHARE = 0x01;

// name_glyphs blob (see §4.4).
export const GLYPHS_MAGIC = "NGLY"; // 4 bytes
export const GLYPHS_VERSION = 1;
export const GLYPHS_HEADER_SIZE = 16;
export const GLYPHS_ENTRY_SIZE = 16;
export const GLYPHS_COMPRESS_NONE = 0;
export const GLYPHS_COMPRESS_RLE = 1;

// Hardcoded render size (px) for embedded glyphs; matches the device built-in
// lv_font. Not a shared/adjustable constant for now (see §3.2 decision).
export const GLYPH_PX = 48;

// Page geometry (pt = px 1:1; device-display oriented, see §3.3).
export const PAGE1_W = 540; // display image page
export const PAGE1_H = 960;
export const PAGE2_W = 1920; // share confirmation page
export const PAGE2_H = 1080;
