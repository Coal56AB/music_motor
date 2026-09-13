/* Verbatim update_overview from ESP commit 00e979e. Do not update with production code. */
#define overview reference_overview
#define overview_started reference_overview_started
#define overview_off reference_overview_off
#define overview_was_active reference_overview_was_active
#define update_overview reference_update_overview
static Motor overview[6];
static uint32_t overview_started[6], overview_off[6];
static uint8_t overview_was_active[6];
#undef OVERVIEW_MIN_MS
#undef OVERVIEW_GAP_MS
#define OVERVIEW_MIN_MS 1000u
#define OVERVIEW_GAP_MS 1000u
static void update_overview(const uint8_t *previous_heights) {
  for (unsigned i = 0; i < 6; ++i) {
    Motor old = overview[i];
    int active = !!(motors[i].flags & 2) || ((flags & 64) && (motors[i].flags & 8));
    int clear = !have_state || !(flags & 1) || sleeping || resetting ||
                !(mask & (1u << i)) || (flags & 4);
    if (!(flags & (32 | 64))) { /* File/queued playback; live and manual stay immediate. */
      overview[i] = motors[i];
      overview[i].flags &= 7;
      if ((flags & 64) && (motors[i].flags & 8)) overview[i].flags |= 2;
      if (clear) overview[i].flags = 0;
      overview_was_active[i] = 0;
    } else if (clear) {
      overview[i] = motors[i];
      overview[i].flags = 0;
      overview_was_active[i] = 0;
    } else if (active) {
      if (!overview_was_active[i] || overview[i].note != motors[i].note)
        overview_started[i] = now;
      overview[i] = motors[i];
      overview[i].flags = (overview[i].flags & 7) | 2; /* Actual note or confirmed queue hold. */
      overview_was_active[i] = 1;
    } else {
      if (overview_was_active[i]) {
        overview_off[i] = now;
        if ((int32_t)(now - overview_started[i]) < (int32_t)OVERVIEW_MIN_MS)
          overview_off[i] = overview_started[i] + OVERVIEW_MIN_MS;
      }
      overview_was_active[i] = 0;
      if ((int32_t)(now - overview_started[i]) >= (int32_t)OVERVIEW_MIN_MS &&
          (int32_t)(now - overview_off[i]) >= (int32_t)OVERVIEW_GAP_MS)
        overview[i].flags &= (uint8_t)~2u;
    }
    if (page == 0) {
      int a = previous_heights ? previous_heights[i] : bar_height(&old);
      int v = bar_height(&overview[i]);
      int w = 75 * widths[width_index] / 100, x = 8 + (int)i * 78;
      if (a != v)
        invalidate(x + (75 - w) / 2, 128 - (a > v ? a : v), w,
                   a > v ? a - v : v - a);
      if ((show_hz ? old.mhz != overview[i].mhz : old.note != overview[i].note) ||
          ((old.flags ^ overview[i].flags) & 2))
        invalidate(x, 130, 75, 18);
      if ((old.flags ^ overview[i].flags) & 2)
        invalidate(x, 150, 75, 48);
    }
  }
}

#undef overview
#undef overview_started
#undef overview_off
#undef overview_was_active
#undef update_overview
