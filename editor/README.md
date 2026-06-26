# NameCardKnot Editor

A small client-side SPA for preparing name-card images for the NameCardKnot
device. The firmware is a plain image viewer, so this editor just converts a
picked image to 540×960, previews it, and saves a PNG you drop onto the SD card.

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
