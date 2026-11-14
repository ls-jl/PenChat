# Bounded native work

The 2026-11-14 performance changes remove repeated work without changing UI
geometry, typography, protocol data, cache budgets or gesture timing.

- `RichText::paintBatch` prepares one block and paints all newly visible raster
  windows from those pieces. The batch ends before creating LVGL objects, so
  object callbacks cannot replace prepared pieces mid-batch. It is not a cache
  across messages or frames. Visible tile membership is a rectangle in the
  existing 512×128 raster grid; unchanged tiles retain their objects. Image
  DELETE callbacks remain the single owner of texture/cache cleanup.
- Formula pieces retain immutable shared bitmaps from the existing bounded
  cache. Public `formula()` still returns an independent value. The local,
  bounded 256-entry glyph cache now includes vertical metrics as well as advance;
  tab position, actual space drawing, style and font fallback remain separate.
- The native editor does not set identical textarea contents twice. Cursor,
  selection and caret synchronization still run. Send colour writes compare the
  local MAIN property, preserving pressed-state styling and theme updates. An
  unused layout lookup/copy is removed from inactive editor ticks.
- Context snapshots fetch image parts once per physical page of 100 messages.
  Effective-reply filtering, pending images, part order, path fallback, checksum
  and asset leases remain unchanged. Single-model lookup returns only the chosen
  credential-free model from the existing worker-owned catalog cache, retaining
  its existing invalidation rules.

## Measured checks

Against `52573ba`, the same 106-message context fixture performs 13 SQL queries
instead of 117, with identical rows and checksum. A portrait editor text change
uses one textarea setter instead of two; a repeated identical sync uses none.
64 inactive ticks perform no editor layout lookup/resolve; 64 steady or pressed
inline-editor ticks perform no redundant Send MAIN colour writes, while enabled,
disabled and theme transitions still update once.

The same original/new renderer fixture produced identical layout/hit data and
pixel digests for 432 blocks and 1288 windows, spanning dark/light, scales, CJK,
Latin, lists, tabs, table cells, formulas, fallback and invalid formulas. Four
new tiles of one ordinary block require one layout instead of four. Two identical
formula runs require one rendering and no internal pixel copies.

A fixed host microbenchmark (eight repetitions of four long-paragraph tiles,
Release build, seven samples) measured medians of 17.531ms before and 6.512ms
after in one paired run. Shared-host timing varies; this is not a device FPS or
end-to-end latency claim. Deterministic work counts and pixel comparisons are the
primary acceptance criteria. No production hot-path counters are added.

## Targeted verification

`RichPerformanceTest` compares batch/single-window output and bounded work counts.
`UiPerformanceTest` uses test-only linker wrappers to count actual LVGL setters;
`PaintPerformanceFixture` checks real App tile reuse, scrolling, clipping, cleanup
and pixels. Run each UI geometry in a separate process, matching production's
single App/MicroTeX lifetime. ContextTest and ProviderSettingsTest cover the
batched reader against an independent per-message reader, page boundaries,
multiple images, hidden tails, rollback, disabled models and cache invalidation.

The host-only `test_render_performance.py` accepts explicit old/new executables
for actual page/scroll/selection pixel comparisons. Artifacts, timing samples,
packages and installation records belong under ignored `.work/perf-redundancy`.
