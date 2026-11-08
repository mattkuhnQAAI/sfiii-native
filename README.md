# Shining Force III Recomp — Scenarios 1, 2 & 3

**Shining Force III Recomp (Shining Force 3 Recomp)** is an experimental native PC static recompilation of Sega Saturn's **Shining Force III**. It supports the original USA Scenario 1 and selected translated editions of Scenarios 1–3 through exact-content disc profiles. The application generates C++ from your own disc's SH-2 code and compiles it locally using bundled tools. The runtime supplies Saturn graphics, sound, storage and other hardware services. This is a static recompilation, rather than a byte-matching source decompilation.

## Latest improvements — v0.9.0

- **2160-line rendering and SSAA:** choose **1× / 2× / 5× / 9×** internal resolution, with optional **four-sample supersampling**. In a 240-line scene, 9× produces 2160 lines. Resolution and SSAA remain separate from window scaling and game speed.
- **Five display filters:** **Nearest, Bilinear, Integer smooth, Bicubic and Lanczos**. Bicubic and Lanczos run on the GPU and support shrinking as well as enlargement. The toolbar cycles filters; the menu explains each choice.
- **Reduced vertex and camera-depth stepping:** guarded vertex precision recovers fractional projected positions for reviewed Scenario 1–3 BOOT families. Higher-precision perspective division reduces another source of stepping as depth changes. Earlier camera-angle, object-transform and keyframe rounding remain unresolved.
- **Sharper supported backgrounds:** enhanced sampling for eligible rotation floors, transparent-plane interiors and RGB888 sources, with ordinary floor blending and signed colour offsets. Native composition now implements extended colour calculation and line scroll/zoom used in captured game scenes. Complex background paths still retain native sampling or remain unresolved.
- **Smoother audio:** presentation catches up on repeated VDP1 fields while retaining new world draws and every game/audio step. Device read-ahead no longer causes unnecessary playback restarts. Capture PNG encoding runs in a bounded background worker.
- **Better display controls:** fullscreen in the menu, F11/Alt+Enter, saved VSync, and an Auto/Shown/Hidden toolbar with F10 access.
- **Apple Silicon Mac package:** alongside Windows x64 and Linux x64. Intel Mac is excluded. Platform build and synthetic tests are separate from real-disc gameplay validation.

See the [v0.9.0 release notes](docs/release-v0.9.0.md), [display controls](docs/USAGE.md#display) and [validation details](docs/DEVELOPMENT.md#presentation-performance-checks). **Use original graphics (AI graphics Off) with 16:9.** Optional AI artwork can misalign or pop in widescreen.

## Downloads

The latest release is [**v0.9.0**](https://github.com/mattkuhnQAAI/sfiii-native/releases/tag/v0.9.0). Extract the complete application archive. Players do not install a compiler, Python, Homebrew or Xcode, or enter compilation commands; **Prepare game** uses the bundled tools.

| Platform | Download | Validation scope |
| --- | --- | --- |
| Windows 10/11 x64 | [Windows application ZIP](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.9.0/SFIII-Native-0.9.0-win64.zip) | Local gameplay routes, GPU/display tests, audio-device checks and limited native-state continuations. |
| Linux x86-64 | [Linux archive](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.9.0/SFIII-Native-0.9.0-linux-x86_64.tar.gz) | Hosted native build and synthetic validation; real-disc gameplay and physical-device behavior remain unvalidated. |
| Apple Silicon macOS 13+ | [Apple Silicon app archive](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.9.0/SFIII-Native-0.9.0-darwin-aarch64.tar.gz) | Hosted ARM64 build and synthetic validation; real-disc gameplay, physical controllers and hardware audio remain unvalidated. Ad-hoc signed, not Apple-notarized. |
| Optional Windows graphics builder | [Separate builder ZIP](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.9.0/SFIII-Offline-Graphics-Builder-0.9.0-Windows-x64.zip) | Builds experimental artwork locally from your own discs. In-game artwork checks are strongest in Scenario 2; AI widescreen remains unsupported. |

Checksums and platform validation summaries accompany the release. Builds contain **no disc images, BIOS, extracted assets, generated game programs, artwork packs or saves**. No BIOS is required by this runtime. Complete campaigns and hardware-faithful rendering/audio have not been fully validated.

## Registered game editions

These **exact-content profiles** ship on all three platforms. Disc identification was checked locally for all six. A filename or translation-version label alone does not establish compatibility. Current Windows route checks cover four installed editions; the table distinguishes older gameplay evidence from current checks.

| Game and registered edition | Current Windows evidence |
| --- | --- |
| Scenario 1 — USA MK-81383 V1.001, unpatched | Current title and fresh-intro routes; older town/battle/transition checks. Full campaign unvalidated. |
| Scenario 1 — Translation Project v25.1 | Current intro, original-save load and fresh-opening routes. Broader gameplay unvalidated. |
| Scenario 2 — Translation Project v24 | Current isolated producer checks for its BOOT family. Towns, battles, items/spells and snapshots were exercised on earlier builds; no complete campaign claim. |
| Scenario 2 — v25.1 migrated from the registered v24 image | Experimental exact locally migrated image. Current producer-family checks; limited intro/save/transition evidence on earlier builds. This does not establish every v25.1 variant or original-Saturn compatibility. |
| Scenario 2 — supplied V1.003 image, header reports V24 | Exact translation version unverified. Eight current routes include title, town/house, player saves, camera/battlefield and native-state continuations. Local high-resolution performance/audio comparisons also use this image. |
| Scenario 3 — Translation Project v25.1 | Current title, recorded dream and fresh ship-introduction routes. Earlier first-battle/priest-interior evidence; broader battles and campaign transfers unvalidated. |
| Premium Disc / unregistered revisions | No profile. Deferred until separately identified and tested. |

The launcher separates original USA Scenario 1, translated Scenario 1, translated Scenario 2 and translated Scenario 3, each with independent game-data paths. Unknown revisions produce a local `unregistered-disc.json` report without replacing progress or builds. Native compilation requires reviewed executable mappings for the exact image. Static compilation, emission, executed routes and hardware fidelity are separate claims.

## Playing and updating

1. Extract the **whole archive** into a new application folder. Open **SFIII Native.exe** on Windows, **SFIII Native** on Linux, or **SFIII Native.app** on Apple Silicon macOS.
2. Choose the scenario, your supported **CUE or CHD**, and a separate writable game-data folder. Keep a CUE's BIN tracks beside it.
3. Click **Prepare game**, wait for automatic local preparation, then **Play**. Keep the application folder: it contains tools used by preparation and save import.
4. For 16:9, leave **AI graphics Off**. Start with 5× and SSAA Off, then increase quality if performance permits.

When updating from a published download, select your existing scenario data folder and run **Prepare game** again using the new application. Ordinary saves remain in that separate folder. **Native quick states and replay checkpoints are build/disc-specific:** retain the previous application/prepared build for its states, and use an ordinary in-game save to move progress between releases. A locally checked checkpoint migration is not automatic release-wide state compatibility.

Controllers include SDL mappings for PlayStation, Xbox and generic devices, plus a binding/test utility for Saturn USB and other pads. The Saturn USB pad has physical local validation; that does not certify every controller model. **F5/F9** save/load native states, **F6** cycles game speed, **F8** captures an issue, **F10** toggles the toolbar, and **F11** toggles fullscreen. Escape opens the menu.

The save manager **imports and exports individual BIN/YSBAK saves** for compatible emulator/Saroo workflows. Internal memory and cartridge memory are separate persistent backup files. Exported ordinary saves can be used with compatible Saroo save editors; native quick states cannot be loaded on a Saturn. Back up progress before importing or switching editions. See [controls, saves and display](docs/USAGE.md).

Platform details: [Windows setup](docs/DESKTOP.md), [Linux/Apple Silicon setup](docs/POSIX.md), [developer source builds](docs/BUILDING.md).

## Optional local AI artwork

The separate Windows builder uses your own Vulkan GPU to generate **Animated / Illustrated / General** artwork from registered discs. Extraction, inference and packaging run locally, with resumable work, cached GPU results and indexed packs. First-run setup downloads verified upstream tools; subsequent processing needs no Codex session, API key or subscription. Full processing can take hours and tens of GB.

World textures, CHR/CHP character frames and supported close-up battle-model textures are included where decoded and matched. Menus/HUD retain original artwork. Unsupported formats, background-plane replacements, seams and complete character/animation/campaign coverage remain unfinished. One Scenario 3 battle-model format remains unsupported. These are precomputed artwork replacements, not DLSS or per-frame AI inference.

When generation finishes, use **Attach packs to game** in the builder or **Optional graphics...** in the Windows launcher and select that exact revision's playable pack report. Keep the output at its current path and restart the game. No full regeneration is required merely to update this renderer. Choose **AI graphics Off** or **Use original graphics** to return to original artwork. For widescreen, use original artwork. [Builder setup and limits](docs/DESKTOP.md#optional-graphics-builder).

## Screenshots

These older Windows captures illustrate actual native rendering and optional artwork. They do not establish new-platform gameplay compatibility or show every v0.9.0 feature. Images are release documentation, not downloadable asset packs.

[![Airo village: Original, Animated, Illustrated and General](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/airo-ai-options.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/airo-ai-options.png)

[![Battle formation: Original, Animated, Illustrated and General](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/battle-ai-options.png)](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.6.0-preview1/battle-ai-options.png)

| Scenario 1 USA title | Scenario 2 translated title |
| --- | --- |
| ![Scenario 1](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario1-title.png) | ![Scenario 2](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-v24-title.png) |

| 2× rendering | 5× rendering |
| --- | --- |
| ![2x](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-2x.png) | ![5x](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-5x.png) |

![Experimental original-graphics widescreen](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.4.0-preview1/screenshot-scenario2-widescreen-5x.png)

## Status and validation

The Windows development candidate passed the six required runtime regressions and compiled-entry emission checks, plus **16 external gameplay routes** across the four locally installed editions. One Scenario 3 ship frame changed from corrected colour blending; it was reviewed while its video-memory/register dump remained identical. There are **213 GPU shader cases**, exercised locally on Vulkan and Direct3D 12, and scaler/UI/state checks. At 9× with SSAA and vertex precision, the recorded Scenario 2 route measured about **59.7–59.8 fields/sec** for Nearest, Bicubic and Lanczos, with zero measured audio shortages. This is one route on one PC, not a universal performance guarantee or proof that every sound path is exact.

Source tests, static module findings, native-state coverage and hardware-model fidelity remain separate. Unvisited indirect targets, scenes, spells and transitions remain unvalidated. Linux and Apple Silicon packages receive native compilation, invented-data CPU/storage/CHD/UI/state tests and shader tests when a usable runner GPU is available. Missing GPU/physical-device/game fixtures are reported as unavailable, not passing gameplay validation. No game data is uploaded to CI.

Remaining limits include earlier camera-angle/object-transform quantization, complex coefficient/window/background paths, incomplete game-side widescreen culling, optional AI widescreen alignment, high-resolution capture hiccups and full-campaign validation. Resolution stays selected; sustained rendering overload can exceed the available performance. [Development details](docs/DEVELOPMENT.md) and [previous release evidence](docs/release-v0.8.0.md).

## Upcoming features

Priorities are faithful gameplay, graphics, audio and reliable saves: expand towns/battles/menu/spell/transition coverage; improve remaining camera/background/widescreen paths; validate more save transfers and snapshot positions; and register additional exact disc editions. Premium Disc remains deferred. Broader artwork extraction/matching and platform gameplay checks need separate validation. No completion dates or universal compatibility guarantees are implied.

## Support development

If you'd like to support development and testing, you can [leave a tip via PayPal](https://paypal.me/mattkuhn14). Contributions are optional; the project remains free and supplies no game data.

## Credits and licensing

Built on [saturn-recomp](https://github.com/callumjenkins/saturn-recomp), derived from [saturnkit](https://github.com/vs-sr-dev/saturnkit). SFIII format/module research uses [SF3Tools](https://github.com/Synival/sf3tools). Controller mappings use [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB); rendering and input use [SDL3](https://github.com/libsdl-org/SDL).

Project source is [MIT licensed](LICENSE); third-party code retains its notices. See [THIRD_PARTY.md](THIRD_PARTY.md). Shining Force III and Sega Saturn belong to their respective rights holders. This unofficial project grants no permission to redistribute game content.
