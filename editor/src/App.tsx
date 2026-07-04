// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
import { useEffect, useMemo, useRef, useState } from "react";
import DevicePreview from "./components/DevicePreview";
import ImagePicker from "./components/ImagePicker";
import SharePreview, { type ShareImage } from "./components/SharePreview";
import { type CropState, defaultCrop } from "./lib/crop";
import { loadDraft, saveDraft } from "./lib/draft";
import { t } from "./i18n";
import { buildNameGlyphs } from "./lib/glyph-raster";
import { cropJpeg, imageFromJpeg } from "./lib/image-export";
import {
  buildNameCardPdf,
  buildSharePdf,
  parseNameCard,
} from "./lib/namecard-pdf";
import { DISPLAY_H, DISPLAY_W, SHARE_H, SHARE_W } from "./lib/targets";

const fitCrop = (): CropState => ({ mode: "fit", x: 0.5, y: 0.5, zoom: 1 });
const draft = loadDraft();

export default function App() {
  const [name, setName] = useState(draft?.name ?? "");
  const [url, setUrl] = useState(draft?.url ?? "");
  const [message, setMessage] = useState(draft?.message ?? "");
  const [share1SameAsDisplay, setShare1SameAsDisplay] = useState(
    draft?.share1SameAsDisplay ?? true,
  );
  const [displayImage, setDisplayImage] = useState<HTMLImageElement | null>(
    null,
  );
  const [displayCrop, setDisplayCrop] = useState<CropState>(defaultCrop());
  const [share1Image, setShare1Image] = useState<HTMLImageElement | null>(null);
  const [share1Crop, setShare1Crop] = useState<CropState>(defaultCrop());
  const [share2Image, setShare2Image] = useState<HTMLImageElement | null>(null);
  const [share2Crop, setShare2Crop] = useState<CropState>(defaultCrop());
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const openRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    saveDraft({ name, url, message, share1SameAsDisplay });
  }, [name, url, message, share1SameAsDisplay]);

  const urlWarning =
    url.trim() !== "" && !/^https?:\/\//i.test(url.trim())
      ? t.urlWarning
      : null;

  const problems: string[] = [];
  if (name.trim() === "") problems.push(t.name);
  if (!displayImage) problems.push(t.displayImage);
  if (!share1SameAsDisplay && !share1Image) problems.push(t.share1);
  const canSave = problems.length === 0 && !busy;

  const shares: ShareImage[] = useMemo(() => {
    const out: ShareImage[] = [];
    const s1 = share1SameAsDisplay
      ? displayImage && { image: displayImage, crop: displayCrop }
      : share1Image && { image: share1Image, crop: share1Crop };
    if (s1) out.push(s1);
    if (share2Image) out.push({ image: share2Image, crop: share2Crop });
    return out;
  }, [
    share1SameAsDisplay,
    displayImage,
    displayCrop,
    share1Image,
    share1Crop,
    share2Image,
    share2Crop,
  ]);

  const onSave = async (sharePdf: boolean) => {
    if (!displayImage || !canSave) return;
    setBusy(true);
    setError(null);
    setNotice(null);
    try {
      const display = await cropJpeg(
        displayImage,
        displayCrop,
        DISPLAY_W,
        DISPLAY_H,
      );
      const shareJpegs = await Promise.all(
        shares.map((s) => cropJpeg(s.image, s.crop, SHARE_W, SHARE_H)),
      );
      const nameGlyphs = await buildNameGlyphs(name);

      const build = sharePdf ? buildSharePdf : buildNameCardPdf;
      const pdf = build({
        name,
        url,
        message,
        display,
        shares: shareJpegs,
        nameGlyphs,
      });
      const blob = new Blob([new Uint8Array(pdf)], { type: "application/pdf" });
      const safeName =
        name.trim().replace(/[\\/:*?"<>|\x00-\x1f]/g, "_") || "namecard";
      const ext = sharePdf ? "snc.pdf" : "mnc.pdf";
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${safeName}.${ext}`;
      a.click();
      URL.revokeObjectURL(a.href);
      setNotice(t.saved(`${safeName}.${ext}`));
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setBusy(false);
    }
  };

  const onOpenPdf = async (file: File | undefined) => {
    if (!file) return;
    setError(null);
    setNotice(null);
    try {
      const card = parseNameCard(new Uint8Array(await file.arrayBuffer()));
      setName(card.name);
      setUrl(card.url);
      setMessage(card.message);
      // Parsed JPEGs are already at their final size; show them un-cropped.
      setDisplayImage(card.display ? await imageFromJpeg(card.display) : null);
      setDisplayCrop(fitCrop());
      setShare1SameAsDisplay(false);
      setShare1Image(
        card.shares[0] ? await imageFromJpeg(card.shares[0]) : null,
      );
      setShare1Crop(fitCrop());
      setShare2Image(
        card.shares[1] ? await imageFromJpeg(card.shares[1]) : null,
      );
      setShare2Crop(fitCrop());
      if (!card.display) setNotice(t.shareOnlyNoDisplay);
    } catch (e) {
      setError(
        t.openFailed(file.name, e instanceof Error ? e.message : String(e)),
      );
    }
  };

  return (
    <main>
      <header>
        <h1>NameCardKnot Editor</h1>
        <button
          type="button"
          className="secondary"
          onClick={() => openRef.current?.click()}
        >
          {t.openMnc}
        </button>
        <input
          ref={openRef}
          type="file"
          accept=".pdf,application/pdf"
          hidden
          onChange={(e) => {
            onOpenPdf(e.target.files?.[0]);
            e.target.value = "";
          }}
        />
      </header>

      {error && (
        <div className="banner error" role="alert">
          <span>{error}</span>
          <button
            type="button"
            aria-label={t.close}
            onClick={() => setError(null)}
          >
            ✕
          </button>
        </div>
      )}
      {notice && (
        <div className="banner notice">
          <span>{notice}</span>
          <button
            type="button"
            aria-label={t.close}
            onClick={() => setNotice(null)}
          >
            ✕
          </button>
        </div>
      )}

      <div className="layout">
        <div className="form">
          <label className="field">
            <span className="field-label">{t.name}</span>
            <input
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
            />
          </label>

          <label className="field">
            <span className="field-label">{t.urlOptional}</span>
            <input
              type="url"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              placeholder="https://example.com"
            />
            {urlWarning && <span className="field-warning">{urlWarning}</span>}
          </label>

          <ImagePicker
            label={t.displayImage}
            help={t.displayImageHelp}
            image={displayImage}
            crop={displayCrop}
            targetW={DISPLAY_W}
            targetH={DISPLAY_H}
            onImage={setDisplayImage}
            onCrop={setDisplayCrop}
            onError={setError}
          />

          <div className="field">
            <span className="field-label">{t.share1}</span>
            <span className="field-help">{t.share1Help}</span>
            <label className="checkbox">
              <input
                type="checkbox"
                checked={share1SameAsDisplay}
                onChange={(e) => setShare1SameAsDisplay(e.target.checked)}
              />
              {t.sameAsDisplay}
            </label>
            {!share1SameAsDisplay && (
              <ImagePicker
                label=""
                image={share1Image}
                crop={share1Crop}
                targetW={SHARE_W}
                targetH={SHARE_H}
                onImage={setShare1Image}
                onCrop={setShare1Crop}
                onError={setError}
              />
            )}
          </div>

          <ImagePicker
            label={t.share2Optional}
            image={share2Image}
            crop={share2Crop}
            targetW={SHARE_W}
            targetH={SHARE_H}
            onImage={setShare2Image}
            onCrop={setShare2Crop}
            onError={setError}
          />

          <label className="field">
            <span className="field-label">{t.shareMessageOptional}</span>
            <textarea
              value={message}
              rows={4}
              onChange={(e) => setMessage(e.target.value)}
            />
          </label>

          <div className="save-area">
            {/* debug: Alt+Shift+click saves the share-only .snc.pdf */}
            <button
              type="button"
              onClick={(e) => onSave(e.altKey && e.shiftKey)}
              disabled={!canSave}
            >
              {busy ? t.generating : t.save}
            </button>
            {problems.length > 0 && (
              <span className="field-help">
                {t.required(problems.join(" / "))}
              </span>
            )}
          </div>
        </div>

        <aside className="preview-pane">
          <DevicePreview image={displayImage} crop={displayCrop} />
          <SharePreview
            name={name}
            url={url}
            message={message}
            shares={shares}
          />
        </aside>
      </div>
    </main>
  );
}
