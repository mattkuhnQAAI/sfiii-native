# Automatic Windows preparation

## v0.9.1

Download [**SFIII-Native-0.9.1-win64.zip**](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.9.1/SFIII-Native-0.9.1-win64.zip), extract the complete ZIP to a new application folder and open **SFIII Native.exe**. Players do not install a compiler or enter build commands. Choose your supported disc and separate scenario data folder, then **Prepare game** and **Play**. If updating, prepare against your existing scenario data folder to retain ordinary saves; keep the old application and build for their build-specific quick states.

v0.9.1 improves loader and save reliability, caches overlay lookups, and adds experimental disc profiles. It includes **9×/2160-line rendering**, optional four-sample SSAA, five display filters including GPU Bicubic/Lanczos, vertex precision, and sharper supported backgrounds. It retains original-graphics 16:9, optional artwork, indexed pack loading, controller setup and save import/export. Rendering choices are **1× / 2× / 5× / 9×**. Use **AI graphics > Off** with 16:9 because optional artwork can misalign or pop in. See [release notes](release-v0.9.1.md) and [optional graphics setup](#optional-graphics-builder).

Linux x64 and Apple Silicon macOS archives are attached to [v0.9.1](https://github.com/mattkuhnQAAI/sfiii-native/releases/tag/v0.9.1), subject to the validation limits in [portable setup](POSIX.md). Intel Mac is excluded. Windows gameplay checks do not establish Linux/macOS gameplay compatibility. The optional builder remains Windows x64 only.

## v0.5.0-alpha additions

v0.5.0-alpha adds Scenario 3 translated-v25.1 preparation, save import and a launcher entry, plus optional 1×/2×/3× game speed. Use 2× initially; normal speed remains the startup default, and rendering resolution stays fixed. A 600-field Scenario 3 battle-menu comparison at 5× resolution measured approximately 2× actual speed for the 2× request and 2.27× for the 3× request, with matching native/GPU frames, virtual PCM and backup contents. This does not certify complete battles or campaigns, and 3× remains experimental.

The Windows controller-setup button now opens its persistent interactive console so detection messages remain visible. Maintainers can supply an external ICO to brand the launcher and native game; artwork stays outside the source repository. These changes are included in v0.5.0-alpha.

## Scenario 3

v0.5.0-alpha adds a separate **Scenario 3 Translation v25.1** launcher entry, disc-content profile and data folder. Only the registered translated dump is accepted; an original Japanese filename does not establish its contents. Earlier Windows v0.3.0 and Linux/macOS v0.4.0-preview1 downloads do not contain this profile. Initial Windows routes reach the title, fresh-game setup, ship dialogue, a first-battle screen and a priest interior. Eight route checks passed without changes to reviewed native frames; five of 169 scene modules were exercised. Complete-game, broader battle, ordinary save/load, full campaign-transfer and Linux/macOS Scenario 3 compatibility remain unvalidated.

The importer accepts Scenario 3 raw `SFORCE33_XX.BIN` payloads (24344 bytes) and matching version-1 JSON YSBAK exports. Internal storage fits one such save; cartridge storage is preferable for earlier-scenario carryover files. Source/generated coverage, observed loading addresses and executed routes remain separate claims.

The release also includes optional Light AA, expanded effect and scene-module compilation, and native-state/loader regression checks. Existing users should extract the new application to a separate folder and run **Prepare game** with their existing scenario data folder. In-game saves remain there. Native quick states require the matching executable/disc: retain the previous application and build for old states and use ordinary in-game saves to move progress.

## v0.3.0-alpha changes

- Select Scenario 1 USA V1.001 or the registered Scenario 2 Translation Project v24 edition, each with its own disc path and game-data folder.
- Use BIN/CUE or standalone CHD input. Bundled chdman converts CHDs locally; users install no extra tools.
- Import supported Scenario 1/2 raw BIN saves or version-1 JSON YSBAK exports into internal or cartridge storage.
- Save/load direct native machine snapshots with F5/F9 or the toolbar. Separate legacy replay actions remain available.
- Choose 1×, 2×, 4× or 5× supported geometry rendering through Vulkan or Direct3D 12. Original texture detail remains unchanged.
- Launcher preferences persist beside the application when writable, with a fallback to the existing per-user settings location.

Scenario 2 remains experimental. The recorded route suite reaches startup, the opening sequence, docks, and imported-save church/town transitions; it does not certify battles, later chapters, campaign transfers or complete-game compatibility. Static mapping/discovery findings remain separate from executed coverage. Widescreen can still expose missing terrain or objects at the edges because the game retains original view-culling assumptions.

Each selector remembers its own disc and data folder. Keep separate folders for every scenario; setup rejects switching revisions inside an existing installation. Internal/cartridge progress, native states and replay checkpoints stay with that installation. Close the game before importing saves or updating.

CHDs are verified and extracted locally with bundled `chdman` during preparation, then the native game reads the extracted CUE/BIN. This is a disc-container conversion, not an interpreter gameplay path. The original CHD remains unchanged. Allow additional space for the decompressed disc and keep `disc-cache` in the selected data folder. Its receipt records the source and extracted-file hashes; a changed cache is rejected. Parent/delta CHDs and ISO input are not supported. Disc identity checks include the IP area, every filesystem file and audio tracks, rather than trusting a filename or translation label.

CHD input details are recorded in `disc-input.log`; compilation details remain under `preparation`. Extraction, preparation and save files never enter the source repository. The bundled MAME 0.289 tool requires an x86-64-v2 CPU. Its notices and corresponding source archive accompany the package.

Download the **Windows ZIP asset** from the current release, rather than GitHub's automatically generated source archive. Extract the entire ZIP and open **SFIII Native.exe**. The included compiler, embedded Python, build tools and runtime source are application components; players do not install them separately or enter commands.

## First run

1. Select the scenario and click **Browse** for your supported CUE or CHD image. Keep all referenced BIN tracks beside a CUE.
2. Keep the default game-data location or choose a writable folder outside the application and every source repository. Allow several GB of free space.
3. Click **Prepare game**. Disc checks, extraction, native code generation and compilation happen locally and offline. Progress appears in the window; preparation can take several minutes.
4. When ready, click **Play**. Controller setup and save import are available from the same window.

Windows 10 (1903 or newer)/11 x64 with .NET Framework 4.8 and a suitable Vulkan/Direct3D 12 driver is the target. The application is unsigned, so Windows may identify it as an unknown publisher. No BIOS file is required. The current Windows package accepts the registered Scenario 1 USA V1.001, Scenario 2 translated-v24 and Scenario 3 translated-v25.1 revisions described above. The Premium Disc, other patches, parent/delta CHDs and ISO input are unsupported.

Keep both the application and game-data folders. The original disc dump remains necessary while playing. Saves live in the separate game-data folder, not inside the downloadable package. Reopening the app remembers the selected paths. Changing a disc's location requires preparing its new path again. Updates retain the old build and saves. Native states and replay checkpoints are tied to their exact executable/disc identity: create a new quick state after an update. Older slots stay intact for their original build. Loading a native state restores its captured internal/cartridge memory; exact pre-load backups remain in that session folder.

## Optional graphics builder

The [**SFIII-Offline-Graphics-Builder-0.8.0-Windows-x64.zip**](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.8.0/SFIII-Offline-Graphics-Builder-0.8.0-Windows-x64.zip) is a separate optional Windows experiment. It generates artwork from your own supported Scenario 1–3 CUE/CHD files. No commercial disc, BIOS, extracted game artwork, generated artwork packs or saves are included in either download.

1. Extract the **whole builder ZIP** to a writable folder outside the application, source repositories and game-data folders. Open **Start Offline Graphics Builder.cmd**.
2. Allow first-run setup to finish. It downloads pinned upstream archives with checksum verification, restores dependencies and prepares the extractor automatically. It needs internet access and several GB of space; you install no compiler or Python separately and perform no manual compilation. The CHD utility downloads the first time a CHD is selected.
3. Select your supported discs, a separate output folder, your Vulkan GPU index and **All three** styles. Leave **Include CHR/CHP characters** checked. GPU numbering varies by PC; choose the dedicated GPU where available.
4. Try **Quick check** to test a small sample, then choose **Start / Resume** for a full run. Generation can take hours; cache plus packs for all scenarios/styles can require tens of GB. **Stop safely** preserves completed work. Closing the builder window or Codex leaves a running worker active. Reopen the builder to monitor it.
5. Review **Open output / reports**, including errors and unsupported formats. A full run automatically builds indexed world/character packs; **complete with gaps** still requires reviewing the listed failures. Quick checks do not produce complete character packs.
6. Close the game, choose **Attach packs to game**, and select its prepared scenario data folder containing `game.json`. Repeat for each scenario you use. Alternatively, select that scenario/data folder in the native launcher, click **Optional graphics...**, and choose the combined `playable-packs/TIMESTAMP/packs-report.json` from the builder output. Attachment verifies the disc profile and requires all three styles.
7. Restart the game and choose **Menu > AI graphics > Off / Animated / Illustrated / General**. Use **Off**, the launcher's **Use original graphics**, or the builder's **Return game to original graphics** to return to original artwork.

In the current source version, those game and builder choices are named **Animated / Illustrated / General**. Existing settings and generated packs continue to use the same underlying style identifiers.

After first-run setup, extraction and GPU upscaling use local hardware without an API key, Codex tokens or paid service. No game data is uploaded. Generated files and downloaded tools remain external; do not redistribute the game-derived output or locally prepared SF3Tools extractor/resources.

The pack covers decoded CHR/CHP character frames and eligible world textures, rather than every game object or every file format. Menus/HUD and unmatched materials retain original artwork. Source transparency is preserved. Background-plane replacement, some large images, tile seams and animation consistency need further work. Extracting all known frames does not establish full character matching or campaign validation. Available Scenario 2 Windows checks demonstrate limited routes and captured rendering cases; other scenarios and platforms need broader AI gameplay validation. View [same-frame examples in the gallery](../README.md#screenshots).

Keep attached output at the same location: the game reads those packs directly and loads replacement pixels when needed. A generated startup index reduces scanning; missing or invalid indexes fall back to directory loading. For your own backups, `archive_packs.py` can create verified lossless ZIPs. Extract and reindex archived packs before attachment; the game does not read compressed ZIPs. Attachment and detachment preserve saves and do not require another **Prepare game** operation for an already compatible build. Updating the native application itself still requires preparation as described above.

## Failure and cancellation

Cancel stops the preparation process and its compiler children. An unfinished build is not selected for play. Existing internal/cartridge backups and checkpoints are not replaced by validation saves. Retry preparation after correcting a missing track, unsupported revision or insufficient space. A lock prevents simultaneous preparations in the same data folder, and setup refuses a folder with a running game.

Local details are under `preparation/BUILD_ID/setup.log` in the game-data folder. These logs and any generated files stay local. Do not upload game data, generated code, saves or memory/video dumps to issues. No automatic upload occurs.

## Building the downloadable package

This is a maintainer step, not a player requirement. After normal dependency setup, use Python 3.11+ and the Windows .NET Framework C# compiler:

```powershell
python tools/package-desktop.py --out 'D:\SFIII-packages\SFIII-Native-0.8.0-win64' --cache 'D:\SFIII-packages\downloads' --zip
```

The packager verifies archive hashes from `desktop/dependencies.json`, copies reusable source and required licenses, compiles the launcher and project-owned GPU shaders, and creates an external file inventory and ZIP checksum. It never reads a disc or takes generated game code as input. LLVM-MinGW supplies its own headers, libraries and static C++ runtime; the end-user build does not use Visual Studio or the Windows SDK. Python is the official isolated embedded distribution. Microsoft C++ headers/libraries are not bundled.

The default developer workflow still supports Visual Studio/Clang. The portable MinGW workflow is separately validated because a compiler/ABI change is not automatically gameplay-equivalent. Before releasing a package, run all six runtime regression targets and the available transition suite with its locally compiled executable, inspect reference frames, and test controller/GPU/checkpoint behavior. A build in a sanitized environment is useful evidence; it is not a substitute for independent tests across Windows PCs and GPU drivers.
