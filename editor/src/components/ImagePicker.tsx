// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Image field: dropzone + result preview with fit/cover, drag-to-pan and zoom.

import { useEffect, useRef } from "react";
import { type CropState, defaultCrop, drawCropped } from "../lib/crop";
import { readImageFile } from "../lib/image-export";

export interface ImagePickerProps {
  label: string;
  help?: string;
  image: HTMLImageElement | null;
  crop: CropState;
  targetW: number;
  targetH: number;
  onImage: (img: HTMLImageElement | null) => void;
  onCrop: (crop: CropState) => void;
  onError: (message: string) => void;
}

export default function ImagePicker({
  label,
  help,
  image,
  crop,
  targetW,
  targetH,
  onImage,
  onCrop,
  onError,
}: ImagePickerProps) {
  const inputRef = useRef<HTMLInputElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const dragRef = useRef<{ pointerId: number; startX: number; startY: number; crop: CropState } | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !image) return;
    drawCropped(canvas.getContext("2d")!, image, crop, targetW, targetH);
  }, [image, crop, targetW, targetH]);

  const pickFile = async (file: File | undefined) => {
    if (!file) return;
    if (!file.type.startsWith("image/")) {
      onError(`「${file.name}」は画像ファイルではありません`);
      return;
    }
    try {
      onImage(await readImageFile(file));
      onCrop(defaultCrop());
    } catch {
      onError(`「${file.name}」を読み込めませんでした（このブラウザが対応していない形式かもしれません）`);
    }
  };

  const openPicker = () => inputRef.current?.click();

  // Drop works both before (dropzone) and after (picker) an image is chosen.
  const dropProps = {
    onDragOver: (e: React.DragEvent<HTMLDivElement>) => {
      e.preventDefault();
      e.currentTarget.classList.add("dragover");
    },
    onDragLeave: (e: React.DragEvent<HTMLDivElement>) =>
      e.currentTarget.classList.remove("dragover"),
    onDrop: (e: React.DragEvent<HTMLDivElement>) => {
      e.preventDefault();
      e.currentTarget.classList.remove("dragover");
      pickFile(e.dataTransfer.files?.[0]);
    },
  };

  const onPointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (!image || crop.mode !== "cover") return;
    e.currentTarget.setPointerCapture(e.pointerId);
    dragRef.current = { pointerId: e.pointerId, startX: e.clientX, startY: e.clientY, crop };
  };
  const onPointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const drag = dragRef.current;
    if (!drag || drag.pointerId !== e.pointerId || !image) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const s = Math.max(targetW / image.width, targetH / image.height) * crop.zoom;
    const overX = image.width * s - targetW;
    const overY = image.height * s - targetH;
    // client px -> canvas px -> fraction of the pannable overflow
    const toCanvas = targetW / rect.width;
    const clamp01 = (v: number) => Math.min(1, Math.max(0, v));
    onCrop({
      ...drag.crop,
      x: overX > 0 ? clamp01(drag.crop.x - ((e.clientX - drag.startX) * toCanvas) / overX) : drag.crop.x,
      y: overY > 0 ? clamp01(drag.crop.y - ((e.clientY - drag.startY) * toCanvas) / overY) : drag.crop.y,
    });
  };
  const endDrag = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (dragRef.current?.pointerId === e.pointerId) dragRef.current = null;
  };

  return (
    <div className="field">
      {label && <span className="field-label">{label}</span>}
      {help && <span className="field-help">{help}</span>}

      {!image ? (
        <div
          className="dropzone"
          role="button"
          tabIndex={0}
          onClick={openPicker}
          onKeyDown={(e) => {
            if (e.key === "Enter" || e.key === " ") {
              e.preventDefault();
              openPicker();
            }
          }}
          {...dropProps}
        >
          クリックして選択 / ここにドロップ
        </div>
      ) : (
        <div className="picker" {...dropProps}>
          <canvas
            ref={canvasRef}
            className={"picker-canvas" + (crop.mode === "cover" ? " pannable" : "")}
            width={targetW}
            height={targetH}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={endDrag}
            onPointerCancel={endDrag}
          />
          <div className="picker-controls">
            <div className="segmented" role="group" aria-label="配置">
              <button
                type="button"
                className={crop.mode === "cover" ? "active" : ""}
                onClick={() => onCrop({ ...crop, mode: "cover" })}
              >
                切り抜く
              </button>
              <button
                type="button"
                className={crop.mode === "fit" ? "active" : ""}
                onClick={() => onCrop({ ...crop, mode: "fit" })}
              >
                余白で収める
              </button>
            </div>
            {crop.mode === "cover" && (
              <label className="zoom">
                ズーム
                <input
                  type="range"
                  min={1}
                  max={3}
                  step={0.01}
                  value={crop.zoom}
                  onChange={(e) => onCrop({ ...crop, zoom: Number(e.target.value) })}
                />
              </label>
            )}
            <div className="picker-buttons">
              <button type="button" className="small" onClick={openPicker}>
                変更
              </button>
              <button type="button" className="small" onClick={() => onImage(null)}>
                削除
              </button>
            </div>
            {crop.mode === "cover" && <span className="field-help">プレビューをドラッグして位置を調整</span>}
          </div>
        </div>
      )}

      <input
        ref={inputRef}
        type="file"
        accept="image/*"
        hidden
        onChange={(e) => {
          pickFile(e.target.files?.[0]);
          e.target.value = ""; // allow re-selecting the same file
        }}
      />
    </div>
  );
}
