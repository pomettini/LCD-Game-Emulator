# Playdate porting notes

## Scope and audit baseline

This port targets physical Playdate hardware (Rev A and Rev B) and the 25
single-screen Nintendo Game & Watch titles listed in the project brief. It uses
user-supplied `.gw` packages produced by LCD-Game-Shrinker. It does not support
Madrigal `.mgw` simulations, multi-screen machines, or bundled game assets.

The audit below describes upstream commit `553ce72` before Playdate adaptation.
The repository contains 4,688 lines of C and headers, no C++, and no existing
build system or host entry point.

## Reusable with minimal or no behavioral change

- The SM500, SM5A, SM510, and SM511 execution and opcode implementations in
  `src/cpus` are small C translation units derived from MAME's BSD-3-Clause
  SM5xx core. SM512 currently uses the SM511 implementation. The supported
  first-wave single-screen games use this family.
- CPU register state, LFSR program counter behavior, stack behavior, RAM access,
  divider timers, LCD state, and melody controller are already represented.
- The package keyboard matrix describes S/K, BA, and B wiring and is a useful
  basis for data-driven controls.
- SM510 LCD state is exposed as A/B/C RAM groups plus BS outputs. SM500/SM5A LCD
  state is exposed as O/W latches. This can feed a platform-neutral segment
  callback without putting Playdate calls in the CPU.
- The clock read/write helpers can be retained after package-provided RAM
  addresses and values are validated.
- The repository-level license is GPLv3. The CPU files retain their upstream
  BSD-3-Clause attribution; the system, graphics, and loader files carry GPLv3
  notices. New combined runtime code must remain GPL-compatible and preserve
  those notices.

## Must be adapted

### Core/platform boundary

`sm510base.c` calls global host functions for buttons and buzzer output, while
`gw_system.c` owns keyboard policy, rendering dispatch, audio buffering, state
serialization, and timing. Introduce a `GWPlatform`-style callback interface and
machine context incrementally. Playdate SDK calls belong only in
`src/platform/playdate` and the ROM-picker frontend.

The existing globals are acceptable for the first single-machine milestone,
but callback installation, loaded-package state, and failure status must have
explicit lifetimes. Save-state code is out of scope and should not be wired to
Playdate persistence.

### CPU timing

All variants use the documented 32,768 Hz oscillator. SM510 begins with a
divide-by-2 instruction clock (16,384 operations/s); SM511/SM512 use divide by 4
(8,192 operations/s). SM500 and SM5A inherit the SM510 divider setup. SM510
opcodes may switch the divider between 2 and 4.

`gw_system_run()` accepts oscillator clocks but truncates each call by the
current divider and returns `m_icount * m_clk_div` after execution, when
`m_icount` is normally zero. The Playdate scheduler must instead accumulate
elapsed monotonic microseconds at 32,768 oscillator cycles/s, preserve the
fractional remainder, and account for cycles actually consumed across divider
changes. Rendering cadence must remain independent. A bounded-resume policy
must discard or resynchronize excessive host backlog rather than execute it in
one update.

### Display

The existing renderer rebuilds a 320x240 RGB565 framebuffer every call and
multiplies 2-, 4-, or 8-bit segment masks into background pixels. Playdate needs
a 400x240, row-padded 1-bit composition path. Retain package coordinates and
segment-state extraction, but replace RGB565 output with precomputed/dithered
1-bit background plus high-contrast active segments. Validate every rectangle
and packed-pixel span before rendering. Begin with a full-frame Ball renderer;
add changed-segment or dirty-region updates only after device measurement.

### Input

The package's ten 32-bit keyboard words are consumed directly and include
generic button bits plus some legacy shortcuts and a Green House-specific hack.
Create a strict per-game database and translate Playdate buttons into logical
Game & Watch actions there. Package keyboard data remains machine wiring, but it
must be size- and alignment-safe. Unsupported signatures must never reach the
core.

### Audio

The current buzzer path writes one byte each time R is updated into a fixed
512-byte buffer. Its index is not bounded in the producer and its copied flag is
an unsynchronized host contract. Replace it with a preallocated single-producer
/single-consumer ring or an audio source callback whose samples are generated
from timestamped buzzer transitions. Resample the 32,768 Hz hardware timeline
to the Playdate callback rate without allocation, file I/O, or blocking in the
callback. Add a short edge ramp only where needed to avoid artificial clicks.

## Must be replaced

### Runtime loader

`gw_romloader.c` is tied to an embedded `ROM_DATA` symbol, an STM32 hardware JPEG
decoder, and project-local LZ4/zlib/LZMA headers that are absent from this fork.
It reserves a 400,000-byte global buffer plus a 115,200-byte JPEG work buffer.
It cannot compile here unchanged.

The loader copies the file header into a native C struct and therefore assumes
32-bit little-endian integers, compiler layout, and suitable alignment. It only
checks that `keyboard + keyboard_size` equals the decoded size. Earlier offsets,
sizes, element counts, coordinate bounds, packed segment spans, program size,
melody size, compression lengths, integer overflow, and overlap are not safely
validated. Several malformed-file paths assert instead of returning an error.
JPEG pointers are cast through 32-bit integers.

Replace this with a bounded file reader using explicit little-endian decoding.
Parse the fixed header first, identify a whitelisted game, validate all
`offset + size` operations before allocation or access, require exactly one
320x240 display, then load only the objects needed by the runtime. Compression
support must be introduced format by format with bounded output. General SVG or
image conversion will not run on-device.

### Host services

There is no implementation of `gw_get_buttons()`, no generic filesystem API,
and no application lifecycle. These become the Playdate adapter and frontend.
Assertions on user-controlled data become recoverable on-device error screens.

## Memory and portability risks

- The old static loader working set is over 500 KiB before the RGB565 output
  framebuffer (another 153,600 bytes), decompressor state, audio, and frontend.
  Rev A budgeting needs measurements of peak heap and stack use. Avoid duplicate
  full-package buffers and retain compact 1-bit graphics where possible.
- `unsigned int` is assumed to be 32 bits and `unsigned short` 16 bits throughout.
  Playdate ARM satisfies that assumption, but package parsing will use
  `uint8_t`, `uint16_t`, `uint32_t`, `size_t`, and explicit endian helpers.
- Package arrays are cast directly from byte storage to 16- and 32-bit pointers.
  The new loader must either guarantee alignment or decode through accessors.
- CPU program reads currently have no bounds check. The whitelist and loader
  must enforce the exact program geometry expected by each CPU before start.
- Segment numbers are `uint8`, while SM510 positions reach 255. Metadata counts
  and all lookup tables require exact consistency checks.
- GCC-specific `optimize("unroll-loops")` attributes are present in graphics;
  they are supported by the device compiler but should not dictate the new
  renderer design.
- The source uses non-prototype declarations such as `void fn()`. Convert public
  APIs to `(void)` as files are touched; do not churn the CPU core blindly.

## Dependency and build decisions

- Runtime code remains C. No C++ dependency has been identified.
- The standard Playdate SDK `common.mk` Makefile workflow is used. The default
  target is `device`; the bootstrap source rejects `TARGET_SIMULATOR` builds.
- Deployment follows the user's device-only `vecx` pattern: `make device` builds
  the PDX, while `make install` copies it to a mounted Playdate data disk. No
  deployment is run without explicit confirmation.
- `pd-rom-picker` is MIT-licensed and will live at `vendor/pd-rom-picker` as a
  pinned Git submodule. Integration should compile a small local C build unit
  that includes the unmodified submodule source, matching the established
  emulator pattern.

## Initial milestones

1. **Device bootstrap:** device-only Makefile, PDX metadata, and lifecycle-safe
   C entry point. It displays a static bootstrap screen and executes no emulator
   code.
2. **Picker:** add and initialize `pd-rom-picker` for `.gw` and `.mgw` visibility;
   `.mgw` selection must lead to the explicit unsupported-format message.
3. **Safe format probe:** bounded header parser and error model, strict Ball-only
   database entry, and rejection tests runnable as host-independent C tests
   without building a Simulator application.
4. **Ball loading:** incrementally read validated program, keyboard, background,
   and segment data with a measured memory budget.
5. **Execution/timing:** install the platform callbacks, start the correct Ball
   CPU, and run the monotonic fractional-cycle scheduler with debug metrics.
6. **Ball video/input/audio:** static 1-bit background, live segment composition,
   database-driven controls, then synchronized buzzer ring/callback.
7. **Clock and lifecycle:** initial system-time injection, explicit resync action,
   reset confirmation, picker return, and bounded pause/resume behavior.
8. **Expansion:** add Silver/Gold, Wide Screen, then New Wide Screen database
   entries only after Ball is confirmed on physical hardware. Preserve Rev A
   memory and performance constraints throughout.

Compilation is only a static milestone. Playability, rendering, sound, speed,
and underrun behavior remain unverified until the required physical-device test.

## Current proof-of-concept status

The repository now builds a device-only C application, integrates the pinned
ROM picker, validates the LZ4/raw package structure and per-game program
checksums, executes the SM5A core from a monotonic 32,768 Hz cycle accumulator,
renders RGB565 artwork and live segments in stable 1-bit output, maps
game-specific controls, streams cycle-synchronous buzzer samples through a
preallocated audio ring, and provides clock sync/reset/controls/picker menu
actions. Optional debug metrics are enabled with
`make device DEBUG_METRICS=1`.

Ball has been confirmed playable on a physical Playdate. Three sampled
five-second debug windows reported no sustained timing deficit (0, -1, and 0
oscillator cycles) and the four startup audio underruns did not increase.
Flagman is the next execution-enabled SM5A title. Its ROM-free host execution
test passes, and its generated package passes the strict loader validation;
physical-device validation remains pending.

JPEG-background packages are parsed but rejected by the current renderer.
Pause, lock, and menu overlays discard host-time backlog and restart the audio
queue; they do not emulate all missed real time. A manual clock synchronization
action is available when that distinction matters.
