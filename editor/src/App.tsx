// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { useState } from "react";
import { buildNameCardPdf } from "./lib/namecard-pdf";
import { containJpeg, readImageFile } from "./lib/image-export";
import { buildNameGlyphs } from "./lib/glyph-raster";

// Fit boxes (px). The display image targets the EPD; share images are smaller.
const DISPLAY_W = 540;
const DISPLAY_H = 960;
const SHARE_W = 405;
const SHARE_H = 720;

function ImageField({
  label,
  image,
  onChange,
}: {
  label: string;
  image: HTMLImageElement | null;
  onChange: (img: HTMLImageElement | null) => void;
}) {
  return (
    <label className="field">
      <span>{label}</span>
      <input
        type="file"
        accept="image/*"
        onChange={async (e) => {
          const file = e.target.files?.[0];
          onChange(file ? await readImageFile(file) : null);
        }}
      />
      {image && <img className="thumb" src={image.src} alt="" />}
    </label>
  );
}

export default function App() {
  const [name, setName] = useState("");
  const [url, setUrl] = useState("");
  const [message, setMessage] = useState("");
  const [displayImage, setDisplayImage] = useState<HTMLImageElement | null>(null);
  const [share1SameAsDisplay, setShare1SameAsDisplay] = useState(true);
  const [share1Image, setShare1Image] = useState<HTMLImageElement | null>(null);
  const [share2Image, setShare2Image] = useState<HTMLImageElement | null>(null);
  const [busy, setBusy] = useState(false);

  // Required: name, display image, and share image 1 (which may reuse display).
  const canSave =
    name.trim() !== "" && !!displayImage && (share1SameAsDisplay || !!share1Image) && !busy;

  const onSave = async () => {
    if (!displayImage || !canSave) return;
    setBusy(true);
    try {
      const display = await containJpeg(displayImage, DISPLAY_W, DISPLAY_H);

      const shares: Uint8Array[] = [];
      const share1Source = share1SameAsDisplay ? displayImage : share1Image;
      if (share1Source) shares.push(await containJpeg(share1Source, SHARE_W, SHARE_H));
      if (share2Image) shares.push(await containJpeg(share2Image, SHARE_W, SHARE_H));

      // Embed glyphs for name characters the device's built-in font lacks.
      const nameGlyphs = await buildNameGlyphs(name);

      const pdf = buildNameCardPdf({ name, url, message, display, shares, nameGlyphs });
      const blob = new Blob([new Uint8Array(pdf)], { type: "application/pdf" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${name || "namecard"}.mnc.pdf`;
      a.click();
      URL.revokeObjectURL(a.href);
    } finally {
      setBusy(false);
    }
  };

  return (
    <main>
      <header>
        <h1>NameCardKnot Editor</h1>
      </header>

      <div className="form">
        <label className="field">
          <span>名前</span>
          <input type="text" value={name} onChange={(e) => setName(e.target.value)} />
        </label>

        <label className="field">
          <span>URL（任意）</span>
          <input
            type="text"
            value={url}
            onChange={(e) => setUrl(e.target.value)}
            placeholder="https://example.com"
          />
        </label>

        <ImageField label="表示用画像" image={displayImage} onChange={setDisplayImage} />

        <div className="field">
          <span>共有用画像 1</span>
          <label className="checkbox">
            <input
              type="checkbox"
              checked={share1SameAsDisplay}
              onChange={(e) => setShare1SameAsDisplay(e.target.checked)}
            />
            表示用と同じ画像を使う
          </label>
          {!share1SameAsDisplay && (
            <>
              <input
                type="file"
                accept="image/*"
                onChange={async (e) => {
                  const file = e.target.files?.[0];
                  setShare1Image(file ? await readImageFile(file) : null);
                }}
              />
              {share1Image && <img className="thumb" src={share1Image.src} alt="" />}
            </>
          )}
        </div>

        <ImageField label="共有用画像 2（任意）" image={share2Image} onChange={setShare2Image} />

        <label className="field">
          <span>共有メッセージ（任意）</span>
          <textarea value={message} rows={4} onChange={(e) => setMessage(e.target.value)} />
        </label>

        <button type="button" onClick={onSave} disabled={!canSave}>
          保存
        </button>
      </div>
    </main>
  );
}
