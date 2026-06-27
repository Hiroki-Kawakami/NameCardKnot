// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { describe, expect, it } from "vitest";
import { deviceHasGlyph } from "./jp-glyphs.ts";
import { missingChars } from "./glyph-raster.ts";

const cp = (c: string) => c.codePointAt(0)!;

describe("jp-glyphs coverage", () => {
  it("covers common chars, not rare kanji", () => {
    expect(deviceHasGlyph(cp("山"))).toBe(true);
    expect(deviceHasGlyph(cp("亜"))).toBe(true);
    expect(deviceHasGlyph(cp("a"))).toBe(true);
    expect(deviceHasGlyph(cp("　"))).toBe(true); // full-width space
    expect(deviceHasGlyph(cp("鬛"))).toBe(false); // rare kanji
  });
});

describe("missingChars", () => {
  it("returns only uncovered names, deduped, first-seen", () => {
    expect(missingChars("山田太郎")).toEqual([]);
    expect(missingChars("鬛亜")).toEqual(["鬛"]);
    expect(missingChars("鬛鬛")).toEqual(["鬛"]);
  });
});
