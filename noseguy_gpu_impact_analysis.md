# GPU Impact Analysis of `noseguy-wayland` / sway Wayland Screensaver

## Goal

Use this document as a prompt/context for an LLM to help reduce the GPU and power impact of a Wayland screensaver based on XScreenSaver Noseguy.

The measurements came from:

```bash
sudo intel_gpu_top -p -c -o nose.csv -n 40
```

During capture:
- the system was idle for about 6 seconds,
- then the screensaver program was started,
- it ran for about 20 seconds,
- then it was stopped and the system returned to idle.

---

## Executive Summary

The program has a **clear and significant GPU impact while running**.

Observed behavior strongly suggests that the screensaver performs **continuous rendering** and keeps the GPU awake almost the whole time instead of allowing it to enter deep idle states.

### Main conclusions

- While the program runs, the **render engine (RCS/0)** is heavily used.
- GPU frequency rises and stays around **~213 MHz average** during the run.
- GPU power rises from almost-idle baseline to about **~2.76 W average**.
- Package power rises to about **~15.17 W average** during the run.
- **RC6** almost disappears while the program runs, which means the GPU spends very little time in deep idle.
- The workload is steady, not bursty, which suggests a **constant redraw loop**.
- There is no meaningful activity in **BCS**, **VCS**, or **VECS**, so this is not video decode/encode or copy-heavy work. It is mainly a rendering workload.

---

## Timeline Interpretation

The CSV can be divided into three rough phases:

1. **Idle before start**: rows 1-5  
2. **Program running**: rows 6-25  
3. **Idle after stop**: rows 27-39  

Row 0 appears to contain startup noise / unrelated activity and should be ignored.

---

## Measured Averages

## Idle before start

Approximate averages:
- **RCS/0**: ~0%
- **GPU frequency**: very low / idle
- **GPU power**: ~0.024 W
- **Package power**: ~5.84 W
- **RC6**: ~96%

Interpretation:
- GPU is essentially idle.
- GPU enters deep idle state most of the time.

## Program running

Approximate averages:
- **RCS/0**: ~69%
- **GPU frequency**: ~213 MHz
- **GPU power**: ~2.76 W
- **Package power**: ~15.17 W
- **RC6**: ~1.85%
- **Interrupt rate**: ~217 IRQ/s

Interpretation:
- The render engine is busy most of the time.
- The GPU is kept awake almost continuously.
- Rendering appears to be continuous and regular.
- The application likely redraws even when only small changes occur.

## Idle after stop

Approximate averages:
- **RCS/0**: ~0%
- **GPU power**: ~0.054 W
- **RC6**: ~92%

Interpretation:
- Once the application stops, the GPU quickly returns to deep idle behavior.
- That strongly confirms the screensaver is the cause of the load.

---

## What the Data Suggests About the Program

The current behavior is consistent with one or more of the following:

- an uncapped render loop,
- repainting every frame regardless of whether anything changed,
- immediate rescheduling of the next frame callback forever,
- redrawing the entire output when only a small sprite region changes,
- too high target FPS for a simple screensaver,
- blocking the GPU from entering idle by keeping a steady flow of render work.

In short, the program behaves more like a continuously animated compositor client than a lightweight screensaver.

---

## Why This Matters

For a simple sprite-based screensaver, this level of sustained render-engine usage is higher than expected.

Effects seen in the data:
- unnecessary GPU activity,
- unnecessary power consumption,
- likely increased battery drain on laptops,
- more heat,
- reduced ability of the system to enter low-power states.

---

## Meaning of the Intel GPU Counters

## RCS / RCS/0

**RCS** usually means **Render Command Streamer**.  
This is the main **3D / rendering engine** of the Intel GPU.

If **RCS/0** is high, the GPU is busy doing rendering work such as:
- drawing,
- compositing,
- OpenGL / EGL rendering,
- shader execution,
- texture operations related to drawing.

In this capture, **RCS/0** is the only engine showing significant activity, so the screensaver is primarily creating a render workload.

## RC0

**RC0** means the GPU is in an **active working state**.

When people talk about Intel GPU power states:
- **RC0** = GPU active / awake / doing work
- **RC6** = GPU deep idle / low-power state

So if RC0 behavior is dominant, the GPU is spending most of its time awake and active.

Note: some `intel_gpu_top` outputs do not show RC0 explicitly as a separate percentage, but conceptually it is the opposite of deep idle.

## RC6

**RC6** is a **deep power-saving idle state** for the GPU.

If **RC6** is high:
- the GPU is idle much of the time,
- power consumption is low,
- the workload is light or bursty enough to let the GPU sleep.

If **RC6** is very low:
- the GPU is being kept awake,
- something is submitting work constantly or too frequently.

In this capture:
- idle state had **~92-96% RC6**
- running state had **~1.85% RC6**

That is a very strong sign that the application prevents the GPU from going idle.

## BCS

**BCS** means **Blitter Command Streamer**.

This engine is typically used for:
- copy operations,
- blits,
- memory transfer type operations on the GPU,
- some image movement / copy work.

If BCS were high, it would suggest the application is doing lots of buffer copies or transfer-heavy work.  
In this capture, **BCS is effectively inactive**, so copying is not the main problem.

## VCS

**VCS** means **Video Command Streamer**.

This engine is usually used for:
- video decode,
- video-related processing,
- media pipeline operations.

If VCS were high, it would suggest video playback or decode-like work.  
In this capture, **VCS is inactive**, so the workload is not video decode related.

## VECS

**VECS** means **Video Enhancement Command Streamer**.

This engine is used for:
- video enhancement,
- media post-processing,
- scaling / enhancement operations in some media workflows.

If VECS were high, it would suggest media enhancement/post-processing.  
In this capture, **VECS is inactive**, so that is not part of the observed load.

---

## Plain-English Interpretation for Another LLM

The application appears to render continuously and keeps the Intel GPU in an active rendering state nearly all the time while it runs.

The render engine (RCS/0) averages about 69% utilization during execution, while RC6 drops from roughly 92-96% at idle to roughly 1.85% during the run. GPU power rises from near-idle baseline to around 2.76 W average, and total package power rises to roughly 15.17 W.

This strongly suggests the program is issuing render work continuously, likely through an uncapped or over-eager animation loop. The data does not indicate significant copy-engine or video-engine activity; the problem is mainly regular rendering/compositing work.

The best optimization targets are likely:
- reducing redraw frequency,
- capping FPS,
- only drawing when animation state changes,
- avoiding full-surface redraws when only a small sprite area changes,
- avoiding immediate endless frame callback chains,
- reducing texture uploads or other per-frame overhead.

---

## Specific Optimization Directions to Investigate

Ask the LLM to inspect the code for these patterns:

### 1. Uncapped render loop
Look for:
- `while (1)` render loops,
- frame rendering with no sleep,
- immediate redraw after present,
- frame callback that always schedules the next frame with no timing control.

Desired fix:
- cap animation to **10-15 FPS** first,
- maybe 20 FPS if animation quality requires it,
- do not render at monitor refresh rate unless truly necessary.

### 2. Redrawing when nothing changed
Look for:
- drawing the whole frame every iteration,
- submitting frames even when sprite position/text has not changed.

Desired fix:
- only redraw when animation state changes,
- skip rendering when the scene is unchanged.

### 3. Full-surface redraws
Look for:
- clearing and repainting the whole screen for tiny sprite movement,
- unnecessary recomposition of static layers.

Desired fix:
- damage only changed regions if the rendering path supports it,
- cache static content,
- update only sprite rectangles / changed text regions where possible.

### 4. Texture uploads or expensive conversions every frame
Look for:
- recreating textures each frame,
- re-uploading the same sprite/image data every frame,
- repeated format conversion.

Desired fix:
- upload sprites once,
- reuse GPU resources,
- keep frame-time CPU/GPU work minimal.

### 5. Wayland frame callback misuse
Look for:
- immediate chaining of frame callbacks with no animation pacing,
- scheduling a new frame every compositor tick regardless of scene changes.

Desired fix:
- use frame callbacks as pacing help, not as a reason to repaint forever at full refresh,
- combine callbacks with explicit animation timing.

### 6. Excessive alpha blending / scaling / effects
Look for:
- unnecessary blending,
- scaling every frame,
- expensive compositing for simple 2D sprites.

Desired fix:
- precompute where possible,
- avoid repeated scaling,
- simplify shader/effect path if present.

---

## Concrete Performance Target

A good target for this kind of screensaver would be:

- much lower average **RCS/0** than ~69%,
- significantly higher **RC6** while animation runs,
- noticeably reduced GPU power,
- stable animation at a modest frame rate,
- no full-refresh rendering loop unless absolutely necessary.

For a simple sprite-based saver, the goal should be to make the workload look **light and bursty**, not **constant and steady**.

---

## Ready-to-Use Prompt for Another LLM

```text
I have a Wayland/sway screensaver based on XScreenSaver Noseguy. I measured its GPU impact with intel_gpu_top and found the following:

- idle before start: GPU power ~0.024 W, RC6 ~96%, RCS/0 ~0%
- while running: RCS/0 ~69%, GPU frequency ~213 MHz, GPU power ~2.76 W, package power ~15.17 W, RC6 ~1.85%
- idle after stop: GPU power ~0.054 W, RC6 ~92%, RCS/0 ~0%

BCS, VCS, and VECS are effectively inactive. Only RCS/0 is significantly loaded.

Interpretation:
- the app appears to keep the render engine busy continuously,
- it prevents the GPU from entering deep idle,
- the workload is steady rather than bursty,
- this suggests an uncapped or over-eager render loop,
- likely causes include rendering every frame, repainting when nothing changed, redrawing the whole surface, immediate frame callback chaining, or unnecessary per-frame texture work.

Please review the rendering/animation loop and propose code-level optimizations to reduce GPU usage and power draw on Intel GPUs under Wayland.

Focus especially on:
- capping FPS to 10-15 initially,
- only redrawing when animation state changes,
- reducing full-surface redraws,
- proper use of Wayland frame callbacks,
- avoiding repeated texture uploads or conversions,
- simplifying compositing for a small 2D sprite-based screensaver.

Also explain which code changes are likely to reduce RCS/0 usage and increase RC6 residency.
```

---

## Final Bottom Line

The measurements strongly indicate that the screensaver is **continuously exercising the Intel render engine** and **preventing deep GPU idle**. The primary optimization direction is to make rendering **less frequent, more conditional, and more localized**.
