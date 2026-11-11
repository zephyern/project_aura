// SPDX-FileCopyrightText: 2025-2026 Volodymyr Papush (21CNCStudio)
// SPDX-License-Identifier: GPL-3.0-or-later
// GPL-3.0-or-later: https://www.gnu.org/licenses/gpl-3.0.html
// Want to use this code in a commercial product while keeping modifications proprietary?
// Purchase a Commercial License: see COMMERCIAL_LICENSE_SUMMARY.md

#pragma once

#include <Arduino.h>
#include <memory>
#include <time.h>
#include "config/AppConfig.h"
#include "config/AppData.h"

class StorageManager;

// One acquisition accumulator and retained history for local charts and export.
class ChartsHistory {
public:
    enum Metric : uint8_t {
        METRIC_CO2, METRIC_TEMPERATURE, METRIC_HUMIDITY, METRIC_PRESSURE,
        METRIC_CO, METRIC_VOC, METRIC_NOX, METRIC_HCHO, METRIC_PM05,
        METRIC_PM1, METRIC_PM25, METRIC_PM4, METRIC_PM10, METRIC_OPTIONAL_GAS,
        // Optional outdoor temperature (docs/WEATHER.md); device charts and
        // history only, never exported on the web, MQTT or the DAC.
        METRIC_OUTDOOR_TEMP,
        METRIC_COUNT
    };
    // Where an outdoor reading came from. Readings from different sources are
    // never combined. 2 is reserved for a physical outdoor thermometer.
    enum OutdoorSource : uint8_t { OUTDOOR_SOURCE_NONE = 0, OUTDOOR_SOURCE_API = 1 };
    static constexpr int kCapacity = Config::CHART_HISTORY_24H_SAMPLES;
    static constexpr int kMetricCount = METRIC_COUNT;
    static constexpr uint32_t kStepSeconds = 300;
    // v4 added METRIC_OUTDOOR_TEMP and Entry::outdoor_source; v3 blocks load.
    static constexpr uint16_t kSchemaVersion = 4;
    static constexpr uint16_t kSchemaVersionV3 = 3;
    // The metrics before outdoor temperature: the v2 snapshot and v3 blocks.
    static constexpr int kLegacyMetricCount = 14;
    enum Kind : uint8_t { Gap = 0, LegacySnapshot = 1, Summary = 2 };
    struct Entry {
        uint32_t start_epoch = 0; // UTC interval start; zero means unanchored RAM history.
        uint16_t valid_mask = 0;
        bool persisted = false; // Runtime/export status; recomputed when a block is loaded.
        uint8_t kind = Gap;
        uint8_t optional_gas_type = 0;
        uint8_t outdoor_source = OUTDOOR_SOURCE_NONE; // set whenever METRIC_OUTDOOR_TEMP has data
        float values[kMetricCount] = {}; // arithmetic mean of valid new acquisitions
        float minimum[kMetricCount] = {};
        float maximum[kMetricCount] = {};
        uint16_t samples[kMetricCount] = {};
        uint16_t first_second[kMetricCount] = {};
        uint16_t last_second[kMetricCount] = {};
    };
    // A completed, trusted summary as it enters the ring, together with the
    // exact statistics the persisted Entry does not keep: the double sum per
    // metric and the second (0..299) at which the low and the high were first
    // reached. RAM only; the Entry layout and schema are unchanged.
    struct CompletedInterval {
        Entry entry;
        double sums[kMetricCount] = {};
        uint16_t min_second[kMetricCount] = {};
        uint16_t max_second[kMetricCount] = {};
    };
    using IntervalListener = void (*)(const CompletedInterval &interval, void *context);
    static constexpr uint16_t metricBit(Metric metric) {
        return static_cast<uint16_t>(1U << static_cast<uint8_t>(metric));
    }
    void load(StorageManager &storage);
    // fresh_mask identifies *new acquisitions*, including equal consecutive values.
    void update(const SensorData &data, StorageManager &storage, bool gas_warmup,
                bool system_time_trusted, uint16_t fresh_mask = 0);
    void clear(StorageManager &storage);
    uint16_t count() const { return count_; }
    uint16_t index() const { return index_; }
    uint32_t latestEpoch() const;
    uint8_t optionalGasType() const { return optional_gas_type_; }
    // The source long-history records must carry to belong on `metric`'s
    // chart now: the fitted gas type, the weather API for outdoor, else 0.
    uint8_t chartSource(Metric metric) const {
        return metric == METRIC_OPTIONAL_GAS ? optional_gas_type_
             : metric == METRIC_OUTDOOR_TEMP ? static_cast<uint8_t>(OUTDOOR_SOURCE_API) : 0;
    }
    uint32_t revision() const { return revision_; }
    bool entryFromOldest(uint16_t offset, Entry &out) const;
    bool metricValueFromOldest(uint16_t offset, Metric metric, float &value, bool &valid) const;
    // The entry's mean for `metric` is real (valid bit set, and for the
    // optional gas, measured by the gas sensor fitted now).
    bool metricValid(const Entry &entry, Metric metric) const;
    // ...and its minimum/maximum are too (only full summaries carry them).
    bool metricHasRange(const Entry &entry, Metric metric) const;
    using NowEpochFn = time_t (*)();
    static void setNowEpochFn(NowEpochFn fn);
    // One listener receives every completed interval that is appended with a
    // trusted start time and at least one valid metric. Gaps, untrusted
    // intervals and intervals refused by the clock checks are never published.
    // Returns false, leaving the current listener, if a different function or
    // context is already registered; pass nullptr to unregister.
    bool setIntervalListener(IntervalListener listener, void *context);

private:
    struct LegacyState {
        uint32_t magic; uint16_t version; uint16_t reserved;
        uint8_t optional_gas_type; uint8_t reserved2;
        uint32_t epoch; uint16_t index; uint16_t count;
        uint16_t valid_mask[kCapacity];
        float values[kLegacyMetricCount][kCapacity];
    };
    // Frozen v3 block layout (14 metrics), read once on an update and rewritten as v4.
    struct EntryV3 {
        uint32_t start_epoch; uint16_t valid_mask; bool persisted; uint8_t kind;
        uint8_t optional_gas_type;
        float values[kLegacyMetricCount]; float minimum[kLegacyMetricCount];
        float maximum[kLegacyMetricCount]; uint16_t samples[kLegacyMetricCount];
        uint16_t first_second[kLegacyMetricCount]; uint16_t last_second[kLegacyMetricCount];
    };
    struct SegmentV3 {
        uint32_t magic; uint32_t hour; uint16_t version; uint16_t present;
        EntryV3 entries[12];
        uint32_t checksum;
    };
    static_assert(sizeof(EntryV3) == 264 && sizeof(SegmentV3) == 3184, "v3 block disk layout is frozen");
    struct Segment {
        uint32_t magic;
        uint32_t hour;
        uint16_t version;
        uint16_t present;
        Entry entries[12];
        uint32_t checksum;
    };
    static_assert(sizeof(Segment) <= 4096, "History block must remain bounded to 4 KiB");
    static_assert(kCapacity == 288, "Review hourly block retention before changing capacity");
    static_assert(kStepSeconds * 1000 == Config::CHART_HISTORY_STEP_MS, "History step mismatch");
    static time_t nowEpochRaw();
    static NowEpochFn now_epoch_fn_;
    static uint32_t checksum(const Segment &segment);
    static uint32_t checksumV3(const SegmentV3 &segment);
    // Loads `path` as a v3 block into segment_ as v4 entries (no outdoor
    // data). False when it is not a valid v3 block. v3 blocks age out as their
    // hours are rewritten as v4.
    bool readSegmentV3(StorageManager &storage, const char *path);
    static bool validEntry(const Entry &entry);
    bool ensureBuffers();
    void append(const Entry &entry);
    void finishInterval();
    void flushOneSegment(StorageManager &storage, uint32_t now_ms);
    int rawIndexFromOldest(uint16_t offset) const;
    void resetActive();
    std::unique_ptr<Entry[]> entries_;
    std::unique_ptr<Segment> segment_;
    bool dirty_[kCapacity] = {};
    uint16_t count_ = 0, index_ = 0;
    uint8_t optional_gas_type_ = 0;
    uint32_t revision_ = 0;
    Entry active_{};
    double sums_[kMetricCount] = {};
    uint16_t min_second_[kMetricCount] = {};
    uint16_t max_second_[kMetricCount] = {};
    IntervalListener interval_listener_ = nullptr;
    void *interval_listener_context_ = nullptr;
    uint32_t active_bucket_ = 0;
    bool active_set_ = false, active_trusted_ = false, restored_ = false;
    uint32_t last_epoch_ = 0, last_tick_ms_ = 0, last_retry_ms_ = 0;
    uint64_t monotonic_ms_ = 0;
    bool retry_pending_ = false;
};
