// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Builds the name_glyphs supplement for the device: rasterizes the name's
// characters that the device's built-in font lacks (jp-glyphs) to 1bpp bitmaps
// at GLYPH_PX via canvas, using Noto Sans JP (local if installed, else Google
// Fonts). The packed GlyphSet is then embedded by buildNameCardPdf.

import { DEFAULT_GLYPH_PX, type Glyph, type GlyphSet } from "./namecard-pdf/glyphs.ts";
import { deviceHasGlyph } from "./jp-glyphs.ts";

// Distinct name characters not covered by the device font, in first-seen order.
export function missingChars(name: string): string[] {
  const out: string[] = [];
  const seen = new Set<number>();
  for (const ch of name) {
    const cp = ch.codePointAt(0)!;
    if (deviceHasGlyph(cp) || seen.has(cp)) continue;
    seen.add(cp);
    out.push(ch);
  }
  return out;
}

let fontFamily: Promise<string> | null = null;

// Resolve a usable Japanese font family for canvas: a locally installed Noto
// Sans JP if present, otherwise the Google Fonts webfont (full coverage via
// on-demand unicode-range subsets).
function ensureJapaneseFont(): Promise<string> {
  if (fontFamily) return fontFamily;
  // Medium (500) to match LVGL's built-in Montserrat (built from Montserrat-Medium).
  fontFamily = (async () => {
    try {
      const local = new FontFace(
        "NckNotoLocal",
        "local('Noto Sans JP Medium'), local('Noto Sans JP'), local('Noto Sans CJK JP')",
        { weight: "500" },
      );
      await local.load();
      document.fonts.add(local);
      return "NckNotoLocal";
    } catch {
      if (!document.getElementById("nck-noto-css")) {
        const link = document.createElement("link");
        link.id = "nck-noto-css";
        link.rel = "stylesheet";
        link.href = "https://fonts.googleapis.com/css2?family=Noto+Sans+JP:wght@500&display=swap";
        document.head.appendChild(link);
      }
      return "'Noto Sans JP'";
    }
  })();
  return fontFamily;
}

export async function buildNameGlyphs(name: string): Promise<GlyphSet> {
  const px = DEFAULT_GLYPH_PX;
  const missing = missingChars(name);
  if (missing.length === 0) {
    return { glyphPx: px, baseLine: Math.round(px * 0.8), lineHeight: Math.round(px * 1.17), glyphs: [] };
  }

  const family = await ensureJapaneseFont();
  const font = `500 ${px}px ${family}`;
  try {
    await document.fonts.load(font, missing.join(""));
  } catch {
    // proceed; missing glyphs will rasterize as tofu/blank rather than fail
  }

  const size = px * 3;
  const canvas = document.createElement("canvas");
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext("2d", { willReadFrequently: true })!;
  ctx.font = font;
  ctx.textAlign = "left";
  ctx.textBaseline = "alphabetic";

  const fm = ctx.measureText("永");
  const baseLine = Math.round(fm.fontBoundingBoxAscent || px * 0.8);
  const lineHeight = Math.round((fm.fontBoundingBoxAscent + fm.fontBoundingBoxDescent) || px * 1.17);

  const penX = px; // integer pen origin, with margin around it
  const penY = px * 2; // baseline (room above for ascenders, below for descenders)
  const glyphs: Glyph[] = [];

  for (const ch of missing) {
    ctx.clearRect(0, 0, size, size);
    ctx.fillStyle = "#000";
    ctx.fillText(ch, penX, penY);
    const advW = Math.min(0xffff, Math.max(0, Math.round(ctx.measureText(ch).width * 16)));

    // Find the inked bounding box (alpha threshold => 1bpp).
    const data = ctx.getImageData(0, 0, size, size).data;
    let x0 = size, y0 = size, x1 = -1, y1 = -1;
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        if (data[(y * size + x) * 4 + 3] >= 128) {
          if (x < x0) x0 = x;
          if (x > x1) x1 = x;
          if (y < y0) y0 = y;
          if (y > y1) y1 = y;
        }
      }
    }

    if (x1 < x0 || y1 < y0) {
      // No ink (e.g. a space): advance only.
      glyphs.push({ codepoint: ch.codePointAt(0)!, advW, boxW: 0, boxH: 0, ofsX: 0, ofsY: 0, bits: new Uint8Array(0) });
      continue;
    }

    const boxW = x1 - x0 + 1;
    const boxH = y1 - y0 + 1;
    const ofsX = x0 - penX;          // pen -> bitmap left
    const ofsY = penY - (y1 + 1);    // baseline -> bitmap bottom (+up)
    const bits = new Uint8Array((boxW * boxH + 7) >> 3);
    let bi = 0;
    for (let y = y0; y <= y1; y++) {
      for (let x = x0; x <= x1; x++) {
        if (data[(y * size + x) * 4 + 3] >= 128) bits[bi >> 3] |= 0x80 >> (bi & 7);
        bi++;
      }
    }
    glyphs.push({ codepoint: ch.codePointAt(0)!, advW, boxW, boxH, ofsX, ofsY, bits });
  }

  return { glyphPx: px, baseLine, lineHeight, glyphs };
}
