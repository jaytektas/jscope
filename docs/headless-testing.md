# Running jscope headless, and looking at it

The app needs Vulkan, and `JAppWindow` selects it unconditionally — an app
cannot ask the framework for its software backend, and this app does not edit
JFramework. That looked like it ruled out a GPU-less display. It does not:
**Mesa's lavapipe is a software Vulkan ICD**, and forcing it is enough.

    Xvfb :99 -screen 0 1920x1080x24 &
    DISPLAY=:99 VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json ./build/jscope &
    DISPLAY=:99 import -window root shot.png      # ImageMagick
    DISPLAY=:99 xdotool ...                       # synthetic input

Verified on this machine: the full UI renders, the 1008C is driven over USB as
normal, and a screenshot is a faithful copy of what the physical display shows —
the miter-spike ghosting in JTracePainter was found and fixed entirely this way,
having been impossible to catch by hand because it needed a screenshot of a
*running* acquisition.

## Measuring instead of squinting

Rendering bugs are much easier to settle with a number than with an opinion.
`isolated trace-coloured pixels` — pixels of the trace colour with no
four-neighbour of the same colour — is zero for any correctly stroked polyline
and non-zero for sub-pixel spikes and stipple, which is what caught the miter
ghosts.

## One hazard, learned the hard way

**Do not `pkill` jscope while it is mid-transaction with the 1008C.** The device
re-enumerates at a new bus address, the app's stored path no longer resolves, and
the next start falls back to the synthetic driver — which silently invalidates
any A/B comparison you were in the middle of. Close it from the UI, or expect to
restart twice and check `session open` in the log before trusting a capture.
