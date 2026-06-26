// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Browser-side image helpers for the editor: load a picked file and scale it to
// fit a target box (preserving aspect) into a baseline JPEG the namecard-pdf
// writer embeds.

export function loadImage(src: string): Promise<HTMLImageElement> {
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

// Scale `img` to fit within maxW×maxH (preserving aspect), and encode it as a
// baseline JPEG. Returns the raw bytes for the PDF writer.
export async function containJpeg(
  img: HTMLImageElement,
  maxW: number,
  maxH: number,
  quality = 0.85,
): Promise<Uint8Array> {
  const scale = Math.min(maxW / img.width, maxH / img.height);
  const w = Math.max(1, Math.round(img.width * scale));
  const h = Math.max(1, Math.round(img.height * scale));
  const canvas = document.createElement("canvas");
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d")!;
  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0, 0, w, h);
  ctx.drawImage(img, 0, 0, w, h);

  const blob = await new Promise<Blob | null>((resolve) =>
    canvas.toBlob(resolve, "image/jpeg", quality),
  );
  if (!blob) throw new Error("canvas.toBlob failed");
  return new Uint8Array(await blob.arrayBuffer());
}
