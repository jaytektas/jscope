# What the OEM software actually does

Recovered by capturing usbmon while driving Hantek's own `Scope.exe` (v1.0.30)
through the UI, with a timestamped marker written before every action so each
interval on the wire is attributable to one change.

The Windows DLL is untouched: `hantek-wine`'s shim forwards every export to the
original `HTNTLVDll.dll` and only patches its file I/O onto libusb. So the
command bytes are Hantek's own, and this is what the instrument is really asked
for — not a reimplementation's idea of it.

## Per frame

```
a4 01 · c0 · [a5 5a / f3] xN · c6 02 → a6 02 xn · c6 03 → a6 03 xm
      · e4 01 · e6 01 · f3 · e4 01 · e6 01
```

and **nothing else**. No `a3`, no `c1`, no `a7`, no `ac`: configuration is sent
when it CHANGES and then left alone. Re-sending `a3` per capture — which this
driver used to do — is what cleared the device's trigger selection and made the
trigger source appear to do nothing.

**`c2` never appears in the loop.** It is FORCE TRIGGER, and the OEM sends it
only during initialisation. Sending it every capture starts the record
immediately and unaligned, which is why the trace would not hold still.

The ready poll alternates `a5 5a` with `f3`, not with a ping.

## Changing the timebase: `a3` then `ac`, nothing more

| Time/DIV | `a3` | `ac` payload | pre | delay_b | delay_c |
|---|---|---|---|---|---|
| 500 us | 11 | `01f4 0009c5 0009c5` | 500 | 2501 | 2501 |
| 1 ms | 12 | `01f4 001389 001389` | 500 | 5001 | 5001 |
| 2 ms | 13 | `01f4 002711 002711` | 500 | 10001 | 10001 |
| 5 ms | 14 | `01d4 0061a9 0061a9` | 468 | 25001 | 25001 |
| 10 ms | 15 | `01ce 00c347 00c347` | 462 | 50007 | 50007 |

This confirms the ladder — code 17 IS 500 us — and the `pre` column
independently pins kRecordLenForId at 4000, 3750 and 3703, because
`pre = 2 * ((N / nch) * percent / 100)` reproduces 500, 468 and 462 exactly at
eight active channels.

## Horizontal position: the delay SPLIT, at a constant sum

Dragging the trigger marker sends one `ac` and nothing else:

| action | `ac` payload | pre | delay_b | delay_c | b+c | implied % |
|---|---|---|---|---|---|---|
| centre | `01f4 0009c5 0009c5` | 500 | 2501 | 2501 | 5002 | 50 |
| dragged right | `0280 000c81 000709` | 640 | 3201 | 1801 | 5002 | 64 |
| dragged left | `0154 0006a5 000ce5` | 340 | 1701 | 3301 | 5002 | 34 |

**b + c is constant** — it is the whole sweep — and the position moves the split.
`delay_b` is the pre-trigger share and `delay_c` the post-trigger share:

```
delay_b = int(percent       * N / 100 * C) + 1
delay_c = int((100-percent) * N / 100 * C) + 1
pre     = 2 * ((N / nch) * percent / 100)
```

Checked against the dragged-right row: 64% at code 17 gives
int(64*40*1.25)+1 = 3201, int(36*40*1.25)+1 = 1801, and 2*(500*0.64) = 640.
All three exact. JHantek1008Protocol::setHorizontalTriggerPosition already
computes precisely this.

## Sweep mode is HOST-SIDE

Switching Auto → Normal → Single produced **no USB traffic at all**. The
instrument has no sweep mode; the OEM implements it in the PC exactly as this
driver does, by choosing how long to wait for the ready poll before giving up.

## A vertical change re-sends the whole vertical block

Stepping CH1's volts/div sends `a0 08`, `aa 01…`, `a2 03…` and `ab 07ee`
together — channel count, channel map, all eight gain codes, and the trigger
LEVEL. The level goes with it because its counts depend on the gain.

## The ladders the OEM actually offers

Read off Hantek's own UI by stepping every control across its whole range with
the OEM driven headlessly, and captured as screenshots of the spinner rather
than inferred from the binary. An earlier pass through this section got two of
the three lists wrong by starting them one entry too late; the numbers below are
the swept ones.

**Probe: 12 entries. Six are voltage attenuators, six are current clamps.**

| idx | label | scale | unit |
|-----|-------------------|-------|------|
| 0   | x1                | 1     | V |
| 1   | x10               | 10    | V |
| 2   | x100              | 100   | V |
| 3   | x1000             | 1000  | V |
| 4   | x10000            | 10000 | V |
| 5   | 20:1              | 20    | V |
| 6   | CC65(1mV/10mA)    | 10    | A |
| 7   | CC65(1mV/100mA)   | 100   | A |
| 8   | CC650(1mV/100mA)  | 100   | A |
| 9   | CC650(1mV/1A)     | 1000  | A |
| 10  | CC1100(100A)      | 100   | A |
| 11  | CC1100(1100A)     | 1000  | A |

The scale is the input quantity per volt at the BNC, which makes the clamps and
the attenuators the same arithmetic: a clamp reading 1 mV per 10 mA is 10 A per
volt. The clamps change the vertical UNIT to amps, and the unit travels with the
probe rather than with the channel.

**Volts/div is ONE nine-step ladder, scaled by the probe:**

```
10.0mV  20.0mV  50.0mV  100mV  200mV  500mV  1.00V  2.00V  5.00V
```

It bottoms at **10.0 mV**, not 20.0 mV, and the top of every probe's list is
5 V x scale — measured as 50.0V at x10, 500V at x100, 50.0kV at x10000, 100V at
20:1, 50.0A on CC65(1mV/10mA) and 5.00kA on CC650(1mV/1A). Under a clamp the
same nine positions read in amps.

**Time/DIV: 32 entries, 2.000 ns to 50.00 s, 1-2-5 throughout.**

```
2ns   5ns   10ns  20ns  50ns  100ns 200ns 500ns
1us   2us   5us   10us  20us  50us  100us 200us
500us 1ms   2ms   5ms   10ms  20ms  50ms  100ms
200ms 500ms 1s    2s    5s    10s   20s   50s
```

The list starts at 2.000 ns, so **wire code = UI index + 1** — not +2, which the
earlier note claimed on the strength of a list that began at 5 ns. The corrected
mapping is confirmed at every preset whose code was captured: 5.000ms is UI
index 19 and goes out as `a3 14` (20); 1.000ms is index 17 -> 18; 10.00ms index
20 -> 21; 500.0ms index 25 -> 26; 1.000s index 26 -> 27.

Codes 24 and above (100ms/div and slower) are the ones whose burst accumulators
this driver's table zeroes, and three of the twelve presets sit there, so those
setups are ROLL mode, not burst.

## The startup chooser: twelve automotive setups

The dialog the OEM opens before its main window is not just a module picker — it
is a list of task PRESETS. Expanded in full, with the row index each leaf takes
in the fully expanded tree:

```
 0 Module
 1 +- Vehicle Diagnosis
 2 |  +- Ignition            3 Primary        4 Secondary
 5 |  +- Sensors             6 Air Flow Meter 7 Camshaft    8 Crankshaft
   |                         9 Distributor   10 Lambda     11 Throttle Position
12 |  +- Bus Diagnosis       (a leaf itself)
13 |  +- Engine             14 Injector Diagnonis  ->  15 Petrol  16 Diesel
17 |  +- Startup & Charge   18 Charging Circuits
19 +- Oscilloscope
20 +- Generator
```

WHERE THE PRESETS LIVE. Not in data files. The install has only three .set files
— lan.set, size.set and Default.set, none of them per-task — and Auto/wfm holds
87 .amr files, which are HTK-DSO-AM REFERENCE WAVEFORMS (UTF-16 magic then
float samples), not setups. The per-task configuration is compiled into
Scope.exe.

HOW THESE WERE READ. Entering each preset in the OEM and reading its screen.
Two traps, both of which produced wrong tables before they were noticed:

- **A preset is only entered by CLICKING its row.** Arrowing down to it moves
  the tree's highlight but the app commits the row that was last clicked, so
  keyboard navigation entered the default module instead. The giveaway is the
  centre title: it says "Oscilloscope" rather than the preset's own name.
- **A preset does not set every field.** The OEM restores its last session from
  Default.set at startup, and anything the preset leaves alone is inherited.
  Air Flow Meter sets volts/div but NOT the probe, so run behind Secondary
  (x10000) it read 20.0kV and behind Common Rail Diesel (a clamp) it read 20.0A
  — the same underlying 2.00V each time. The table below was taken with
  Default.set overwritten by a fixed x1 baseline before every launch, so what it
  records is what a preset actually gives you from a clean start.

The trigger is CH1 rising at 0.00uV and the acquisition mode is Auto in all
twelve; only the columns below differ.

| Preset (tree) | Title the OEM shows | Time/div | Channels | Volts/div | Probe | Coupling |
|---|---|---|---|---|---|---|
| Primary            | Primary Ignition (Voltage)                | 1.000ms | CH1     | 100V           | 20:1            | DC |
| Secondary          | Secondary Ignition Distributor Type       | 1.000ms | CH1     | 1.00kV         | x10000          | DC |
| Air Flow Meter     | Air Flow Meter (Hot Wire)                 | 500.0ms | CH1     | 2.00V          | x1              | AC |
| Camshaft           | Camshaft (Inductive)                      | 10.00ms | CH1     | 1.00V          | x1              | AC |
| Crankshaft         | Crankshaft Inductive Running              | 5.000ms | CH1     | 2.00V          | x1              | AC |
| Distributor        | Distributor Pick-up (Hall Effect)         | 10.00ms | CH1     | 2.00V          | x1              | DC |
| Lambda Sensors     | Lambda Sensor Titania                     | 1.000s  | CH1     | 2.00V          | x1              | DC |
| Throttle Position  | Throttle Position Potentiometer           | 200.0ms | CH1     | 1.00V          | x1              | DC |
| Bus Diagnosis      | CAN Bus Data View                         | 200.0us | CH1 CH2 | 1.00V each     | x1              | DC |
| Petrol             | Single-point Injector(Voltage)            | 1.000ms | CH1     | 10.0V          | 20:1            | DC |
| Diesel             | Common Rail Diesel(Current)               | 20.00ms | CH1-CH4 | 5.00A each     | CC65(1mV/10mA)  | DC |
| Charging Circuits  | Charging Circuits Current/Voltage         | 50.00ms | CH1 CH2 | 20.0A / 5.00V  | CC650(1mV/1A) / x1 | AC / DC |

Three of them — Air Flow Meter, Throttle Position and Lambda Sensor — sit at
100ms/div or slower, which is past the burst codes entirely. Those are roll-mode
setups.

### Cross-checking the table against the OEM's own setup file

Default.set is rewritten when a preset loads, so a copy taken at that moment is
the OEM's own record of what it just applied. Diffing the twelve confirms the
table independently of anything read off a screen:

| offset | field | evidence |
|--------|-------|----------|
| 0x0028 | CH1 volts/div, index into the nine-step ladder | 08 = 5V for Primary (x20 -> 100V), 03 = 100mV for Secondary (x10000 -> 1.00kV), 01 = 20mV for Charging (x1000 -> 20.0A) |
| 0x002a | CH1 coupling, 1 = AC | set for exactly the four presets whose footer shows the AC symbol |
| 0x002c | CH1 probe, index into the twelve-entry list | 05 = 20:1, 04 = x10000, 06 = CC65(1mV/10mA), 09 = CC650(1mV/1A) |
| 0x0042 / 0x0046 / 0x004a | CH2 enable / volts-div / probe | set only for Bus, Diesel and Charging, the three with a second channel |

Every one of the twelve reconciles: displayed = ladder[index] x probe scale, in
the probe's unit. It also recovers Secondary's coupling, which the screen could
not give because that preset draws no channel footer.

The volts/div index is 0-BASED over the nine-entry ladder that starts at 10.0mV.
A 1-based reading of an eight-entry ladder starting at 20.0mV fits every index
these presets use and is wrong only at index 0, which none of them selects — the
swept ladder is what settles it.

Two fields are NOT established. 0x0074 holds the timebase code for nine of the
twelve — matching the ladder mapping exactly, including the `a3 14` captured on
the wire for Crankshaft — but reads 0 or 0xfb for Bus, Diesel and Charging, the
three multi-channel presets. And the CH3/CH4 offsets rest on Diesel alone, which
is a single data point. Neither is safe to build on without more captures.
