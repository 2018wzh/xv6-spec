# VisionFive 2 four-hart usertests evidence

- Board: StarFive VisionFive 2 (JH7110, four SiFive U74 harts)
- Boot: SPI U-Boot 2021.10 -> TFTP legacy uImage + DTB -> xv6-spec VF2 kernel
- Storage: SD card, xv6fs partition at LBA `0x7675000`
- Date: 2026-08-16 (local checkpoint)
- Result: `ALL TESTS PASSED`

## Files

- `vf2-four-hart-usertests-part1.log` — first capture segment (boot, self-tests,
  usertests quick + slow tests, up to ~900s).
- `vf2-four-hart-usertests-part2-resume.log` — resumed capture from the live
  serial stream through `ALL TESTS PASSED`.
- `vf2-four-hart-usertests-summary.txt` — deduplicated key lines (boot markers,
  hart starts, test names, final result).

## How it was captured

The first user process was temporarily changed from `sh` to `usertests` in
`platform/visionfive2/user/init.c` so the four-hart run did not depend on
interactive console input. The kernel was loaded over TFTP and the serial
console was captured until `ALL TESTS PASSED`. The temporary init change has
been reverted in the committed source; the evidence logs document the verified
hardware run.
