# Playing

In the desktop package, click **Play** after preparation. Source builds can launch with **Launch Native SFIII.cmd** from the external work directory. Use **Controller Setup.cmd** to bind a generic or Saturn USB pad; **Controller Test.cmd** checks its controls. SDL mappings cover PS5, Xbox and other controllers. The Saturn USB pad has been physically tested locally; PS5/Xbox support has not received equivalent physical-device validation.

Keyboard defaults: arrows for movement, Enter for Start, Z/X/C for A/B/C, A/S/D for X/Y/Z, Q/W for L/R. Escape opens the menu. F11 or Alt+Enter toggles fullscreen.

## Display

**Display filter** now cycles through **Nearest / Bilinear / Integer smooth / Bicubic / Lanczos**. Bicubic uses a smooth cubic kernel; Lanczos uses a sharper three-lobe kernel. Both use separable GPU passes, widen their sampling footprint when shrinking, and filter the completed game picture before the native toolbar/menu. They are independent of render resolution and SSAA. Fine-edge ringing can occur with either sharper filter. Unsupported renderers or sampling budgets retain bilinear and show a fallback message.

Vertex precision also removes truncation of the perspective reciprocal within each verified native pixel cell. This reduces another source of scale stepping during camera-depth changes. It does not reconstruct rounding in earlier camera angles, object transforms or animation keyframes. Sharper backgrounds additionally handles supported transparent-plane interiors, RGB888 sources and ordinary floor colour calculation. Native silhouettes remain authoritative at transparency boundaries; complex coefficient/window combinations still use native sampling.

Local audio updates avoid restarting playback on brief device read-ahead. Issue-capture PNG export runs in the background so saving a high-resolution image has less impact on music; wait briefly before requesting another capture. Closing normally finishes pending exports. These changes retain your selected resolution, SSAA and VSync settings.

The local display build also catches up on repeated VDP1 display fields when rendering falls behind. Every game and audio step still runs, and new VDP1 draws are retained. This helps music stay continuous with Vertex precision at demanding resolution/SSAA settings. It keeps your chosen image quality; sustained GPU overload can still exceed the available performance.

The local build adds saved **Vertex precision** and **Sharper backgrounds** On/Off menu choices. Both default to On and work above 1x with GPU rendering. Vertex precision reduces whole-pixel projection snapping in reviewed Scenario 1–3 model paths. Sharper backgrounds samples supported opaque rotating floors/backgrounds at the internal resolution. Unknown revisions, ambiguous geometry and unsupported effects retain native paths. Neither option changes gameplay timing, collisions, original textures or save formats. Camera/object-transform quantization and texture shimmer can remain.

The local display-development build also offers **9x** and **Menu > Supersampling > Off / 4x**. In a 240-line scene, 9x renders 2160 lines: 2880x2160 in 4:3 or 3843x2160 before widescreen aspect correction. Four-sample SSAA renders at twice each axis and averages back to the chosen internal size on the GPU. Start at 5x with SSAA off, then compare it on; 9x plus SSAA is substantially more demanding. The original 1x setting retains exact native samples. The menu reports when GPU limits prevent SSAA. **Display filter** controls final window resizing independently. These local additions are not part of the published v0.8.0 download.

The v0.8.0 Windows application offers **1× / 2× / 5×** in the menu and toolbar. Its renderer still accepts 4× for diagnostic comparisons, and an existing 4× player preference opens at 5×. 5× renders 56% more pixels than 4×, so it needs more GPU headroom. Rendering resolution improves supported geometry; the separate optional artwork packs described below add source texture detail. Vulkan is preferred; Direct3D 12 is another supported backend. `SF3_GPU_BACKEND=vulkan` or `direct3d12` can force one for troubleshooting. Unsupported GPU/video paths retain a native fallback, with different performance and enhancement coverage.

At normal speed, gameplay and audio keep the original roughly 59.8 Hz NTSC clock. Presentation targets that rate; higher monitor refresh does not speed up the game, and individual animations can update less often. Internal resolution stays at your chosen value: there is no automatic quality reduction. GPU tile binning and parallel composition of supported background rows reduce rendering overhead on suitable hardware, but performance in untested scenes is not guaranteed.

**Menu > Aspect ratio (beta) > 16:9** adds horizontal scenery; choose **4:3** for original framing. HUD/dialogue remain centered. The rotation-floor viewport window expands horizontally, retaining its vertical limits. Game-side object culling still uses original assumptions, so missing objects or pop-in at new edges can remain. Higher resolution and widescreen are experimental presentation features, not a claim of exact hardware output.

## Game speed

Choose **Menu > Game speed (F6) > 1× / 2× / 3×**, click the **Speed** toolbar button, or press **F6** to cycle speeds. Each new process starts at 1×. Game speed is separate from rendering resolution: 5× resolution can stay selected while playing at 2× speed.

Start with 2× for long animations and return to 1× for normal play. The optional 3× setting is experimental; maintaining the selected multiplier is secondary to correct gameplay. No battle logic or save format is patched to force a speed increase.

Fast-forward runs the original game ticks sooner in real time. Battle calculations and virtual hardware clocks retain their original relationships. It applies to the whole game, including dialogue and menus; switch back to 1× for precise input. Audio plays faster and at a higher pitch. Each speed change clears queued playback and primes fresh audio, avoiding a backlog when returning to 1×. Requested 2×/3× speed requires CPU/GPU headroom and is not guaranteed in every scene. No automatic resolution reduction occurs. The experimental `--realtime` mode does not support the speed selector.

Game speed is included in v0.5.0-alpha; earlier v0.3.0/v0.4.0-preview1 downloads predate it. Developers can run `tools/validate-game-speed.py --help` for a windowed, real-audio comparison of an external route at all three speeds; it checks final native pixels, video memory, virtual PCM and backup contents independently from measured speed.

## Optional AI graphics

The v0.8.0 Windows application can attach locally generated artwork and offers **Menu > AI graphics > Off / Animated / Illustrated / General**. Packs are built from your own registered Scenario 1–3 discs using the named Real-ESRGAN models. They include eligible world textures and decoded CHR/CHP character frames, including normal/scaled character sprites and matching distorted-sprite textures. Menus and HUD retain original artwork, source transparency is preserved, and unmatched textures use the original artwork. **Off** restores original artwork while retaining your chosen rendering resolution. AI artwork is not a supported widescreen path; keep it **Off** in 16:9.

The current source uses the clearer labels **Off / Animated / Illustrated / General**, in the same order. Existing packs and saved style selections keep their original identifiers and remain compatible. The published preview retains its earlier labels until a new release is available.

Artwork is precomputed on your GPU, then matched and displayed by the native renderer during play. This is not DLSS or per-frame AI inference. Generating all extracted frames does not establish matching for every character animation, battle object or scene. Unsupported formats, background-plane replacements, some large images, texture seams and full-campaign visual coverage remain unfinished. AI styles can alter details and should be treated as optional experiments. The available Scenario 2 Windows rendering and gameplay checks do not certify AI gameplay on other scenarios or platforms.

The new Windows preview hides the earlier AA and ray-shadow rows. Their shader code remains available for diagnostics; the sections below describe earlier builds.

### Standalone offline graphics builder

Download the separate [Windows graphics-builder ZIP](https://github.com/mattkuhnQAAI/sfiii-native/releases/download/v0.8.0/SFIII-Offline-Graphics-Builder-0.8.0-Windows-x64.zip), extract it completely outside any source repository and open **Start Offline Graphics Builder.cmd**. First-run setup needs internet access: it downloads pinned upstream archives, verifies their checksums, restores the extractor's dependencies and builds the adapter automatically. No compiler, Python installation or manual compilation is required. The CHD utility downloads separately the first time a CHD is used. Once setup is complete, extraction and inference run locally without Codex, API keys, subscriptions or an API connection. No game data is uploaded.

Choose supported local Scenario 1–3 CUE/CHD files, an external output folder, a GPU index and **All three** styles. Leave **Include CHR/CHP characters** checked to produce combined character/world packs automatically. GPU index identifies the processor used by the upscaler; on a machine with integrated and dedicated graphics, check its log and choose the dedicated GPU. The numbers depend on the PC. A Vulkan-capable driver is required for generation. Try **Quick check** first, then **Start / Resume** for full output. **Extract only** skips GPU processing. Full processing may take hours, and all scenarios/styles plus their cache can require tens of GB.

You may close Codex and the builder window while the worker continues. **Stop safely** stops its current child process and retains completed work for resume. Keep the selected discs available until extraction finishes. Reopen the builder and use **Open output / reports** to check progress, failures and remaining gaps. A sample/quick check is not a full pack-generation run.

The builder hashes revisions, inventories supported artwork through a separately supplied SF3Lib adapter, deduplicates images across scenarios, and caches GPU results. It preserves source transparency and supplies periodic context for identified surface tiles. Extraction includes map textures/animations, available floor/sky images and tilesets with assignment maps, sprite frames, and recognized embedded image tables. Unknown formats, decoding errors, oversized images and rejected GPU results remain visible in local reports. A completed job does not certify complete artwork coverage, seamless terrain or animation consistency.

At the end of a full run, the builder assembles indexed character/world packs and writes `playable-packs-report.json` in the output folder; its timestamped `playable-packs` subfolder contains the corresponding `packs-report.json`. Packaging verifies source/replacement hashes and exact scaled transparency without repeating AI inference. The top-level `packs-report.json` describes world-only output. Read the reports: failed extraction or rejected GPU results remain gaps, and **complete with gaps** does not mean every asset succeeded.

Close the game, choose **Attach packs to game** in the builder, and select the prepared scenario game-data folder containing `game.json`. Alternatively, in the Windows launcher select the scenario and its prepared data folder, choose **Optional graphics...**, and select the combined `playable-packs/TIMESTAMP/packs-report.json`. It must contain all three styles for that exact registered revision. Restart the game, then choose **AI graphics** in the menu. Repeat attachment for each scenario's separate data folder. Keep the builder output at its current path; attachment references those files rather than copying several GB into the game folder. Attachment does not replace saves or prepare a new game build.

Use **Return game to original graphics** in the builder, or the launcher's **Use original graphics** button, to detach the packs. **AI graphics > Off** also lets you leave the attachment in place while playing with original artwork. Packs are never activated automatically by generation. No commercial disc, BIOS, extracted game art, generated artwork pack or save is shipped in either download; all such files remain in your external local folders.

Newly packaged artwork includes a startup index, so compatible builds avoid scanning every material at launch and load replacement pixels when needed. The index is regenerable and does not change image quality. `index_packs.py PACK_ROOT` can index existing local packs. If an index is missing, stale or damaged, the game uses the directory loader. Keep attached packs unchanged; create a new pack when editing artwork. Optional `archive_packs.py` makes verified lossless ZIP backups for your own use. Unpack them and regenerate indexes before playing; gameplay does not read compressed archives directly. Compression ratios vary by style and content. Do not redistribute game-derived artwork or the locally prepared SF3Tools extractor/resources.

## Light anti-aliasing (earlier builds)

Choose **Menu > Edge AA (beta) > Light** to soften selected diagonal geometry edges on the GPU. **Off** restores the unfiltered picture; new processes start with AA off. Use 2×, 4× or 5× GPU rendering to try it. This is a small spatial filter with no previous-frame history or ray-tracing requirement, and it does not add texture detail.

The filter uses known geometry ownership to preserve ordinary sprite/text paths and unsupported background paths. The native toolbar and menu are drawn afterward. Unclassified geometry edges are deliberately left alone, and unusual game UI paths still need broader validation. Available with the GPU renderer; there is no CPU filtering fallback. Resolution, game speed, native video memory, game logic and save formats are unchanged. Light AA is included in v0.5.0-alpha. Developer route comparisons can enable it with `validate-game-speed.py --aa`.

## Ray shadows (earlier development builds)

Choose **Menu > Ray shadows > Low** to try subtle directional shadows on identified town and battle geometry. **Off** restores the exact unfiltered GPU image and is the default at every launch. This effect can be combined with Edge AA and 2×, 4×, or 5× rendering.

This is a short **screen-space ray** effect in the GPU shader, using visible geometry ownership. It is an inexpensive visual experiment, not hardware ray tracing or physically accurate lighting: hidden surfaces and geometry without known ownership cannot cast shadows. Sprites, text, menus, and unsupported background layers are protected. It does not change gameplay, audio, saves, or native video memory. Windows Vulkan has been tested locally; other GPU backends still need gameplay validation.

## Game saves

Use the game's save command for ordinary persistent progress. The external files are `experimental-backup.bin` (32 KiB internal capacity) and `experimental-backup.bin.cartridge` (512 KiB cartridge capacity). They contain native backup records, **not raw emulator memory images**. Back them up before changing versions or importing saves. The full game's save behavior has not been validated.

**Import Saturn Saves.cmd** accepts per-save raw `SFORCE31_XX.BIN` (12904 bytes), `SFORCE32_XX.BIN` (16248 bytes), and version-1 JSON `.ysbak` exports with matching filename, size and checksum. Development builds also accept `SFORCE33_XX.BIN` (24344 bytes) and matching YSBAK exports. Scenario 2 gameplay requires the experimental translated-v24 profile; Scenario 3 development uses the registered Translation Project v25.1 disc. Accepting a save format does not validate every scene it can load. The published Windows v0.3.0 and Linux/macOS v0.4.0-preview1 packages predate Scenario 3 support.

Choose internal or cartridge storage. A Scenario 2 save occupies 33 cartridge blocks; internal storage fits one Scenario 2 save. Imports preserve payloads and back up the destination. YSBAK filename, ASCII comment, language and save date are retained; embedded screenshots and remote URLs are ignored. Raw BIN imports generate comment/date metadata. Emulator and Saroo exports must match a supported format; whole memory dumps and emulator save states are not interchangeable. Stop the game before importing. The desktop package supplies Python; source builds need Python 3.11+ installed.

A Scenario 3 save occupies 49 cartridge blocks. Internal memory fits one Scenario 3 save; use cartridge storage when keeping completed earlier-scenario files alongside it. Each launcher scenario remembers its own disc and data folder. Carryover requires explicitly importing a completed earlier-scenario save into the destination scenario's backup storage; the launcher does not merge or overwrite your other games' progress.

## Native save states and replay checkpoints

v0.3.0 builds with native state continuations provide **Save state / Load state** in the toolbar and menu, or **F5 / F9**. They capture and restore the machine directly, including both CPUs, RAM, video, sound, CD state and internal/cartridge backup contents. Loading briefly restarts the native process; it does not replay earlier inputs. **Load Native State.cmd** loads the latest compatible state without first playing through the title screen.

There is one quick slot per exact executable/disc identity under `native-states` in the game-data folder. Wait for the window title to confirm the write. Corrupt and incompatible states are rejected. Updating the executable requires a new state; older slots remain intact. Native states are validated on limited Scenario 2 translated-v24 routes and a Scenario 3 v25.1 startup-to-ship route, not the complete game.

**Loading a native state restores its backup memory too.** Exact pre-load internal/cartridge files and the loaded state are retained in that session directory. Ordinary game saves and older replay checkpoints remain available. Replay checkpoint creation/loading within a session restored from a native state is disabled; use native states there.

The public v0.2.0 build and older native builds retain the replay controls described below. v0.3.0 keeps them as separate menu actions; F5/F9 now select native states.

Toolbar/menu Save checkpoint and Load checkpoint, or F5 and F9, record a route and restart/fast-forward it. **Load Replay Checkpoint.cmd** loads the latest compatible checkpoint. Keep the complete checkpoint, including initial internal/cartridge backups, clock, settings and input. They are tied to the exact disc and native executable and are not instant save states. New executable versions can invalidate them.

Replay sessions keep their own saves so loading an old checkpoint does not roll back ordinary progress. `publish-replay-save.ps1` is an explicit, separate action for adopting progress from a replay session; never copy historical validation backups over live progress automatically.

## Local session files

Sessions, input recordings and checkpoints stay in your work directory. Optional **Capture issue** (F8) also writes screenshots, memory/video dumps and context there. These are local troubleshooting features; no automatic upload occurs. Do not attach memory/video dumps, extracted assets, generated game code or saves to a repository issue. Describe a problem with the disc revision, scene, settings and reproduction steps instead. The normal launcher does not enable instruction/transition tracing; developers can opt in with `run-native.ps1 -Work WORK -Diagnostics`.
