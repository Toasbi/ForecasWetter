# Rain bar tier-scale Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the linear rain-bar scale and binary overflow gradient with a 5-tier stepped scale: fixed bar heights per tier, gradient caps of increasing depth, full-slot bar width, and tier-boundary separator lines that draw over the gradient too.

**Architecture:** All visual changes live in `src/c/layers/forecast_layer.c`. A small lookup table and a tier-classifier helper drive the per-bar render. The drawing block goes from "linear + conditional overlay" to "discrete height + per-tier gradient + per-tier-boundary separators". Bar width becomes per-frame (`(int)entry_w - 1`). One fixture file (`fixtures/rainy.json`) is updated so the emulator can exercise all five tiers.

**Tech Stack:** Pebble C SDK, pebble-tool via mise, fixture JSON loaded by the dev profile.

**Reference:** Design spec at `docs/superpowers/specs/2026-06-03-rain-bar-tier-scale-design.md`.

**Note on TDD:** This is a UI rendering change in a Pebble C app with no unit-test harness for rendering. There are no automated tests to run. The user handles all build and emulator commands themselves — the implementer must NOT run `mise build`, `mise install-emulator`, or any other build/run command. After the implementer makes the edits and commits, the user will build, run the emulator, and verify visually.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/c/layers/forecast_layer.c` | Modify | Tier constants, palettes, classifier helper, per-bar drawing block, bar width |
| `fixtures/rainy.json` | Modify | Hourly `rainMm` series that hits all five tiers |
| `docs/superpowers/specs/2026-06-01-rain-bar-overflow-tiers-design.md` | (no change) | Already-superseded spec stays in place as historical record |

No new files. The change is intentionally narrow.

---

## Task 1: Add tier constants, palettes, and classifier (additive only)

This task is purely additive — nothing is removed yet, so the old drawing block keeps compiling and working. New definitions sit next to the existing ones.

**Files:**
- Modify: `src/c/layers/forecast_layer.c:58-77` (palette + constants region)

- [ ] **Step 1: Add the two new gradient palettes**

Open `src/c/layers/forecast_layer.c`. Find the `#ifdef PBL_COLOR` block at line 58–69 that defines `s_rain_overflow_green` and `s_rain_overflow_orange`. Add two more palette declarations *inside the same `#ifdef PBL_COLOR` block*, just before the closing `#endif`:

```c
static const GColor8 s_rain_tier_blue[] = {
    { .argb = GColorCobaltBlueARGB8 },
    { .argb = GColorPictonBlueARGB8 },
    { .argb = GColorWhiteARGB8 },
};
static const GColor8 s_rain_tier_red[] = {
    { .argb = GColorRedARGB8 },
    { .argb = GColorSunsetOrangeARGB8 },
    { .argb = GColorWhiteARGB8 },
};
```

So the block looks like (after edit):

```c
#ifdef PBL_COLOR
static const GColor8 s_rain_overflow_green[] = {
    { .argb = GColorGreenARGB8 },
    { .argb = GColorMintGreenARGB8 },
    { .argb = GColorWhiteARGB8 },
};
static const GColor8 s_rain_overflow_orange[] = {
    { .argb = GColorOrangeARGB8 },
    { .argb = GColorChromeYellowARGB8 },
    { .argb = GColorWhiteARGB8 },
};
static const GColor8 s_rain_tier_blue[] = {
    { .argb = GColorCobaltBlueARGB8 },
    { .argb = GColorPictonBlueARGB8 },
    { .argb = GColorWhiteARGB8 },
};
static const GColor8 s_rain_tier_red[] = {
    { .argb = GColorRedARGB8 },
    { .argb = GColorSunsetOrangeARGB8 },
    { .argb = GColorWhiteARGB8 },
};
#endif
```

- [ ] **Step 2: Add tier lookup tables and classifier**

Immediately after the existing `RAIN_GREEN_TIER_MAX_TENTHS / RAIN_GREEN_OVERLAY_HEIGHT_MM / RAIN_ORANGE_OVERLAY_HEIGHT_MM` defines (around line 77), and before the `typedef struct NightSegment` at line 79, append:

```c
// Five-tier stepped rain scale. Each tier maps to a discrete bar height
// (in fifths of plot_h) and a gradient cap depth (also in fifths). See
// docs/superpowers/specs/2026-06-03-rain-bar-tier-scale-design.md.
#define RAIN_TIER_COUNT 5
static const int RAIN_TIER_HEIGHT_FIFTHS[RAIN_TIER_COUNT]   = { 1, 2, 3, 4, 5 };
static const int RAIN_TIER_GRADIENT_FIFTHS[RAIN_TIER_COUNT] = { 0, 1, 2, 2, 3 };
// Inclusive upper bounds in wire tenths for tiers 1..4. Tier 5 catches the rest.
static const int RAIN_TIER_MAX_TENTHS[RAIN_TIER_COUNT - 1] = { 1, 5, 20, 100 };

static int rain_tier_of_tenths(int tenths)
{
    // Caller must ensure tenths > 0.
    for (int i = 0; i < RAIN_TIER_COUNT - 1; ++i)
    {
        if (tenths <= RAIN_TIER_MAX_TENTHS[i]) { return i + 1; }
    }
    return RAIN_TIER_COUNT;
}
```

- [ ] **Step 3: Commit**

```bash
git add src/c/layers/forecast_layer.c
git commit -m "feat(forecast): add tier-scale constants, palettes, and classifier

Purely additive: new s_rain_tier_blue and s_rain_tier_red palettes plus
RAIN_TIER_* lookup tables and rain_tier_of_tenths() helper. Wiring into
the per-bar drawing block follows in the next commit."
```

Do not run `mise build` — the user runs builds themselves. Pause here for the user's build feedback before starting Task 2.

---

## Task 2: Switch the per-bar drawing block to the tier model and remove obsolete constants

This task does the substantive change: deletes the linear-scale constants, renames the overflow palettes to their tier names, and replaces the entire per-bar drawing block with the tier-based render.

**Files:**
- Modify: `src/c/layers/forecast_layer.c:37-77` (constants region)
- Modify: `src/c/layers/forecast_layer.c:553` (entry_w line — `bar_w` is computed nearby)
- Modify: `src/c/layers/forecast_layer.c:662-745` (per-bar drawing block + its comment)

- [ ] **Step 1: Remove the obsolete constants**

In `src/c/layers/forecast_layer.c`, replace the block at lines 37–50:

```c
#ifdef PBL_PLATFORM_EMERY
#define BAR_WIDTH_PX 3
#else
#define BAR_WIDTH_PX 2
#endif
#define BAR_COLOR PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
// Separator runs across the bar at every 1mm so intensity is readable at a glance.
// Matches the precip fill so each segment looks like a notch out of the bar.
#define BAR_SEPARATOR_COLOR PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorLightGray)
// Bar scale: full height = RAIN_MAX_MM mm/h. Anything above is clamped to a full bar.
// 5 mm/h covers the common case; heavier rain still pegs but stays legible at low intensities.
#define RAIN_MAX_MM 5
#define RAIN_TENTHS_PER_MM 10
#define RAIN_TENTHS_MAX (RAIN_MAX_MM * RAIN_TENTHS_PER_MM)
```

with:

```c
#define BAR_COLOR PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)
// Separator runs across the bar at every internal tier-segment boundary
// (1/5..(tier-1)/5 of plot_h). On color platforms the cobalt color reads
// cleanly over both the white base and the colored gradient cap.
#define BAR_SEPARATOR_COLOR PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorLightGray)
```

This deletes `BAR_WIDTH_PX` and all `RAIN_MAX_MM`/`RAIN_TENTHS_*` defines, and rewrites the separator comment.

- [ ] **Step 2: Remove the linear-overflow tier defines and comment**

Replace lines 71–77:

```c
// Tier thresholds (in wire tenths, i.e., mm/h * 10):
//   tenths <= RAIN_TENTHS_MAX                              -> plain bar (no overlay)
//   RAIN_TENTHS_MAX < tenths <= RAIN_GREEN_TIER_MAX_TENTHS -> green overlay, top 2 mm of the bar
//   tenths > RAIN_GREEN_TIER_MAX_TENTHS                    -> orange overlay, top 3 mm of the bar
#define RAIN_GREEN_TIER_MAX_TENTHS 100
#define RAIN_GREEN_OVERLAY_HEIGHT_MM   2
#define RAIN_ORANGE_OVERLAY_HEIGHT_MM  3
```

with an empty line (delete entirely). The replacement tier table and classifier are already in place from Task 1.

- [ ] **Step 3: Rename the overflow palettes to tier palettes**

The palettes still exist with their old names from Task 1. Rename them so all four tier palettes share the `s_rain_tier_*` prefix. In the `#ifdef PBL_COLOR` block, do these two renames:

- `s_rain_overflow_green` → `s_rain_tier_green`
- `s_rain_overflow_orange` → `s_rain_tier_orange`

After the renames the block reads (in order):

```c
#ifdef PBL_COLOR
static const GColor8 s_rain_tier_green[] = {
    { .argb = GColorGreenARGB8 },
    { .argb = GColorMintGreenARGB8 },
    { .argb = GColorWhiteARGB8 },
};
static const GColor8 s_rain_tier_orange[] = {
    { .argb = GColorOrangeARGB8 },
    { .argb = GColorChromeYellowARGB8 },
    { .argb = GColorWhiteARGB8 },
};
static const GColor8 s_rain_tier_blue[] = {
    { .argb = GColorCobaltBlueARGB8 },
    { .argb = GColorPictonBlueARGB8 },
    { .argb = GColorWhiteARGB8 },
};
static const GColor8 s_rain_tier_red[] = {
    { .argb = GColorRedARGB8 },
    { .argb = GColorSunsetOrangeARGB8 },
    { .argb = GColorWhiteARGB8 },
};
#endif
```

Also update the surrounding comment (lines 52–57) to describe tier-keyed gradients instead of "overflow":

```c
// Per-tier gradient palettes. Index 0 = target color (top of the colored
// section), last entry = white so the bottom blends into the plain bar
// below. See spec 2026-06-03-rain-bar-tier-scale.
```

- [ ] **Step 4: Replace the per-bar drawing block**

Find the rain-bar drawing region at lines 662–745 (header comment plus the `for` loop). Replace the whole region with:

```c
    // Rain-amount bars. Drawn after the night overlay so night-hour bars
    // aren't covered by the solid night-precip fill. Five discrete heights
    // (1/5..5/5 plot_h) encode tier; on color platforms the top of the bar
    // gets a per-tier gradient cap whose depth grows with severity.
    const int16_t bar_plot_h = graph_plot_rect.size.h;
    const int16_t bar_plot_bottom = graph_plot_rect.origin.y + bar_plot_h;
    // Bar width fills the hour slot minus a 1 px gap so adjacent bars stay
    // visually distinct. Falls back to 1 px if entry_w is too small.
    const int bar_w = (entry_w >= 2.0f) ? (int)entry_w - 1 : 1;
    for (int i = 0; i < num_entries; ++i)
    {
        int tenths = rain_tenths[i];
        if (tenths <= 0) { continue; }

        const int tier = rain_tier_of_tenths(tenths);
        int bar_h  = (bar_plot_h * RAIN_TIER_HEIGHT_FIFTHS[tier - 1]) / 5;
        int grad_h = (bar_plot_h * RAIN_TIER_GRADIENT_FIFTHS[tier - 1]) / 5;
        if (bar_h <= 0) { bar_h = 1; }
        if (grad_h > bar_h) { grad_h = bar_h; }

        const int bar_x = graph_bounds.origin.x + (int)(i * entry_w);
        const int bar_top_y = bar_plot_bottom - bar_h;

        // Pass 1: plain-color base for the full bar height.
        graphics_context_set_fill_color(ctx, BAR_COLOR);
        graphics_fill_rect(ctx, GRect(bar_x, bar_top_y, bar_w, bar_h), 0, GCornerNone);

#ifdef PBL_COLOR
        // Pass 2: per-tier gradient cap (top grad_h pixels of the bar).
        const GColor8 *palette = NULL;
        int palette_len = 0;
        switch (tier)
        {
            case 2: palette = s_rain_tier_blue;   palette_len = 3; break;
            case 3: palette = s_rain_tier_green;  palette_len = 3; break;
            case 4: palette = s_rain_tier_orange; palette_len = 3; break;
            case 5: palette = s_rain_tier_red;    palette_len = 3; break;
            default: break;  // tier 1: no gradient
        }

        if (grad_h > 0 && palette != NULL)
        {
            const int color_top_y = bar_top_y;
            const int color_bot_y = bar_top_y + grad_h;
            const int denom = (grad_h > 1) ? (grad_h - 1) : 1;
            for (int y = color_top_y; y < color_bot_y; ++y)
            {
                int rows_from_top = y - color_top_y;
                int idx = (rows_from_top * (palette_len - 1)) / denom;
                if (idx >= palette_len) { idx = palette_len - 1; }
                graphics_context_set_fill_color(ctx, palette[idx]);
                graphics_fill_rect(ctx, GRect(bar_x, y, bar_w, 1), 0, GCornerNone);
            }
        }
#endif

        // Pass 3: tier-segment separator lines at every internal segment
        // boundary the bar reaches. Draws on top of the gradient too — the
        // cobalt color reads cleanly against blue/green/orange/red caps and
        // the lines still carry intensity-reading information.
        if (tier > 1)
        {
            graphics_context_set_fill_color(ctx, BAR_SEPARATOR_COLOR);
            for (int s = 1; s < tier; ++s)
            {
                const int sep_y = bar_plot_bottom - (s * bar_plot_h) / 5;
                graphics_fill_rect(ctx, GRect(bar_x, sep_y, bar_w, 1), 0, GCornerNone);
            }
        }
    }
```

Verify visually that the loop ends just before `// Draw the precipitation line` and `s_path_precip_top.num_points = num_entries;` (the next code section is untouched).

- [ ] **Step 5: Sanity-check for stale references**

Run: `grep -n s_rain_overflow src/c/layers/forecast_layer.c`
Expected: no matches. If anything prints, that's a stale call site left over from the rename — fix it before committing.

- [ ] **Step 6: Commit**

```bash
git add src/c/layers/forecast_layer.c
git commit -m "feat(forecast): switch rain bars to 5-tier stepped scale

Removes the linear mm->pixels height calc and the binary overflow gradient.
Each tier now has a fixed bar height (1/5..5/5 plot_h) and a gradient cap
of growing depth (0/1/2/2/3 segments) in blue/green/orange/red. Bars are
widened to fill (entry_w - 1) px. Tier-segment separator lines draw at
every internal boundary the bar reaches, including over the gradient cap.

Supersedes the linear+overflow implementation from spec 2026-06-01."
```

Do not run `mise build` — the user runs builds themselves. Pause for the user's build feedback before starting Task 3.

---

## Task 3: Augment `fixtures/rainy.json` to cover all five tiers

The current `rainMm` series jumps straight into tier 3+ territory. To exercise tier 1 (0.1 mm) and tier 2 (0.2–0.5 mm) on first emulator boot, replace the leading entries.

**Files:**
- Modify: `fixtures/rainy.json` (the `rainMm` array)

- [ ] **Step 1: Update the `rainMm` array**

Edit `fixtures/rainy.json`. Find the line:

```json
    "rainMm":   [ 0.2, 1.0, 2.5, 4.0, 7.0, 9.0, 14.0, 18.0, 12.0, 6.0, 2.5, 1.0, 0.5, 0.2, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 ],
```

Replace with:

```json
    "rainMm":   [ 0.1, 0.3, 0.5, 1.0, 2.5, 5.0, 9.0, 14.0, 18.0, 12.0, 5.0, 2.5, 1.0, 0.5, 0.3, 0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 ],
```

This series visits, in order: tier 1 (0.1), tier 2 (0.3), tier 2 (0.5), tier 3 (1.0), tier 4 (2.5), tier 4 (5.0), tier 4 (9.0), tier 5 (14.0), tier 5 (18.0), tier 5 (12.0), tier 4 (5.0), tier 4 (2.5), tier 3 (1.0), tier 2 (0.5), tier 2 (0.3), tier 1 (0.1), then 8 dry hours. All five tiers appear at least once.

- [ ] **Step 2: Confirm JSON is still valid**

Run: `python3 -m json.tool fixtures/rainy.json > /dev/null && echo OK`
Expected: prints `OK`.

- [ ] **Step 3: Commit**

```bash
git add fixtures/rainy.json
git commit -m "test(fixtures): cover all five rain tiers in rainy.json

Leading entries now hit tier 1 (0.1), tier 2 (0.3, 0.5), tier 3 (1.0),
tier 4 (2.5, 5.0, 9.0) and tier 5 (14.0, 18.0) so the emulator can
exercise the new tier-scale render on first boot."
```

---

## Task 4: Visual-verification checklist (user runs the emulator)

The implementer does NOT run the emulator. After Task 3 commits, hand control back to the user with this checklist so they can run `mise install-emulator` themselves and verify by eye.

**Files:** none modified.

- [ ] **Step 1: Color-platform checklist (basalt / chalk / emery)**

When the user runs the emulator on a color platform with `fixtures/rainy.json`, the following should be visible, walking left-to-right across the forecast row:

- Hour 1 (0.1 mm, tier 1): 1/5-height bar, plain white, no cap, no separator.
- Hour 2 (0.3 mm, tier 2): 2/5-height bar, blue gradient covering its top 1/5, one cobalt separator at the 1/5 plot_h row.
- Hour 4 (1.0 mm, tier 3): 3/5-height bar, green gradient covering its top 2/5, two cobalt separators at 1/5 and 2/5 rows.
- Hour 5 (2.5 mm, tier 4): 4/5-height bar, orange gradient covering its top 2/5, three cobalt separators at 1/5, 2/5, 3/5 rows.
- Hour 8 (14.0 mm, tier 5): full-height bar, red gradient covering its top 3/5, four cobalt separators at 1/5, 2/5, 3/5, 4/5 rows.

Also:
- Each bar fills almost the entire hour slot horizontally (≈ `entry_w - 1` px wide), with a thin gap between adjacent bars — no thin sliver bars.
- Dry hours have no bar.
- No per-mm notches anywhere (the only horizontal lines in bars are at tier boundaries).

- [ ] **Step 2: B&W-platform checklist (aplite / diorite)**

On B&W, the user should see:

- Five distinct bar heights (one per tier — 1/5..5/5 of plot_h).
- Bars solid black, no gradient.
- Separator lines at every internal tier-segment boundary the bar reaches, rendered in light gray.
- Bars fill the slot (minus the 1 px gap).

- [ ] **Step 3: Wait for the user's verdict**

If the user reports the render is correct, the plan is done. If they report a visual bug, capture the specific symptom and diagnose with the spec in hand — likely culprits: off-by-one in `bar_w` (slot-fill issue), wrong index into a lookup table, missing `#ifdef PBL_COLOR` guard around a B&W path, or a missed `s_rain_overflow_*` rename.

No commit on this task — verification only.

---

## Self-Review

Spec coverage check:

| Spec section | Covered by |
|--------------|------------|
| Visual model + tier table | Task 2, Step 4 (drawing block uses `RAIN_TIER_HEIGHT_FIFTHS` and the classifier) |
| Boundary convention (upper-inclusive on tenths) | Task 1, Step 2 (`RAIN_TIER_MAX_TENTHS = {1, 5, 20, 100}` with `tenths <= max` test) |
| Tier-segment separators (incl. over the gradient) | Task 2, Step 4 ("Pass 3" loop) |
| Slot-width bar | Task 2, Step 4 (`bar_w = (int)entry_w - 1`) |
| Gradient palettes (4 tiers × 3 entries) | Task 1, Step 1 + Task 2, Step 3 (rename) |
| `BAR_SEPARATOR_COLOR` stays | Task 2, Step 1 (kept; comment updated) |
| Constants removed | Task 2, Steps 1–2 |
| Fixture coverage of all 5 tiers | Task 3, Step 1 |
| B&W behavior (no gradient, heights + gray separators) | Task 2, Step 4 (separators run on both branches; gradient inside `#ifdef PBL_COLOR`) |
| Build clean across platforms | User-driven post-Task 2 build (the implementer does not run builds) |
| Visual correctness across platforms | Task 4, Steps 1–2 (color + B&W checklists for the user) |

No gaps found. No placeholders. Type and name consistency verified: `RAIN_TIER_*`, `s_rain_tier_*`, `rain_tier_of_tenths`, `bar_w`, `bar_h`, `grad_h` all match between tasks.
