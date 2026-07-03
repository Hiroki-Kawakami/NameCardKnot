// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Read a container back into its assets via the NCK footer index — no PDF
// syntax parsing, same approach as the C++ reader.

import { AssetType } from "./constants.ts";
import { crc32 } from "./crc32.ts";
import { parseFooter, type FooterEntry } from "./footer.ts";

export interface ParsedNameCard {
  name: string;
  url: string;
  message: string;
  display: Uint8Array | null; // null for a share-only (.snc.pdf) file
  shares: Uint8Array[];
}

export function parseNameCard(file: Uint8Array): ParsedNameCard {
  const { entries } = parseFooter(file);

  // Copy (not subarray) so the returned assets don't pin the whole file buffer.
  const payload = (e: FooterEntry): Uint8Array => {
    if (e.offset + e.length > file.length) throw new Error("asset out of range");
    if (crc32(file, e.offset, e.offset + e.length) !== e.crc) throw new Error("asset crc mismatch");
    return file.slice(e.offset, e.offset + e.length);
  };
  const text = (type: AssetType): string => {
    const e = entries.find((en) => en.type === type);
    return e ? new TextDecoder().decode(payload(e)) : "";
  };

  const displayEntry = entries.find((en) => en.type === AssetType.DisplayJpeg);
  return {
    name: text(AssetType.Name),
    url: text(AssetType.Url),
    message: text(AssetType.Message),
    display: displayEntry ? payload(displayEntry) : null,
    shares: entries.filter((en) => en.type === AssetType.ShareJpeg).map(payload),
  };
}
