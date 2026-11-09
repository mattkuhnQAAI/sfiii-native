# Linux and Apple Silicon macOS

v0.9.1 supplies **Linux x86-64** and **Apple Silicon ARM64** packages alongside Windows. Intel Mac is excluded. Download the appropriate archive from [v0.9.1](https://github.com/mattkuhnQAAI/sfiii-native/releases/tag/v0.9.1).

## Player setup

Extract the complete archive. On Linux open **SFIII Native**; the `.sh` wrapper is also available. On Apple Silicon macOS move **SFIII Native.app** to a local application location and open it. The Mac app is ad-hoc signed and not Apple-notarized; macOS may require explicit approval in Privacy & Security. Do not disable system security globally.

Choose a registered Scenario 1, 2 or 3 image, select its CUE or standalone CHD, and use a separate writable game-data folder for each edition outside the application. **Prepare game** performs local generation/compilation using bundled Python, Zig C/C++ tools, SDL, CMake, Ninja and chdman. Players do not install a compiler, Python, Homebrew or Xcode. Click **Play** when preparation completes. No game data or BIOS is supplied or uploaded; no BIOS is required by this runtime.

Linux targets Ubuntu 22.04 or newer x86-64 desktops with normal X11/Wayland, audio and Vulkan drivers. Apple Silicon targets macOS 13 or newer and uses Metal shaders generated from the same project-owned shader source. A hosted package build does not establish every distribution, GPU, controller or Steam Deck configuration.

## Display, controls and saves

The common runtime includes 1×/2×/5×/9× rendering, optional four-sample SSAA, five display filters, vertex precision, sharper supported backgrounds, fullscreen and saved toolbar/VSync settings. Actual performance and feature availability depend on the GPU/backend. Start at 5× with SSAA Off. **Keep AI graphics Off in 16:9**; optional artwork is not a supported widescreen path.

Controller setup/testing, ordinary saves, BIN/YSBAK imports/exports and native states are carried over. F5/F9 save/load direct machine snapshots; legacy replay actions remain separate. Before loading a state, the session retains exact pre-load internal/cartridge files. Stop the game before setup/import; native locks prevent concurrent operations against one installation. The Windows offline artwork builder is a separate optional download; generating packs is not a requirement to play.

When updating, extract a new application folder, choose the existing scenario data folder and run **Prepare game** again. Ordinary in-game saves remain persistent in that data folder. Native snapshots/replays require the exact executable and disc identity; keep the previous application/build for old states, or transfer progress using ordinary saves. A locally checked state migration does not establish automatic compatibility across releases or operating systems.

## Validation scope

The portable workflow builds on each native CPU/OS with only reusable source and invented fixtures. It validates bundled tool relocation; synthetic CPU/DSP, storage, background, audio-buffer, controller/UI and native-state behavior; synthetic SH-2 emission/execution; and a mixed invented data/audio CHD round trip. It also checks independently invoked chdman's library dependencies. No Saturn emulator application enters the package.

GPU shader/filter checks run when the runner exposes a usable GPU/display. Software Vulkan checks are separate from physical GPU testing. Missing devices or fixtures are reported **unavailable**, not passing gameplay validation. Real-disc transitions, towns/battles, physical controllers and audio devices on Linux/macOS remain unvalidated until exercised locally with retained input, backups, clock and disc/build identities. Windows gameplay evidence does not certify another OS/compiler/backend.

Maintainers use `.github/workflows/portable.yml` and `desktop/posix-dependencies.json`. Exact dependencies, checksums, notices and corresponding CHD-tool source travel with each package. Archives and validation summaries are attached to the release; all caches, builds and game-derived files stay outside the source repository.
