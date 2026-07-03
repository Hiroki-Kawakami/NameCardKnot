// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Share-page (page 2) layout, shared by the PDF writer and the editor preview
// so both place text/images identically: images right-aligned, name/URL/message
// down the left column. All coordinates are PDF-style (origin bottom-left).
//
// Width math is exact, not approximate: ASCII runs use the standard Helvetica
// AFM advance widths and everything else advances 1.0 em (the CID fonts carry
// no /W array, so viewers use DW=1000).

import { PAGE2_H, PAGE2_W } from "./constants.ts";

export type ShareFont = "title" | "body"; // title = bold/gothic, body = regular/mincho

export interface ShareTextLine {
  text: string;
  font: ShareFont;
  size: number;
  x: number;
  y: number; // baseline
  gray: number;
}

export interface ShareRect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface SharePageLayout {
  texts: ShareTextLine[];
  rule: ShareRect;
  images: ShareRect[]; // final draw rects (image fitted into its box)
}

const MARGIN = 80;
const IMG_W = 405; // image box = the share-image target size, drawn 1:1
const IMG_H = 720;
const IMG_GAP = 36;
const TEXT_IMG_GAP = 90;
const TEXT_MAX_W = 980; // cap the measure when few/no images free up width

const NAME_SIZE = 88;
const NAME_LH = 112;
const RULE = { w: 140, h: 8, gapAbove: 40, gapBelow: 52 };
const URL_SIZE = 40;
const URL_LH = 52;
const URL_GRAY = 0.45;
const MSG_SIZE = 36;
const MSG_LH = 58;
const MSG_GRAY = 0.25;

// Helvetica / Helvetica-Bold AFM advance widths for chars 32..126 (per mille).
// prettier-ignore
const HELV: Record<ShareFont, number[]> = {
  body: [
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584,
  ],
  title: [
    278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611,
    975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778,
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556,
    333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611,
    611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584,
  ],
};

export function textWidth(s: string, size: number, font: ShareFont): number {
  let w = 0;
  for (const ch of s) {
    const c = ch.codePointAt(0)!;
    w += c >= 32 && c <= 126 ? HELV[font][c - 32] / 1000 : 1;
  }
  return w * size;
}

// Split into runs by "drawable with the Latin font" — must match textLine's
// routing in index.ts (charCode < 0x80).
export function splitLatinRuns(s: string): { latin: boolean; text: string }[] {
  const runs: { latin: boolean; text: string }[] = [];
  for (let i = 0; i < s.length; ) {
    const latin = s.charCodeAt(i) < 0x80;
    let j = i;
    while (j < s.length && s.charCodeAt(j) < 0x80 === latin) j++;
    runs.push({ latin, text: s.slice(i, j) });
    i = j;
  }
  return runs;
}

// Greedy wrap: break anywhere between CJK chars, keep ASCII words together,
// hard-split a word longer than the line (e.g. a long URL).
export function wrapText(s: string, size: number, maxW: number, font: ShareFont): string[] {
  const tokens = s.match(/ +|[!-~]+|./gu) ?? [];
  const lines: string[] = [];
  let line = "";
  let lineW = 0;
  const flush = () => {
    line = line.replace(/ +$/, "");
    if (line !== "") lines.push(line);
    line = "";
    lineW = 0;
  };
  for (const tok of tokens) {
    let t = tok;
    let tw = textWidth(t, size, font);
    if (lineW + tw > maxW && line !== "") {
      flush();
      if (t.trim() === "") continue; // don't carry the break's whitespace
    }
    while (tw > maxW) {
      // hard-split an over-long token
      let n = Math.max(1, Math.floor((t.length * maxW) / tw));
      while (n > 1 && textWidth(t.slice(0, n), size, font) > maxW) n--;
      lines.push(line + t.slice(0, n));
      line = "";
      lineW = 0;
      t = t.slice(n);
      tw = textWidth(t, size, font);
    }
    line += t;
    lineW += tw;
  }
  flush();
  return lines.length ? lines : [""];
}

export function layoutSharePage(
  name: string,
  url: string,
  message: string,
  shareSizes: { w: number; h: number }[],
): SharePageLayout {
  const n = shareSizes.length;
  const blockW = n > 0 ? n * IMG_W + (n - 1) * IMG_GAP : 0;
  const blockX = PAGE2_W - MARGIN - blockW;
  const textW = Math.min((n > 0 ? blockX - TEXT_IMG_GAP : PAGE2_W - MARGIN) - MARGIN, TEXT_MAX_W);

  const images = shareSizes.map((s, i) => {
    const scale = Math.min(IMG_W / s.w, IMG_H / s.h);
    const w = Math.round(s.w * scale);
    const h = Math.round(s.h * scale);
    return {
      x: blockX + i * (IMG_W + IMG_GAP) + Math.round((IMG_W - w) / 2),
      y: Math.round((PAGE2_H - IMG_H) / 2 + (IMG_H - h) / 2),
      w,
      h,
    };
  });

  const texts: ShareTextLine[] = [];
  let top = PAGE2_H - MARGIN;
  const pushLines = (lines: string[], font: ShareFont, size: number, lh: number, gray: number) => {
    for (const text of lines) {
      const y = Math.round(top - size * 0.85);
      if (y < MARGIN + size * 0.15) break; // clip at the bottom margin
      texts.push({ text, font, size, x: MARGIN, y, gray });
      top = y - size * 0.15 - (lh - size);
    }
    top += lh - size; // no leading after the block's last line
  };

  pushLines(wrapText(name, NAME_SIZE, textW, "title"), "title", NAME_SIZE, NAME_LH, 0);

  top -= RULE.gapAbove;
  const rule = { x: MARGIN, y: Math.round(top - RULE.h), w: RULE.w, h: RULE.h };
  top -= RULE.h + RULE.gapBelow;

  if (url.trim() !== "") {
    pushLines(wrapText(url, URL_SIZE, textW, "body"), "body", URL_SIZE, URL_LH, URL_GRAY);
    top -= 24;
  }
  if (message.trim() !== "") {
    top -= 12;
    const lines = message.split("\n").flatMap((l) => wrapText(l, MSG_SIZE, textW, "body"));
    pushLines(lines, "body", MSG_SIZE, MSG_LH, MSG_GRAY);
  }

  return { texts: texts.filter((t) => t.text !== ""), rule, images };
}
