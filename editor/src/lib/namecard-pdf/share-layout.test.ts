// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { describe, expect, it } from "vitest";
import { layoutSharePage, textWidth, wrapText } from "./share-layout.ts";

describe("wrapText", () => {
  it("breaks CJK anywhere at the width limit", () => {
    const lines = wrapText("あいうえおかきくけこ", 36, 36 * 4 + 1, "body");
    expect(lines).toEqual(["あいうえ", "おかきく", "けこ"]);
  });

  it("keeps ASCII words together", () => {
    const w = textWidth("hello ", 36, "body") + textWidth("world", 36, "body");
    const lines = wrapText("hello world", 36, w - 1, "body");
    expect(lines).toEqual(["hello", "world"]);
  });

  it("hard-splits a token longer than the line", () => {
    const lines = wrapText("aaaaaaaaaa", 36, textWidth("aaa", 36, "body") + 1, "body");
    expect(lines.length).toBeGreaterThan(2);
    expect(lines.join("")).toBe("aaaaaaaaaa");
    for (const l of lines) expect(textWidth(l, 36, "body")).toBeLessThanOrEqual(textWidth("aaa", 36, "body") + 1);
  });

  it("returns one empty line for empty input", () => {
    expect(wrapText("", 36, 100, "body")).toEqual([""]);
  });
});

describe("layoutSharePage", () => {
  it("right-aligns image boxes and keeps text left of them", () => {
    const L = layoutSharePage("山田太郎", "https://example.com", "こんにちは", [
      { w: 405, h: 720 },
      { w: 405, h: 720 },
    ]);
    expect(L.images).toHaveLength(2);
    expect(L.images[1].x + L.images[1].w).toBe(1920 - 80);
    expect(L.images[0].x).toBeGreaterThan(L.texts[0].x);
    for (const t of L.texts) expect(t.y).toBeGreaterThanOrEqual(80);
  });

  it("preserves explicit newlines in the message", () => {
    const L = layoutSharePage("名", "", "一行目\n二行目", []);
    const msg = L.texts.filter((t) => t.size === 36).map((t) => t.text);
    expect(msg).toEqual(["一行目", "二行目"]);
  });

  it("scales a landscape image into its box", () => {
    const L = layoutSharePage("名", "", "", [{ w: 800, h: 600 }]);
    expect(L.images[0].w).toBe(405);
    expect(L.images[0].h).toBe(Math.round((405 * 600) / 800));
  });
});
