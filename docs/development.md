# Development

This guide covers the development environment, build targets, simulator, device builds, web editor, and repository structure. Run commands from the repository root unless noted otherwise.

## Prerequisites

- Git
- [Nix](https://nixos.org/) with flakes enabled
- A supported device and USB connection for device builds and flashing

The Nix flake provides ESP-IDF v6.0.2, CMake, Ninja, GCC, SDL2, libjpeg, cJSON, Node.js 22, and the other build dependencies. Run build commands through `nix develop -c <command>` or from inside a `nix develop` shell. If you use direnv, `.envrc` enters the development shell automatically.

Clone the repository with its submodules:

```sh
git clone --recurse-submodules https://github.com/Hiroki-Kawakami/NameCardKnot.git
cd NameCardKnot
```

If the repository was cloned without submodules, initialize them with:

```sh
git submodule update --init
```

`esp-devkit/` is a separate Git repository included as a submodule. Changes made under that directory must be committed in the `esp-devkit` repository before updating the submodule reference in NameCardKnot.

## Targets

| Board | SoC | Display | Build root |
| --- | --- | --- | --- |
| `paper_s3` | ESP32-S3 | ED047TC1 over i80 | `esp32s3/` |
| `paper` | ESP32 | IT8951E over SPI | `esp32/` |
| Simulator | Host SDL | Simulated EPD, defaults to `paper_s3` | `simulator/` |

Both device targets share the application code under `app/` and the app-specific components under `components/`.

## Simulator

Build and run the interactive SDL simulator:

```sh
nix develop -c ./run.sh
```

The simulator uses the `paper_s3` board configuration by default. To build the `paper` configuration in a separate build directory:

```sh
nix develop -c cmake -S simulator -B build-paper -G Ninja -DBSP_BOARD=paper
nix develop -c cmake --build build-paper
nix develop -c ./build-paper/simulator
```

In the interactive simulator, press `r` or `l` to rotate the host view and `Esc` to quit.

## Device Builds

Build a target without flashing:

```sh
nix develop -c idf.py -C esp32s3 build
nix develop -c idf.py -C esp32 build
```

Flash the selected target and open its serial monitor:

```sh
nix develop -c ./run.sh esp32s3
nix develop -c ./run.sh esp32
```

The flash-and-monitor commands require a connected device and a TTY.

## NameCardKnot Editor

Start the Vite development server:

```sh
nix develop -c ./run.sh editor
```

`run.sh` installs the editor's npm dependencies when `editor/node_modules` is missing. Extra arguments are passed to Vite, for example:

```sh
nix develop -c ./run.sh editor --host
```

Run the editor tests or create a production build with:

```sh
nix develop -c npm --prefix editor test
nix develop -c npm --prefix editor run build
```

See the [editor documentation](../editor/README.md) for format and deployment details.

## Verification

Run the minimal headless simulator verification with:

```sh
nix develop -c ./run.sh simverify simulator/verify/smoke.txt
```

Captured frames are written to paths specified by the verification script, normally under `simulator/verify/out/`. See [Testing](testing.md) for the simulator scripting format and all host test commands.

## Repository Structure

| Path | Purpose |
| --- | --- |
| `app/` | Shared application logic, screens, and generated LVGL resources |
| `components/` | App-specific reusable components, including image and PDF processing |
| `editor/` | TypeScript, React, and Vite web editor for authoring name-card PDFs |
| `simulator/` | Host SDL simulator build root and verification scripts |
| `esp32s3/` | ESP-IDF build root for PaperS3 |
| `esp32/` | ESP-IDF build root for M5Paper |
| `esp-devkit/` | Submodule containing the reusable BSP, simulator backend, libraries, and LVGL port |
| `docs/` | User, development, architecture, testing, and component documentation |

## Technical Documentation

- [`CLAUDE.md`](../CLAUDE.md): architecture, BSP interfaces, build wiring, and component model
- [Testing](testing.md): simulator verification and host tests
- [Gotchas](gotchas.md): build environment, EPD, threading, and hardware caveats
- [Image processor](image_processor.md): image decoding and EPD conversion pipeline
- [Name-card PDF](namecard_pdf.md): `.mnc.pdf` and `.snc.pdf` container format
- [My Card storage](mycard.md): on-device flash storage and resume cache
- [dokan](dokan.md): peer-to-peer transfer transport

The Nix flake only includes Git-tracked files. After adding a source or build file, stage it before testing through Nix. See [Gotchas](gotchas.md) for details.
