# varx9 fork fixes

Fixes on [varx9/moonlight-qt](https://github.com/varx9/moonlight-qt).

Investigated and implemented with assistance from **Grok 4.5** (xAI).

## Branches

| Branch | Base | What it fixes |
|--------|------|----------------|
| [`fix/brightness-plvk-sdr`](https://github.com/varx9/moonlight-qt/tree/fix/brightness-plvk-sdr) | upstream `master` | Over-bright / over-saturated SDR with Vulkan + libplacebo |
| [`fix/audio-hdmi-reopen`](https://github.com/varx9/moonlight-qt/tree/fix/audio-hdmi-reopen) | upstream `master` | Silence after HDMI/DP audio sink teardown until restart |
| [`fix/varx9-master`](https://github.com/varx9/moonlight-qt/tree/fix/varx9-master) | upstream `master` | **Both** of the above |

Each single-fix branch is one purposeful commit on current upstream master (plus this documentation where present).

---

## Brightness: over-bright SDR (Vulkan / libplacebo)

### Symptom

On Linux (observed: **Nvidia + Wayland**), builds of post-6.1.0 **master** that select:

```text
Using Vulkan video decoding
Renderer 'Vulkan (libplacebo)' chosen
```

can look **over-bright and over-saturated** compared to Arch’s **6.1.0** package or the same build forced onto VAAPI+EGL (`FORCE_VAAPI=1`).

### Cause (summary)

1. PlVk requested **full** color range and always forced `PL_COLOR_LEVELS_FULL` (old AMF AV1 workaround), which interacted badly with host encode / client interpretation.
2. SDR BT.709 used a **BT.1886**-oriented swapchain path; desktop displays are typically **sRGB**. Hinting sRGB for SDR BT.709 matches VAAPI+EGL brightness.

### Fix (`plvk.cpp`)

- Request **limited** color range (like other renderers).
- **Trust** bitstream / `pl_map_avframe` levels (no blanket force-full).
- For **SDR BT.709**, hint an **sRGB** swapchain (HDR left on the frame’s native colorspace).

### Build / run

```bash
git clone https://github.com/varx9/moonlight-qt.git
cd moonlight-qt
git checkout fix/brightness-plvk-sdr
git submodule update --init --recursive
qmake6 moonlight-qt.pro CONFIG+=release
make -j"$(nproc)"
./app/moonlight
```

### Verify

- Stream the same non-HDR desktop/app on this branch vs stock 6.1.0 or `FORCE_VAAPI=1` on unpatched master.
- Logs should still be able to show Vulkan/libplacebo when that path is chosen; brightness should match the good reference.

---

## Audio: HDMI/DP sink reopen

### Symptom

During a stream, power-cycling a monitor or otherwise destroying the **default HDMI/DP audio sink** leaves Moonlight silent until the app is restarted.

### Fix

- Detect `SDL_AUDIO_STOPPED` and PipeWire/Pulse **queue starvation** (device still “playing” but not draining).
- Handle `SDL_AUDIODEVICEREMOVED` / `SDL_AUDIODEVICEADDED` to schedule reopen.
- Recreate the SDL audio renderer on the **new** default device with exponential backoff.

### Build / run

```bash
git checkout fix/audio-hdmi-reopen
git submodule update --init --recursive
qmake6 moonlight-qt.pro CONFIG+=release
make -j"$(nproc)"
./app/moonlight
```

### Verify

1. Start a stream with audio through HDMI/DP.
2. Power-cycle the monitor or remove the sink so the default output disappears and returns.
3. Audio should return without restarting Moonlight.

---

## Combined branch

```bash
git checkout fix/varx9-master
```

Contains both master-based commits for a single tip with audio + brightness fixes.

---

## Upstream

These branches are for sharing and testing. Opening PRs against [moonlight-stream/moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) is optional and separate.
