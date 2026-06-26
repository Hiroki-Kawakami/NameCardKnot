// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { describe, expect, it } from "vitest";
import { buildNameCardPdf, buildSharePdf, parseFooter } from "./index.ts";
import { AssetType, FLAG_STRIP_ON_SHARE } from "./constants.ts";
import { crc32 } from "./crc32.ts";
import { utf8Bytes } from "./bytes.ts";

// Minimal JPEG-shaped bytes: a real SOF0 (so jpegSize works) + filler. Good
// enough for footer/extraction tests; real decoding is covered by the E2E.
function fakeJpeg(w: number, h: number, fillByte: number, fillLen = 32): Uint8Array {
  const sof = [
    0xff, 0xd8, // SOI
    0xff, 0xc0, 0x00, 0x11, 0x08, (h >> 8) & 0xff, h & 0xff, (w >> 8) & 0xff, w & 0xff,
    0x03, 0x01, 0x11, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01,
  ];
  const fill = new Array(fillLen).fill(fillByte);
  return new Uint8Array([...sof, ...fill, 0xff, 0xd9]);
}

const baseInput = () => ({
  name: "山田太郎",
  url: "https://example.com/yamada",
  message: "はじめまして。よろしくお願いします。",
  display: fakeJpeg(540, 960, 0x11),
  shares: [fakeJpeg(800, 600, 0x22), fakeJpeg(640, 480, 0x33)],
});

function slice(file: Uint8Array, off: number, len: number): Uint8Array {
  return file.subarray(off, off + len);
}

describe("buildNameCardPdf", () => {
  it("round-trips every asset via the footer index", () => {
    const input = baseInput();
    const pdf = buildNameCardPdf(input);
    const { entries, baseTotalLength } = parseFooter(pdf);

    expect(baseTotalLength).toBeGreaterThan(0);

    const byType = (t: AssetType) => entries.filter((e) => e.type === t);
    // strings
    for (const [type, val] of [
      [AssetType.Name, input.name],
      [AssetType.Url, input.url],
      [AssetType.Message, input.message],
    ] as const) {
      const e = byType(type)[0];
      expect(e).toBeDefined();
      expect(slice(pdf, e.offset, e.length)).toEqual(utf8Bytes(val));
      expect(crc32(slice(pdf, e.offset, e.length))).toBe(e.crc);
    }
    // every entry's bytes match its recorded crc
    for (const e of entries) {
      expect(crc32(slice(pdf, e.offset, e.length))).toBe(e.crc);
    }
    // share images
    expect(byType(AssetType.ShareJpeg)).toHaveLength(2);
    expect(slice(pdf, byType(AssetType.ShareJpeg)[0].offset, byType(AssetType.ShareJpeg)[0].length)).toEqual(
      input.shares[0],
    );
    // display image is present and flagged strip-on-share
    const disp = byType(AssetType.DisplayJpeg)[0];
    expect(disp).toBeDefined();
    expect(disp.flags & FLAG_STRIP_ON_SHARE).toBe(FLAG_STRIP_ON_SHARE);
    expect(slice(pdf, disp.offset, disp.length)).toEqual(input.display);
    // name_glyphs present
    expect(byType(AssetType.NameGlyphs)).toHaveLength(1);
    // share entries are NOT stripped
    for (const e of byType(AssetType.ShareJpeg)) expect(e.flags & FLAG_STRIP_ON_SHARE).toBe(0);
  });

  it("is a structurally plausible 2-page PDF", () => {
    const pdf = buildNameCardPdf(baseInput());
    const text = new TextDecoder("latin1").decode(pdf);
    expect(text.startsWith("%PDF-1.7")).toBe(true);
    expect(text).toContain("/Count 2");
    expect((text.match(/%%EOF/g) ?? []).length).toBe(2); // (A) + (B)
    expect(text).toContain("/Prev "); // incremental update chains xref
  });
});

describe("buildSharePdf / truncation equivalence", () => {
  it("share PDF == full PDF truncated at base_total_length", () => {
    const input = baseInput();
    const full = buildNameCardPdf(input);
    const share = buildSharePdf(input);
    const { baseTotalLength } = parseFooter(full);
    expect(share).toEqual(full.subarray(0, baseTotalLength));
  });

  it("share PDF has footer A: no display, base_total_length 0", () => {
    const share = buildSharePdf(baseInput());
    const { entries, baseTotalLength } = parseFooter(share);
    expect(baseTotalLength).toBe(0);
    expect(entries.some((e) => e.type === AssetType.DisplayJpeg)).toBe(false);
    expect(entries.some((e) => e.type === AssetType.Name)).toBe(true);
    const text = new TextDecoder("latin1").decode(share);
    expect(text).toContain("/Count 1");
  });
});
