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
