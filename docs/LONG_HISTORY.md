# Long-term Aura history (SD card)

Status: being built as a stack of small pull requests. Nothing here changes device
behaviour until the `history.log_enabled` setting is turned on; it ships off. This file
grows with each pull request in the stack.

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

## Recording 15-minute rows

`src/modules/QuarterHourLogger.{h,cpp}` is ChartsHistory's interval listener. It runs only
when the setting `history.log_enabled` (`/config.json`, key `history.log_enabled`) is on;
the setting ships off, and a missing key keeps it off. Turning it on from the device and
the web page arrives with the history settings later in the stack; until then it can only
be set in `/config.json`.

- It adds the three five-minute intervals of each UTC quarter hour together with
  `HistoryAggregate::combine`, so the row's sum, count, low and high are exact, and the low
  and high times are the interval start plus the second ChartsHistory reported.
- A quarter is written when its third interval arrives, when an interval of a later
  quarter arrives, or, from the main loop's `poll()`, once trusted time is five minutes past
  the quarter's end (ChartsHistory completes an interval when the next one begins).
- A quarter without any valid metric gets no row. An interval for a quarter already
  written is refused and logged.
- If the optional gas sensor type changes within a quarter, the earlier gas readings of
  that quarter are dropped and logged (gases are never mixed, as in ChartsHistory).
- Rows go to the UTC day file through the forward-only rule; a new file starts with the
  header. A power cut loses at most the unwritten quarter (up to 15 minutes).
- When the card becomes ready it is prepared once: torn-tail repair of the newest file,
  then the newest row's time for the forward-only rule. A newest file that cannot be read
  stops recording with a write error rather than being overtaken.

`status()` is typed for the storage status and the alert bar that come later in the stack:
`Off`, `NoCard`, `Recording`, `ClockBehind` (with the last row's time) or `WriteError`
(with the storage failure kind), plus counters of rows written and quarters refused.

Host tests: `test/test_quarter_hour_logger` (default off, exact row from three intervals,
gaps and the poll close, restart mid-quarter with torn tail, clock behind, UTC midnight
file switch, no card and write failure, optional gas change, single listener).

## One derivation for every summary

`src/modules/HistoryBucket.{h,cpp}` is the only code that turns finer records into a
coarser bucket: hours, local days and week levels from 15-minute rows, and chart lines
resampled from those. `Builder::begin(start, end)` opens a bucket for the UTC interval
`[start, end)`; `add` takes 15-minute rows or other summary records in time order and
combines each metric exactly. `deriveBucket` does the same for rows already in memory.

- Only records lying wholly inside the bucket count; others are `Outside` and counted,
  which is normal when scanning a whole file.
- A record that starts before the previous one ended is `OutOfOrder` and an empty or
  reversed interval is `Invalid`; both are refused and logged.
- A partial bucket keeps its real sample count and the number of records it combined
  (`source_records`), so a chart can tell a short hour from a full one.
- Low and high keep the earliest UTC second they were reached, through every level.
- When the optional gas sensor type changes inside a bucket, the newer gas replaces the
  earlier part (`HistoryAggregates::mergeNewer`, the same rule the 15-minute logger
  uses), counted in `Stats::gas_replaced` and logged.

Host tests: `test/test_history_bucket` (brute force over raw samples, hours-then-3-hours
equals direct, partial bucket, refused records, gas change).

## Hourly summaries

`src/modules/HourlySummary.{h,cpp}` defines the hourly level: one record per UTC hour in
`/aura/history/hourly/YYYY-MM.csv`, named by the UTC year and month. The records are
written by `src/modules/SummaryWriter.{h,cpp}`, which every summary level shares, through
`src/modules/SummaryAppender.{h,cpp}` (torn-tail repair, forward-only rule, header on a new
file). Summary files use the span layout of the row codec: the same columns with an
exclusive `end_utc` after the start, and low/high seconds counted from the record start.

- The writer listens to the 15-minute logger's rows and runs only while
  `history.log_enabled` is on. Each row is combined into its hour by `HistoryBucket`
  (the one derivation); an hour is written when its last quarter arrives, when a row of a
  later hour arrives, or from `poll()` ten minutes after the hour ends if its last quarter
  never came. An hour without any data gets no record.
- When the card becomes ready it is prepared once, after the 15-minute logger: the newest
  hourly file is repaired and read, and every hour missing after its last record is rebuilt
  from the 15-minute files, so a reboot or power cut leaves no hole the 15-minute rows can
  fill. Rebuilt records are identical to live ones (same code path).
- A newest hourly file whose header is not a summary header is removed and rebuilt from
  the 15-minute rows of that month. Bad lines elsewhere are skipped and logged by the
  shared reader.
- `readRange(start, end)` returns the records starting in `[start, end)` from every UTC
  month file the range touches, so a local calendar month reads from up to two files.

Host tests: `test/test_hourly_summary` (span layout round trip, live hour from logged
quarters, boot rebuild equals live and continues the open hour, damaged file rebuilt, close
without a last quarter, range across two UTC month files).

## Daily summaries

`src/modules/DailySummary.{h,cpp}` defines the daily level: one record per **local**
calendar day in `/aura/history/days/YYYY.csv`, named by the local year, written by the
shared `SummaryWriter`. Each record stores its own UTC `[start, end)`. Every time zone the
device offers is a whole number of quarter hours from UTC, so a local day is a whole number
of 15-minute rows: 96 normally, 92 or 100 on a daylight-saving change day.

- The day containing a quarter is found with the time zone in effect now (`localtime`,
  `mktime`), so DST days come out 23 or 25 hours long without special cases.
- Time-zone change: finished days are never rewritten. If the zone changes while a day is
  open, that day keeps its start and ends at the next local midnight of the new zone. If a
  change makes the next day start before the last written one ended, the next day starts
  where the last one ended, so records never overlap.
- `readYear(year)` returns that local year's records in time order.

`SummaryWriter` applies these two rules to every level; for the UTC levels (hourly,
weekly) the grouping never changes, so they never trigger.

Host tests: `test/test_daily_summary` (local day bounds and file, 92/100-quarter DST days,
DST day as one exact record, zone change inside a day and between days, boot rebuild equal
to the derivation).
