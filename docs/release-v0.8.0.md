# v0.8.0 — official widescreen release

This release makes the widescreen renderer available for the registered Scenario 1–3 editions. It expands the original Saturn viewport to 16:9 while preserving the native game framing and uses the GPU path for the additional geometry. Windows hybrid-GPU systems request the high-performance adapter before SDL initializes.

Use **AI graphics: Off** with widescreen. The optional local artwork packs are still experimental and can misalign, pop in, or leave seams when the viewport is expanded. Original game graphics are the supported widescreen presentation in v0.8.0.

Included:

- Windows x64 first-run package with local preparation for CUE and CHD images.
- Linux x86-64 package built by the portable workflow.
- Optional Windows offline graphics builder; it is not required for widescreen.
- Registered USA Scenario 1 and translated Scenario 1–3 profiles, each kept in its own external game-data folder.
- 1×, 2× and 5× GPU rendering, native save states, save import/export, controller setup and persistent internal/cartridge backup storage.

Validation:

- `dsp_regression`, `core_regression`, `bup_regression`, `machine_regression`, `background_regression` and `audio_buffer` pass.
- The compiled-entry check passes.
- The available Scenario 2 V1.003 title, town/house and current-player-save routes pass with unchanged review frames.
- Complete campaigns, every scene, all spell/character paths, and real-disc Linux gameplay remain unvalidated.

macOS is not included in this release. Intel Mac packages are no longer published; an Apple Silicon package will return when a hosted build is available.
