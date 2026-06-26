# NameCardKnot Editor

A small client-side SPA for authoring NameCardKnot cards. It collects the name,
URL, message, a display image, and up to two share images, then saves a
**`.mnc.pdf`** container (a normal PDF that also carries the data + embedded
JPEGs for the device — see [`../docs/namecard_pdf.md`](../docs/namecard_pdf.md)).
The display image is scaled to fit 540×960 and share images to fit 405×720
(aspect preserved); share image 1 can reuse the display image.

The container writer is a framework-free library in `src/lib/namecard-pdf/`
(`buildNameCardPdf` / `buildSharePdf`), unit-tested with vitest (`npm test`).

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
Publish the `dist/` directory produced by `npm run build`.
