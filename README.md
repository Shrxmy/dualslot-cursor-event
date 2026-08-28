# DualSlot DS

DualSlot is a Qt 6 desktop shell that presents two unmodified emulator libraries as one Nintendo DS-like device:

- **Slot-1 (`.nds`, `.dsi`, `.ids`)** runs the melonDS core.
- **Slot-2 (`.gba`, `.gb`, `.gbc`)** runs mGBA when Slot-1 is empty.
- Inserting/ejecting Slot-1 automatically flushes SRAM and switches the active core.
- A GBA cartridge remains exposed to DS software through melonDS Slot-2 while a DS card is active.

Only one core ticks at a time.

## Build (MSYS2 UCRT64)

Install CMake, Ninja, a C/C++ toolchain, Qt 6, and SDL2, then run:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/dualslot.exe
```

The root CMake configures mGBA with `LIBMGBA_ONLY=ON`, melonDS with `BUILD_QT_SDL=OFF`, and builds the `dualslot` Qt executable.

## Runtime files

No Nintendo BIOS, firmware, NAND, or ROM is distributed by this project. Put your own legal dumps under `firmware/` next to the executable or at the repository root. Discovery is recursive and accepts common names such as:

- DS: `bios7.bin`/`biosnds7.bin`, `bios9.bin`/`biosnds9.bin`, `firmware.bin`/`dsfirmware.bin`
- DSi: `bios7i.bin`/`biosdsi7.bin`, `bios9i.bin`/`biosdsi9.bin`, `dsifirmware.bin`, `nand.bin`/`dsinand.bin`
- GBA: `gba_bios.bin` (optional; mGBA can run without it)

SRAM is written to `<rom>.sav`. Savestates and screenshots go under `saves/<rom-stem>/`.

## Default controls

| DS control | Keyboard |
|---|---|
| D-pad | Arrow keys |
| A / B | X / Z |
| X / Y | Q / W |
| L / R | A / S |
| Start / Select | Enter / Backspace or Shift |
| Touch | Mouse on bottom screen |
| Pause / Fast forward / Fullscreen | Space / Tab / F11 |

SDL game controllers use conventional face, shoulder, Start/Back, D-pad, and left-stick mappings.

## License

The DualSlot frontend is GPL-3.0-or-later because it links melonDS. The vendored upstream projects retain their own licenses and copyright notices; mGBA source remains unmodified MPL-2.0 code.
