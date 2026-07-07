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

## Key Features

- **User Interface:** Dear ImGui integration with custom style controls, UI scaling, and status bar.
- **Parametric Track Styles:** Dynamic track styling using recursive folder scanning (`track_styles/`). Supports GLTF/GLB asset models.
- **Rider-Local Geometric Sections:** Support for creating track shapes in a rider-local coordinate system (`geometricriderlocal`).
- **Force Analysis Offsets:** Seating position offset support for calculating forces at specific rider locations.
- **Undo/Redo System:** Project-level undo and redo tracking up to 200 states.
- **High-Resolution Screenshots:** Multiplied viewport resolution capture (up to 8x) for anti-aliased images via F12 shortcut.
- **Environment Settings:** Support for configurable mist distance and custom skybox textures.
- **Orthographic Views:** Top, Front, and Side orthographic viewports with projected floor grids and highlighted axes.
- **Viewport Navigation:** Automatic zoom-to-selection and immediate rendering updates on parameter changes.
- **Data Import/Export:**
  - Track export to NoLimits 2 (`.nl2elem`) and CSV.
  - Track import from CSV with automated Bezier generation and geometric smoothing.
  - Multi-STL 3D environment loading integrated with project files.
- **Renderer:** Vulkan viewport rendering (MoltenVK on macOS) with floor grid and shadow projection.

## Documentation

- [Coaster Specifications Reference](docs/coaster_specs.md) - A comprehensive list of heartline offsets and track gauges for various coaster manufacturers and styles.

## Community Track Styles

The following parametric track styles have been contributed by the community. See [How to Install Track Styles](https://github.com/H27CK/FVeiD#installing-track-styles) for instructions:

| Author | Style | Version |
| :--- | :--- | :---: |
| nerfer10 | [Arrow](track_styles/nerfer10/Arrow) | 1.0 |
| nerfer10 | [B&M](track_styles/nerfer10/B&M) | 1.0 |
| xwaldo | [B&M Classic](track_styles/xwaldo/bm_classic) | 1.0 |
| xwaldo | [GCI](track_styles/xwaldo/gci) | 1.0 |
| nerfer10 | [Generic Flat](track_styles/nerfer10/Generic%20Flat) | 1.0 |
| nerfer10 | [Raptor](track_styles/nerfer10/Raptor) | 1.0 |
| Loundlim | [Intamin LSM 0.9m](track_styles/Loundlim/Intamin%20LSM%200.9m) | 1.0 |
| Loundlim | [Intamin Tri Tube 0.9m](track_styles/Loundlim/Intamin%20Tri%20Tube%200.9m) | 1.0 |
| Loundlim | [RMC IBox Track](track_styles/Loundlim/RMC%20IBox%20Track) | 1.0 |
| Loundlim | [RMC Topper Track](track_styles/Loundlim/RMC%20Topper%20Track) | 1.0 |
| Loundlim | [S&S Airlaunch & Looper](track_styles/Loundlim/S%26S%20Air%20Launch%20%26%20Looper) | 1.0 |
| nerfer10 | [Schwarzkopf](track_styles/nerfer10/Schwarzkopf) | 1.0 |
| nerfer10 | [Small Flat](track_styles/nerfer10/Small%20Flat) | 1.0 |
| xwaldo | [Vekoma MK1101](track_styles/xwaldo/vekoma_mk1101) | 1.0 |

## Community Clearance Envelopes

The following clearance envelope track styles have been contributed by the community:

| Author | Style | Version | Reference Train / Model |
| :--- | :--- | :---: | :--- |
| Loundlim | [Arrow 4D](track_styles/Loundlim/Clearance%20Envelopes/Arrow%204D%20clearance%20(NL2).fvdstyle) | 1.0 | Arrow 4D (NL2) |
| Loundlim | [Arrow Corkscrew](track_styles/Loundlim/Clearance%20Envelopes/Arrow%20Corkscrew%20clearance%20(NL2).fvdstyle) | 1.0 | Arrow Corkscrew (NL2) |
| Loundlim | [Arrow Suspended](track_styles/Loundlim/Clearance%20Envelopes/Arrow%20Suspended%20clearance%20(NL2).fvdstyle) | 1.0 | Arrow Suspended (NL2) |
| Loundlim | [B&M Dive](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Dive%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Dive (NL2) |
| Loundlim | [B&M Dive with scoops](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Dive%20with%20scoops%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Dive with scoops (NL2) |
| Loundlim | [B&M Floorless](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Floorless%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Floorless (NL2) |
| Loundlim | [B&M Flyer](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Flyer%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Flyer (NL2) |
| Loundlim | [B&M Hyper (4 across)](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Hyper%20(4%20across)%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Hyper 4-across (NL2) |
| Loundlim | [B&M Hyper (staggered)](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Hyper%20(staggered)%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Hyper staggered (NL2) |
| Loundlim | [B&M Hyper (staggered with scoops)](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Hyper%20(staggered%20with%20scoops)%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Hyper staggered with scoops (NL2) |
| Loundlim | [B&M Invert](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Invert%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Invert (NL2) |
| Loundlim | [B&M Sitdown](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Sitdown%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Sitdown (NL2) |
| Loundlim | [B&M Standup](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Standup%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Standup (NL2) |
| Loundlim | [B&M Wing](track_styles/Loundlim/Clearance%20Envelopes/B%26M%20Wing%20clearance%20(NL2).fvdstyle) | 1.0 | B&M Wing (NL2) |
| Loundlim | [GCI Millenium Flyer](track_styles/Loundlim/Clearance%20Envelopes/GCI%20Millenium%20Flyer%20clearance%20(NL2).fvdstyle) | 1.0 | GCI Millenium Flyer (NL2) |
| Loundlim | [Gerstlauer Bobsled](track_styles/Loundlim/Clearance%20Envelopes/Gerstlauer%20Bobsled%20clearance%20(NL2).fvdstyle) | 1.0 | Gerstlauer Bobsled (NL2) |
| Loundlim | [Gerstlauer Eurofighter (1&2)](track_styles/Loundlim/Clearance%20Envelopes/Gerstlauer%20Eurofighter%20(1%262)%20clearance%20(NL2).fvdstyle) | 1.0 | Gerstlauer Eurofighter (NL2) |
| Loundlim | [Gerstlauer Infinity 1st gen](track_styles/Loundlim/Clearance%20Envelopes/Gerstlauer%20Infinity%201st%20gen%20clearance%20(NL2).fvdstyle) | 1.0 | Gerstlauer Infinity 1st Gen (NL2) |
| Loundlim | [Gerstlauer Infinity 2nd gen](track_styles/Loundlim/Clearance%20Envelopes/Gerstlauer%20Infinity%202nd%20gen%20clearance%20(NL2).fvdstyle) | 1.0 | Gerstlauer Infinity 2nd Gen (NL2) |
| Loundlim | [Gerstlauer Spinner](track_styles/Loundlim/Clearance%20Envelopes/Gerstlauer%20Spinner%20clearance%20(NL2).fvdstyle) | 1.0 | Gerstlauer Spinner (NL2) |
| Loundlim | [Gravity Group Timberliner](track_styles/Loundlim/Clearance%20Envelopes/Gravity%20Group%20Timberliner%20clearance%20(NL2).fvdstyle) | 1.0 | Gravity Group Timberliner (NL2) |
| Loundlim | [Intamin Hyper](track_styles/Loundlim/Clearance%20Envelopes/Intamin%20Hyper%20clearance%20(NL2).fvdstyle) | 1.0 | Intamin Hyper (NL2) |
| Loundlim | [Intamin Impulse](track_styles/Loundlim/Clearance%20Envelopes/Intamin%20Impulse%20clearance%20(NL2).fvdstyle) | 1.0 | Intamin Impulse (NL2) |
| Loundlim | [Intamin LSM](track_styles/Loundlim/Clearance%20Envelopes/Intamin%20LSM%20clearance%20(Serming).fvdstyle) | 1.0 | Intamin LSM (Serming) |
| Loundlim | [Intamin Rocket](track_styles/Loundlim/Clearance%20Envelopes/Intamin%20Rocket%20clearance%20(NL2).fvdstyle) | 1.0 | Intamin Rocket (NL2) |
| Loundlim | [Mack Extreme Spinner](track_styles/Loundlim/Clearance%20Envelopes/Mack%20Extreme%20Spinner%20clearance.fvdstyle) | 1.0 | Mack Extreme Spinner |
| Loundlim | [Mack LSM & Hyper](track_styles/Loundlim/Clearance%20Envelopes/Mack%20LSM%20%26%20Hyper%20clearance%20(NL2).fvdstyle) | 1.0 | Mack LSM & Hyper (NL2) |
| Loundlim | [Maurer Spinner](track_styles/Loundlim/Clearance%20Envelopes/Maurer%20Spinner%20clearance%20(NL2).fvdstyle) | 1.0 | Maurer Spinner (NL2) |
| Loundlim | [Maurer X-Car](track_styles/Loundlim/Clearance%20Envelopes/Maurer%20X-Car%20clearance%20(NL2).fvdstyle) | 1.0 | Maurer X-Car (NL2) |
| Loundlim | [Morgan Wooden Trailored](track_styles/Loundlim/Clearance%20Envelopes/Morgan%20Wooden%20Trailored%20clearance%20(NL2).fvdstyle) | 1.0 | Morgan Wooden Trailored (NL2) |
| Loundlim | [Permier LIM](track_styles/Loundlim/Clearance%20Envelopes/Permier%20LIM%20clearance%20(NL2).fvdstyle) | 1.0 | Premier LIM (NL2) |
| Loundlim | [Premier Skyrocket](track_styles/Loundlim/Clearance%20Envelopes/Premier%20Skyrocket%20clearance%20(Coasterpete).fvdstyle) | 1.0 | Premier Skyrocket (Coasterpete) |
| Loundlim | [PTC 4 & 6 seater](track_styles/Loundlim/Clearance%20Envelopes/PTC%204%20%26%206%20seater%20clearance%20(NL2).fvdstyle) | 1.0 | PTC 4 & 6-seater (NL2) |
| Loundlim | [RMC Hybrid 1st gen](track_styles/Loundlim/Clearance%20Envelopes/RMC%20Hybrid%201st%20gen%20clearance%20(NL2).fvdstyle) | 1.0 | RMC Hybrid 1st Gen (NL2) |
| Loundlim | [RMC Raptor](track_styles/Loundlim/Clearance%20Envelopes/RMC%20Raptor%20clearance%20(TheCodeMaster).fvdstyle) | 1.0 | RMC Raptor (TheCodeMaster) |
| Loundlim | [Schwarzkopf Looper 1st gen](track_styles/Loundlim/Clearance%20Envelopes/Schwarzkopf%20Looper%201st%20gen%20clearance%20(NL2).fvdstyle) | 1.0 | Schwarzkopf Looper 1st Gen (NL2) |
| Loundlim | [Schwarzkopf Looper 2nd gen](track_styles/Loundlim/Clearance%20Envelopes/Schwarzkopf%20Looper%202nd%20gen%20clearance%20(NL2).fvdstyle) | 1.0 | Schwarzkopf Looper 2nd Gen (NL2) |
| Loundlim | [Vekoma Flying Dutchman](track_styles/Loundlim/Clearance%20Envelopes/Vekoma%20Flying%20Dutchman%20clearance%20(NL2).fvdstyle) | 1.0 | Vekoma Flying Dutchman (NL2) |
| Loundlim | [Vekoma Inverted Boomerang](track_styles/Loundlim/Clearance%20Envelopes/Vekoma%20Inverted%20Boomerang%20clearance%20(NL2).fvdstyle) | 1.0 | Vekoma Inverted Boomerang (NL2) |
| Loundlim | [Vekoma Minetrain](track_styles/Loundlim/Clearance%20Envelopes/Vekoma%20Minetrain%20clearance%20(NL2).fvdstyle) | 1.0 | Vekoma Minetrain (NL2) |
| Loundlim | [Vekoma MK1101](track_styles/Loundlim/Clearance%20Envelopes/Vekoma%20MK1101%20clearance%20(NL2).fvdstyle) | 1.0 | Vekoma MK1101 (NL2) |
| Loundlim | [Vekoma Motorbike](track_styles/Loundlim/Clearance%20Envelopes/Vekoma%20Motorbike%20clearance%20(NL2).fvdstyle) | 1.0 | Vekoma Motorbike (NL2) |
| Loundlim | [Vekoma SLC](track_styles/Loundlim/Clearance%20Envelopes/Vekoma%20SLC%20clearance%20(NL2).fvdstyle) | 1.0 | Vekoma SLC (NL2) |
| Loundlim | [Vekoma STC](track_styles/Loundlim/Clearance%20Envelopes/Vekoma%20STC%20clearance%20(Pieter%20%26%20Serming).fvdstyle) | 1.0 | Vekoma STC (Pieter & Serming) |

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

On macOS the build produces an application bundle with the community track styles included. Launch it with:
```bash
open build/fvd.app
```

Downloaded builds (for example CI artifacts) are not notarized, so macOS reports them as damaged. Clear the quarantine flag once:
```bash
xattr -cr fvd.app
```
User data (options, logs, track styles, skyboxes) lives in `~/Library/Application Support/FVD++`. The shipped track styles are copied there on first launch; running the app from a source checkout keeps using the checkout's `track_styles/` directly.

## Installing Track Styles

Track styles customize the rails, spines, cross-sections, etc. of your coasters. You can easily install them using either of the following methods.

### Location of the `track_styles` Folder:
- **Windows / Linux:** Located right next to your `fvd.exe` / `fvd` executable.
- **macOS:** Located inside your user's Application Support directory: `~/Library/Application Support/FVD++`. *(You can open Finder, press `Cmd+Shift+G`, and paste this path to navigate there directly.)*

---

### Method 1: Install All Community Styles at Once (Recommended)

With every official release, we publish a pre-packaged `track_styles.zip` containing all available community styles.
1. Download `track_styles.zip` from the [Latest GitHub Release](https://github.com/H27CK/openFVeiD/releases/latest).
2. Extract the ZIP. It will contain a `community` folder.
3. Move or copy this `community` folder directly into your local `track_styles` directory.
4. Use **File > Reload Assets** in the main menu (or restart FVD++) to immediately use them!

---

### Method 2: Install Separate Track Styles Individually

If you want to download a specific style from the repository (e.g. from the `track_styles/` folder):
1. Navigate to the style directory on GitHub (for example, `track_styles/Loundlim/Clearance Envelopes/`).
2. Download the `.fvdstyle` file (which defines the style) and its associated `.glb` mesh asset file (which contains the 3D model).
3. Place **both files next to each other** inside your local `track_styles` directory (or a custom subfolder inside it, such as `track_styles/mine/`).
4. Use **File > Reload Assets** in the main menu (or restart FVD++) to immediately reload and select the style in the drop-down menu!

## License

FVD++ is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License v3.0** as published by the Free Software Foundation. See the `LICENSE` file for the full text.
