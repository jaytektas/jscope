# CLAUDE.md

## Project

`jscope` — a GUI oscilloscope front end for the **Hantek 1008C**, an 8-channel
automotive scope on vendor-specific USB bulk with no kernel driver, built on the
JFramework toolkit and driven through a driver HAL.

The HAL is not there for its own sake: this instrument has no front panel, so
every setting it has exists only in this application, and the shared layers are
written so a second scope would be a second driver rather than a second set of
special cases.

## The framework's rules are this project's rules

`/home/jay/workspace/JFramework/CLAUDE.md` applies here in full. In particular:
one public class per header, file name == class name; no hardcoded visual
constants; no shims, stubs, TODO placeholders or dead code; widgets never
position themselves; no platform types in public headers; `JLOGC` only, never
`printf` or `std::cout`.

This app is DOWNSTREAM of a shipped SDK. It consumes JFramework through
`find_package(JFramework CONFIG)` and never edits it. If something is missing
from the framework, the answer is more code here — not a framework change.

## Additions specific to this project

**Visual constants come from `JScopeTheme`, not `JStyle`.** `JStyle` is a fixed
struct in the SDK and cannot be extended from an app, so the scope's own
vocabulary (trace colours, graticule, cursors, markers) lives in `JScopeTheme`,
which follows the identical `static JScopeTheme& current()` pattern and seeds
itself from `JStyle` roles where one already exists. No file under `src/ui/`
contains a numeric literal.

**Device numbers are data, not visual constants.** A protocol delay of 0.002 s,
a 4095-count full scale, the 26 ns/div ids — these are facts about hardware, and
they live in named `constexpr` blocks in the protocol and codec files. The theme
rule does not reach them.

**Layout.**

    src/            shared: usb, scope (the HAL), ui, measure, capture, the app
                    shell, and sources/ for the synthetic and replay drivers
    apps/scope1008/ the Hantek 1008C application -> jscope

The HAL and the widget set are deliberately instrument-agnostic and the driver
lives beside its application, so a second scope means a second driver directory
and a second executable -- never a control in the shared shell written for
whichever device cannot do the thing.

**Subsystems do not reach into each other.**

- `src/usb/`     — includes no GUI header and no scope header.
- `src/scope/`, `src/sources/`, `src/capture/`, `src/measure/`, and the app's
  driver directory — headless. No GUI header.
- `src/ui/`, `src/app/` — may use everything below them.

**Threading.** Exactly three kinds of thread and no others: the main/UI thread,
one acquisition thread per open driver, and one `JWorkerThread` per active
recording. An acquisition thread never touches a widget, `JSettings`, or the
driver registry, and never emits a `JSignal` directly — everything crosses back
through `JMainThreadDispatcher`, the contract `JSerialPort` already honours.

**No big translation units.** studio-jf's `main.cpp` reached 304 KB. The app
shell is several small classes, not one class with several sections. If
`-Wa,-mbig-obj` is ever needed, that is a bug about the structure.

## Build

    cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
    cmake --build build
    cmake --build build --target scope_tests && ctest --test-dir build

Tests are plain `int main()` + `<cassert>` — no GTest, no Catch2 — and every one
must pass with no hardware attached. Hardware tools live in `probes/` and are
never registered with `add_test`.
