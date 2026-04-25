---
layout: default
title: Pipeline Integration
---

# Pipeline Integration

This page shows how JefeCheck's preferences and loading options can be customized with the command line and environment variables. This allows TDs to write scripts that launch JefeCheck with the options needed by each project automatically.

## Contents

1. [Command Line Arguments](#command-line-arguments)
2. [Environment Variables](#environment-variables)
3. [LUT Formats and Custom LUT Creation](#lut-formats-and-custom-lut-creation)
   - [Truelight .cub files](#truelight-cub-files)
   - [JefeCheck .tga 3D LUT Format](#jefecheck-tga-3d-lut-format)
   - [JefeCheck .lut 1D LUT Format](#jefecheck-lut-1d-lut-format)
4. [Nuke and Maya Integration](#nuke-and-maya-integration)

---

## Command Line Arguments

You can load files directly from the command line. Specify a filename and parameters for up to 4 sequences — the first sequence is loaded into track A, the second into B, etc.

The first time an option is encountered it applies to track A, the second time to track B, and so on. Options that affect a viewport instead of a track follow the same pattern (viewport 1, then viewport 2, etc.).

| Flag | Long form | Description |
|------|-----------|-------------|
| `-h` | `--help` | Produce help message |
| `-f` | `--from` | Start loading from this frame *(track option)* |
| `-t` | `--to` | Stop loading at this frame *(track option)* |
| `-s` | `--scale` | Scale percentage for loading sequences (100, 50, etc.) *(track option)* |
| `-r` | `--frameRate` | Frame rate for playback *(global option)* |
| `-x` | `--fx` | FX Stack file *(viewport option)* |
| `-l` | `--lut` | LUT for viewport *(viewport option)* |

### Examples

```sh
jefecheck /home/images/sequence1.0001.dpx
```
Loads `sequence1` into track A, finding all frames that belong to the same sequence as frame 0001.

```sh
jefecheck /home/images/sequence1.0001.dpx /home/images/sequence2.0001.dpx -r 24
```
Loads `sequence1` into track A and `sequence2` into track B. Sets target playback rate to 24 FPS.

```sh
jefecheck /home/images/sequence1.0001.dpx -f 12 -t 45 \
          /home/images/sequence2.0001.dpx -f 520 -t 530
```
Loads frames 12–45 of `sequence1` into track A, and frames 520–530 of `sequence2` into track B.

```sh
jefecheck /home/images/sequence1.####.dpx -f 12 -t 45
```
Loads `sequence1` using 4-digit padding from frame 12 through 45.

> When using `#` to specify padding, the range must be explicitly stated with `-f` and `-t`.

```sh
jefecheck /home/images/sequence1.0001.dpx -s 100 \
          /home/images/sequence2.0001.dpx -s 50
```
Loads `sequence1` at 100% scale into track A and `sequence2` at 50% scale into track B.

Argument *position* doesn't matter, only their order *relative to other arguments of the same name*. The following is equivalent:

```sh
jefecheck /home/images/sequence1.0001.dpx /home/images/sequence2.0001.dpx -s 100 -s 50
```

```sh
jefecheck /home/images/sequence1.0001.dpx -x /home/fxStacks/myColorCorrection.fxs
```
Loads `sequence1` into track A and applies the FX stack from the `.fxs` file to viewport 1.

```sh
jefecheck /home/images/sequence1.0001.dpx -l LogToLinToSRGB.lut
```
Loads `sequence1` into track A and sets the LUT for viewport 1 to `LogToLinToSRGB.lut`.

> The argument to `--lut` is the name of the LUT file (not the whole path). The LUT must be in the JefeCheck LUT path or loaded previously through the LUT Manager and set to auto-load.

---

## Environment Variables

JefeCheck reads these environment variables to modify its behavior:

| Variable | Description |
|----------|-------------|
| `JEFECHECK_LUT_PATH` | Overrides the default LUT path. e.g. `/home/myLUTs/` |
| `JEFECHECK_FX_PATH` | Overrides the default FX path. e.g. `/home/myFXs/` |
| `JEFECHECK_DEFAULT_BROWSE_PATH` | Overrides the default browse path. The file browser opens here when loading a sequence. e.g. `/home/projectFiles/projectA/images/` |
| `JEFECHECK_DEFAULT_LUT` | Overrides the default LUT for all viewports defined in the LUT Manager. Command-line `--lut` takes precedence. e.g. `LogToLinToSRGB.lut` |

---

## LUT Formats and Custom LUT Creation

JefeCheck supports several LUT formats. You can drop LUT files into the LUT path, or load them on demand through the LUT Manager.

### Truelight .cub files

JefeCheck supports Truelight `.cub` 3D cubes. Tested resolutions: 16×16×16 and 32×32×32. Higher resolutions should also work.

Truelight `.cub` files for a particular color correction can be generated from The Foundry's Nuke using a CMS node and exporting it as a `.cub` file.

### JefeCheck .tga 3D LUT Format

JefeCheck has a native 3D LUT format that can replicate any color pipeline with reasonable accuracy. The LUT is a small 64×64 pixel TGA image containing color samples for a 16×16×16 color cube.

**To create a custom LUT:**

1. Take the canonical "unit cube" image — a TGA encoding the identity 3D LUT.
2. Pass it through your color pipeline (Nuke, Shake, Photoshop, etc.) — any software that can read, alter, and write an image.
3. Save the result as a TGA file (lossless format).
4. Load the rendered TGA back into JefeCheck by placing it in the LUT folder or loading it through the LUT Manager.

JefeCheck will use the patch to create a 3D LUT.

> Use the canonical UnitCube.tga and save to TGA — it must be lossless.

LUTs exported from Nuke as TGA files are also supported (16×16×16 yields 448×448 pixel images). They use more pixels per sample but work the same way.

### JefeCheck .lut 1D LUT Format

JefeCheck has a native 1D LUT format that's as simple as it gets. A 1D LUT takes a single floating-point input value and outputs another floating-point value. When applied to an image, each color component passes through the LUT independently.

The `.lut` file is a plain text file with the following structure:

- **Header:** `#JefeCheck LUT Header v1.0`
- **Number of entries** (usually 256 for 8-bit, 1024 for 10-bit, etc.)
- **Input bit depth** (deprecated but still required for compatibility): 8 for 8-bit, 10 for 10-bit, etc.
- **Output bit depth** (deprecated but still required): 8, 10, 16
- **Entries:** The actual output values, **normalized to 0.0–1.0**.

For an 8-bit LUT there would be 256 entries: the first is the output for input 0, the second for input 1, and so on up to 255.

**Example** — a truncated invert LUT:

```
#JefeCheck LUT Header v1.0
256
8
8
1.0000
0.9961
0.9922
0.9882
...
0.0078
0.0039
0.0000
```

---

## Nuke and Maya Integration

Integration scripts to replace the default flipbook in Nuke and Maya are coming soon. Contributions welcome — see the [GitHub repo](https://github.com/danielgollas/jefecheck) to submit a script.
