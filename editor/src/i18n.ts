// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Minimal i18n: locale is detected once from the browser (ja / en, en default);
// there is no runtime switch, so components read the static `t` catalog directly.

type Locale = "ja" | "en";

export const locale: Locale = /^ja/i.test(navigator.language) ? "ja" : "en";

interface Messages {
  openMnc: string;
  close: string;
  name: string;
  urlOptional: string;
  urlWarning: string;
  displayImage: string;
  displayImageHelp: string;
  share1: string;
  share1Help: string;
  sameAsDisplay: string;
  share2Optional: string;
  shareMessageOptional: string;
  generating: string;
  save: string;
  required: (fields: string) => string;
  saved: (file: string) => string;
  shareOnlyNoDisplay: string;
  openFailed: (file: string, detail: string) => string;
  notImageFile: (file: string) => string;
  readFailed: (file: string) => string;
  clickOrDrop: string;
  placement: string;
  crop: string;
  fit: string;
  zoom: string;
  change: string;
  remove: string;
  dragToAdjust: string;
  preview: string;
  noDisplayImage: string;
  sharePageTitle: string;
}

const dict = {
  ja: {
    openMnc: ".mnc.pdf を開く",
    close: "閉じる",
    name: "名前",
    urlOptional: "URL（任意）",
    urlWarning:
      "http(s):// で始まっていません。デバイスの QR コードが正しく機能しない可能性があります",
    displayImage: "表示用画像",
    displayImageHelp: "デバイスの画面に表示する画像 (540×960)",
    share1: "共有用画像 1",
    share1Help: "共有相手に転送する画像 (405×720)",
    sameAsDisplay: "表示用と同じ画像を使う",
    share2Optional: "共有用画像 2（任意）",
    shareMessageOptional: "共有メッセージ（任意）",
    generating: "生成中…",
    save: "保存",
    required: (fields) => `必須項目: ${fields}`,
    saved: (file) =>
      `${file} を保存しました。SD カードにコピーしてデバイスで開けます。`,
    shareOnlyNoDisplay:
      "共有専用ファイル（.snc.pdf）のため表示用画像は含まれていません。",
    openFailed: (file, detail) =>
      `「${file}」を読み込めませんでした（NameCardKnot のファイルではない可能性があります）: ${detail}`,
    notImageFile: (file) => `「${file}」は画像ファイルではありません`,
    readFailed: (file) =>
      `「${file}」を読み込めませんでした（このブラウザが対応していない形式かもしれません）`,
    clickOrDrop: "クリックして選択 / ここにドロップ",
    placement: "配置",
    crop: "切り抜く",
    fit: "余白で収める",
    zoom: "ズーム",
    change: "変更",
    remove: "削除",
    dragToAdjust: "プレビューをドラッグして位置を調整",
    preview: "プレビュー",
    noDisplayImage: "表示用画像が未選択です",
    sharePageTitle: "共有ページ（PDF 2ページ目・近似）",
  },
  en: {
    openMnc: "Open .mnc.pdf",
    close: "Close",
    name: "Name",
    urlOptional: "URL (optional)",
    urlWarning:
      "Does not start with http(s)://. The QR code on the device may not work correctly.",
    displayImage: "Display image",
    displayImageHelp: "Image shown on the device screen (540×960)",
    share1: "Share image 1",
    share1Help: "Image sent to the person you share with (405×720)",
    sameAsDisplay: "Use the same image as the display",
    share2Optional: "Share image 2 (optional)",
    shareMessageOptional: "Share message (optional)",
    generating: "Generating…",
    save: "Save",
    required: (fields) => `Required: ${fields}`,
    saved: (file) =>
      `Saved ${file}. Copy it to an SD card to open it on the device.`,
    shareOnlyNoDisplay:
      "This is a share-only file (.snc.pdf), so it has no display image.",
    openFailed: (file, detail) =>
      `Could not read "${file}" (it may not be a NameCardKnot file): ${detail}`,
    notImageFile: (file) => `"${file}" is not an image file`,
    readFailed: (file) =>
      `Could not read "${file}" (this browser may not support the format)`,
    clickOrDrop: "Click to choose / drop here",
    placement: "Placement",
    crop: "Crop",
    fit: "Fit with margins",
    zoom: "Zoom",
    change: "Change",
    remove: "Remove",
    dragToAdjust: "Drag the preview to adjust the position",
    preview: "Preview",
    noDisplayImage: "No display image selected",
    sharePageTitle: "Share page (PDF page 2, approximate)",
  },
} satisfies Record<Locale, Messages>;

export const t = dict[locale];
