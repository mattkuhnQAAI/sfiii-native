# Building on Windows

Players using the Windows desktop package do **not** need these steps; see [automatic desktop setup](DESKTOP.md). This page describes the developer source-build workflow.

Use a Windows x64 machine with Git for Windows, Python 3.11+, LLVM/Clang, and Visual Studio 2022 Build Tools (or Community) with **Desktop development with C++**, a Windows SDK, and the CMake tools component. LLVM 21.1.8 is the tested compiler. Put `python`, `clang` and `clang++` on PATH. The build scripts locate Visual Studio with `vswhere` and use its CMake/Ninja tools. Run in PowerShell; if local script execution is disabled, use `powershell -ExecutionPolicy Bypass -File SCRIPT ...` for the script you intend to run.

## 1. Dependencies

```powershell
.\tools\setup-dependencies.ps1
```

This downloads the exact revisions in `dependencies.json` into ignored `.local/`: saturn-recomp, SDL3 3.4.18, DirectX Shader Compiler 1.9.2609, and SDL_GameControllerDB. Archive hashes are verified. The source runtime patch supplies the fixes and presentation features used by this project. The historical patch filename is `tools/runtime-diagnostics.patch`; it contains essential runtime/compiler changes, not captured game data. Keep it when copying the source.

Setup does not install Visual Studio or LLVM, and does not download any game or BIOS data. Existing dependency checkouts at another revision are rejected rather than overwritten. Some upstream dependency files retain their original permissive licenses; see the upstream notices. The optional GPL rasterizer is off in this build.

## 2. Prepare your disc

Keep all tracks referenced by your CUE beside it. The source commands below target unpatched Scenario 1 USA MK-81383 V1.001, data-track SHA-256 `089c77470dec14f1dbffaad1ab50d584f06df5e28b475fc712b8e67e5890f951`. A header match alone is insufficient. The generation profile rejects other data-track hashes.

```powershell
.\tools\prepare-game.ps1 -Cue 'D:\Games\SFIII\Shining Force III (USA).cue' -Work 'D:\SFIII-local' -Python python
```

Choose a new work directory outside every Git repository. The script hashes and inventories the dump, extracts files, audits executable modules, generates C++, checks emitted entry points, and compiles a windowed executable. Everything derived from the game stays in that external folder. If interrupted, preserve any saves/checkpoints before retrying; the preparation wrapper refuses an already active build folder. It never updates your existing playing installation automatically.

The first build is substantial. Required disk space and build time have not been characterized across PCs. Avoid treating a completed build as full-game compatibility certification.

## 3. Launch

Open `D:\SFIII-local\Launch Native SFIII.cmd`. The external `game.json` records the original CUE path; update it if you move the dump. Disc data must remain available while playing. The build uses high-level BIOS services and has no BIOS-file parameter.

The work folder includes `build-bulk/saturn.exe`, SDL3, GPU shaders, controller helpers/mappings, save import scripts and launch scripts. These are local build products; do not push them to GitHub or publish a generated game executable as a release.

## Manual stages

From the source directory:

```powershell
python tools/probe.py --cue 'D:\Games\SFIII\Shining Force III (USA).cue' --framework .local/saturn-recomp --out 'D:\SFIII-local'
.\tools\build-scenario1.ps1 -Work 'D:\SFIII-local' -Python python
```

The manual build leaves launcher activation unchanged for testing. `prepare-game.ps1` supplies external configuration and selects the first build in a new work folder. Keep prior runnable builds and compatible checkpoint seeds when developing updates.

## Optional Windows branding

Keep artwork outside the source repository. `tools/package-desktop.py --icon 'D:\Artwork\sfiii.ico' --out ... --cache ...` embeds a locally supplied ICO in the Windows launcher and carries it in the package for subsequent native game preparation. The game applies its embedded icon to both native window icon sizes, including the title bar and taskbar. The icon participates in the package identity and file inventory. Source/manual builds can set `SF3_APP_ICON` to the external ICO path before building, or pass `-AppIcon` to `tools/build-headless.ps1`. Builds without a supplied icon retain the platform default. No artwork is fetched automatically or added to Git.
