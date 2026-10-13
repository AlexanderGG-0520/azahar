# Yo-kai Watch 2 Shinuchi save-date normalization (experimental)

When Azahar's **Offset Time** is used to save in the future, Yo-kai Watch 2
may keep that date in `game1.yw` after returning to the present. Daily
content then stops resetting until the saved date is reached again.

This fork includes an **experimental, limited** save-close repair:
- Affects only **Japanese Yo-kai Watch 2 Shinuchi** (title `0004000000155100`).
- Only writes to the title's `game1.yw`, after a writable handle closes.
- Does not change YW2's RTC behavior, game code, or normal same-day saves.
- Authenticates the original save, checks the known 71,048-byte format, and
  verifies the edited save before replacement.
- If the internal saved calendar day is later than the emulated calendar day,
  rewrites its timestamp to the current emulated local date/time.
- Keeps the first original file as `game1.yw.before-yw2-datefix.bak`.
- If the backup or atomic replacement fails, leaves the existing save alone.

**Important limitations:** The game may cache the date in memory until restarted.
This patch does not operate on other regions, `head.yw`,
Ganso/Honke version 2.x, or remote Artic save storage. A savestate restored while
a save handle is open does not retain this experimental post-close hook.
Keep a separate backup of the whole save directory before using this feature.

## Reproduction / manual verification

1. Back up the entire savedata folder.
2. Boot Shinuchi with +150 days Offset Time, save in-game, and quit.
3. Reset Offset Time to zero, boot normally, and save again.
4. Quit and inspect `game1.yw` and the additional `.bak`. The current save
   should decrypt and have the current local date rather than the future date.
5. Boot again and check the next 06:00 daily-event reset.

This change is experimental and has **not** been built or tested on a live
Azahar instance in the authoring environment.
