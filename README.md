# NameCardKnot

NameCardKnot is firmware for M5Paper and PaperS3 devices that displays name-card artwork on a 540×960 grayscale e-paper screen and shares name-card data between devices.

## User Guides

- [English](docs/user_guide.md)
- [日本語](docs/user_guide_ja.md)

## Quick Start

Clone the repository with its submodules and start the host simulator:

```sh
git clone --recurse-submodules https://github.com/Hiroki-Kawakami/NameCardKnot.git
cd NameCardKnot
nix develop -c ./run.sh
```

See the [development guide](docs/development.md) for device builds, the web editor, verification, and the repository structure.

## License

NameCardKnot is licensed under the [MIT License](LICENSE).

Bundled third-party assets retain their respective licenses:

- Lucide icons: [ISC License](app/resources/Lucide_License.txt)
- Noto Sans JP: [SIL Open Font License](app/resources/OFL.txt)
