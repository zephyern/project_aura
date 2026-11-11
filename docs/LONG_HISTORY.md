# Long-term Aura history (SD card)

Status: being built as a stack of small pull requests. Nothing here changes device
behaviour until the `history.log_enabled` setting exists and is turned on; it ships
off. This file grows with each pull request in the stack.

The existing five-minute history ([FIVE_MINUTE_HISTORY.md](FIVE_MINUTE_HISTORY.md)) keeps
the last 24 hours on flash and is unchanged. Long-term history is a separate, append-only
record on the SD card built from the same completed five-minute intervals.

## Exact aggregates

`src/modules/HistoryAggregate.{h,cpp}` holds, per metric and time bucket, the exact
`double` sum and the sample count instead of a rounded mean, together with the low, the
high and the UTC second at which each was first reached. Every coarser bucket is built by
combining finer ones, so its mean equals the mean computed directly from the raw samples
and no rounding accumulates across levels.

- Combining with an empty aggregate changes nothing.
- When both sides reach the same low (or high), the earlier time is kept.
- Each aggregate carries a metric-specific `source` code. Two non-empty aggregates with
  different sources are never combined; `combine` returns `SourceMismatch` and leaves the
  target unchanged. This is the same rule ChartsHistory applies to optional gas sensors.
- A combined count that would overflow 32 bits returns `CountOverflow`, target unchanged.

Host tests: `test/test_history_aggregate`.

## Source: completed five-minute intervals

ChartsHistory publishes each completed interval to one registered listener
(`ChartsHistory::setIntervalListener`) at the moment it is appended to the ring. The
`CompletedInterval` carries the ring `Entry` plus RAM-only statistics the persisted entry
rounds away: the exact `double` sum per metric and the second (0-299) at which the low
and the high were first reached. The `Entry` layout and the flash schema (v3) are
unchanged.

Only intervals with a trusted UTC start and at least one valid metric are published.
Gaps, intervals started without trusted time, and intervals discarded or refused by the
existing clock checks (backward correction, not later than the newest record) are never
published, so a listener can never receive the same start twice from one session.
One listener is registered at a time: registering a different function or context while
one is set is refused (`false`); registering the same pair again is harmless; unregister
with `nullptr`.

Host tests: `test/test_charts_history` (publication, exact sums and low/high seconds,
gaps, untrusted time, backward clock, single listener).

## Quarter-hour row format

`src/modules/QuarterHourRow.{h,cpp}` encodes and decodes one CSV row per UTC quarter hour
(start a multiple of 900 seconds). The first header column, `start_utc_v1`, also carries
the format version. Then, for each metric in ChartsHistory order with the keys
DailyExtremaHistory already uses (`co2`, `temperature`, ... `optional_gas`), six columns:
`<key>_sum,<key>_count,<key>_min,<key>_min_s,<key>_max,<key>_max_s`. The last column,
`optional_gas_type`, names which optional gas sensor produced `optional_gas_*` (0 = none),
so a reader can show the right unit.

- `_sum` is the exact double sum (`%.17g`); low and high are floats (`%.9g`); both round
  trip bit for bit. `_min_s`/`_max_s` are the seconds into the quarter (0-899) at which the
  low and the high were first reached.
- A metric without samples has `_count` 0 and its other five fields empty.
- Only plain numbers appear, so there is no quoting or escaping, and `.` is always the
  decimal separator.
- Decoding is strict and typed: `UnsupportedVersion` (another `start_utc_vN`), `BadHeader`,
  `UnknownColumn`, `Malformed` (wrong field count, which is what a row torn by a power cut
  looks like, or a field that is not a plain number), `NonFinite`, `BadValue`. Encoding
  refuses a bad row with a typed result and leaves the output untouched.

Host tests: `test/test_quarter_hour_row`.

## Calendar helpers

`src/core/LocalDate.{h,cpp}` is the one place that turns epochs into dates: thread-safe
`localtimeInto`/`gmtimeInto`, `localDayKey`/`utcDayKey` (YYYYMMDD, only for trusted time
after `Config::TIME_VALID_EPOCH` and years from 2020), `formatDayKey` ("YYYY-MM-DD") and
`formatLocalTime` ("HH:MM:SS"). DailyExtremaHistory and TimeManager used to carry their
own private copies of these; both now call LocalDate, with unchanged behaviour. Long-term
history files are named by `utcDayKey`; charts that group by the viewer's day use
`localDayKey`, which follows the device's time zone through daylight-saving changes
(23- and 25-hour days).

Host tests: `test/test_local_date` (local vs UTC day, both US daylight-saving days,
untrusted time, formatting), plus the existing DailyExtremaHistory and TimeManager tests.

## Directory listing and empty-directory removal

`DailyHistoryStorage` gains `listDir` and `removeEmptyDir`, implemented by SdCardManager with
the POSIX `opendir`/`readdir`/`rmdir` calls the ESP-IDF FAT VFS already provides (it fills
`d_type`; a filesystem that leaves it unknown falls back to `stat`). Results are typed:

- `listDir` → `Listed`, `Absent` (no such directory, not a card fault) or `Failed`
  (`lastFailureKind()` says why). The callback runs under the storage lock and must not
  call back into storage; it returns `false` to stop early.
- `removeEmptyDir` → `Removed`, `Absent`, `NotEmpty` or `Failed`. FatFs reports `rmdir` of a
  non-empty directory as `FR_DENIED`, which the VFS maps to `EACCES`, the same error as a
  read-only card, so SdCardManager first checks for entries and returns `NotEmpty` without
  marking the card unhealthy. The card root is never removed.

Host tests run SdCardManager against a real temporary directory standing in for the card
(`SdCardManager::setTestMountRoot`, host tests only): `test/test_sd_storage_files`. The
DailyExtremaHistory fake storage implements both operations from its in-memory files.

## Files, torn-tail repair and the shared reader

`src/modules/LongHistoryFiles.{h,cpp}`:

- `quarterFilePath` builds `/aura/history/15min/YYYY/MM/YYYY-MM-DD.csv` from a UTC day key.
- `findNewestQuarterFile` walks the year, month and day directories (A5 listing) newest
  first and ignores anything that is not a well-formed day file in the matching directory.
- `repairTail` removes the bytes after the last `\n`, which is what a power cut during an
  append leaves. A file that never completed its header is replaced by the header. The
  file is rewritten only when something is removed, through
  `DailyHistoryStorage::truncateAtomic`, which streams the kept prefix through a temporary
  file and the same atomic replace the other SD writes use, so it never holds the file in
  RAM. Each repair logs one line naming the file and the bytes removed; it is not shown as
  a user error. It runs at boot on the newest file of each kind, before anything appends.
- `readQuarterFile` is the one reader of 15-minute files. It checks the header, then
  delivers every row that decodes; a bad line in the middle (or an unterminated last line)
  is skipped and logged with its line number and reason, and counted in `ReadStats`.
  `DailyHistoryStorage::readRange` reads the file in 4 KiB pieces.

Host tests: `test/test_long_history_files` (real SdCardManager on a temporary directory:
torn row, torn header, untouched clean file, mid-file skip logged, unterminated last line,
foreign header, newest-file search, range read and atomic truncate).

## Forward-only rule

`src/modules/ForwardOnlyGuard.{h,cpp}` is the one rule every long-term history appender
follows: a record is written only if it starts after the file's last record
(`accept(last, new)` → `Accept`, or `ClockBehind` with the last epoch). Equal and earlier
starts are refused, so a clock that moved back pauses recording, with status `ClockBehind`
for the alert bar, until it passes the last record; a clock that moved forward just leaves
a gap. At boot, after the torn-tail repair, `lastQuarterEpoch` reads each file's last good
row. A file whose header is not supported is reported `Failed` and never appended to.
Upstream's own short hold for DailyExtremaHistory keeps working unchanged.

`clearFutureQuarterRows(now)` is the logic behind the "clear future-dated history" action
(its Settings button arrives with the history settings): for a clock once set far ahead,
it cuts today's file at its first row starting after `now`, removes every later day file,
and removes the month and year directories left empty. It refuses to run without trusted
time and logs one line with the counts.

Host tests: `test/test_forward_only_guard`.
