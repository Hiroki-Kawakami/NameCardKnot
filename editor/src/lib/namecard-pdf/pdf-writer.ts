// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Minimal self-written PDF 1.7 writer for the NameCardKnot container.
// See docs/namecard_pdf.md §3.3. Tracks both xref object offsets and the
// absolute byte spans of payloads that the NCK footer indexes.

import { ByteWriter, utf8Bytes } from "./bytes.ts";

export interface Span {
  offset: number;
  length: number;
}

// Parse width/height out of a baseline JPEG (first SOF0..SOF3 marker).
export function jpegSize(jpeg: Uint8Array): { w: number; h: number } {
  let i = 2; // skip SOI (FFD8)
  while (i + 1 < jpeg.length) {
    if (jpeg[i] !== 0xff) {
      i++;
      continue;
    }
    const marker = jpeg[i + 1];
    i += 2;
    // Standalone markers (no length): SOI/EOI/RSTn/TEM.
    if (marker === 0xd8 || marker === 0xd9 || (marker >= 0xd0 && marker <= 0xd7) || marker === 0x01) {
      continue;
    }
    const len = (jpeg[i] << 8) | jpeg[i + 1];
    // SOFn frame headers carry the dimensions (skip DHT/JPG/DAC variants).
    if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc) {
      const h = (jpeg[i + 3] << 8) | jpeg[i + 4];
      const w = (jpeg[i + 5] << 8) | jpeg[i + 6];
      return { w, h };
    }
    i += len;
  }
  throw new Error("no JPEG SOF marker");
}

export function pdfLiteral(s: string): string {
  return "(" + s.replace(/([\\()])/g, "\\$1") + ")";
}

// Encode a BMP string as the UCS-2BE hex expected by /UniJIS-UCS2-H.
export function ucs2beHex(s: string): string {
  let out = "";
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    out += ((c >> 8) & 0xff).toString(16).padStart(2, "0");
    out += (c & 0xff).toString(16).padStart(2, "0");
  }
  return out;
}

export class PdfWriter {
  readonly w = new ByteWriter(4096);
  private xref = new Map<number, number>(); // objNum -> byte offset of "N 0 obj"
  private nextObj = 1;

  get length(): number {
    return this.w.length;
  }

  allocObj(): number {
    return this.nextObj++;
  }

  // Mark the next allocated object number without consuming, used to keep
  // numbering contiguous when emitting a known group.
  peekObj(): number {
    return this.nextObj;
  }

  header() {
    // Binary comment after the header so tools treat the file as binary.
    this.w.ascii("%PDF-1.7\n").ascii("%\xe2\xe3\xcf\xd3\n");
  }

  beginObj(num: number) {
    this.xref.set(num, this.w.length);
    this.w.ascii(`${num} 0 obj\n`);
  }

  endObj() {
    this.w.ascii("\nendobj\n");
  }

  // Plain (non-stream) object with a raw body.
  dictObj(num: number, body: string) {
    this.beginObj(num);
    this.w.ascii(body);
    this.endObj();
  }

  // Stream object; returns the absolute span of the raw payload bytes.
  streamObj(num: number, extraDict: string, payload: Uint8Array): Span {
    this.beginObj(num);
    this.w.ascii(`<< /Length ${payload.length}${extraDict ? " " + extraDict : ""} >>\nstream\n`);
    const offset = this.w.length;
    this.w.bytes(payload);
    this.w.ascii("\nendstream\nendobj\n");
    return { offset, length: payload.length };
  }

  // Emit the classic xref table for the given object numbers + trailer.
  // Returns the byte offset of the "xref" keyword (for startxref / /Prev).
  writeXref(objNums: number[], trailerExtra: string): number {
    const nums = [...objNums].sort((a, b) => a - b);
    const xrefOff = this.w.length;
    this.w.ascii("xref\n");

    // Group contiguous runs into subsections. The first full table also emits
    // the mandatory free object 0.
    const includeZero = nums[0] === 1 && !trailerExtra.includes("/Prev");
    const runs: number[][] = [];
    let run: number[] = [];
    for (const n of nums) {
      if (run.length && n === run[run.length - 1] + 1) run.push(n);
      else {
        if (run.length) runs.push(run);
        run = [n];
      }
    }
    if (run.length) runs.push(run);

    if (includeZero) {
      this.w.ascii("0 1\n").ascii("0000000000 65535 f\r\n");
    }
    for (const r of runs) {
      this.w.ascii(`${r[0]} ${r.length}\n`);
      for (const n of r) {
        const off = this.xref.get(n)!;
        this.w.ascii(`${off.toString().padStart(10, "0")} 00000 n\r\n`);
      }
    }
    this.w.ascii(`trailer\n<< ${trailerExtra} >>\nstartxref\n${xrefOff}\n%%EOF\n`);
    return xrefOff;
  }

  toBytes(): Uint8Array {
    return this.w.toBytes();
  }
}

export { utf8Bytes };
