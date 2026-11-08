# v0.9.0 — Display, background and audio improvements

## New in this release

- 9× / 2160-line internal rendering and optional four-sample GPU SSAA.
- Nearest, Bilinear, Integer smooth, GPU Bicubic and GPU Lanczos display filters.
- Saved vertex-precision and sharper-background controls for reviewed Scenario 1–3 profiles; bounded higher-precision perspective reduces camera-depth scale stepping.
- High-resolution sampling for supported transparent rotation-plane interiors, RGB888 sources and ordinary floor blending.
- Extended colour calculation and NBG0/NBG1 line scroll/zoom, with tests of enabled table fields, intervals and colour-format restrictions.
- Fullscreen in the menu, F11/Alt+Enter, toolbar Auto/Shown/Hidden with F10, and persistent VSync.
- Audio-friendly catch-up on repeated VDP1 fields, improved device read-ahead handling, faster PNG encoding and bounded asynchronous capture export.
- Windows x64, Linux x86-64 and Apple Silicon ARM64 application builds. Intel Mac is excluded.

All application packages bundle preparation tools. Players supply a registered CUE/CHD and use **Prepare game**; no compiler, Python, Homebrew or Xcode installation is needed. No disc, BIOS, extracted asset, generated game program, artwork pack or save is distributed. The optional Windows graphics builder is separate; generation stays local.

## Starting settings and updating

Start at 5× with SSAA Off, then increase quality. 9× with SSAA is demanding. Display filters affect final resizing and can look similar at high internal resolutions. Vertex precision and sharper backgrounds are saved switches; unsupported paths retain native output. Use **AI graphics Off** with 16:9.

Extract the new application into a separate folder, select your existing scenario data folder and run **Prepare game** again. Ordinary in-game saves remain separate and persistent. Keep the old application/prepared build for old native quick states and replay checkpoints: these require an exact build/disc identity. The locally tested checkpoint migration is not automatic compatibility for public downloads. Use ordinary saves between releases.

## Validation and limits

- Six required native runtime regressions and compiled-entry emission checks pass locally.
- Sixteen Windows routes cover original/translated Scenario 1, supplied Scenario 2 V1.003 and translated Scenario 3. The expected Scenario 3 blending change was reviewed; its raw video dump is unchanged.
- 213 shader cases pass locally on Vulkan and Direct3D 12. Five-filter GPU tests cover resizing, alpha overlays, constants, minification and target restoration; UI tests cover persistence and switching.
- Five supplied BOOT files representing four unique families pass isolated projection/formatter tests. This is separate from campaign coverage.
- The recorded Scenario 2 route with 9×, SSAA and vertex precision measured about 59.7–59.8 fields/sec with Nearest, Bicubic and Lanczos and zero measured audio shortages. These are local measurements, not a performance guarantee.
- Hosted Linux/Apple Silicon validation uses invented data and bundled tools. It cannot validate real-disc gameplay, physical controllers or audio devices. Shader checks require a usable GPU/display; unavailable checks remain labelled unavailable.

Earlier camera-angle/transform/keyframe rounding, several complex VDP2 paths, widescreen culling gaps and full-game coverage remain unresolved. F8 GPU readback can still cause a brief high-resolution capture interruption. Optional AI artwork is experimental and is not a supported widescreen path.

The Apple Silicon app is ad-hoc signed and not Apple-notarized. macOS may require explicit approval to open it. Linux targets Ubuntu 22.04 or newer x86-64 desktops with their normal graphics/system libraries; no Steam Deck or every-distribution certification is implied.
