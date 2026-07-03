// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Crop model shared by pickers, previews, and the JPEG export, so the preview
// is exactly what gets embedded. fit = letterbox (output keeps the image's own
// aspect); cover = fill the box, x/y (0..1) picking the window of the overflow.

export interface CropState {
  mode: "fit" | "cover";
  x: number;
  y: number;
  zoom: number;
}

export const defaultCrop = (): CropState => ({ mode: "cover", x: 0.5, y: 0.5, zoom: 1 });

// Draw `img` into the tw×th rect at the ctx origin (caller translates/clips).
export function drawCropped(
  ctx: CanvasRenderingContext2D,
  img: HTMLImageElement,
  crop: CropState,
  tw: number,
  th: number,
): void {
  ctx.fillStyle = "#fff";
  ctx.fillRect(0, 0, tw, th);
  if (crop.mode === "cover") {
    const s = Math.max(tw / img.width, th / img.height) * crop.zoom;
    const sw = img.width * s;
    const sh = img.height * s;
    ctx.drawImage(img, -(sw - tw) * crop.x, -(sh - th) * crop.y, sw, sh);
  } else {
    const s = Math.min(tw / img.width, th / img.height);
    const sw = img.width * s;
    const sh = img.height * s;
    ctx.drawImage(img, (tw - sw) / 2, (th - sh) / 2, sw, sh);
  }
}

// Output pixel size of the exported JPEG for this crop.
export function croppedSize(
  img: HTMLImageElement,
  crop: CropState,
  tw: number,
  th: number,
): { w: number; h: number } {
  if (crop.mode === "cover") return { w: tw, h: th };
  const s = Math.min(tw / img.width, th / img.height);
  return { w: Math.max(1, Math.round(img.width * s)), h: Math.max(1, Math.round(img.height * s)) };
}
