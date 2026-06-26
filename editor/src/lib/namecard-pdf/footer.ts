// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// NCK footer: header + {type,offset,length,crc} index + fixed 24-byte trailer.
// See docs/namecard_pdf.md §4.

import { ByteWriter } from "./bytes.ts";
import { crc32 } from "./crc32.ts";
import {
  AssetSubtype,
  AssetType,
  FOOTER_END_MAGIC,
  FOOTER_HEADER_SIZE,
  FOOTER_MAGIC,
  FOOTER_TRAILER_SIZE,
  FOOTER_VERSION,
} from "./constants.ts";

export interface FooterEntry {
  type: AssetType;
  flags: number;
  subtype: AssetSubtype;
  offset: number; // absolute file offset of the payload
  length: number;
  crc: number; // crc32 of the payload
}

// Build a footer block to append at `footerOffset` (= current file length).
// `baseTotalLength` is the share-only truncation point, or 0 in footer A.
export function encodeFooter(
  entries: FooterEntry[],
  footerOffset: number,
  baseTotalLength: number,
): Uint8Array {
  const w = new ByteWriter(FOOTER_HEADER_SIZE + entries.length * 16 + FOOTER_TRAILER_SIZE);
  w.ascii(FOOTER_MAGIC).u16(FOOTER_VERSION).u16(0).u16(entries.length).u16(0);
  for (const e of entries) {
    w.u8(e.type).u8(e.flags).u8(e.subtype).u8(0);
    w.u32(e.offset).u32(e.length).u32(e.crc);
  }
  const body = w.toBytes(); // header + index, the crc'd region
  const footerCrc = crc32(body);
  w.u32(baseTotalLength).u32(footerOffset).u32(footerCrc).u32(0).ascii(FOOTER_END_MAGIC);
  return w.toBytes();
}

export interface ParsedFooter {
  entries: FooterEntry[];
  footerOffset: number;
  baseTotalLength: number;
}

const ascii = (b: Uint8Array, off: number, n: number) =>
  String.fromCharCode(...b.subarray(off, off + n));

// Parse the footer out of a full file image (mirrors the C++ reader).
export function parseFooter(file: Uint8Array): ParsedFooter {
  if (file.length < FOOTER_TRAILER_SIZE) throw new Error("file too small for trailer");
  const dv = new DataView(file.buffer, file.byteOffset, file.byteLength);
  const tEnd = file.length;
  const tStart = tEnd - FOOTER_TRAILER_SIZE;
  if (ascii(file, tEnd - 8, 8) !== FOOTER_END_MAGIC) throw new Error("bad trailer magic");
  const baseTotalLength = dv.getUint32(tStart, true);
  const footerOffset = dv.getUint32(tStart + 4, true);
  const footerCrc = dv.getUint32(tStart + 8, true);

  if (footerOffset + FOOTER_HEADER_SIZE > tStart) throw new Error("bad footer offset");
  if (ascii(file, footerOffset, 4) !== FOOTER_MAGIC) throw new Error("bad footer magic");
  const version = dv.getUint16(footerOffset + 4, true);
  if (version !== FOOTER_VERSION) throw new Error(`unsupported version ${version}`);
  const count = dv.getUint16(footerOffset + 8, true);

  const indexEnd = footerOffset + FOOTER_HEADER_SIZE + count * 16;
  if (indexEnd !== tStart) throw new Error("index size mismatch");
  if (crc32(file, footerOffset, indexEnd) !== footerCrc) throw new Error("footer crc mismatch");

  const entries: FooterEntry[] = [];
  let p = footerOffset + FOOTER_HEADER_SIZE;
  for (let i = 0; i < count; i++) {
    entries.push({
      type: file[p],
      flags: file[p + 1],
      subtype: file[p + 2],
      offset: dv.getUint32(p + 4, true),
      length: dv.getUint32(p + 8, true),
      crc: dv.getUint32(p + 12, true),
    });
    p += 16;
  }
  return { entries, footerOffset, baseTotalLength };
}
