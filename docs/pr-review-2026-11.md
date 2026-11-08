# Community pull-request review

The 17 open contributions were reviewed against v0.9.0. Changes from twelve were adapted into the current source, one was superseded, and four proposals were deferred as written. Adaptation preserves current renderer, save-identity and packaging behavior rather than replacing the runtime patch with an older snapshot. Credit: [damiantw](https://github.com/damiantw).

These are source updates after v0.9.0; existing published archives have not been replaced. New builds and new generated game programs are required to use them. Ordinary saves remain the cross-build progress path. Retain old applications/prepared builds for their native states and replay checkpoints.

| PR | Disposition | Reason and retained work |
| --- | --- | --- |
| [#2](https://github.com/mattkuhnQAAI/sfiii-native/pull/2) | Adapted | WRAM-L and backup-memory mirrors plus open-bus SINIT/MINIT reads. Added boundary, width, bulk-transfer and cache-through checks. |
| [#3](https://github.com/mattkuhnQAAI/sfiii-native/pull/3) | Adapted with correction | Non-local return frames and Scenario 3 X006 entry seed. Call-frame lists are CPU-thread-local, with an interrupt barrier and a test proving master/slave isolation. The proposed process-global list could mix their host stacks. |
| [#4](https://github.com/mattkuhnQAAI/sfiii-native/pull/4) | Deferred as written | Changes native pixels and needs the GPU-side rules rebased and independently checked together. Preserved in [renderer follow-up #21](https://github.com/mattkuhnQAAI/sfiii-native/issues/21). |
| [#5](https://github.com/mattkuhnQAAI/sfiii-native/pull/5) | Superseded | v0.9.0 already implements extended colour and NBG0/NBG1 line scroll/zoom, including additional rounding/format tests. The old patch would overlap those corrections. |
| [#6](https://github.com/mattkuhnQAAI/sfiii-native/pull/6) | Adapted | Flush backup-file data before replacement, and attempt a POSIX directory flush after replacement. Improves durability; does not guarantee every storage device survives power loss. |
| [#7](https://github.com/mattkuhnQAAI/sfiii-native/pull/7) | Adapted | Disc-free Linux host CI, bundled Zig/linker compatibility, isolated launcher tests and numeric Mac bundle versions. CI includes the current display-filter shader and filter tests. |
| [#8](https://github.com/mattkuhnQAAI/sfiii-native/pull/8) | Deferred as written | Replaces guest cycle/bus timing and code generation. Fitted reference measurements need independent reproduction and new timing-sensitive routes. Tracked in [#20](https://github.com/mattkuhnQAAI/sfiii-native/issues/20). |
| [#9](https://github.com/mattkuhnQAAI/sfiii-native/pull/9) | Partly adapted | Retained full status-type CD command reports, with synthetic status/track/index/FAD checks. Accurate-drive timing changes remain in #20. |
| [#10](https://github.com/mattkuhnQAAI/sfiii-native/pull/10) | Deferred as written | Changes VBlank/frame-change timing and fitted VDP1 draw duration. Needs reviewed reference routes, including independence from presentation resolution. Tracked in #20. |
| [#11](https://github.com/mattkuhnQAAI/sfiii-native/pull/11) | Partly adapted | Fixed SCSP MADRS decoding: writes to reserved 0x7C0–0x7FF no longer overwrite delay taps. Boundary/byte/word/long tests cover all 32 taps. The larger envelope/LFO/68000 timing rewrite remains in #20. |
| [#12](https://github.com/mattkuhnQAAI/sfiii-native/pull/12) | Partly adapted | Native states retain activation-time overlay headers, fixing restore after an image load but before its first call. Lookup-cache generations are invalidated on header restoration. Kept strict executable/disc identities; omitted the weaker CLI fallback. Slave/sample/cycle changes remain in #20. |
| [#13](https://github.com/mattkuhnQAAI/sfiii-native/pull/13) | Deferred as written | Depends on #4 and older renderer data. Its 1× center shortcut returns the CPU image before geometry sampling, so that parity result does not validate the new sampling path. Needs actual enlarged/fractional/widescreen shader tests; tracked in #21. |
| [#14](https://github.com/mattkuhnQAAI/sfiii-native/pull/14) | Adapted | Header/dispatch caching, dynamic composition bands and invented-data benchmark. Included snapshot-header cache invalidation. The benchmark's dependency on the deferred VDP1 rasterizer was removed. |
| [#16](https://github.com/mattkuhnQAAI/sfiii-native/pull/16) | Adapted | Three exact V25.1 disc profiles. Registrations are experimental; contributor build evidence is distinct from maintainer validation. |
| [#17](https://github.com/mattkuhnQAAI/sfiii-native/pull/17) | Adapted with tighter validation | Scene-table callback discovery and a verdict log. Added delay-slot literal bounds and rejection of pointers overlapping either half of a known pool word. |
| [#18](https://github.com/mattkuhnQAAI/sfiii-native/pull/18) | Adapted | Detect literal-pool stores in a delayed branch's slot. Existing runtime-literal builds already avoid folding these loads; the fix also protects generator use without that option. |
| [#19](https://github.com/mattkuhnQAAI/sfiii-native/pull/19) | Adapted | Four exact default V25+ add-on profiles, consistency and identity-uniqueness tests. Other selections remain unregistered. No game assets or patches are included. |

## Validation scope

The candidate is gated by the six required runtime regression targets, the general upstream runtime suite, an overlay snapshot/dispatch test, Python tests and the invented-data benchmark. Windows timing tests now advance the virtual clock so coarse host sleeps cannot skip short CD status windows; frame-change tests choose a time beyond earlier tests. Expected hardware behavior was not relaxed to hide failures.

Retained gameplay routes are run against new generated executables for original/translated Scenario 1, supplied Scenario 2 V1.003 and translated Scenario 3. Only external test copies of native states are identity-retagged; their payloads, initial internal/cartridge backups, clocks and inputs are preserved. Missing late-game crash routes and the seven new disc fixtures remain unavailable validation.

The reported contributor timing/reference-match percentages are not maintainer measurements. Static compilation, emitted entries, executed gameplay and hardware fidelity remain separate claims. Neither these fixes nor the current route suite certify every transition, full campaigns, all translations or the Premium Disc.
