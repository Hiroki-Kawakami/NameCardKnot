// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Approximate the device rendering: linear-light Rec.709 luma (matching the
// firmware pipeline) quantized to the EPD's 16 grays with 8x8 Bayer dither.

const BAYER8 = [
  0, 32, 8, 40, 2, 34, 10, 42,
  48, 16, 56, 24, 50, 18, 58, 26,
  12, 44, 4, 36, 14, 46, 6, 38,
  60, 28, 52, 20, 62, 30, 54, 22,
  3, 35, 11, 43, 1, 33, 9, 41,
  51, 19, 59, 27, 49, 17, 57, 25,
  15, 47, 7, 39, 13, 45, 5, 37,
  63, 31, 55, 23, 61, 29, 53, 21,
];

const SRGB_TO_LINEAR = new Float32Array(256).map((_, i) => {
  const c = i / 255;
  return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
});

function linearToSrgb(l: number): number {
  return l <= 0.0031308 ? l * 12.92 : 1.055 * Math.pow(l, 1 / 2.4) - 0.055;
}

export function applyEpdLook(ctx: CanvasRenderingContext2D, w: number, h: number): void {
  const id = ctx.getImageData(0, 0, w, h);
  const d = id.data;
  for (let y = 0; y < h; y++) {
    const row = (y & 7) << 3;
    for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4;
      const luma =
        0.2126 * SRGB_TO_LINEAR[d[i]] +
        0.7152 * SRGB_TO_LINEAR[d[i + 1]] +
        0.0722 * SRGB_TO_LINEAR[d[i + 2]];
      const gray = linearToSrgb(luma) * 15; // 0..15
      const level = Math.min(15, Math.floor(gray + (BAYER8[row | (x & 7)] + 0.5) / 64));
      d[i] = d[i + 1] = d[i + 2] = level * 17;
      d[i + 3] = 255;
    }
  }
  ctx.putImageData(id, 0, 0);
}
