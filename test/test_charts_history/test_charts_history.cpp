#include <unity.h>
#include <atomic>
#include <thread>
#include <math.h>
#include "ArduinoMock.h"
#include "TimeMock.h"
#include "core/ChartsRuntimeState.h"
#include "modules/ChartsHistory.h"
#include "modules/StorageManager.h"
#include "modules/OutdoorFeed.h"
#include "web/WebChartsApiUtils.h"

constexpr uint32_t base_epoch = 1767225600; // UTC hour boundary
constexpr auto TEMP = ChartsHistory::METRIC_TEMPERATURE;
constexpr auto CO2 = ChartsHistory::METRIC_CO2;
constexpr auto GAS = ChartsHistory::METRIC_OPTIONAL_GAS;
constexpr uint16_t temp_mask = ChartsHistory::metricBit(TEMP);
void at(uint32_t s) { setMillis(s * 1000); setNowEpoch(base_epoch + s); }
void setUp() {
    at(0); ChartsHistory::setNowEpochFn(&mockNow);
    StorageManager::setTestForceSaveFailure(false);
    ChartsRuntimeState::setSnapshotCopyHook(nullptr);
    FreeRtosSemaphoreMock::resetBlockedTakeHook();
}
void tearDown() { StorageManager::setTestForceSaveFailure(false); ChartsHistory::setNowEpochFn(nullptr); }
SensorData temp(float v) { SensorData d; d.temperature=v; d.temp_valid=true; return d; }
void observe(ChartsHistory &h, StorageManager &s, uint32_t second, float value, uint16_t mask=temp_mask) {
    at(second); h.update(temp(value),s,false,true,mask);
}
ChartsHistory::Entry entry(ChartsHistory &h, uint16_t n=0) {
    ChartsHistory::Entry e; TEST_ASSERT_TRUE(h.entryFromOldest(n,e)); return e;
}
class View : public WebChartsApiUtils::HistoryView {
public:
    explicit View(const ChartsRuntimeState::Snapshot &s): s_(s) {}
    uint16_t count() const override { return s_.count(); }
    uint32_t latestEpoch() const override { return s_.latestEpoch(); }
    bool latestMetric(ChartsHistory::Metric m,float &v) const override { return s_.latestMetric(m,v); }
    bool entryFromOldest(uint16_t n,ChartsHistory::Entry &e) const override { return s_.entryFromOldest(n,e); }
    bool metricValueFromOldest(uint16_t n,ChartsHistory::Metric m,float &v,bool &b) const override { return s_.metricValueFromOldest(n,m,v,b); }
private: const ChartsRuntimeState::Snapshot &s_;
};
// Frozen v3 block layout (14 metrics), mirrored here to write an old file.
struct EntryV3 {
    uint32_t start_epoch; uint16_t valid_mask; bool persisted; uint8_t kind; uint8_t optional_gas_type;
    float values[14]; float minimum[14]; float maximum[14];
    uint16_t samples[14]; uint16_t first_second[14]; uint16_t last_second[14];
};
struct SegmentV3 { uint32_t magic; uint32_t hour; uint16_t version; uint16_t present; EntryV3 entries[12]; uint32_t checksum; };
static_assert(sizeof(SegmentV3) == 3184, "test mirror of the v3 block");
uint32_t fnv(const void *d, size_t n) {
    const uint8_t *b = static_cast<const uint8_t *>(d); uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    return h;
}
constexpr auto OUT = ChartsHistory::METRIC_OUTDOOR_TEMP;
SensorData outdoor(float v) {
    SensorData d; d.outdoor_temp = v; d.outdoor_temp_valid = true;
    d.outdoor_temp_source = ChartsHistory::OUTDOOR_SOURCE_API; return d;
}
void test_v3_block_loads_with_every_value_and_no_outdoor() {
    StorageManager s; s.begin();
    SegmentV3 old{}; old.magic = 0x43485333; old.version = 3; old.hour = base_epoch / 3600; old.present = 0x3;
    for (int slot = 0; slot < 2; ++slot) {
        EntryV3 &e = old.entries[slot];
        e.start_epoch = base_epoch + slot * 300; e.kind = ChartsHistory::Summary; e.persisted = true;
        e.valid_mask = ChartsHistory::metricBit(TEMP) | ChartsHistory::metricBit(CO2) | ChartsHistory::metricBit(GAS);
        e.optional_gas_type = 3;
        for (int m : {int(TEMP), int(CO2), int(GAS)}) {
            e.values[m] = 10.0f * m + slot + 1; e.minimum[m] = e.values[m] - 1; e.maximum[m] = e.values[m] + 2;
            e.samples[m] = 7 + slot; e.first_second[m] = 4; e.last_second[m] = 290;
        }
    }
    old.checksum = fnv(&old, offsetof(SegmentV3, checksum));
    char path[32]; StorageManager::chartsSegmentPath(static_cast<int>(old.hour % StorageManager::kChartsSegmentCount), path, sizeof(path));
    TEST_ASSERT_TRUE(s.saveBlobAtomic(path, &old, sizeof(old)));
    at(900); ChartsHistory h; h.load(s);
    TEST_ASSERT_EQUAL(2, h.count());
    for (int slot = 0; slot < 2; ++slot) {
        const auto e = entry(h, slot); const EntryV3 &o = old.entries[slot];
        TEST_ASSERT_EQUAL(o.start_epoch, e.start_epoch); TEST_ASSERT_EQUAL(o.valid_mask, e.valid_mask);
        TEST_ASSERT_EQUAL(o.kind, e.kind); TEST_ASSERT_EQUAL(3, e.optional_gas_type);
        TEST_ASSERT_EQUAL(ChartsHistory::OUTDOOR_SOURCE_NONE, e.outdoor_source);
        for (int m = 0; m < 14; ++m) {
            TEST_ASSERT_EQUAL_FLOAT(o.values[m], e.values[m]); TEST_ASSERT_EQUAL_FLOAT(o.minimum[m], e.minimum[m]);
            TEST_ASSERT_EQUAL_FLOAT(o.maximum[m], e.maximum[m]); TEST_ASSERT_EQUAL(o.samples[m], e.samples[m]);
            TEST_ASSERT_EQUAL(o.first_second[m], e.first_second[m]); TEST_ASSERT_EQUAL(o.last_second[m], e.last_second[m]);
        }
        TEST_ASSERT_EQUAL(0, e.samples[OUT]); TEST_ASSERT_FALSE(e.valid_mask & ChartsHistory::metricBit(OUT));
    }
}
void test_v3_block_with_bad_checksum_is_skipped() {
    StorageManager s; s.begin();
    SegmentV3 old{}; old.magic = 0x43485333; old.version = 3; old.hour = base_epoch / 3600; old.present = 0x1;
    old.entries[0].start_epoch = base_epoch; old.entries[0].kind = ChartsHistory::Gap;
    old.checksum = fnv(&old, offsetof(SegmentV3, checksum)) ^ 1U;
    char path[32]; StorageManager::chartsSegmentPath(static_cast<int>(old.hour % StorageManager::kChartsSegmentCount), path, sizeof(path));
    TEST_ASSERT_TRUE(s.saveBlobAtomic(path, &old, sizeof(old)));
    at(900); ChartsHistory h; h.load(s); TEST_ASSERT_EQUAL(0, h.count());
}
void test_outdoor_reading_is_summarised_with_its_source_and_survives_reload() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    const uint16_t bit = ChartsHistory::metricBit(OUT);
    at(0); h.update(outdoor(4.5f), s, false, true, bit);
    at(150); h.update(outdoor(6.5f), s, false, true, bit);
    at(300); h.update(SensorData{}, s, false, true, 0);
    auto e = entry(h);
    TEST_ASSERT_EQUAL(ChartsHistory::OUTDOOR_SOURCE_API, e.outdoor_source); TEST_ASSERT_EQUAL(2, e.samples[OUT]);
    TEST_ASSERT_FLOAT_WITHIN(.001, 5.5, e.values[OUT]); TEST_ASSERT_TRUE(h.metricHasRange(e, OUT));
    ChartsHistory r; r.load(s); auto again = entry(r);
    TEST_ASSERT_EQUAL(ChartsHistory::OUTDOOR_SOURCE_API, again.outdoor_source);
    TEST_ASSERT_FLOAT_WITHIN(.001, 5.5, again.values[OUT]);
}
void test_interval_without_outdoor_carries_no_source() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    observe(h, s, 0, 20); observe(h, s, 300, 0, 0);
    TEST_ASSERT_EQUAL(ChartsHistory::OUTDOOR_SOURCE_NONE, entry(h).outdoor_source);
}
void test_web_history_export_never_emits_outdoor() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    SensorData d = outdoor(3.0f); d.temperature = 21; d.temp_valid = true;
    at(0); h.update(d, s, false, true, ChartsHistory::metricBit(OUT) | temp_mask);
    at(300); h.update(SensorData{}, s, false, true, 0);
    ChartsRuntimeState runtime; runtime.update(h); auto snap = runtime.copySnapshot(); TEST_ASSERT_NOT_NULL(snap.get());
    View view(*snap); ArduinoJson::JsonDocument batch;
    WebChartsApiUtils::fillHistoryJson(batch.to<ArduinoJson::JsonObject>(), view);
    auto metrics = batch["records"][0]["metrics"];
    TEST_ASSERT_TRUE(metrics["1"].is<ArduinoJson::JsonObject>());
    char key[4]; snprintf(key, sizeof(key), "%d", static_cast<int>(OUT));
    TEST_ASSERT_FALSE(metrics[key].is<ArduinoJson::JsonObject>());
}
void test_outdoor_feed_copies_only_a_good_check() {
    WeatherManager::Snapshot snap{}; snap.status = WeatherApi::Status::Ok; snap.temp_c = 7.25f; snap.seq = 1;
    SensorData d; OutdoorFeed::apply(snap, d);
    TEST_ASSERT_TRUE(d.outdoor_temp_valid); TEST_ASSERT_EQUAL_FLOAT(7.25f, d.outdoor_temp);
    TEST_ASSERT_EQUAL(ChartsHistory::OUTDOOR_SOURCE_API, d.outdoor_temp_source);
    uint32_t last = 0;
    TEST_ASSERT_EQUAL(ChartsHistory::metricBit(OUT), OutdoorFeed::freshBit(snap, last));
    TEST_ASSERT_EQUAL(0, OutdoorFeed::freshBit(snap, last)); // same reading again is not fresh
    snap.status = WeatherApi::Status::NoNetwork; snap.seq = 2; OutdoorFeed::apply(snap, d);
    TEST_ASSERT_FALSE(d.outdoor_temp_valid); TEST_ASSERT_EQUAL(ChartsHistory::OUTDOOR_SOURCE_NONE, d.outdoor_temp_source);
    TEST_ASSERT_EQUAL(0, OutdoorFeed::freshBit(snap, last));
}
void test_summary_counts_new_equal_values_and_preserves_peak() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    observe(h,s,0,20); observe(h,s,10,20); observe(h,s,20,80);
    for(uint32_t n=21;n<300;++n) observe(h,s,n,999,0); // repeated cached data is not acquired
    observe(h,s,300,10);
    TEST_ASSERT_EQUAL(1,h.count()); const auto e=entry(h);
    TEST_ASSERT_FLOAT_WITHIN(.001,40,e.values[TEMP]);
    TEST_ASSERT_FLOAT_WITHIN(.001,20,e.minimum[TEMP]); TEST_ASSERT_FLOAT_WITHIN(.001,80,e.maximum[TEMP]);
    TEST_ASSERT_EQUAL(3,e.samples[TEMP]); TEST_ASSERT_EQUAL(0,e.first_second[TEMP]); TEST_ASSERT_EQUAL(20,e.last_second[TEMP]);
    TEST_ASSERT_TRUE(e.persisted); TEST_ASSERT_EQUAL(base_epoch,e.start_epoch);
    observe(h,s,600,0,0); TEST_ASSERT_FLOAT_WITHIN(.001,10,entry(h,1).values[TEMP]);
}
void test_validity_warmup_and_metric_counts_are_independent() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    SensorData d=temp(20); d.co2_valid=true; d.co2=700; d.voc_valid=true; d.voc_index=40;
    h.update(d,s,true,true,0x3fff);
    at(10); d.temperature=NAN; d.co2=900; h.update(d,s,false,true,ChartsHistory::metricBit(CO2)|temp_mask);
    at(300); h.update(d,s,false,true,0);
    auto e=entry(h); TEST_ASSERT_EQUAL(1,e.samples[TEMP]); TEST_ASSERT_EQUAL(2,e.samples[CO2]);
    TEST_ASSERT_FLOAT_WITHIN(.001,800,e.values[CO2]); TEST_ASSERT_EQUAL(0,e.samples[ChartsHistory::METRIC_VOC]);
}
void test_gaps_are_not_pressure_measurements() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    SensorData d=temp(20); d.pressure_valid=true; d.pressure=1000;
    h.update(d,s,false,true,0x3fff); at(1200); h.update(d,s,false,true,0);
    TEST_ASSERT_EQUAL(4,h.count());
    for(int n=1;n<4;++n) { auto e=entry(h,n); TEST_ASSERT_EQUAL(ChartsHistory::Gap,e.kind); TEST_ASSERT_EQUAL(0,e.valid_mask); }
}
void test_save_retry_and_reboot_do_not_claim_unsaved_records() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s); observe(h,s,0,22);
    StorageManager::setTestForceSaveFailure(true); observe(h,s,300,0,0);
    TEST_ASSERT_FALSE(entry(h).persisted); ChartsHistory reboot; reboot.load(s); TEST_ASSERT_EQUAL(0,reboot.count());
    StorageManager::setTestForceSaveFailure(false); observe(h,s,311,0,0);
    TEST_ASSERT_TRUE(entry(h).persisted); reboot.load(s); TEST_ASSERT_EQUAL(1,reboot.count());
    TEST_ASSERT_FLOAT_WITHIN(.001,22,entry(reboot).values[TEMP]);
    at(312); reboot.update(temp(24),s,false,true,temp_mask); observe(reboot,s,600,0,0);
    TEST_ASSERT_EQUAL(2,reboot.count()); TEST_ASSERT_EQUAL(1,entry(reboot,1).samples[TEMP]);
    TEST_ASSERT_EQUAL(12,entry(reboot,1).first_second[TEMP]);
}
void test_retention_ring_and_blocks_restore_last_day() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    for(uint32_t n=0;n<=300;++n) observe(h,s,n*300,static_cast<float>(n));
    TEST_ASSERT_EQUAL(288,h.count()); TEST_ASSERT_EQUAL(base_epoch+12*300,entry(h).start_epoch);
    ChartsHistory r; r.load(s); TEST_ASSERT_EQUAL(288,r.count());
    TEST_ASSERT_EQUAL(base_epoch+299*300,r.latestEpoch()); TEST_ASSERT_EQUAL(base_epoch+12*300,entry(r).start_epoch);
    TEST_ASSERT_FLOAT_WITHIN(.001,299,entry(r,287).values[TEMP]);
}
void test_clock_rollback_never_rewrites_completed_summary() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    observe(h,s,0,20); observe(h,s,300,30); observe(h,s,310,40);
    observe(h,s,250,999); TEST_ASSERT_EQUAL(1,h.count());
    observe(h,s,315,50); observe(h,s,600,0,0);
    TEST_ASSERT_EQUAL(2,h.count()); TEST_ASSERT_FLOAT_WITHIN(.001,20,entry(h).values[TEMP]);
    TEST_ASSERT_FLOAT_WITHIN(.001,50,entry(h,1).values[TEMP]);
}
void test_untrusted_time_keeps_ram_history_out_of_export() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    h.update(temp(20),s,false,false,temp_mask); at(300); h.update(temp(30),s,false,false,temp_mask);
    TEST_ASSERT_EQUAL(1,h.count()); TEST_ASSERT_EQUAL(0,entry(h).start_epoch); TEST_ASSERT_FALSE(entry(h).persisted);
    at(301); h.update(temp(40),s,false,true,temp_mask); observe(h,s,600,0,0);
    TEST_ASSERT_EQUAL(1,h.count()); TEST_ASSERT_EQUAL(base_epoch+300,entry(h).start_epoch);
}
struct Legacy {
    uint32_t magic=0x43524849; uint16_t version=2,reserved=0; uint8_t gas=0,reserved2=0;
    uint32_t epoch=base_epoch-10; uint16_t index=1,count=1;
    uint16_t masks[288]{}; float values[14][288]{};
};
void test_legacy_snapshot_is_preserved_without_invented_statistics() {
    StorageManager s; s.begin(); Legacy old; old.masks[0]=temp_mask; old.values[TEMP][0]=19;
    TEST_ASSERT_TRUE(s.saveBlobAtomic(StorageManager::kChartsPath,&old,sizeof(old)));
    ChartsHistory h; h.load(s); auto e=entry(h);
    TEST_ASSERT_EQUAL(ChartsHistory::LegacySnapshot,e.kind); TEST_ASSERT_EQUAL(old.epoch,e.start_epoch);
    TEST_ASSERT_FLOAT_WITHIN(.001,19,e.values[TEMP]); TEST_ASSERT_EQUAL(0,e.samples[TEMP]);
    TEST_ASSERT_TRUE(h.metricValid(e,TEMP)); TEST_ASSERT_FALSE(h.metricHasRange(e,TEMP)); // no band for legacy
    h.update(temp(500),s,false,false,temp_mask); TEST_ASSERT_EQUAL(1,h.count());
    observe(h,s,0,20); observe(h,s,300,0,0); TEST_ASSERT_EQUAL(2,h.count());
    ChartsHistory r; r.load(s); TEST_ASSERT_EQUAL(2,r.count());
}
void test_optional_gas_keeps_record_identity_and_filters_current_chart() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s); SensorData d;
    d.optional_gas_sensor_present=d.optional_gas_valid=true; d.optional_gas_type=1; d.optional_gas_ppm=12;
    h.update(d,s,false,true,ChartsHistory::metricBit(GAS)); at(300); h.update(d,s,false,true,0);
    d.optional_gas_type=5; d.optional_gas_ppm=.1; at(301); h.update(d,s,false,true,ChartsHistory::metricBit(GAS));
    at(600); h.update(d,s,false,true,0);
    auto old=entry(h); TEST_ASSERT_EQUAL(1,old.optional_gas_type); TEST_ASSERT_EQUAL(1,old.samples[GAS]);
    float v; bool valid; h.metricValueFromOldest(0,GAS,v,valid); TEST_ASSERT_FALSE(valid);
    h.metricValueFromOldest(1,GAS,v,valid); TEST_ASSERT_TRUE(valid); TEST_ASSERT_FLOAT_WITHIN(.001,.1,v);
    TEST_ASSERT_FALSE(h.metricHasRange(old,GAS)); ChartsHistory::Entry now; h.entryFromOldest(1,now);
    TEST_ASSERT_TRUE(h.metricHasRange(now,GAS)); TEST_ASSERT_FALSE(h.metricValid(now,TEMP));
}
void test_clear_removes_legacy_and_segment_history() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    observe(h,s,0,20); observe(h,s,300,30); h.clear(s);
    TEST_ASSERT_EQUAL(0,h.count()); ChartsHistory r; r.load(s); TEST_ASSERT_EQUAL(0,r.count());
}
void test_chart_and_bounded_export_share_exact_summary_values() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    for(uint32_t n=0;n<=12;++n) observe(h,s,n*300,20+n);
    ChartsRuntimeState runtime; runtime.update(h); auto snap=runtime.copySnapshot(); TEST_ASSERT_NOT_NULL(snap.get());
    View view(*snap); ArduinoJson::JsonDocument chart, batch;
    WebChartsApiUtils::fillJson(chart.to<ArduinoJson::JsonObject>(),view,"1h","core","ppm",true);
    WebChartsApiUtils::fillHistoryJson(batch.to<ArduinoJson::JsonObject>(),view);
    TEST_ASSERT_EQUAL(8,batch["records"].size()); TEST_ASSERT_TRUE(batch["has_more"].as<bool>());
    TEST_ASSERT_FLOAT_WITHIN(.001,20,batch["records"][0]["metrics"]["1"]["mean"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(.001,20,chart["series"][1]["values"][0].as<float>());
    TEST_ASSERT_EQUAL(1,chart["series"][1]["count"][0].as<int>());
    auto cursor=batch["next_after"].as<uint32_t>(); batch.clear();
    WebChartsApiUtils::fillHistoryJson(batch.to<ArduinoJson::JsonObject>(),view,cursor);
    TEST_ASSERT_EQUAL(4,batch["records"].size()); TEST_ASSERT_FALSE(batch["has_more"].as<bool>());
}
std::atomic<bool> copying{false}, waiting{false};
void blocked(SemaphoreHandle_t) { waiting=true; }
void copying_hook() { copying=true; while(!waiting.load()) std::this_thread::yield(); }
void test_snapshot_remains_one_generation_during_concurrent_update() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s); observe(h,s,0,20); observe(h,s,300,30);
    ChartsRuntimeState runtime; runtime.update(h); observe(h,s,600,0,0);
    copying=false; waiting=false; FreeRtosSemaphoreMock::setBlockedTakeHook(&blocked);
    ChartsRuntimeState::setSnapshotCopyHook(&copying_hook);
    std::thread writer([&] { while(!copying.load()) std::this_thread::yield(); runtime.update(h); });
    auto old=runtime.copySnapshot(); writer.join();
    ChartsRuntimeState::setSnapshotCopyHook(nullptr); FreeRtosSemaphoreMock::resetBlockedTakeHook();
    TEST_ASSERT_EQUAL(1,old->count()); auto current=runtime.copySnapshot(); TEST_ASSERT_EQUAL(2,current->count());
    ChartsHistory::Entry e; old->entryFromOldest(0,e); TEST_ASSERT_FLOAT_WITHIN(.001,20,e.values[TEMP]);
}

void test_export_waits_for_durable_save_and_does_not_advance_cursor() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    observe(h,s,0,20); StorageManager::setTestForceSaveFailure(true); observe(h,s,300,0,0);
    ChartsRuntimeState runtime; runtime.update(h); auto snapshot=runtime.copySnapshot(); View view(*snapshot);
    ArduinoJson::JsonDocument batch; WebChartsApiUtils::fillHistoryJson(batch.to<ArduinoJson::JsonObject>(),view);
    TEST_ASSERT_EQUAL(0,batch["records"].size()); TEST_ASSERT_EQUAL(0,batch["next_after"].as<uint32_t>());
    TEST_ASSERT_TRUE(batch["has_more"].as<bool>());
    StorageManager::setTestForceSaveFailure(false); observe(h,s,311,0,0); runtime.update(h);
    auto saved=runtime.copySnapshot(); View saved_view(*saved); batch.clear();
    WebChartsApiUtils::fillHistoryJson(batch.to<ArduinoJson::JsonObject>(),saved_view);
    TEST_ASSERT_EQUAL(1,batch["records"].size());
}
void test_retention_boundary_stays_stable_between_completed_intervals() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    for(uint32_t n=0;n<=288;++n) observe(h,s,n*300,20);
    observe(h,s,288*300+1,20); TEST_ASSERT_EQUAL(288,h.count());
    TEST_ASSERT_EQUAL(base_epoch,entry(h).start_epoch);
}
void test_clear_all_removes_segment_files_too() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    observe(h,s,0,20); observe(h,s,300,20); s.clearAll();
    ChartsHistory reboot; reboot.load(s); TEST_ASSERT_EQUAL(0,reboot.count());
}
void test_corrupt_block_is_not_exported_as_valid_measurements() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s);
    observe(h,s,0,20); observe(h,s,300,20);
    char path[32]; StorageManager::chartsSegmentPath((base_epoch/3600)%StorageManager::kChartsSegmentCount,path,sizeof(path));
    const uint8_t garbage[3]={1,2,3}; TEST_ASSERT_TRUE(s.saveBlobAtomic(path,garbage,sizeof(garbage)));
    ChartsHistory reboot; reboot.load(s); TEST_ASSERT_EQUAL(0,reboot.count());
}

void test_sen0321_history_summary_export_and_model_change() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s); SensorData d{};
    d.optional_gas_sensor_present=d.optional_gas_valid=true;
    d.optional_gas_type=7; d.optional_gas_ppm=.02f;
    at(0); h.update(d,s,false,true,ChartsHistory::metricBit(GAS));
    at(100); d.optional_gas_ppm=.08f; h.update(d,s,false,true,ChartsHistory::metricBit(GAS));
    at(300); h.update(d,s,false,true,0);
    ChartsRuntimeState runtime; runtime.update(h); auto snap=runtime.copySnapshot(); View view(*snap);
    ArduinoJson::JsonDocument chart, batch;
    WebChartsApiUtils::fillJson(chart.to<ArduinoJson::JsonObject>(),view,"1h","gas","ppb",true);
    const auto series=chart["series"][4]; const auto index=Config::CHART_HISTORY_1H_STEPS-1;
    TEST_ASSERT_FLOAT_WITHIN(.001,50,series["values"][index].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(.001,20,series["min"][index].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(.001,80,series["max"][index].as<float>());
    WebChartsApiUtils::fillHistoryJson(batch.to<ArduinoJson::JsonObject>(),view);
    const auto gas_key=std::to_string(static_cast<int>(GAS));
    TEST_ASSERT_EQUAL(7,batch["records"][0]["optional_gas_type"].as<int>());
    TEST_ASSERT_FLOAT_WITHIN(.00001,.05,batch["records"][0]["metrics"][gas_key]["mean"].as<float>());
    d.optional_gas_type=5; at(301); h.update(d,s,false,true,ChartsHistory::metricBit(GAS));
    float v; bool valid; h.metricValueFromOldest(0,GAS,v,valid); TEST_ASSERT_FALSE(valid);
}
struct Published {
    int calls = 0;
    ChartsHistory::CompletedInterval last{};
    static void onInterval(const ChartsHistory::CompletedInterval &i, void *ctx) {
        auto &p = *static_cast<Published *>(ctx); ++p.calls; p.last = i;
    }
};
void otherListener(const ChartsHistory::CompletedInterval &, void *) {}
void test_completed_interval_publishes_exact_sum_and_low_high_seconds() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s); Published p;
    TEST_ASSERT_TRUE(h.setIntervalListener(&Published::onInterval, &p));
    observe(h,s,0,20.1f); observe(h,s,10,80.3f); observe(h,s,20,80.3f);
    observe(h,s,40,10.7f); observe(h,s,50,10.7f); observe(h,s,60,33.0f);
    TEST_ASSERT_EQUAL(0,p.calls);
    observe(h,s,300,0,0);
    TEST_ASSERT_EQUAL(1,p.calls);
    const auto &i=p.last;
    TEST_ASSERT_EQUAL(base_epoch,i.entry.start_epoch);
    TEST_ASSERT_EQUAL(ChartsHistory::Summary,i.entry.kind);
    TEST_ASSERT_EQUAL(6,i.entry.samples[TEMP]);
    const double expected=static_cast<double>(20.1f)+80.3f+80.3f+10.7f+10.7f+33.0f;
    TEST_ASSERT_TRUE(expected==i.sums[TEMP]);
    TEST_ASSERT_EQUAL_FLOAT(10.7f,i.entry.minimum[TEMP]); TEST_ASSERT_EQUAL(40,i.min_second[TEMP]);
    TEST_ASSERT_EQUAL_FLOAT(80.3f,i.entry.maximum[TEMP]); TEST_ASSERT_EQUAL(10,i.max_second[TEMP]);
    TEST_ASSERT_EQUAL(0,i.entry.samples[CO2]); TEST_ASSERT_EQUAL(0,i.max_second[CO2]);
    // The persisted ring entry is the same record.
    TEST_ASSERT_EQUAL_FLOAT(i.entry.values[TEMP],entry(h).values[TEMP]);
    // A fresh interval starts with fresh low/high seconds.
    observe(h,s,305,5); observe(h,s,600,0,0);
    TEST_ASSERT_EQUAL(2,p.calls); TEST_ASSERT_EQUAL(5,p.last.min_second[TEMP]); TEST_ASSERT_EQUAL(5,p.last.max_second[TEMP]);
}
void test_gaps_untrusted_and_rolled_back_intervals_are_not_published() {
    StorageManager s; s.begin(); ChartsHistory h; h.load(s); Published p;
    TEST_ASSERT_TRUE(h.setIntervalListener(&Published::onInterval, &p));
    h.update(temp(20),s,false,false,temp_mask); at(300); h.update(temp(30),s,false,false,temp_mask);
    TEST_ASSERT_EQUAL(1,h.count()); TEST_ASSERT_EQUAL(0,p.calls); // untrusted
    at(301); h.update(temp(40),s,false,true,temp_mask); observe(h,s,1200,0,0);
    TEST_ASSERT_EQUAL(1,p.calls); TEST_ASSERT_EQUAL(base_epoch+300,p.last.entry.start_epoch);
    observe(h,s,1500,0,0); // the interval at 1200 had no samples: a gap
    TEST_ASSERT_EQUAL(1,p.calls);
    // Clock moved back: the unfinished interval is discarded and sampling
    // holds until the clock catches up, so nothing is published twice.
    observe(h,s,1510,22); observe(h,s,1400,99); observe(h,s,1505,98);
    observe(h,s,1520,23); observe(h,s,1800,0,0);
    TEST_ASSERT_EQUAL(2,p.calls); TEST_ASSERT_EQUAL(base_epoch+1500,p.last.entry.start_epoch);
    TEST_ASSERT_EQUAL(1,p.last.entry.samples[TEMP]); TEST_ASSERT_EQUAL_FLOAT(23,p.last.entry.minimum[TEMP]);
    TEST_ASSERT_EQUAL(20,p.last.min_second[TEMP]);
}
void test_only_one_interval_listener_can_be_registered() {
    ChartsHistory h; Published p, q;
    TEST_ASSERT_TRUE(h.setIntervalListener(&Published::onInterval, &p));
    TEST_ASSERT_TRUE(h.setIntervalListener(&Published::onInterval, &p)); // same pair again
    TEST_ASSERT_FALSE(h.setIntervalListener(&Published::onInterval, &q)); // same function, other object
    TEST_ASSERT_FALSE(h.setIntervalListener(&otherListener, nullptr));
    TEST_ASSERT_TRUE(h.setIntervalListener(nullptr, nullptr));
    TEST_ASSERT_TRUE(h.setIntervalListener(&otherListener, nullptr));
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sen0321_history_summary_export_and_model_change);
    RUN_TEST(test_export_waits_for_durable_save_and_does_not_advance_cursor);
    RUN_TEST(test_retention_boundary_stays_stable_between_completed_intervals);
    RUN_TEST(test_clear_all_removes_segment_files_too);
    RUN_TEST(test_corrupt_block_is_not_exported_as_valid_measurements);
    RUN_TEST(test_summary_counts_new_equal_values_and_preserves_peak);
    RUN_TEST(test_validity_warmup_and_metric_counts_are_independent);
    RUN_TEST(test_gaps_are_not_pressure_measurements);
    RUN_TEST(test_save_retry_and_reboot_do_not_claim_unsaved_records);
    RUN_TEST(test_retention_ring_and_blocks_restore_last_day);
    RUN_TEST(test_clock_rollback_never_rewrites_completed_summary);
    RUN_TEST(test_untrusted_time_keeps_ram_history_out_of_export);
    RUN_TEST(test_legacy_snapshot_is_preserved_without_invented_statistics);
    RUN_TEST(test_optional_gas_keeps_record_identity_and_filters_current_chart);
    RUN_TEST(test_clear_removes_legacy_and_segment_history);
    RUN_TEST(test_chart_and_bounded_export_share_exact_summary_values);
    RUN_TEST(test_snapshot_remains_one_generation_during_concurrent_update);
    RUN_TEST(test_completed_interval_publishes_exact_sum_and_low_high_seconds);
    RUN_TEST(test_gaps_untrusted_and_rolled_back_intervals_are_not_published);
    RUN_TEST(test_only_one_interval_listener_can_be_registered);
    RUN_TEST(test_v3_block_loads_with_every_value_and_no_outdoor);
    RUN_TEST(test_v3_block_with_bad_checksum_is_skipped);
    RUN_TEST(test_outdoor_reading_is_summarised_with_its_source_and_survives_reload);
    RUN_TEST(test_interval_without_outdoor_carries_no_source);
    RUN_TEST(test_web_history_export_never_emits_outdoor);
    RUN_TEST(test_outdoor_feed_copies_only_a_good_check);
    return UNITY_END();
}
