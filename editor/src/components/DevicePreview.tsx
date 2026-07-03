// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Display image on the 540x960 EPD glass, grayscale-dithered like the
// firmware pipeline. Debounced — the EPD transform walks every pixel.

import { useEffect, useRef } from "react";
import { type CropState, drawCropped } from "../lib/crop";
import { applyEpdLook } from "../lib/epd-preview";
import { PAGE1_H, PAGE1_W } from "../lib/namecard-pdf/constants";

export default function DevicePreview({
  image,
  crop,
}: {
  image: HTMLImageElement | null;
  crop: CropState;
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const timer = setTimeout(() => {
      const ctx = canvas.getContext("2d")!;
      ctx.fillStyle = "#fff";
      ctx.fillRect(0, 0, PAGE1_W, PAGE1_H);
      if (image) {
        drawCropped(ctx, image, crop, PAGE1_W, PAGE1_H);
        applyEpdLook(ctx, PAGE1_W, PAGE1_H);
      }
    }, 80);
    return () => clearTimeout(timer);
  }, [image, crop]);

  return (
    <section className="preview-block">
      <h2>デバイス表示（モノクロ変換後）</h2>
      <div className="device-frame">
        <canvas ref={canvasRef} width={PAGE1_W} height={PAGE1_H} />
        {!image && <span className="preview-empty">表示用画像が未選択です</span>}
      </div>
    </section>
  );
}
