// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Preview of the PDF share page, driven by the same share-layout module as the
// writer. Runs are advanced by the layout's exact PDF widths, so positions
// match a viewer; only the glyph shapes are the browser's approximations.

import { useEffect, useRef } from "react";
import { type CropState, croppedSize, drawCropped } from "../lib/crop";
import { t } from "../i18n";
import { PAGE2_H, PAGE2_W } from "../lib/namecard-pdf/constants";
import {
  layoutSharePage,
  splitLatinRuns,
  textWidth,
  type ShareFont,
} from "../lib/namecard-pdf/share-layout";
import { SHARE_H, SHARE_W } from "../lib/targets";

export interface ShareImage {
  image: HTMLImageElement;
  crop: CropState;
}

// Canvas stand-ins for the PDF fonts (F1/F3 Helvetica, F2 Ryumin = mincho,
// F4 GothicBBB-Medium = gothic).
const CANVAS_FONTS: Record<ShareFont, { latin: string; cjk: string }> = {
  body: {
    latin: "%s Helvetica, Arial, sans-serif",
    cjk: "%s 'Hiragino Mincho ProN', 'Noto Serif JP', serif",
  },
  title: {
    latin: "bold %s Helvetica, Arial, sans-serif",
    cjk: "600 %s 'Hiragino Kaku Gothic ProN', 'Noto Sans JP', sans-serif",
  },
};

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
    const layout = layoutSharePage(
      name,
      url,
      message,
      shares.map((s) => croppedSize(s.image, s.crop, SHARE_W, SHARE_H)),
    );

    ctx.fillStyle = "#fff";
    ctx.fillRect(0, 0, PAGE2_W, PAGE2_H);

    layout.images.forEach((r, i) => {
      const y = PAGE2_H - r.y - r.h; // PDF -> canvas coords
      ctx.save();
      ctx.translate(r.x, y);
      ctx.beginPath();
      ctx.rect(0, 0, r.w, r.h);
      ctx.clip();
      drawCropped(ctx, shares[i].image, shares[i].crop, r.w, r.h);
      ctx.restore();
      ctx.strokeStyle = "rgb(209,209,209)";
      ctx.lineWidth = 2;
      ctx.strokeRect(r.x, y, r.w, r.h);
    });

    ctx.fillStyle = "#000";
    ctx.fillRect(
      layout.rule.x,
      PAGE2_H - layout.rule.y - layout.rule.h,
      layout.rule.w,
      layout.rule.h,
    );

    ctx.textBaseline = "alphabetic";
    for (const t of layout.texts) {
      const v = Math.round(255 * t.gray);
      ctx.fillStyle = `rgb(${v},${v},${v})`;
      let x = t.x;
      for (const run of splitLatinRuns(t.text)) {
        const tpl = CANVAS_FONTS[t.font][run.latin ? "latin" : "cjk"];
        ctx.font = tpl.replace("%s", `${t.size}px`);
        ctx.fillText(run.text, x, PAGE2_H - t.y);
        x += textWidth(run.text, t.size, t.font);
      }
    }
  }, [name, url, message, shares]);

  return (
    <section className="preview-block">
      <h2>{t.sharePageTitle}</h2>
      <canvas
        ref={canvasRef}
        className="share-canvas"
        width={PAGE2_W}
        height={PAGE2_H}
      />
    </section>
  );
}
