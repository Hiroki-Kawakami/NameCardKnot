# NameCardKnot

Firmware for an M5PaperS3-class name-card device — an **ESP32-S3 + 960×540
grayscale EPD** (`paper_s3` board). It is built on the reusable
[`esp-devkit`](https://github.com/Hiroki-Kawakami/esp-devkit) BSP / simulator
infrastructure, so the UI and app logic can be developed and verified on a host
**SDL simulator** without hardware. The device EPD (ED047TC1 over the ESP32-S3
i80 bus) and touch (GT911 over I2C) paths are implemented, and the UI is built
with **LVGL** through the `ui_framework` port abstraction.

## Build environment

The ESP-IDF (v5.4.3) toolchain and the host-simulator tools (cmake, ninja, gcc,
SDL2, libjpeg, cjson) all come from the Nix flake. Run build commands through
`nix develop -c <cmd>` (or from inside a `nix develop` shell); direnv users get
the shell automatically via `.envrc`.

`esp-devkit/` is a git submodule — clone with `--recurse-submodules` (or run
`git submodule update --init` after cloning).

## Build & run

```sh
./run.sh                                          # build + run the interactive SDL simulator
./run.sh simverify simulator/verify/smoke.txt     # headless scripted verification
./run.sh esp32s3                                  # idf.py -C esp32s3 flash monitor (needs a TTY)
```

(Prefix with `nix develop -c`, e.g. `nix develop -c ./run.sh`.)

## Repo layout

| Path           | What                                                            |
| -------------- | -------------------------------------------------------------- |
| `app/`         | shared app logic — `NameCardKnot.cpp`, `screens/`, `resources/` |
| `simulator/`   | host SDL simulator build root + verification scripts            |
| `esp32s3/`     | device ESP-IDF project build root                               |
| `esp-devkit/`  | submodule — reusable BSP, simulator backend, LVGL port, sim harness |

## Documentation

- [`CLAUDE.md`](CLAUDE.md) — architecture, BSP seam, build wiring, component model.
- [`docs/testing.md`](docs/testing.md) — simulator UI verification.
- [`docs/gotchas.md`](docs/gotchas.md) — build/env, EPD, and threading gotchas.

## License

Lucide icons are bundled under the ISC license — see
[`app/resources/Lucide_License.txt`](app/resources/Lucide_License.txt).  
Noto Sans JP is bundled under the SIL Open Font License — see
[`app/resources/OFL.txt`](app/resources/OFL.txt).
