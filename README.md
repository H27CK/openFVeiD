# FVD++ (Modernized)

FVD++ is an advanced Force Vector Design tool for creating high-precision roller coaster layouts. This version is a modern rewrite of the original software, transitioning from legacy Qt-based roots to a high-performance **Dear ImGui** architecture. This transition establishes a modern foundation for expanding the tool with advanced design features. All ongoing development and updates will be hosted in this repository. Note that while this repository is named `openFVeiD` to distinguish it from repositories containing legacy code, the application itself is still referred to as **FVD++**.

## Heritage & Credits

This project is a successor to the original [openFVD](https://github.com/altlenny/openFVD) project. While the architecture has been modernized for today's hardware and workflows, it maintains the mathematical precision and core design philosophy of the original application.

- **Original Author:** [Stephan "Lenny" Alt](https://github.com/altlenny) (<alt.stephan@web.de>)
- **Modernization & Maintenance:** [Veia](https://github.com/H27CK) (<h27ck@proton.me>)
- **Vulkan Renderer & macOS Port:** [Ercan "geforcefan" Akyürek](https://github.com/geforcefan) (<ercan.akyuerek@gmail.com>)

### Original Contributors
- Lucas van den Bosch (Documentation and tutorials)
- Ercan "geforcefan" Akyürek (Early cross-platform testing, Vulkan renderer, macOS support)

### Testing & QA (0.9+)
Special thanks to:
- Loundlim (`loundlim`)
- Maverick (`barracuda4099`)
- nerfer10 (`peterc8117`)
- TheTalbotHound (`thetalbothound`)
- xwaldo (`xwaldo`)

## Community & Discord

To submit feature requests, report issues, and keep track of development, with notifications on new releases, etc., [join the Discord server](https://discord.gg/Sq7QmsmfrC).

## Documentation

- [Coaster Specifications Reference](docs/coaster_specs.md) - A comprehensive list of heartline offsets and track gauges for various coaster manufacturers and styles.

## Community Submitted Tracks

The following parametric custom tracks have been contributed by the community. See the **Installing Custom Tracks** section below for instructions:

| Author | Style | Version |
| :--- | :--- | :---: |
| nerfer10 | Arrow | 1.0 |
| nerfer10 | B&M | 1.0 |
| xwaldo | B&M Classic | 1.0 |
| xwaldo | B&M Dive | 1.0 |
| xwaldo | B&M Modern | 1.0 |
| xwaldo | GCI | 1.0 |
| nerfer10 | Generic Flat | 1.0 |
| xwaldo | Intamin Hot Racer | 1.0 |
| Loundlim | Intamin LSM 0.9m | 1.0 |
| Loundlim | Intamin Tri Tube 0.9m | 1.0 |
| nerfer10 | Raptor | 1.0 |
| xwaldo | RMC | 1.0 |
| Loundlim | RMC IBox Track | 1.0 |
| Loundlim | RMC Topper Track | 1.0 |
| Loundlim | S&S Airlaunch & Looper | 1.0 |
| nerfer10 | Schwarzkopf | 1.0 |
| nerfer10 | Small Flat | 1.0 |
| xwaldo | Vekoma MK1101 | 1.0 |

## Community Submitted Clearance Tunnels

The following clearance tunnel custom tracks have been contributed by the community:

| Author | Style | Version | Reference Train / Model |
| :--- | :--- | :---: | :--- |
| Loundlim | Arrow 4D | 1.0 | Arrow 4D (NL2) |
| Loundlim | Arrow Corkscrew | 1.0 | Arrow Corkscrew (NL2) |
| Loundlim | Arrow Suspended | 1.0 | Arrow Suspended (NL2) |
| Loundlim | B&M Dive | 1.0 | B&M Dive (NL2) |
| Loundlim | B&M Dive with scoops | 1.0 | B&M Dive with scoops (NL2) |
| Loundlim | B&M Floorless | 1.0 | B&M Floorless (NL2) |
| Loundlim | B&M Flyer | 1.0 | B&M Flyer (NL2) |
| Loundlim | B&M Hyper (4 across) | 1.0 | B&M Hyper 4-across (NL2) |
| Loundlim | B&M Hyper (staggered) | 1.0 | B&M Hyper staggered (NL2) |
| Loundlim | B&M Hyper (staggered with scoops) | 1.0 | B&M Hyper staggered with scoops (NL2) |
| Loundlim | B&M Invert | 1.0 | B&M Invert (NL2) |
| Loundlim | B&M Sitdown | 1.0 | B&M Sitdown (NL2) |
| Loundlim | B&M Standup | 1.0 | B&M Standup (NL2) |
| Loundlim | B&M Wing | 1.0 | B&M Wing (NL2) |
| Loundlim | GCI Millenium Flyer | 1.0 | GCI Millenium Flyer (NL2) |
| Loundlim | Gerstlauer Bobsled | 1.0 | Gerstlauer Bobsled (NL2) |
| Loundlim | Gerstlauer Eurofighter (1&2) | 1.0 | Gerstlauer Eurofighter (NL2) |
| Loundlim | Gerstlauer Infinity 1st gen | 1.0 | Gerstlauer Infinity 1st Gen (NL2) |
| Loundlim | Gerstlauer Infinity 2nd gen | 1.0 | Gerstlauer Infinity 2nd Gen (NL2) |
| Loundlim | Gerstlauer Spinner | 1.0 | Gerstlauer Spinner (NL2) |
| Loundlim | Gravity Group Timberliner | 1.0 | Gravity Group Timberliner (NL2) |
| Loundlim | Intamin Hyper | 1.0 | Intamin Hyper (NL2) |
| Loundlim | Intamin Impulse | 1.0 | Intamin Impulse (NL2) |
| Loundlim | Intamin LSM | 1.0 | Intamin LSM (Serming) |
| Loundlim | Intamin Rocket | 1.0 | Intamin Rocket (NL2) |
| Loundlim | Mack Extreme Spinner | 1.0 | Mack Extreme Spinner |
| Loundlim | Mack LSM & Hyper | 1.0 | Mack LSM & Hyper (NL2) |
| Loundlim | Maurer Spinner | 1.0 | Maurer Spinner (NL2) |
| Loundlim | Maurer X-Car | 1.0 | Maurer X-Car (NL2) |
| Loundlim | Morgan Wooden Trailored | 1.0 | Morgan Wooden Trailored (NL2) |
| Loundlim | Permier LIM | 1.0 | Premier LIM (NL2) |
| Loundlim | Premier Skyrocket | 1.0 | Premier Skyrocket (Coasterpete) |
| Loundlim | PTC 4 & 6 seater | 1.0 | PTC 4 & 6-seater (NL2) |
| Loundlim | RMC Hybrid 1st gen | 1.0 | RMC Hybrid 1st Gen (NL2) |
| Loundlim | RMC Raptor | 1.0 | RMC Raptor (TheCodeMaster) |
| Loundlim | Schwarzkopf Looper 1st gen | 1.0 | Schwarzkopf Looper 1st Gen (NL2) |
| Loundlim | Schwarzkopf Looper 2nd gen | 1.0 | Schwarzkopf Looper 2nd Gen (NL2) |
| Loundlim | Vekoma Flying Dutchman | 1.0 | Vekoma Flying Dutchman (NL2) |
| Loundlim | Vekoma Inverted Boomerang | 1.0 | Vekoma Inverted Boomerang (NL2) |
| Loundlim | Vekoma Minetrain | 1.0 | Vekoma Minetrain (NL2) |
| Loundlim | Vekoma MK1101 | 1.0 | Vekoma MK1101 (NL2) |
| Loundlim | Vekoma Motorbike | 1.0 | Vekoma Motorbike (NL2) |
| Loundlim | Vekoma SLC | 1.0 | Vekoma SLC (NL2) |
| Loundlim | Vekoma STC | 1.0 | Vekoma STC (Pieter & Serming) |

## Community Submitted Trains

The following train models have been contributed by the community:

| Author | Style | Version |
| :--- | :--- | :---: |
| nerfer10 | GCI Millenium Flyer | 1.0 |
| Bränninen | Kex LSM | 1.0 |

## Known Issues

- **Unit Adoption:** Selected units are not yet fully adopted across all UI panels; metric remains predominant.
- **Graph Updates:** Graphs do not update immediately when switching between time and distance section arguments.

## Building from Source

### Dependencies

FVD++ renders through Vulkan and needs:
- **GLM** (v1.0.3)
- **Vulkan loader and headers** plus **glslangValidator** for SPIR-V compilation (a Vulkan SDK installation covers both)

#### System Prerequisites

Ensure you have CMake, standard build tools, Vulkan and windowing libraries installed on your system:
- **Linux (Debian/Ubuntu):**
  ```bash
  sudo apt install -y build-essential cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libvulkan-dev glslang-tools libglm-dev
  ```
- **macOS (Homebrew):**
  ```bash
  brew install cmake glm molten-vk vulkan-loader vulkan-headers glslang
  ```
- **Windows:** install the [LunarG Vulkan SDK](https://vulkan.lunarg.com/) and GLM via vcpkg:
  ```powershell
  vcpkg install glm:x64-windows-static
  ```

#### Configuring the GLM Path

If GLM is not found automatically by CMake, it will fall back to a local dependency folder.
To configure this fallback:
1. Open `CMakeLists.txt` and locate the fallback section:
   ```cmake
   set(DEPS_ROOT "/path/to/dependencies")
   ```
2. Modify `/path/to/dependencies` to point to the directory containing your `glm-1.0.3` folder.

### Build Commands

#### Windows (PowerShell)

1. Open PowerShell and run:
   ```powershell
   mkdir build; cd build
   cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
   cmake --build . --config Release
   ```
   *(Adjust the `CMAKE_TOOLCHAIN_FILE` path to match your local vcpkg or dependency setup if different.)*

#### Linux / macOS

1. Open a terminal and run:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

On macOS the build produces an application bundle with the community custom tracks included. Launch it with:
```bash
open build/fvd.app
```

Downloaded builds (for example CI artifacts) are not notarized, so macOS reports them as damaged. Clear the quarantine flag once:
```bash
xattr -cr fvd.app
```
User data (options, logs, custom tracks, skyboxes) lives in `~/Library/Application Support/FVD++`. The shipped custom tracks are copied there on first launch; running the app from a source checkout keeps using the checkout's `track_styles/` directly.

### Skyboxes and Environment Presets

Install each skybox in its own folder under `skybox/`. The preferred face names are
`nx`, `ny`, `nz`, `px`, `py`, and `pz`, with PNG, JPEG, BMP, or TGA extensions.
The `cubemap_0` through `cubemap_5` naming format is also detected. FVD++ watches this
directory, so complete sets appear in the Environment window without restarting.

`.fvdenv` files store environment presets. Presets anywhere under `skybox/` appear in the Environment
Preset dropdown and can be switched without restarting. The selected preset is watched
and reapplied when the file changes.

## Installing Custom Tracks

Custom tracks customize the rails, spines, cross-sections, etc. of your coasters. You can easily install them using either of the following methods.

### Location of the `track_styles` Folder:
- **Windows / Linux:** Located right next to your `fvd.exe` / `fvd` executable.
- **macOS:** Located inside your user's Application Support directory: `~/Library/Application Support/FVD++`. *(You can open Finder, press `Cmd+Shift+G`, and paste this path to navigate there directly.)*

---

### Method 1: Use the Community Pack File (Recommended)

With every official release, all community track styles, trains, and clearance envelopes are bundled into a single `community.fvdpack` file.
1. Download `community.fvdpack` from the [Latest GitHub Release](https://github.com/H27CK/openFVeiD/releases/latest) (if not already included with your distribution).
2. Place `community.fvdpack` directly next to the `fvd.exe` / `fvd` executable (or in `~/Library/Application Support/FVD++` on macOS).
3. Launch FVD++ to immediately access all community tracks, trains, and clearance tunnels without extracting anything to disk!

---

### Method 2: Install Separate Custom Tracks Individually

If you want to download a specific style from the repository (e.g. from the `community/tracks/` folder):
1. Navigate to the style directory on GitHub (for example, `community/tracks/rmc_ibox_track/`).
2. Download the `.fvdtrack` file (which defines the style) and its associated `.glb` mesh asset file (which contains the 3D model).
3. Place **both files next to each other** inside your local `tracks` directory (or next to the executable).
4. Launch FVD++ to select the style in the Track Properties panel!

## License

FVD++ is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License v3.0** as published by the Free Software Foundation. See the `LICENSE` file for the full text.
