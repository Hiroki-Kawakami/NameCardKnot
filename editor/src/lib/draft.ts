// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Hiroki Kawakami
// Text-field draft persisted to localStorage so a reload doesn't lose the form.
// Images are not persisted (too large for localStorage).

export interface Draft {
  name: string;
  url: string;
  message: string;
  share1SameAsDisplay: boolean;
}

const KEY = "nck-editor-draft";

export function loadDraft(): Draft | null {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return null;
    const d = JSON.parse(raw);
    return {
      name: typeof d.name === "string" ? d.name : "",
      url: typeof d.url === "string" ? d.url : "",
      message: typeof d.message === "string" ? d.message : "",
      share1SameAsDisplay: d.share1SameAsDisplay !== false,
    };
  } catch {
    return null;
  }
}

export function saveDraft(d: Draft): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(d));
  } catch {
    // Quota/private-mode failures just lose the draft, never the edit session.
  }
}
