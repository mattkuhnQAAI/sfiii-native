# Shining Force III Recomp — Scenarios 1, 2 & 3

**Shining Force III Recomp (Shining Force 3 Recomp)** is an experimental native PC static recompilation of Sega Saturn's **Shining Force III**, with profiles for **Scenario 1 USA MK-81383 V1.001**, **Scenario 2 Translation Project v24** and **Scenario 3 Translation Project v25.1**. Game SH-2 code is generated locally as C++ and compiled for the PC. Windows is the established development platform; experimental Linux x64, Apple Silicon and Intel Mac candidates are built separately. The runtime supplies Saturn graphics, sound, storage and other hardware services; static recompilation does not remove the need to model those systems.

## Current status — October 24, 2026

**The project is playable in tested Windows routes and remains an experimental preview.** The latest published preview is [**v0.6.0-preview1**](https://github.com/mattkuhnQAAI/sfiii-native/releases/tag/v0.6.0-preview1). Complete campaigns and hardware-faithful rendering/audio have not been fully validated.

| Component | Available now | Validation status |
| --- | --- | --- |
| Windows x64 application | [v0.6.0-preview1](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/SFIII-Native-0.6.0-preview1-win64.zip), with automatic local game preparation | Recorded gameplay routes, limited native-state continuations and captured GPU rendering cases tested locally. |
| Optional graphics builder | [Separate Windows download](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/SFIII-Offline-Graphics-Builder-0.6.0-preview1-Windows-x64.zip), about 22 MB | Automatic setup, all three registered discs and three GPU styles tested with limited samples; in-game artwork validation is focused on Scenario 2. |
| Linux x64 and macOS Apple Silicon / Intel | [v0.5.0-alpha published packages](https://github.com/mattkuhnQAAI/sfiii-native/releases/tag/v0.5.0-alpha) | New v0.6.0 candidates also [passed hosted builds and synthetic validation](https://github.com/mattkuhnQAAI/sfiii-native/actions/runs/40089746225) on all three targets. Real-disc gameplay and AI graphics on these platforms remain unvalidated. |

The v0.6.0 Linux/macOS candidates have not been added to the release downloads; the linked v0.5.0-alpha packages remain the published baseline. Build and synthetic-test success is separate from real-game compatibility. See [portable setup](docs/POSIX.md).

### Registered game editions

| Game | Accepted disc revision | Current gameplay coverage |
| --- | --- | --- |
| Scenario 1 | USA MK-81383 V1.001, unpatched | Local title, town, battle and transition routes; full campaign unvalidated. |
| Scenario 2 | Shining Force III Translation Project v24, registered disc contents | The broadest local gameplay testing: towns, battles, menus, selected item/spell paths and native-state continuations. Other routes and effects remain unvalidated. |
| Scenario 2 | Translation Project V25.1 (default selection, no V25+ add-ons) applied to a GS-9188 V1.003 or V2.000 (Rev A) disc, registered disc contents (`scenario2-v25_1-v1_003.json`, `scenario2-v25_1-v2_000.json`) | Experimental. Native boot to the opening scene checked headless on Linux; gameplay unvalidated. |
| Scenario 3 | Shining Force III Translation Project v25.1, registered disc contents | Fresh-game setup, ship dialogue, first-battle screen and priest interior reached in limited Windows routes. Broader battles and campaign transfers remain unvalidated. |
| Scenario 3 | Translation Project V25.1 (default selection, no V25+ add-ons) applied to a GS-9203 V1.001 Redump disc, registered disc contents (`scenario3-v25_1-redump.json`) | Experimental. Native boot to the opening scene checked headless on Linux; gameplay unvalidated. |
| Scenarios 1, 2 and 3 | Translation Project V25.1 plus the V25+ add-ons (default add-on selection) on the Scenario 1 USA V1.001, Scenario 2 V1.003 or V2.000 (Rev A) and Scenario 3 Redump discs, registered disc contents (`scenario1-v25_1-addons.json`, `scenario2-v25_1-addons-v1_003.json`, `scenario2-v25_1-addons-v2_000.json`, `scenario3-v25_1-addons-redump.json`) | Experimental. Boot, Chapter 1 field and battle start, field-menu and debug-warp routes and a short soak checked headless on Linux; full campaign unvalidated. See [add-on profiles](docs/DEVELOPMENT.md#v251--v25-add-on-profiles). |
| Premium Disc / other revisions | No registered profile yet | Planned; currently unsupported. |

Disc verification checks contents, so a matching filename or translation label alone does not establish compatibility. Static module compilation and actual gameplay coverage are tracked separately in [Status and validation](#status-and-validation).

### What the latest preview adds

- **Optional local AI artwork:** generate eligible world textures and CHR/CHP character frames from your own discs, then choose **Off / AnimeVideo v3 / x4plus anime / x4plus general**. Menus and HUD retain original artwork. View the [same-frame screenshot comparisons](#screenshots).
- **A standalone GPU builder:** generation runs on your own Vulkan GPU, with resumable work and automatic playable-pack assembly. First-run setup downloads verified tools; processing then runs locally without Codex, API keys or paid services. Generated artwork packs are not distributed. See [builder setup](docs/DESKTOP.md#optional-graphics-builder).
- **Faster pack loading and bounded caches:** startup indexes reduce file scanning, replacement pixels load as needed, and the GPU uploads visible materials. The [measured local result](#status-and-validation) covers artwork initialization rather than total launch time.
- **Launcher pack controls and simpler resolution choices:** attach optional packs or restore original artwork; the new Windows menu offers **1× / 2× / 5×** rendering. Earlier AA and ray-shadow menu experiments are not offered in this preview.

Current priorities are broader gameplay/save-state validation, remaining background and texture defects, improved widescreen culling, and more disc editions. Background-plane artwork replacement and complete character/campaign artwork coverage remain unfinished. See [upcoming work](#upcoming-features).

### Disc and translation compatibility

The target is **original USA Scenario 1 plus v25-family translations of
Scenarios 1–3**. This is not a claim that every such image currently works.
Native compilation requires a reviewed profile for the exact disc contents;
renaming an image or recognizing its title screen cannot establish compatibility.
Premium Disc is deferred. Unregistered patch versions need their executable
mappings and native routes investigated before being listed as compatible.

The development launcher has separate entries for original USA Scenario 1,
translated Scenario 1, translated Scenario 2 and translated Scenario 3, each with
independent game-data paths. It selects among registered revisions within each entry.
Scenario 1 translation
v25.1 has an independent profile, with intro, original-save load and fresh opening
routes checked; the original USA profile remains separate. Development profiles also
include the experimental Scenario 2 v24-to-v25.1 translation migration and the
supplied V1.003 image whose header reports V24. Its exact translation version is
unverified, and its historical battle replay does not reach battle. These local
additions are **not included in the existing published downloads**.

The development save manager also exports ordinary saves as individual raw BIN
files for use with compatible emulator or Saroo save tools. Export does not
modify the native backup-memory files. The optional artwork builder now decodes
supported close-up battle-model textures; one Scenario 3 battle-model format
remains unsupported, and full artwork coverage is not established.

Unknown revisions produce a local `unregistered-disc.json` identification report
without replacing existing saves or builds. A different revision needs a separate
game-data folder. Ordinary saves can be imported between compatible game versions;
native snapshots and replay checkpoints remain tied to their original build/disc.

When updating, extract into a new application folder and choose your existing scenario data folder, then run **Prepare game** again. The bundled tools handle preparation automatically. Existing in-game saves remain in the separate data folder. Native quick states are build-specific: keep your previous application and prepared build if you need its old states; use an ordinary in-game save to transfer progress to the new build.

## Features

- Keyboard and SDL controllers, including PlayStation, Xbox, generic USB and configurable Saturn USB controllers.
- 1×, 2× and 5× rendering of supported geometry through Vulkan or Direct3D 12 on the new Windows preview. Earlier packages also offer 4×; Metal is available in the macOS baseline preview.
- Experimental 16:9 rendering with centered original framing and additional scenery. Game-side object culling is still incomplete, so edge gaps and pop-in can remain.
- Persistent internal and cartridge backup storage, with import of supported Scenario 1/2/3 raw BIN and version-1 JSON YSBAK saves.
- Direct native save states (F5/F9), with separate legacy replay checkpoints. Snapshot validation covers limited Scenario 2 translated-v24 routes; broader snapshot compatibility remains unvalidated.
- A portable Windows first-run application that prepares your own disc automatically, with controller setup and save import buttons.
- Experimental Linux/macOS launchers with bundled offline preparation tools, controller setup and save imports.
- Separate 1×/2×/3× game-speed controls (F6 or menu), with accelerated audio. Rendering resolution stays fixed; available speed depends on scene and hardware.
- Optional locally generated artwork: **Off / AnimeVideo v3 / x4plus anime / x4plus general**, with eligible world textures and decoded CHR/CHP character frames. Exact source transparency is retained; unmatched artwork, menus and HUD stay original. Extraction counts do not establish complete character, object or campaign coverage.

## Screenshots

Windows examples illustrate development progress; they do not establish Linux/macOS gameplay compatibility. Images are release documentation, not downloadable game-asset packs.

**Optional AI artwork:** the same Scenario 2 translated-v24 gameplay frames rendered through the native GPU paths at 5×, with original artwork followed by the three local AI styles. Characters and world textures use the selected style; HUD and menus retain original artwork. These are native renderer compositions of actual captured gameplay states, with identical camera and framing, rather than image-generation mockups. Contact sheets scale the complete images for comparison.

[![Airo village: Original, AnimeVideo v3, x4plus anime and x4plus general](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/airo-ai-options.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/airo-ai-options.png)

Full-size Airo village: [Original](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-airo-original.png) · [AnimeVideo v3](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-airo-video.png) · [x4plus anime](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-airo-anime.png) · [x4plus general](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-airo-general.png).

[![Battle formation: Original, AnimeVideo v3, x4plus anime and x4plus general](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/battle-ai-options.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/battle-ai-options.png)

Full-size battle: [Original](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-battle-original.png) · [AnimeVideo v3](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-battle-video.png) · [x4plus anime](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-battle-anime.png) · [x4plus general](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/scenario2-battle-general.png).

The styles can smooth away texture detail or introduce seams. Background-plane replacements and full-campaign artwork validation remain unfinished. **Off** restores original artwork with your selected native rendering resolution.

Earlier screenshots below use original artwork. Click an image to open its full resolution.

| Scenario 1 USA title | Scenario 2 translated v24 title |
| --- | --- |
| [![Scenario 1 USA title screen](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario1-title.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario1-title.png) | [![Scenario 2 Translation Project v24 title screen](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-v24-title.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-v24-title.png) |

**Internal resolution comparison:** the same captured Scenario 2 video state rendered through the native GPU renderer at each enhanced scale. Supported geometry is rendered at higher resolution; the original textures and sprites retain their original detail. Open the originals to compare edges, since the previews below share the available page width.

| 2× — 640 × 480 | 4× — 1280 × 960 | 5× — 1600 × 1200 |
| --- | --- | --- |
| [![Scenario 2 town at 2x internal resolution](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-2x.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-2x.png) | [![The same Scenario 2 town frame at 4x internal resolution](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-4x.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-4x.png) | [![The same Scenario 2 town frame at 5x internal resolution](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-5x.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-5x.png) |

**Experimental widescreen at 5× (2135 × 1200):** the same scene with an expanded viewport. Edge gaps and missing scenery remain visible where game-side culling needs more work.

[![Scenario 2 town with experimental widescreen and 5x GPU rendering](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-widescreen-5x.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-widescreen-5x.png)

Selected PNG screenshots are hosted as release documentation, outside the source tree and application packages. Local disc data, video dumps, saves and diagnostic captures remain outside the repository.

## What you supply

- **Windows:** Windows 10 (1903 or newer)/11 x64 with .NET Framework 4.8 (normally included), a Vulkan or Direct3D 12 capable GPU, and several GB of free disk space.
- **Linux/macOS preview:** Ubuntu 22.04 or newer x86-64 desktop, or macOS 13 or newer on Apple Silicon/Intel; see [platform requirements](docs/POSIX.md).
- Your own supported **BIN/CUE or standalone CHD** disc: unpatched Scenario 1 USA V1.001, the registered Scenario 2 Translation Project v24 edition or V25.1 on V1.003/V2.000, or the registered Scenario 3 Translation Project v25.1 edition (including V25.1 on the Redump base), or one of these V25.1 discs with the default V25+ add-on selection. Disc contents are verified; a filename or patch label alone is insufficient. The Scenario 1 data-track SHA-256 is:

  `089c77470dec14f1dbffaad1ab50d584f06df5e28b475fc712b8e67e5890f951`

The current runtime provides BIOS services at a high level and **does not load or require a Saturn BIOS file**. No BIOS, game assets, disc images, saves, extracted files or generated game code are distributed here. Do not upload those files, including in issues or pull requests. Other revisions, translation patch versions, parent/delta CHDs and ISO input are not accepted. CHDs are converted locally with bundled chdman; gameplay uses compiled native code.

## Get started

**Windows players:** download [**SFIII-Native-0.6.0-preview1-win64.zip**](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/SFIII-Native-0.6.0-preview1-win64.zip), extract the **whole ZIP**, and open **SFIII Native.exe**. Choose the scenario, your CUE or CHD image and a separate game-data folder, click **Prepare game**, then **Play**. Preparation runs offline using bundled tools; there is no compiler installation, manual compilation, Python installation or terminal command to perform. Keep the application folder after preparation. See [desktop setup](docs/DESKTOP.md).

**Optional artwork experiment:** download [**SFIII-Offline-Graphics-Builder-0.6.0-preview1-Windows-x64.zip**](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/SFIII-Offline-Graphics-Builder-0.6.0-preview1-Windows-x64.zip) separately. Its first run downloads verified upstream tools and prepares its extractor automatically; afterward extraction and GPU upscaling run on your own PC without Codex, API keys or paid services. A full run can take hours and tens of GB across all scenarios/styles. Use **Attach packs to game** when finished, then select **AI graphics** in the game's menu. See [builder setup and limits](docs/DESKTOP.md#optional-graphics-builder).

**Linux/macOS:** download your platform archive from [v0.5.0-alpha](https://github.com/mattkuhnQAAI/sfiii-native/releases/tag/v0.5.0-alpha), extract the complete archive, and open **SFIII Native** (**SFIII Native.app** on Mac). See [portable platform setup](docs/POSIX.md) for preparation and validation limits. Hosted native builds do not certify real-disc gameplay.

The application compiles game code locally during preparation. No generated game executable is distributed. The complete game is still unvalidated; this is an experimental alpha.

**Developers building from source:** clone this repository, install the [build prerequisites](docs/BUILDING.md), then run from PowerShell in its directory:

```powershell
.\tools\setup-dependencies.ps1
.\tools\prepare-game.ps1 -Cue 'D:\Games\SFIII\Shining Force III (USA).cue' -Work 'D:\SFIII-local' -Python python
```

The work folder must be outside the source repository. Preparation inventories your disc, generates and compiles code locally, and writes a launcher into that work folder. It does not upload your disc or saves. Compilation can take substantial time and disk space.

Source builds write **Launch Native SFIII.cmd** into the work folder. See [building](docs/BUILDING.md) for manual steps and [controls, saves and display settings](docs/USAGE.md) for use. Earlier v0.1.0-alpha downloads are source-only; use the current Windows ZIP for automatic preparation.

## Bring your own saves

The included **Import Saturn Saves.cmd** utility lets you bring your existing progress into the native application. The launcher accepts supported raw `SFORCE31_XX.BIN`, `SFORCE32_XX.BIN` and `SFORCE33_XX.BIN` saves, plus version-1 JSON `.ysbak` exports containing those records. Use the matching registered scenario edition. Import-format support does not establish that campaign carryover or every save works in gameplay. Arbitrary `.bin` files, whole backup-memory images and emulator save states are not automatically compatible.

The current Windows file-picker title still says “Scenarios 1 and 2”; the importer also accepts supported Scenario 3 records.

Close the game, click **Import saves** in the desktop application (or open **Import Saturn Saves.cmd** in your work folder), select your exported saves, and choose **internal memory** or **cartridge memory**. Then launch the game and use its own load menu. The importer validates the save layout and checksum, leaves your original export untouched, and backs up an existing destination before writing. The desktop package includes Python; source builds require Python 3.11+. See [save formats and storage details](docs/USAGE.md#game-saves).

## Status and validation

The v0.6.0-preview1 Windows candidate passed **six runtime regression targets and nine focused Scenario 2 transition routes**: 16 checks including emission. Twelve captured scene/style GPU rendering cases and four live style/state continuations also passed with unchanged native frames. These checks cover the available fixtures, rather than every spell, character animation, transition or campaign. Five older route fixtures are unavailable and were not counted as passing or rerun for this candidate.

The local Windows Python suite ran **113 tests: 112 passed and one POSIX-only file-lock test was skipped**. The release source also [passed hosted source/synthetic checks](https://github.com/mattkuhnQAAI/sfiii-native/actions/runs/40089746216), and [Linux, Apple Silicon and Intel Mac candidates passed their build and synthetic-validation jobs](https://github.com/mattkuhnQAAI/sfiii-native/actions/runs/40089746225). Hosted jobs use invented data and do not validate real-disc gameplay, physical controllers or audio devices. The standalone builder's first-run setup, CHD conversion and limited GPU samples were tested separately on Windows.

With the local three-style artwork set, measured warm artwork-pack initialization dropped from **16.9 seconds to 1.16 seconds** after indexing/caching. This measures artwork initialization on one PC, not complete game launch time or a performance guarantee for other hardware.

The Scenario 1 development audit compiles 252 configured/candidate modules, including 169 X1 scene modules. Its nine-route regression suite exercises 12 X1 scenes; it does not validate all 169 scenes. Three mapping/entry reviews, nine discovery findings and 935 unresolved indirect sites remain review items. X046 now has revision-specific loader evidence for compilation; its gameplay path remains untested. Compilation, emitted entry coverage, executed gameplay and hardware fidelity are separate claims.

The Scenario 2 development audit compiles 285 configured/candidate modules, including 195 X1 scene programs and 59 X3 effects. Historical local routes exercised 14 X1 scenes, including imported-save church/town and battle transitions, Healing Drop use through X3ITEM, the magic menu's no-target path, and an Egress return to the church; this historical coverage is separate from the current candidate's nine-route suite above. There are three mapping/entry reviews, 25 discovery findings and 1159 unresolved indirect sites; the larger indirect-site count includes newly compiled effect code. Repeated native snapshots before and during the item animation matched video, audio and backup contents. Repeated native snapshots during the Egress transition also matched video, audio and backup contents. Other effects, unvisited gameplay and other snapshot times remain unvalidated.

The spell/special dispatch tables have also been reviewed: all 81 Scenario 1, 93 Scenario 2 and 111 Scenario 3 entries resolve to mapped modules or container sections. This is static dispatch coverage, not validation of every spell animation. Additional direct loader/call evidence maps X049 in all three scenarios and X025/X048 in Scenarios 2/3. Seven mapping/entry reviews remain across the revisions: X004 in each scenario, and X019/X3ELEM5 in Scenarios 1/2. X019 in Scenarios 1/2 is modeled as monster-table data; its executable classification remains under review. X3ELEM5 does not appear in the reviewed effect dispatch tables and is not assigned a speculative load address.

The v0.3.0 candidate passed the six required runtime regression targets, the four Scenario 2 routes and 64 GPU shader cases, including 5×, plus native-state and toolbar checks. Synthetic tests cover arithmetic/DSP, storage, DMA, background composition, controller input, checkpoint integrity and GPU rendering. Captured gameplay diagnostics and historical route fixtures stay local. See [development and publication rules](docs/DEVELOPMENT.md).

Scenario 3 v25.1 development compiles 273 configured/candidate modules, including 169 X1 scene programs and 69 separately loaded sections of EFFECT.BIN. Eight Windows routes reach the title, fresh-game setup, ship dialogue, a first-battle screen and a priest interior, exercising five X1 scenes. One module mapping/entry, 17 discovery findings and 1299 indirect sites remain review items; individual container effects have not received gameplay validation. The runtime also reports unimplemented VDP2 scroll/extended-color paths in this route; rendering is not certified hardware-faithful. Downloaded completed saves are isolated test fixtures, never installed as player progress. Broader battles, ordinary in-game saving/loading, full campaign transfers and Linux/macOS Scenario 3 gameplay remain unvalidated.

## Upcoming features

Planned directions, with no release dates or compatibility guarantees. Faithful gameplay, graphics, audio and reliable saves take priority over enhancements.

- **Broader gameplay validation:** exercise more towns, battles, menus and transitions, fixing crashes and rendering/audio defects as coverage grows.
- **More complete widescreen:** improve game-side object and terrain culling so additional scenery remains visible as the camera moves and rotates.
- **More supported editions:** register additional disc revisions while keeping each installation’s progress separate.
- **Scenario 2/3 coverage, Premium Disc and more translations:** expand gameplay validation and add revision-specific compilation profiles, identifying supported patch versions explicitly.
- **Save portability and campaign transfers:** expand verified emulator/Saroo import and export formats, and validate backed-up progress transfers between compatible scenarios and editions.
- **Broader optional artwork coverage:** improve unsupported formats, large images, background planes, texture seams and animation consistency, while retaining original artwork and expanding gameplay validation.
- **Broader native save-state validation:** extend snapshot tests across battles, transitions and additional scenarios, beyond the limited Scenario 2/3 routes tested so far.

These are roadmap items; the supported profiles and tested coverage are listed above.

## Support development

If you'd like to support development and testing, you can [leave a tip via PayPal](https://paypal.me/mattkuhn14). Contributions are optional; the project remains free, and no game data is provided.

## Credits and licensing

Built on [saturn-recomp](https://github.com/callumjenkins/saturn-recomp), derived from [saturnkit](https://github.com/vs-sr-dev/saturnkit). SFIII module mappings were informed by [SF3Tools](https://github.com/Synival/sf3tools). Controller mappings use [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB). Rendering and input use [SDL3](https://github.com/libsdl-org/SDL).

Project source is provided under the [MIT license](LICENSE); third-party code retains its own notices. See [THIRD_PARTY.md](THIRD_PARTY.md). Shining Force III and Sega Saturn belong to their respective rights holders. This is an unofficial project and includes no permission to redistribute game content.
