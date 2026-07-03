// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Approximate preview of the PDF share page, mirroring shareContent() in
// namecard-pdf. Positions are exact; fonts are the browser's, not a viewer's.

import { useEffect, useRef } from "react";
import { type CropState, croppedSize, drawCropped } from "../lib/crop";
import { PAGE2_H, PAGE2_W } from "../lib/namecard-pdf/constants";
import { SHARE_H, SHARE_W } from "../lib/targets";

export interface ShareImage {
  image: HTMLImageElement;
  crop: CropState;
}

const FAMILY = "system-ui, 'Hiragino Sans', 'Noto Sans JP', sans-serif";

export default function SharePreview({
  name,
  url,
  message,
  shares,
}: {
  name: string;
  url: string;
  message: string;
  shares: ShareImage[];
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d")!;
    ctx.fillStyle = "#fff";
    ctx.fillRect(0, 0, PAGE2_W, PAGE2_H);
    ctx.fillStyle = "#000";
    ctx.textBaseline = "alphabetic";
    ctx.font = `500 72px ${FAMILY}`;
    ctx.fillText(name, 100, 120);
    ctx.font = `36px ${FAMILY}`;
    ctx.fillText(url, 100, 200);
    // The PDF draws the message as one line (newlines don't advance) — mirror it.
    ctx.fillText(message.replace(/\n+/g, ""), 100, 280);

    shares.forEach(({ image, crop }, i) => {
      const out = croppedSize(image, crop, SHARE_W, SHARE_H);
      const dw = 480;
      const dh = Math.round((dw * out.h) / out.w);
      const x = 100 + i * 520;
      const y = PAGE2_H - 80 - dh;
      ctx.save();
      ctx.translate(x, y);
      ctx.beginPath();
      ctx.rect(0, 0, dw, dh);
      ctx.clip();
      drawCropped(ctx, image, crop, dw, dh);
      ctx.restore();
    });
  }, [name, url, message, shares]);

  return (
    <section className="preview-block">
      <h2>共有ページ（PDF 2ページ目・近似）</h2>
      <canvas ref={canvasRef} className="share-canvas" width={PAGE2_W} height={PAGE2_H} />
    </section>
  );
}
