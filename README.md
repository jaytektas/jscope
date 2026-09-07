<p align="center">
  <img src="src/app/assets/jaytek-logo.png" alt="JAYTEK" width="240">
</p>

# jscope

A GUI oscilloscope front end for the **Hantek 1008C** — the 8-channel automotive
scope that ships as a vendor-specific USB bulk device (`0783:5725`) with no
front panel, no kernel driver, and no documentation.

Written in C++20 on [JFramework](https://github.com/jaytektas/JFramework), a
Vulkan-rendered widget toolkit. It builds and runs natively on Linux and
cross-compiles to Windows.

## What it does

- **Eight channels** at three vertical ranges, with per-channel probe
  attenuation, coupling and colour.
- **Roll and burst acquisition** — the 1008C streams continuously with no
  trigger in roll mode, and captures a windowed sweep in burst mode. Both are
  presented as the same thing: whatever the newest frame holds.
- **Auto / Normal / Single sweeps** with a software trigger, level and slope.
- **Measurements and cursors** — Vpp, Vrms, Vmax, Vmin, Vavg, frequency,
  period, duty, rise and fall, plus draggable X/Y cursors with deltas.
- **The pattern generator.** The 1008C has eight digital outputs driven from a
  pattern of up to 1440 pulses at a speed expressed in RPM — a crank and cam
  simulator. `jscope` implements it as a real editor: click a pulse to flip it,
  drag the L1/L2 angle cursors, save and load the OEM's own `.squ` files, and
  download the pattern to the instrument.
- **Capture and replay** to a `.jscope` container, so a session can be recorded
  and scrubbed with no hardware attached.
- **Floating, dockable panels**, including tear-out from the centre.

## Building

Needs [JFramework](https://github.com/jaytektas/JFramework) installed as an SDK,
plus `libusb-1.0`, Vulkan and a C++20 compiler.

```sh
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/jframework-sdk
cmake --build build
./build/jscope
```

### Tests

Plain `int main()` + `<cassert>`. No GTest, no Catch2, and **every one passes
with no hardware attached** — the protocol tests assert against recorded byte
transcripts, and the acquisition tests run against a synthetic source.

```sh
cmake --build build --target scope_tests && ctest --test-dir build
```

Anything that needs a real instrument lives in `probes/`, is
`EXCLUDE_FROM_ALL`, and is never registered with `add_test`.

### Windows

```sh
./packaging/build-windows.sh
```

Cross-compiles with mingw-w64 and links the runtime statically, so the result
is a single `.exe`. See [`docs/windows-cross-compile.md`](docs/windows-cross-compile.md).

## Access to the device

The 1008C binds to no kernel driver, so libusb can claim it directly. It only
needs permission:

```
# /etc/udev/rules.d/99-hantek-1008c.rules
SUBSYSTEM=="usb", ATTR{idVendor}=="0783", ATTR{idProduct}=="5725", MODE="0666"
```

Note that the device does not identify itself as a Hantek — it enumerates as
`"YDJ-2088"` by `"C3PO"`. Driver matching keys on VID/PID, never on strings.

## The protocol

Undocumented, and worked out here from the wire and from the behaviour of the
OEM software. It is written down in full so the next person does not have to
repeat it:

- [`docs/hantek1008-protocol.md`](docs/hantek1008-protocol.md) — opcodes,
  framing, the echo/ready handshake, calibration, and the generator.
- [`docs/hantek1008-oem-behaviour.md`](docs/hantek1008-oem-behaviour.md) — what
  the vendor application actually does, and why.
- [`docs/headless-testing.md`](docs/headless-testing.md) — driving the GUI
  without a person in front of it.

A protocol is not a copyrightable work. Nothing here is derived from anyone
else's source.

## Licence

GPLv3. See [`LICENSE`](LICENSE).

Copyright © 2025 Jason Roughley &lt;pis.controller@gmail.com&gt;
