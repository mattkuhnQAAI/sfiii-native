# v0.9.1 — Loader, save and runtime fixes

- Fixes Saturn RAM and backup-memory mirroring, native returns across overlay calls, scene callback discovery, and a recompiler edge case involving stores in branch delay slots.
- Flushes backup memory before replacing the save file and restores native states saved between an overlay load and its first call.
- Caches overlay dispatch and balances background composition across CPU workers.
- Returns complete CD status replies and prevents reserved sound-register writes from overwriting DSP delay taps.
- Registers seven experimental exact disc profiles, including three additional v25.1 images and four default V25+ add-on combinations. See the [edition table](../README.md#registered-game-editions).

Thanks to [damiantw](https://github.com/damiantw) for the contributions behind these fixes. See the [PR review](pr-review-2026-11.md) for individual changes and deferred work.

## Download and update

Applications are available for Windows x64, Linux x86-64, and Apple Silicon macOS 13+. Extract the full archive and use **Prepare game** with your own supported CUE or CHD. The tools are bundled. No BIOS is required. The optional Windows graphics builder remains a separate download on the [v0.9.0 release](https://github.com/mattkuhnQAAI/sfiii-native/releases/tag/v0.9.0).

To update, extract into a new application folder and prepare against your existing scenario data folder. Ordinary in-game saves stay in that folder. Keep the previous application and prepared build for its build-specific native states and replay checkpoints. The launcher save manager imports and exports individual BIN and YSBAK saves.

Use **AI graphics Off** with 16:9. Optional artwork can misalign in widescreen.

## Validation

Windows checks cover fresh disc preparation, save import, runtime regressions, and 16 recorded routes across original and translated Scenario 1, the supplied Scenario 2 V1.003 edition, and translated Scenario 3. The seven new disc profiles lack local gameplay fixtures. Linux and Apple Silicon packages use hosted native builds and synthetic tests. Gameplay and physical-device behavior on those platforms remain unverified. The macOS app uses an ad-hoc signature.
