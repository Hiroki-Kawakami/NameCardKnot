// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Browser-side image helpers for the editor: load a picked file / parsed JPEG
// bytes, and render an image through its CropState into a baseline JPEG the
// namecard-pdf writer embeds.

import { type CropState, croppedSize, drawCropped } from "./crop.ts";

function loadImage(src: string): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = reject;
    img.src = src;
  });
}

export function readImageFile(file: File): Promise<HTMLImageElement> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(loadImage(reader.result as string));
    reader.onerror = reject;
    reader.readAsDataURL(file);
  });
}

export function imageFromJpeg(bytes: Uint8Array): Promise<HTMLImageElement> {
  const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)], { type: "image/jpeg" }));
  return loadImage(url).finally(() => URL.revokeObjectURL(url));
}

async function canvasJpeg(canvas: HTMLCanvasElement, quality: number): Promise<Uint8Array> {
  const blob = await new Promise<Blob | null>((resolve) =>
    canvas.toBlob(resolve, "image/jpeg", quality),
  );
  if (!blob) throw new Error("canvas.toBlob failed");
  return new Uint8Array(await blob.arrayBuffer());
}

// Render `img` for the tw×th target box per `crop` and encode as baseline JPEG.
export async function cropJpeg(
  img: HTMLImageElement,
  crop: CropState,
  tw: number,
  th: number,
  quality = 0.85,
): Promise<Uint8Array> {
  const { w, h } = croppedSize(img, crop, tw, th);
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d")!;
  drawCropped(ctx, img, crop, w, h);
  return canvasJpeg(canvas, quality);
}
