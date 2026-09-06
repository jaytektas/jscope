
## The pattern generator

Eight digital outputs playing a repeating pattern. It is a crank/cam simulator,
not a function generator: there is no amplitude, no offset and no sine. The OEM
gives it a top-level module of its own ("Generator", row 20 of the startup tree).

### Wire format

Every generator write begins with `b7 00`. Its meaning is unknown; the reference
sends it before each of them and it is reproduced for the same reason the rest of
this protocol is — the sequence is only known to work as a whole.

| Command | Bytes | Meaning |
|---|---|---|
| Output on/off | `b7 00`, `bb 08 01` / `bb 08 00` | drivers on or off |
| Speed | `b9 01 <pulse:u32 LE>` | ticks per step |
| Pattern | `b7 00`, `bf <len:u16 LE>`, `b8 01 <62 bytes>` | length, then the steps |

The pulse length is **little-endian**, where the trigger level and record length
on the same wire are big-endian. That is not a mistake in either place; this
device simply is not consistent, and each command's byte order is the one
observed for it.

`b8` always carries a full 62 bytes whatever `bf` said, zero padded. Sending a
short payload leaves the tail of the previous pattern in the device.

### Steps, and why 62 rather than 1440

One byte is one **step**, and its bit *i* drives output *i* — so a step is a
snapshot of all eight lines at one instant, not a sample of one waveform.

The device holds 1440 steps, but a pattern is written in a single command and a
command must fit one 64-byte packet: `b8` + `01` + 62 steps. Whatever chunking
would reach the remaining 1378 has never been observed on the wire, so the driver
publishes 62 as its maximum. A capability that overstates what works is worse
than a modest one, because the UI builds itself from it.

### Speed is a divisor, so most speeds are not reachable

One pass of the pattern is one revolution, which is what makes RPM the natural
unit and is how the OEM presents it.

    steps x pulseLength ticks = one revolution
    rpm = 60 x 48e6 / (steps x pulseLength)

48 MHz is not a guess. The reference computes `(8 * 360e6 / steps) / rpm`, and
`8 * 360e6 / 60` is exactly `48e6`; the identity `pulseLengthFor(300000, 8) == 1200`
is asserted in both the reference and our tests.

`pulseLength` is whole ticks, so the achievable speed is a floor of the requested
one — **and it moves when the pattern length changes**, because the same speed
needs a different pulse when a revolution is made of more steps. This is why the
OEM shows "Set Speed" and "Real Speed" as two separate readouts, and why this
application does too. At idle the rounding is below 1 rpm and invisible; by
100000 rpm a pulse is a few hundred ticks and the wheel decides what is reachable.

### Above 750000 rpm

The reference notes the first parameter byte of `b9` becomes `02` and the
encoding changes in a way it never worked out. Refused rather than guessed.

### What the OEM's Generator module offers

Read from `Lan_English.lug`: Output ON/OFF, "Set Speed" and "Real Speed",
"Edit Point", "StartLevel", "Pulse", "Download" (send the pattern), "Default Set".
So the OEM edits the pattern point by point and shows both speeds. This
application takes the same two speeds but asks for a **wheel** — tooth count,
missing teeth, and which outputs carry it — because a raw editor over 62 steps
and 8 lines is nearly 500 switches, and what people arrive wanting is a crank
signal. See `JCrankWheel`.
