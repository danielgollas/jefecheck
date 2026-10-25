# Neural keyer (CorridorKey) — design

**Date:** 2026-10-17, revised 2026-10-24 for CorridorKey 2.0
**Status:** draft, for review
**Upstream:** CorridorKey 2.0 by Niko Pueringer, Zachary Harris / Corridor Digital — https://github.com/nikopueringer/CorridorKey2.0 (v1: https://github.com/nikopueringer/CorridorKey)
**First step:** local extraction — a worker on the same machine writes keyed EXRs to disk; JefeCheck reads them. Sharing keyed frames with remote participants comes later (shared storage today; P2P media transfer is designed, not built).

## Goal

Key a green- or blue-screen plate in JefeCheck with CorridorKey's neural unmixing: a plate range in (with an optional hint), a linear alpha matte and a clean foreground out. Two ways in — key a range, and a keyed node on a plate — both served by one cache, so nothing is ever computed twice.

## Non-goals

Training or fine-tuning. Shipping the weights. Realtime keying of uncached frames. Windows and Linux in the first version (the design keeps the door open; see Runtime).

## Why this is not a `.jfx`

JefeCheck's FX plugins are GLSL: `gfcFX::load` reads a `.jfx` and compiles `.vert`/`.frag` into the plate's shader chain (`src/gfcfx.{h,cpp}`). A neural keyer has no shader form — it needs a tensor runtime, weights and several hundred MB of VRAM. So this adds a second kind of FX node rather than another shader.

## Architecture

```
optional hint: annotation strokes or hint track ──┐
plate range (frame directory) ────────────────────┼──> keyer worker (separate process)
                                                  │    CorridorKey 2.0: CUDA upstream,
                                                  │    Apple Silicon via an MPS port
                                                  v
                                             key cache (EXR per frame)
                                                  │
                             ┌────────────────────┴────────────────────┐
                             v                                         v
                        Key Range…                               Keyed FX node
                (fills the cache, optionally              (playback, cache only;
                 exports EXRs elsewhere)                    a miss shows the plate)
```

**The worker is a separate process**, discovered the way FFmpeg already is (`$JEFECHECK_CORRIDORKEY` → QSettings → bundled → PATH; see `developer_notes.md` §19). It wraps upstream `infer.py`: a job is a plate range (a frame directory), an optional hint, an output directory and parameters; it writes an EXR sequence. Reasons, in order of weight:

1. **Licence.** CorridorKey 2.0's licence (see Licence notes) requires anything built on the code or model to stay free, under the same licence, keeping the CorridorKey name; JefeCheck is GPL v2. Invoking a separate program, as the app already does with FFmpeg, keeps the two apart. The weights (~5 GB) are downloaded by the user, never shipped.
2. **Runtime.** Upstream is Python (PyTorch 2.9). No ONNX export exists upstream, so there is nothing to link against today.
3. **Blast radius.** A model that exhausts VRAM kills the worker, not the session.

**Everything goes through the cache.** A keyed frame is written once and reused by all three entry points. The live FX node never blocks on inference: a hit draws, a miss draws the raw plate and queues a job.

## The key cache

`<AppDataLocation>/keys/<cacheId>/<frame>.exr`, with `<cacheId>` the SHA-1 of:

- the model id and version (so a new model does not collide with the old one),
- the media's `fp1:` fingerprint (`gfcMediaFingerprint` — already built, already stored in the notes sidecar),
- the hint's content hash,
- the keyer parameters.

Two frames of the same plate with the same hint share a cache id; changing the hint makes a new one. `--config-dir` redirects it, like the package cache. **File → Clear Keyer Cache…** reports size and empties it, reusing the `packageCacheEntries`/`clearPackageCache` shape added for review packages (only directories named like a cache id are measured or removed).

## The hint

**Optional in v2.** The model keys with no hint; a first-frame hint (`--hint_first_frame`) or a per-frame hint video (`--hint_video`) steer it when it guesses wrong. The first version ships without a hint UI. When added, either source, because both are cheap:

- **Annotation strokes.** The JEF-39 note overlay already rasterises strokes and boxes for EXR stamping; the same rasteriser produces a black-and-white hint. Drawing a rough hint is then an existing, tested gesture.
- **A hint track.** The FX panel's texture-source picker already offers Track A–D; a matte made elsewhere feeds straight in.

The hint is written to the job's temp directory as PNG, and its bytes are hashed into the cache id.

## Entry points

| Entry | Behaviour |
|---|---|
| **Key Range…** | The Render dialog's incremental pattern (`QTimer::singleShot(0)` per frame, progress, cancel): writes EXRs with alpha to a chosen directory, filling the cache as it goes. Cancel leaves no partial frame. |
| **Keyed FX node** | A plate FX that draws the cached keyed frame when there is one, the raw plate when there is not, and queues the miss. Playback never stalls. |

## Runtime, first version

**v2 works on clips, not frames.** It reasons over 80-frame windows (40-frame stride, `--hann_chunk`/`--hann_stride`), so the unit of work and of caching is a range. "Key This Frame" from the v1 draft is dropped; a short range around the current frame serves the same purpose.

**Upstream is CUDA-only.** The open-source runtime hard-codes `cuda` (~30 sites in `corridorkey/runtime.py`, device defaults in `model.py`/`foundations.py`/`rvm.py`, `torch.autocast("cuda", bfloat16)`, `torch.cuda.mem_get_info`). The paid OpenFX plugin runs on Apple Silicon; the free code does not. The model itself is plain PyTorch — attention is `F.scaled_dot_product_attention`, no Triton, flash-attn or custom kernels — so an MPS port is mechanical: a device helper replacing the hard-coded calls. That port lives in a separate fork under the CorridorKey licence (offered upstream), never in this repo.

**Order:** (1) NVIDIA Windows/Linux with upstream as-is; (2) Apple Silicon via the MPS port, if it runs at a usable speed on a 16 GB M3 (`--low_vram`, `--model_scale` below 1.0 likely needed). The worker contract does not change between them.

## Colour

CorridorKey returns a **straight** foreground and a **linear** alpha. Input is display-coded sRGB by default; `--frame_dir_linear` takes scene-linear EXR frames, and `--cutout_linear` writes a linear cutout (`--output_format exr`, `--exr_dtype half`). JefeCheck's pipeline is linear. The worker writes a premultiplied linear EXR, and the conversion happens there, once, rather than in the GL path.

## Testing

- `NOTE-KEYCACHE` (pure, in `--notes-test`): cache id composition, hint hashing, only-cache-id directories measured or removed, a missing root measuring zero.
- `--key-test <image>`: a synthetic green plate and a box hint; asserts a cache miss runs the worker, a hit does not run it again (counted), the output has a non-opaque alpha somewhere, and a changed hint produces a different cache id. **Skips with a clear message when no worker is installed**, so the suite stays green on a machine without CorridorKey.
- Worker-missing, worker-crash and cancel paths each report rather than hang.

## Open questions

1. **Python dependency.** The worker needs a Python environment with PyTorch, CorridorKey and ~5 GB of weights (CorridorKey 2.66 GB, NVIDIA C-RADIOv4-SO400M 1.73 GB, MoGe-2 0.42 GB, RVM 0.27 GB). Bundle a venv in the release, or require the user to install it and point JefeCheck at it?
2. **Frame times.** Upstream publishes none. Until measured, "Key Range" is an overnight tool, not an interactive one, and the live node is cache-only.
3. **VRAM.** 4 GB at 1440p, 6 GB minimum at 4K upstream (tested on RTX 30-series and newer). On a shared-memory Mac this competes with the player's own textures; the worker being a separate process is what keeps that survivable.

## Licence notes

CorridorKey 2.0: free for personal and commercial work, including commercial footage; organisations over $20M annual revenue need a seat per artist. Derivatives of the code or model must be freely accessible, stay under the same licence and keep the CorridorKey name. No charging for access or running inference as a paid API without permission. The foundation models carry their own licences (C-RADIOv4: NVIDIA; MoGe-2: MIT). JefeCheck integrates and attributes; it does not redistribute the model.

## Measured on an M3 / 16 GB, 2026-10-24

Installed upstream (`uv sync`, Python 3.13) and keyed a synthetic 2048x1152 green plate with a hand-made coarse hint.

| | |
|---|---|
| Per frame, 1024 backbone, 2K plate | **~7 s** (torch/MPS) |
| Model load and startup, per process | **~11 s** |
| 2048 backbone | **OOM** — killed on a 16 GB machine |
| Weights on disk | 380 MB (`CorridorKey_v1.0.safetensors`) |

**`--image-size` is the inference backbone, not the output size.** A 1024 backbone still returns a full 2048x1152 matte (the position embedding is resized, 262144 -> 65536 tokens). So 2K plates work here; only the larger backbone does not fit. Backbone size is a quality knob to expose, not a resolution limit.

**The ~11 s startup is per process**, so a worker invoked per frame would spend more time loading than keying. The worker must take a frame *range* and stay alive for it — which is what Key Range already wants, and another reason the live FX node is cache-only.

### Worker contract, as it really is

- `corridorkey run-inference`, with every interactive prompt suppressed by flag (`--srgb`, `--despill`, `--despeckle`, `--refiner`, `--image-size`, `--comp`, `--gpu-post`, `--tile`, `--screen-color`); miss one and it blocks on stdin.
- Input lives in `ClipsForInference/<clip>/Input/`, the hint in `AlphaHint/` (**not** `Alpha/` — it silently creates an empty `AlphaHint/` and reports "marking for generation"). It also reorganises the clip folder on every run.
- Output lands in `<clip>/Output/{FG,Matte,Comp,Processed}` — inside the clip, not the top-level `Output/`. FG is straight sRGB, Matte is linear single-channel, Processed is premultiplied.
- The model takes **4 channels**: plate RGB plus the hint ("Patched input layer: 3 -> 4 channels").
- Alpha can land slightly outside [0,1] (measured -0.078 to 1.064). Clamp deliberately, or carry the overshoot.
- **It exits 0 when it processed nothing at all** (e.g. no clip has both assets). The worker wrapper must verify the expected output files exist and fail loudly when they do not.
- `--backend mlx` needs a separate third-party package (`corridorkey-mlx`); upstream's Apple Silicon default is torch on MPS.
- The automatic hint generators are not options here: GVM and VideoMaMa want ~80 GB and 24 GB. BiRefNet is the light one; JefeCheck's annotation strokes are the real answer.
- Hints should be **coarse, blurred and eroded** — the model fills detail in but is poor at subtracting an over-expanded mask. Rasterise strokes generously blurred, never tight.
