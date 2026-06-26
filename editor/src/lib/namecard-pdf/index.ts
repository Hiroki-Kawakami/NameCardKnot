// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// NameCardKnot PDF container: public API.
// buildNameCardPdf -> full 2-page PDF + footer B.
// buildSharePdf    -> the (A) base = share-only PDF + footer A (byte-identical
//                     to truncating a full PDF at base_total_length).
// See docs/namecard_pdf.md.

import { utf8Bytes } from "./bytes.ts";
import {
  AssetSubtype,
  AssetType,
  FLAG_STRIP_ON_SHARE,
  GLYPH_PX,
  PAGE1_H,
  PAGE1_W,
  PAGE2_H,
  PAGE2_W,
} from "./constants.ts";
import { crc32 } from "./crc32.ts";
import { encodeFooter, type FooterEntry } from "./footer.ts";
import { encodeNameGlyphs, type GlyphSet } from "./glyphs.ts";
import { jpegSize, PdfWriter, pdfLiteral, ucs2beHex } from "./pdf-writer.ts";

export interface NameCardInput {
  name: string;
  url: string;
  message: string;
  display: Uint8Array; // baseline JPEG (the display image, page 1)
  shares?: Uint8Array[]; // baseline JPEGs (share images, page 2)
  nameGlyphs?: GlyphSet; // rasterized by the caller (browser canvas); optional
}

export { type GlyphSet, type Glyph } from "./glyphs.ts";
export { parseFooter, type FooterEntry, type ParsedFooter } from "./footer.ts";
export { AssetType, AssetSubtype } from "./constants.ts";

const IMG_DICT = (w: number, h: number) =>
  `/Type /XObject /Subtype /Image /Width ${w} /Height ${h} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode`;

function emptyGlyphSet(): GlyphSet {
  return {
    glyphPx: GLYPH_PX,
    baseLine: Math.round(GLYPH_PX * 0.8),
    lineHeight: Math.round(GLYPH_PX * 1.17),
    glyphs: [],
  };
}

// page2 (share) content stream: name (large), URL, message, and share images.
function shareContent(input: NameCardInput, shareSizes: { w: number; h: number }[]): Uint8Array {
  const ops: string[] = [];
  ops.push(`BT /F2 72 Tf 100 ${PAGE2_H - 120} Td <${ucs2beHex(input.name)}> Tj ET`);
  ops.push(`BT /F1 36 Tf 100 ${PAGE2_H - 200} Td ${pdfLiteral(input.url)} Tj ET`);
  ops.push(`BT /F2 36 Tf 100 ${PAGE2_H - 280} Td <${ucs2beHex(input.message)}> Tj ET`);
  shareSizes.forEach((s, i) => {
    const dw = 480;
    const dh = Math.round((dw * s.h) / s.w);
    const x = 100 + i * 520;
    ops.push(`q ${dw} 0 0 ${dh} ${x} 80 cm /S${i} Do Q`);
  });
  return utf8Bytes(ops.join("\n") + "\n");
}

interface Base {
  entries: FooterEntry[];
  baseTotalLength: number;
  xrefOffsetA: number;
  catalogObj: number;
  pagesObj: number;
  sharePageObj: number;
}

// Write region (A) + footer A into `p`. Identical bytes whether called from
// buildSharePdf or buildNameCardPdf, so share == prefix of the full file.
function buildBase(p: PdfWriter, input: NameCardInput): Base {
  p.header();
  const catalog = p.allocObj();
  const pages = p.allocObj();
  const page = p.allocObj();
  const content = p.allocObj();
  const fHelv = p.allocObj();
  const fType0 = p.allocObj();
  const cidFont = p.allocObj();
  const fontDesc = p.allocObj();
  const shares = input.shares ?? [];
  const shareObjs = shares.map(() => p.allocObj());
  const nameObj = p.allocObj();
  const urlObj = p.allocObj();
  const msgObj = p.allocObj();
  const glyphsObj = p.allocObj();

  const shareSizes = shares.map((j) => jpegSize(j));
  const contentBytes = shareContent(input, shareSizes);
  const xobjDict = shareObjs.map((o, i) => `/S${i} ${o} 0 R`).join(" ");

  p.dictObj(catalog, `<< /Type /Catalog /Pages ${pages} 0 R >>`);
  p.dictObj(pages, `<< /Type /Pages /Kids [${page} 0 R] /Count 1 >>`);
  p.dictObj(
    page,
    `<< /Type /Page /Parent ${pages} 0 R /MediaBox [0 0 ${PAGE2_W} ${PAGE2_H}]` +
      ` /Resources << /Font << /F1 ${fHelv} 0 R /F2 ${fType0} 0 R >> /XObject << ${xobjDict} >> >>` +
      ` /Contents ${content} 0 R >>`,
  );
  p.streamObj(content, "", contentBytes);
  p.dictObj(fHelv, `<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>`);
  p.dictObj(
    fType0,
    `<< /Type /Font /Subtype /Type0 /BaseFont /Ryumin-Light /Encoding /UniJIS-UCS2-H` +
      ` /DescendantFonts [${cidFont} 0 R] >>`,
  );
  p.dictObj(
    cidFont,
    `<< /Type /Font /Subtype /CIDFontType0 /BaseFont /Ryumin-Light` +
      ` /CIDSystemInfo << /Registry (Adobe) /Ordering (Japan1) /Supplement 7 >>` +
      ` /FontDescriptor ${fontDesc} 0 R >>`,
  );
  p.dictObj(
    fontDesc,
    `<< /Type /FontDescriptor /FontName /Ryumin-Light /Flags 4 /FontBBox [-100 -250 1100 900]` +
      ` /ItalicAngle 0 /Ascent 880 /Descent -120 /CapHeight 700 /StemV 80 >>`,
  );

  const entries: FooterEntry[] = [];
  const addString = (obj: number, type: AssetType, s: string) => {
    const bytes = utf8Bytes(s);
    const span = p.streamObj(obj, "", bytes);
    entries.push({ type, flags: 0, subtype: 0, offset: span.offset, length: span.length, crc: crc32(bytes) });
  };

  shareObjs.forEach((o, i) => {
    const span = p.streamObj(o, IMG_DICT(shareSizes[i].w, shareSizes[i].h), shares[i]);
    entries.push({
      type: AssetType.ShareJpeg,
      flags: 0,
      subtype: AssetSubtype.Jpeg,
      offset: span.offset,
      length: span.length,
      crc: crc32(shares[i]),
    });
  });
  addString(nameObj, AssetType.Name, input.name);
  addString(urlObj, AssetType.Url, input.url);
  addString(msgObj, AssetType.Message, input.message);

  const glyphsBlob = encodeNameGlyphs(input.nameGlyphs ?? emptyGlyphSet());
  const gSpan = p.streamObj(glyphsObj, "", glyphsBlob);
  entries.push({
    type: AssetType.NameGlyphs,
    flags: 0,
    subtype: 0,
    offset: gSpan.offset,
    length: gSpan.length,
    crc: crc32(glyphsBlob),
  });

  const allNums = Array.from({ length: p.peekObj() - 1 }, (_, i) => i + 1);
  const xrefOffsetA = p.writeXref(allNums, `/Size ${p.peekObj()} /Root ${catalog} 0 R`);

  const footerOffsetA = p.length;
  p.w.bytes(encodeFooter(entries, footerOffsetA, 0));
  return {
    entries,
    baseTotalLength: p.length,
    xrefOffsetA,
    catalogObj: catalog,
    pagesObj: pages,
    sharePageObj: page,
  };
}

export function buildSharePdf(input: NameCardInput): Uint8Array {
  const p = new PdfWriter();
  buildBase(p, input);
  return p.toBytes();
}

export function buildNameCardPdf(input: NameCardInput): Uint8Array {
  const p = new PdfWriter();
  const base = buildBase(p, input);

  // (B) incremental update: append the display image + a display page, then
  // override the Pages object so the display becomes page 1.
  const displayObj = p.allocObj();
  const dispPage = p.allocObj();
  const dispContent = p.allocObj();

  const { w, h } = jpegSize(input.display);
  const span = p.streamObj(displayObj, IMG_DICT(w, h), input.display);
  p.dictObj(
    dispPage,
    `<< /Type /Page /Parent ${base.pagesObj} 0 R /MediaBox [0 0 ${PAGE1_W} ${PAGE1_H}]` +
      ` /Resources << /XObject << /Im ${displayObj} 0 R >> >> /Contents ${dispContent} 0 R >>`,
  );
  p.streamObj(dispContent, "", utf8Bytes(`q ${PAGE1_W} 0 0 ${PAGE1_H} 0 0 cm /Im Do Q\n`));
  p.dictObj(
    base.pagesObj,
    `<< /Type /Pages /Kids [${dispPage} 0 R ${base.sharePageObj} 0 R] /Count 2 >>`,
  );

  p.writeXref(
    [base.pagesObj, displayObj, dispPage, dispContent],
    `/Size ${p.peekObj()} /Root ${base.catalogObj} 0 R /Prev ${base.xrefOffsetA}`,
  );

  const displayEntry: FooterEntry = {
    type: AssetType.DisplayJpeg,
    flags: FLAG_STRIP_ON_SHARE,
    subtype: AssetSubtype.Jpeg,
    offset: span.offset,
    length: span.length,
    crc: crc32(input.display),
  };
  const footerOffsetB = p.length;
  p.w.bytes(encodeFooter([...base.entries, displayEntry], footerOffsetB, base.baseTotalLength));
  return p.toBytes();
}
