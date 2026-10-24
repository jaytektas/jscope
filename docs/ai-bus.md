# Driving jscope over the AI bus

jscope can be driven by a script — or by an AI assistant at the bench — through
JFramework's AI bus. Everything a person can do in the window is a line of text:
set the channels, the timebase and the trigger, run and stop, read the
measurements, pull the edge times off a channel, record, replay, move the
cursors, show and hide the docks, choose the instrument.

The bus is **off unless asked for**. With it off, jscope behaves exactly as it
always has and there is nothing listening.

    ./jscope --ai-bus                 # or: JF_AI_BUS=1 ./jscope

A client talks to it with `jf-busctl`, which ships with JFramework (and inside
jayecu's studio AppImage):

    jf-busctl call help
    jf-busctl call status
    jf-busctl call "channel 5 on vdiv=2"
    jf-busctl call "edges 5 6 level=2.5"

The generic part of the bus — the widget tree (`jf-busctl tree`), focus and
click by id — works as it does for any JFramework application. `call` is
jscope's own: the verbs below.

## Answers

Every answer is JSON, beginning `{"ok":true` or `{"ok":false,"error":"..."}` —
the same shape jayecu's studio answers in, so one client reads both.

**A setting answers with what the instrument applied, not with what was asked
for.** Every edit goes through `JScopeActions`, the path the controls use: the
driver quantises it to a step it really has, and the reply is the read-back. Ask
for 0.037 V/div and the answer says 0.05. A refusal says why, in the words the
window would have used (`"this device has no force trigger"`).

Channels are numbered as the screen labels them, **1 to 8**; `5`, `ch5` and
`CH5` all mean the fifth.

## The verbs

| Verb | Syntax | What it does |
|---|---|---|
| `help` | `help` | Every verb with its syntax. |
| `status` | `status` | The device, the run state, the timebase, the trigger, every channel's settings, the generator, replay position, the last frame (sequence, sample interval, length, trigger point) and whether it is recording. |
| `caps` | `caps` | What the open instrument can do: volts/div, s/div and stream-rate steps, couplings, probe ratios, offset range, trigger modes, acquisition modes. |
| `channel` | `channel <n> [on\|off] [vdiv=V] [offset=V] [coupling=dc\|ac\|gnd] [probe=X] [invert=on\|off] [bwlimit=on\|off]` | The Channels panel. |
| `timebase` | `timebase [sdiv=S] [mode=windowed\|streaming] [length=N] [rate=Sa/s] [pos=0..1]` | The Timebase panel. `pos` is where the trigger sits in the record. |
| `trigger` | `trigger [mode=auto\|normal\|single] [slope=rising\|falling\|either] [source=n] [level=V] [pos=0..1] [timeout=S]` | The Trigger panel; `timeout` is how long Auto waits. |
| `run` `stop` `single` `force` `autoset` `toggle` | — | The Acquire menu. Autoset runs on the device worker; `status` shows when it has finished. |
| `mode` | `mode windowed\|streaming` | Acquisition mode. |
| `options` / `option` | `options` · `option <id> <value>` | The Instrument menu: the instrument's own settings, in its own words. |
| `generator` | `generator [on\|off] [rpm=N]` · `generator load <path.squ>` · `generator save <path.squ>` · `generator download` | The pattern generator and the Generator menu. |
| `measure` | `measure [n...] [whole]` | Vpp, Vmax, Vmin, Vavg, Vrms, Top, Base, Ampl, Freq, Period, Duty, Rise, Fall for each channel, from the frame on display — between the X cursors when they are on, as the Measure panel does, unless `whole`. |
| `edges` | `edges <n...> [level=V] [hyst=V] [polarity=rising\|falling\|both] [from=S] [to=S] [ref=trigger\|start] [max=N] [file=path]` | Where each channel crosses a level, interpolated between samples. See below. |
| `frame` | `frame` · `frame save <path.csv>` · `frame samples <n> [first] [count] [step]` | The frame on display: its header, the whole record as CSV, or one channel's samples in volts. |
| `export` | `export [path.csv]` | File > Export CSV — the cursor window when the X cursors are on. |
| `record` | `record start [path]` · `record stop` · `record status` | File > Start/Stop Recording, to a `.jscope` capture. |
| `capture` | `capture open <path.jscope>` | Open a capture for replay (closes the instrument). |
| `replay` | `replay [status\|play\|pause\|seek N\|step N\|rate X\|loop on\|off]` | The replay transport. |
| `devices` | `devices` | Instruments on the bus now, indexed. |
| `open` | `open <index\|driverId>` | Connect to one. Asynchronous — bringing the 1008C up takes about twenty seconds; poll `status`. |
| `cursors` | `cursors [x=on\|off] [y=on\|off] [x1=S] [x2=S] [y1=V] [y2=V] [ych=n] [reset]` | The cursors, and their deltas and frequency. |
| `view` | `view [reset] [start=F] [span=F] [position=n:V] [reference=name]` | Zoom (as fractions of the record), each trace's vertical position, and the reference trace (any prefix of its name; `reference=` takes the rest of the line). |
| `docks` / `dock` | `docks` · `dock <title> on\|off` | The View menu's panels. |
| `screenshot` | `screenshot` | The next rendered frame to `/tmp/jscope_shot_<surface>.ppm`. |
| `update` | `update` | Help > Check for Updates. |
| `quit` | `quit` | Close the window; the setup is saved as on any close. |

## Edges

`edges` is the reason the bus exists. A conditioned trigger signal — a VR crank
or cam through an interface chip, a Hall sensor — is judged by *where its edges
sit*, and reading that off a screen with cursors is slow and imprecise.

    jf-busctl call "edges 5 6"

For each channel it finds the levels (the histogram top and base), crosses at the
midpoint with 10 % of the amplitude as hysteresis unless `level=` and `hyst=`
say otherwise, and answers:

- `rising`, `falling`: how many edges of each;
- `rising_interval`, `falling_interval`: count, mean, min and max of the time
  between successive edges of the same direction — a missing-tooth wheel's gap
  shows as the max;
- `high_time`: each rising edge to the falling edge after it;
- `edges`: the edges themselves, `[time, "r"|"f"]`, up to `max=` (2000 by
  default; `max=0` lists none).

Times are seconds from the trigger point when the frame triggered (`ref=start`
for the start of the record), and `from=` / `to=` keep a span of them. `file=`
writes every edge as CSV (`channel,time_s,edge`) whatever `max=` says.

## Threading

The bus services one action per rendered frame, on the main thread, inside the
render loop. A verb reads the session's latest frame, which the main thread
owns, and changes settings through `JScopeActions` like any button. Nothing
reaches the acquisition thread.

## Where it lives

- `src/app/JScopeBusCommands` — the verbs. Headless: tested against the
  synthetic driver by `tests/test_bus_commands.cpp`.
- `src/app/JScopeBusHooks.h` — what only the window can do, as callbacks.
- `src/app/JScopeAiBus` — binds those to `JScopeApp` and answers `call`.
