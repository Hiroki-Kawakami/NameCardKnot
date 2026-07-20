# NameCardKnot Editor

A small client-side SPA for authoring NameCardKnot cards. It collects the name,
URL, message, a display image, and up to two share images, then saves a
**`.mnc.pdf`** container (a normal PDF that also carries the data + embedded
JPEGs for the device — see [`../docs/namecard_pdf.md`](../docs/namecard_pdf.md)).
The display image targets 540×960 and share images 405×720; each image has a
cover ("切り抜く": drag-to-pan + zoom) or fit ("余白で収める") placement, and
share image 1 can reuse the display image. The right pane live-previews the
device rendering (16-gray dithered, `src/lib/epd-preview.ts`) and an
approximation of the PDF share page. A saved `.mnc.pdf`/`.snc.pdf` can be
re-opened for editing (footer-index reader, `src/lib/namecard-pdf/pdf-reader.ts`);
text fields survive reloads via a localStorage draft.

The container writer/reader is a framework-free library in
`src/lib/namecard-pdf/` (`buildNameCardPdf` / `buildSharePdf` /
`parseNameCard`), unit-tested with vitest (`npm test`).

TypeScript + React + Vite, no backend.

## Develop

From the repo root (uses the Nix flake + `nodejs_22`):

```sh
nix develop -c ./run.sh editor            # Vite dev server
nix develop -c ./run.sh editor --host     # expose on the LAN
```

Or directly inside `editor/`:

```sh
npm install
npm run dev
npm run build      # type-check + production build → dist/
npm run preview    # serve the built dist/
```

## GitHub Pages

`vite.config.ts` sets `base: "./"` so the build works under any Pages sub-path.
`.github/workflows/editor-pages.yml` runs the tests, builds the editor, and
publishes `dist/` when an editor change reaches `main`. It can also be started
manually from the Actions tab.
