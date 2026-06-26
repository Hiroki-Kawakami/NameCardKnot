// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// name_glyphs blob encode/decode (see docs/namecard_pdf.md §4.4).
// Rasterization (canvas) lives elsewhere; this module only serializes a
// GlyphSet into the 1bpp / bit-run-RLE container the device parses.

import { ByteWriter } from "./bytes.ts";
import {
  GLYPHS_COMPRESS_RLE,
  GLYPHS_ENTRY_SIZE,
  GLYPHS_HEADER_SIZE,
  GLYPHS_MAGIC,
  GLYPHS_VERSION,
  GLYPH_PX,
} from "./constants.ts";
import { rleDecode, rleEncode } from "./rle.ts";

export interface Glyph {
  codepoint: number;
  advW: number; // advance width in 1/16 px (8.4 fixed)
  boxW: number;
  boxH: number;
  ofsX: number;
  ofsY: number;
  bits: Uint8Array; // packed MSB-first, ceil(boxW*boxH/8) bytes (empty if box 0)
}

export interface GlyphSet {
  glyphPx: number;
  baseLine: number;
  lineHeight: number;
  glyphs: Glyph[];
}

export function encodeNameGlyphs(set: GlyphSet): Uint8Array {
  const glyphs = [...set.glyphs].sort((a, b) => a.codepoint - b.codepoint);

  // Build the compressed data section, recording per-glyph offset/len.
  const data = new ByteWriter(256);
  const placed = glyphs.map((g) => {
    const nbits = g.boxW * g.boxH;
    const off = data.length;
    if (nbits > 0) data.bytes(rleEncode(g.bits, nbits));
    return { g, dataOff: off, dataLen: data.length - off };
  });
  const dataBytes = data.toBytes();

  const w = new ByteWriter(GLYPHS_HEADER_SIZE + glyphs.length * GLYPHS_ENTRY_SIZE + dataBytes.length);
  w.ascii(GLYPHS_MAGIC).u8(GLYPHS_VERSION).u8(1).u8(GLYPHS_COMPRESS_RLE).u8(0);
  w.u16(set.glyphPx).u16(set.baseLine).u16(set.lineHeight).u16(glyphs.length);
  for (const { g, dataOff, dataLen } of placed) {
    w.u32(g.codepoint).u16(g.advW).u8(g.boxW).u8(g.boxH).i8(g.ofsX).i8(g.ofsY);
    w.u32(dataOff).u16(dataLen);
  }
  w.bytes(dataBytes);
  return w.toBytes();
}

// Decode for round-trip tests (expands each glyph's RLE back to packed bits).
export function decodeNameGlyphs(blob: Uint8Array): GlyphSet {
  const dv = new DataView(blob.buffer, blob.byteOffset, blob.byteLength);
  const magic = String.fromCharCode(...blob.subarray(0, 4));
  if (magic !== GLYPHS_MAGIC) throw new Error("bad glyphs magic");
  const glyphPx = dv.getUint16(8, true);
  const baseLine = dv.getUint16(10, true);
  const lineHeight = dv.getUint16(12, true);
  const count = dv.getUint16(14, true);
  const dataStart = GLYPHS_HEADER_SIZE + count * GLYPHS_ENTRY_SIZE;

  const glyphs: Glyph[] = [];
  let p = GLYPHS_HEADER_SIZE;
  for (let i = 0; i < count; i++) {
    const codepoint = dv.getUint32(p, true);
    const advW = dv.getUint16(p + 4, true);
    const boxW = blob[p + 6];
    const boxH = blob[p + 7];
    const ofsX = dv.getInt8(p + 8);
    const ofsY = dv.getInt8(p + 9);
    const dataOff = dv.getUint32(p + 10, true);
    const dataLen = dv.getUint16(p + 14, true);
    const nbits = boxW * boxH;
    const bits = nbits > 0 ? rleDecode(blob, dataStart + dataOff, dataLen, nbits) : new Uint8Array(0);
    glyphs.push({ codepoint, advW, boxW, boxH, ofsX, ofsY, bits });
    p += GLYPHS_ENTRY_SIZE;
  }
  return { glyphPx, baseLine, lineHeight, glyphs };
}

export const DEFAULT_GLYPH_PX = GLYPH_PX;
