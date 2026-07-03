// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { describe, expect, it } from "vitest";
import { buildNameCardPdf, buildSharePdf, parseFooter } from "./index.ts";
import { AssetType } from "./constants.ts";
import { parseNameCard } from "./pdf-reader.ts";

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
  message: "はじめまして。",
  display: fakeJpeg(540, 960, 0x11),
  shares: [fakeJpeg(405, 720, 0x22), fakeJpeg(405, 300, 0x33)],
});

describe("parseNameCard", () => {
  it("round-trips a full .mnc.pdf", () => {
    const input = baseInput();
    const card = parseNameCard(buildNameCardPdf(input));
    expect(card.name).toBe(input.name);
    expect(card.url).toBe(input.url);
    expect(card.message).toBe(input.message);
    expect(card.display).toEqual(input.display);
    expect(card.shares).toEqual(input.shares);
  });

  it("round-trips a share-only .snc.pdf (no display)", () => {
    const card = parseNameCard(buildSharePdf(baseInput()));
    expect(card.display).toBeNull();
    expect(card.shares).toHaveLength(2);
  });

  it("rejects a corrupted asset", () => {
    const pdf = buildNameCardPdf(baseInput());
    const name = parseFooter(pdf).entries.find((e) => e.type === AssetType.Name)!;
    pdf[name.offset] ^= 0xff;
    expect(() => parseNameCard(pdf)).toThrow(/crc/);
  });

  it("rejects a non-container file", () => {
    expect(() => parseNameCard(new TextEncoder().encode("%PDF-1.7 not a container"))).toThrow();
  });
});
