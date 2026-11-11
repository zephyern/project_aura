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
