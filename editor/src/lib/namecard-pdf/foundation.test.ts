// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { describe, expect, it } from "vitest";
import { crc32 } from "./crc32.ts";
import { rleDecode, rleEncode } from "./rle.ts";
import { encodeFooter, parseFooter, type FooterEntry } from "./footer.ts";
import { AssetSubtype, AssetType, FLAG_STRIP_ON_SHARE } from "./constants.ts";
import { decodeNameGlyphs, encodeNameGlyphs, type Glyph } from "./glyphs.ts";
import { utf8Bytes } from "./bytes.ts";

describe("crc32", () => {
  it("matches the standard '123456789' vector", () => {
    expect(crc32(utf8Bytes("123456789"))).toBe(0xcbf43926);
  });
  it("is 0 for empty input", () => {
    expect(crc32(new Uint8Array(0))).toBe(0);
  });
});

function packBits(values: number[]): Uint8Array {
  const out = new Uint8Array((values.length + 7) >> 3);
  values.forEach((v, i) => {
    if (v) out[i >> 3] |= 0x80 >> (i & 7);
  });
  return out;
}

describe("bit-run RLE", () => {
  const cases: number[][] = [
    [],
    [0],
    [1], // leading one -> zero-length 0-run first
    [0, 0, 0, 0, 0, 0, 0, 0],
    [1, 1, 1, 1, 1, 1, 1, 1],
    [0, 1, 0, 1, 0, 1, 0, 1],
    Array.from({ length: 300 }, () => 0), // run > 127 (varint)
    Array.from({ length: 300 }, (_, i) => (i < 200 ? 1 : 0)),
  ];
  it("round-trips", () => {
    for (const c of cases) {
      const bits = packBits(c);
      const enc = rleEncode(bits, c.length);
      const dec = rleDecode(enc, 0, enc.length, c.length);
      for (let i = 0; i < c.length; i++) {
        expect((dec[i >> 3] >> (7 - (i & 7))) & 1).toBe(c[i]);
      }
    }
  });

  it("round-trips pseudo-random 48x48 bitmaps", () => {
    let seed = 12345;
    const rnd = () => ((seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x7fffffff);
    for (let t = 0; t < 20; t++) {
      const nbits = 48 * 48;
      const vals = Array.from({ length: nbits }, () => (rnd() > 0.7 ? 1 : 0));
      const bits = packBits(vals);
      const enc = rleEncode(bits, nbits);
      const dec = rleDecode(enc, 0, enc.length, nbits);
      for (let i = 0; i < nbits; i++) {
        expect((dec[i >> 3] >> (7 - (i & 7))) & 1).toBe(vals[i]);
      }
    }
  });
});

describe("footer", () => {
  it("round-trips header + index + trailer", () => {
    const entries: FooterEntry[] = [
      { type: AssetType.Name, flags: 0, subtype: 0, offset: 100, length: 9, crc: 0xdeadbeef },
      { type: AssetType.Url, flags: 0, subtype: 0, offset: 120, length: 20, crc: 0x12345678 },
      {
        type: AssetType.DisplayJpeg,
        flags: FLAG_STRIP_ON_SHARE,
        subtype: AssetSubtype.Jpeg,
        offset: 500,
        length: 4096,
        crc: 0xabcddcba,
      },
    ];
    const footerOffset = 4000;
    const footer = encodeFooter(entries, footerOffset, 3000);
    // Simulate a file: pad up to footerOffset then append the footer.
    const file = new Uint8Array(footerOffset + footer.length);
    file.set(footer, footerOffset);
    const parsed = parseFooter(file);
    expect(parsed.footerOffset).toBe(footerOffset);
    expect(parsed.baseTotalLength).toBe(3000);
    expect(parsed.entries).toEqual(entries);
  });

  it("rejects a corrupted index (crc mismatch)", () => {
    const entries: FooterEntry[] = [
      { type: AssetType.Name, flags: 0, subtype: 0, offset: 10, length: 4, crc: 1 },
    ];
    const footer = encodeFooter(entries, 0, 0);
    footer[13] ^= 0xff; // flip a byte inside the index
    expect(() => parseFooter(footer)).toThrow();
  });
});

describe("name_glyphs blob", () => {
  it("round-trips a small glyph set", () => {
    const glyphs: Glyph[] = [
      // codepoints intentionally out of order -> encoder sorts
      { codepoint: 0x9b3c, advW: 48 * 16, boxW: 5, boxH: 3, ofsX: 1, ofsY: -2, bits: packBits([1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1]) },
      { codepoint: 0x20, advW: 24 * 16, boxW: 0, boxH: 0, ofsX: 0, ofsY: 0, bits: new Uint8Array(0) },
      { codepoint: 0x4e9c, advW: 48 * 16, boxW: 8, boxH: 2, ofsX: 0, ofsY: 0, bits: packBits([1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1]) },
    ];
    const blob = encodeNameGlyphs({ glyphPx: 48, baseLine: 40, lineHeight: 56, glyphs });
    const out = decodeNameGlyphs(blob);
    expect(out.glyphPx).toBe(48);
    expect(out.baseLine).toBe(40);
    expect(out.lineHeight).toBe(56);
    expect(out.glyphs.map((g) => g.codepoint)).toEqual([0x20, 0x4e9c, 0x9b3c]);
    for (const g of out.glyphs) {
      const src = glyphs.find((x) => x.codepoint === g.codepoint)!;
      expect(g.advW).toBe(src.advW);
      expect(g.boxW).toBe(src.boxW);
      expect(g.boxH).toBe(src.boxH);
      expect(g.ofsX).toBe(src.ofsX);
      expect(g.ofsY).toBe(src.ofsY);
      const nbits = g.boxW * g.boxH;
      for (let i = 0; i < nbits; i++) {
        expect((g.bits[i >> 3] >> (7 - (i & 7))) & 1).toBe((src.bits[i >> 3] >> (7 - (i & 7))) & 1);
      }
    }
  });
});
