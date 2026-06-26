// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Bit-run RLE for 1bpp glyph bitmaps (see docs/namecard_pdf.md §4.4).
//
// The bitmap is a continuous, row-major, MSB-first bit stream of `nbits` bits
// (nbits = box_w*box_h, no per-row byte padding). Runs are encoded as
// alternating colors starting with color 0; each run length is an unsigned
// LEB128 varint. A bitmap starting with a 1 emits a zero-length 0-run first.

import { ByteWriter } from "./bytes.ts";

function getBit(bits: Uint8Array, i: number): number {
  return (bits[i >> 3] >> (7 - (i & 7))) & 1;
}

export function rleEncode(bits: Uint8Array, nbits: number): Uint8Array {
  const w = new ByteWriter(Math.max(8, nbits >> 3));
  let cur = 0;
  let run = 0;
  for (let i = 0; i < nbits; i++) {
    const bit = getBit(bits, i);
    if (bit === cur) {
      run++;
    } else {
      w.varint(run);
      cur = bit;
      run = 1;
    }
  }
  w.varint(run);
  return w.toBytes();
}

// Decode `len` RLE bytes from `data[off..]` into a fresh packed bit buffer of
// ceil(nbits/8) bytes (MSB-first). Mirrors the C++ decoder.
export function rleDecode(data: Uint8Array, off: number, len: number, nbits: number): Uint8Array {
  const out = new Uint8Array((nbits + 7) >> 3);
  let bit = 0;
  let cur = 0;
  let i = off;
  const endIn = off + len;
  while (i < endIn && bit < nbits) {
    let run = 0;
    let shift = 0;
    let b: number;
    do {
      b = data[i++];
      run |= (b & 0x7f) << shift;
      shift += 7;
    } while (b & 0x80 && i < endIn);
    run = run >>> 0;
    if (cur) {
      for (let k = 0; k < run && bit < nbits; k++, bit++) out[bit >> 3] |= 0x80 >> (bit & 7);
    } else {
      bit += run; // zeros: already cleared
    }
    cur ^= 1;
  }
  return out;
}
