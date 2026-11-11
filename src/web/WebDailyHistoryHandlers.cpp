// SPDX-FileCopyrightText: 2025-2026 Volodymyr Papush (21CNCStudio)
// SPDX-License-Identifier: GPL-3.0-or-later
// GPL-3.0-or-later: https://www.gnu.org/licenses/gpl-3.0.html
// Want to use this code in a commercial product while keeping modifications proprietary?
// Purchase a Commercial License: see COMMERCIAL_LICENSE_SUMMARY.md

#include "web/WebDailyHistoryHandlers.h"

#include <ArduinoJson.h>
#include <errno.h>
#include <stdio.h>

#include "core/Logger.h"
#include "core/LocalDate.h"
#include "modules/DailyExtremaHistory.h"
#include "modules/LongHistory.h"
#include "modules/LongHistoryFiles.h"
#include "modules/SdCardManager.h"
#include "web/WebResponseUtils.h"

namespace {

constexpr size_t kCsvStreamBufferSize = 768;

class ScopedSdFileLock {
public:
    explicit ScopedSdFileLock(SdCardManager &sd_card, uint32_t timeout_ms = 5000)
        : sd_card_(sd_card), locked_(sd_card.acquireFileAccess(timeout_ms)) {}

    ~ScopedSdFileLock() {
        if (locked_) {
            sd_card_.releaseFileAccess();
        }
    }

    ScopedSdFileLock(const ScopedSdFileLock &) = delete;
    ScopedSdFileLock &operator=(const ScopedSdFileLock &) = delete;

    bool locked() const { return locked_; }

private:
    SdCardManager &sd_card_;
    bool locked_ = false;
};

void fill_file_status(ArduinoJson::JsonObject target, SdCardManager &sd_card, const char *path) {
    target["path"] = path ? path : "";
    bool exists = false;
    size_t size = 0;
    const bool checked = path && sd_card.fileInfo(path, exists, size);
    target["exists"] = exists;
    target["size_bytes"] = exists ? size : 0;
    if (!checked) {
        target["error"] = "stat failed";
    }
}

const char *failure_kind_text(DailyStorageFailureKind kind) {
    switch (kind) {
        case DailyStorageFailureKind::None: return "none";
        case DailyStorageFailureKind::Busy: return "busy";
        case DailyStorageFailureKind::Missing: return "missing";
        case DailyStorageFailureKind::Io: return "io";
        case DailyStorageFailureKind::NoSpace: return "no_space";
        case DailyStorageFailureKind::ReadOnly: return "read_only";
        case DailyStorageFailureKind::Invalid: return "invalid";
        case DailyStorageFailureKind::Unknown: return "unknown";
    }
    return "unknown";
}

void fill_state_file_status(ArduinoJson::JsonObject target, SdCardManager &sd_card) {
    target["path"] = "/aura/history/current_day.*.bin";
    const char *paths[] = {
        DailyExtremaHistory::kStatePathA,
        DailyExtremaHistory::kStatePathB,
        DailyExtremaHistory::kLegacyStatePath,
    };
    bool any_exists = false;
    size_t total_size = 0;
    for (const char *path : paths) {
        bool exists = false;
        size_t size = 0;
        if (!sd_card.fileInfo(path, exists, size)) {
            target["exists"] = any_exists;
            target["size_bytes"] = total_size;
            target["error"] = "stat failed";
            return;
        }
        if (exists) {
            any_exists = true;
            total_size += size;
        }
    }
    target["exists"] = any_exists;
    target["size_bytes"] = total_size;
}

void send_json(WebRequest &server, int status_code, ArduinoJson::JsonDocument &doc) {
    String json;
    serializeJson(doc, json);
    WebResponseUtils::sendNoStoreHeaders(server);
    server.send(status_code, "application/json", json);
}

void send_error(WebRequest &server, int status_code, const char *message) {
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = false;
    root["error"] = message ? message : "Unknown error";
    send_json(server, status_code, doc);
}

bool require_sd_ready(WebHandlerContext &context,
                      bool ota_busy,
                      const char *ota_busy_json,
                      WebRequest *&server) {
    server = context.server;
    if (!server) {
        return false;
    }
    if (ota_busy) {
        WebResponseUtils::sendNoStoreHeaders(*server);
        server->send(503, "application/json", ota_busy_json);
        return false;
    }
    if (!context.sd_card || !context.sd_card->isReady()) {
        send_error(*server, 503, "SD card is not mounted");
        return false;
    }
    return true;
}

bool remove_if_exists(SdCardManager &sd_card, const char *path, bool &existed) {
    existed = false;
    bool exists = false;
    size_t size = 0;
    if (!sd_card.fileInfo(path, exists, size)) {
        return false;
    }
    existed = exists;
    return sd_card.removeFile(path);
}

void add_remove_result(ArduinoJson::JsonArray results,
                       const char *path,
                       bool existed,
                       bool ok) {
    ArduinoJson::JsonObject item = results.add<ArduinoJson::JsonObject>();
    item["path"] = path ? path : "";
    item["existed"] = existed;
    item["removed"] = existed && ok;
    item["ok"] = ok;
}

// --- Long-term history (history.log_enabled on; docs/LONG_HISTORY.md) ---

constexpr const char *kLongDailyPath = LongHistoryFiles::kDailyRoot;

bool long_history_on(const WebHandlerContext &context) { return context.long_history != nullptr; }

bool long_units_c(const WebHandlerContext &context) {
    return context.daily_extrema ? context.daily_extrema->preferredUnitsC() : true;
}

// Fails the request when the log is on but the history could not start.
bool require_long_history_running(WebHandlerContext &context, WebRequest &server) {
    if (context.long_history->enabled()) return true;
    send_error(server, 503, "Long-term history is not running; see the device log");
    return false;
}

void fill_long_status(ArduinoJson::JsonObject root, WebHandlerContext &context) {
    LongHistory &history = *context.long_history;
    const bool units_c = long_units_c(context);
    ArduinoJson::JsonObject daily = root["daily"].to<ArduinoJson::JsonObject>();
    daily["csv_path"] = kLongDailyPath;
    uint32_t day_key = 0;
    uint32_t samples = 0;
    const bool has_day = history.currentDay(day_key, samples);
    daily["current_day"] = has_day ? day_key : 0;
    daily["current_sample_count"] = has_day ? samples : 0;
    daily["last_write_ok"] = history.lastWriteOk();
    // Stored in C and converted at download with the current setting.
    daily["current_day_units_c"] = units_c;
    daily["preferred_units_c"] = units_c;
    // The long-term writers have no retry queue: a failed write is reported
    // at once (last_write_ok, sd status), so these are truly zero.
    daily["pending_day_count"] = 0;
    daily["oldest_pending_day"] = 0;
    daily["dropped_pending_days"] = 0;
    daily["pending_retry_ms"] = 0;

    ArduinoJson::JsonObject long_history = root["long_history"].to<ArduinoJson::JsonObject>();
    char first_day[16] = {};
    if (history.firstDay(first_day, sizeof(first_day))) long_history["first_day"] = first_day;

    if (!context.sd_card || !context.sd_card->isReady()) return;
    ArduinoJson::JsonObject files = root["files"].to<ArduinoJson::JsonObject>();
    ArduinoJson::JsonObject csv = files["daily_csv"].to<ArduinoJson::JsonObject>();
    csv["path"] = kLongDailyPath;
    size_t bytes = 0;
    uint32_t last_end = 0;
    const LongHistory::CsvResult r = history.dailyCsv(units_c, UINT32_MAX, nullptr, nullptr, bytes, last_end);
    csv["exists"] = r == LongHistory::CsvResult::Ready;
    csv["size_bytes"] = r == LongHistory::CsvResult::Ready ? bytes : 0;
    if (r == LongHistory::CsvResult::Failed) csv["error"] = "read failed";
    uint32_t utc_day = 0;
    char today_path[LongHistoryFiles::kPathCapacity] = {};
    if (LocalDate::utcDayKey(time(nullptr), utc_day) && LongHistoryFiles::quarterFilePath(utc_day, today_path)) {
        fill_file_status(files["current_day_state"].to<ArduinoJson::JsonObject>(), *context.sd_card, today_path);
    }
}

struct StreamSink {
    WebRequest *server;
    int last_error = 0;
};
bool stream_text(const char *text, size_t len, void *context) {
    StreamSink &sink = *static_cast<StreamSink *>(context);
    size_t sent = 0;
    while (sent < len) {
        if (!sink.server->waitUntilWritable(250, sink.last_error)) return false;
        const int32_t written = sink.server->writeStreamChunk(reinterpret_cast<const uint8_t *>(text) + sent,
                                                              len - sent, sink.last_error);
        if (written <= 0) return false;
        sent += static_cast<size_t>(written);
    }
    return sink.server->clientConnected();
}

void handle_long_csv(WebHandlerContext &context, WebRequest &server) {
    if (!require_long_history_running(context, server)) return;
    const bool units_c = long_units_c(context);
    size_t bytes = 0;
    uint32_t last_end = 0;
    switch (context.long_history->dailyCsv(units_c, UINT32_MAX, nullptr, nullptr, bytes, last_end)) {
        case LongHistory::CsvResult::Failed:
            send_error(server, 500, "Failed to read long-term daily history");
            return;
        case LongHistory::CsvResult::NoRecords:
            send_error(server, 404, "Daily history CSV is not available yet");
            return;
        case LongHistory::CsvResult::Ready:
            break;
    }
    WebResponseUtils::sendNoStoreHeaders(server);
    server.sendHeader("Content-Disposition", "attachment; filename=\"aura-daily-history.csv\"");
    if (!server.beginStreamResponse(200, "text/csv; charset=utf-8", bytes)) return;
    StreamSink sink{&server};
    size_t sent = 0;
    uint32_t sent_end = 0;
    // Same limit, so the same text as the size pass (records are append-only).
    const LongHistory::CsvResult r =
        context.long_history->dailyCsv(units_c, last_end, &stream_text, &sink, sent, sent_end);
    if (r == LongHistory::CsvResult::Ready && sent == bytes) {
        server.endStreamResponse();
    } else {
        Logger::log(Logger::Warn, "Web", "long-term daily CSV stream interrupted sent=%u/%u err=%d",
                    static_cast<unsigned>(sent), static_cast<unsigned>(bytes), sink.last_error);
    }
}

void send_op_results(WebRequest &server, LongHistory::OpStatus status, const LongHistory::OpResults &results) {
    switch (status) {
        case LongHistory::OpStatus::Busy:
            send_error(server, 503, "Long-term history is busy; try again");
            return;
        case LongHistory::OpStatus::Off:
            send_error(server, 503, "Long-term history is not running; see the device log");
            return;
        case LongHistory::OpStatus::NoTime:
            send_error(server, 409, "Device time is not set; today is unknown");
            return;
        case LongHistory::OpStatus::Done:
            break;
    }
    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    const bool ok = results.ok();
    root["success"] = ok;
    ArduinoJson::JsonArray files = root["files"].to<ArduinoJson::JsonArray>();
    for (const LongHistory::OpResult &r : results.items) {
        ArduinoJson::JsonObject item = files.add<ArduinoJson::JsonObject>();
        item["path"] = r.path.c_str();
        item["existed"] = r.existed;
        item["removed"] = r.removed;
        item["ok"] = r.ok;
    }
    send_json(server, ok ? 200 : 500, doc);
}

} // namespace

namespace WebDailyHistoryHandlers {

void handleStatus(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    if (!context.server) {
        return;
    }
    if (ota_busy) {
        WebResponseUtils::sendNoStoreHeaders(*context.server);
        context.server->send(503, "application/json", ota_busy_json);
        return;
    }

    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = true;

    ArduinoJson::JsonObject sd = root["sd"].to<ArduinoJson::JsonObject>();
    if (context.sd_card) {
        const SdCardManager::Status status = context.sd_card->status();
        sd["state"] = SdCardPolicy::stateText(status.state);
        sd["optional"] = true;
        sd["mounted"] = status.mounted;
        sd["attempted"] = status.attempted;
        sd["ready"] = status.ready;
        sd["can_reconnect"] = status.can_reconnect;
        sd["recovery_pending"] = status.recovery_pending;
        sd["recovering"] = status.recovering;
        sd["recovery_paused"] = status.recovery_paused;
        sd["recovery_blocked"] = status.recovery_blocked;
        sd["recovery_retry_ms"] = status.recovery_retry_ms;
        sd["recovery_attempts"] = status.recovery_attempts;
        sd["recovery_successes"] = status.recovery_successes;
        sd["write_verification_pending"] = status.write_verification_pending;
        sd["has_successful_write"] = status.has_successful_write;
        sd["last_write_age_ms"] = status.last_write_age_ms;
        ArduinoJson::JsonObject first = sd["first_error"].to<ArduinoJson::JsonObject>();
        first["operation"] = status.first_error_operation;
        first["stage"] = status.first_error_stage;
        first["code"] = status.first_error_code;
        first["domain"] = status.first_error_domain;
        first["uptime_ms"] = status.first_error_ms;
        sd["mount_point"] = status.mount_point;
        sd["card_size_bytes"] = status.card_size_bytes;
        sd["filesystem_total_bytes"] = status.filesystem_total_bytes;
        sd["filesystem_free_bytes"] = status.filesystem_free_bytes;
        sd["last_error"] = status.last_error;
        sd["last_error_code"] = status.last_error_code;
        sd["runtime_healthy"] = status.runtime_healthy;
        sd["write_fault"] = status.write_fault;
        sd["runtime_error_kind"] = failure_kind_text(status.runtime_failure_kind);
        sd["runtime_error_code"] = status.runtime_error_code;
        sd["last_operation"] = status.last_operation;
        sd["last_stage"] = status.last_stage;
        sd["runtime_error"] = status.runtime_error;
        sd["last_failure_ms"] = status.last_failure_ms;
        sd["last_write_success_ms"] = status.last_write_success_ms;
        sd["consecutive_failures"] = status.consecutive_failures;
        sd["total_failures"] = status.total_failures;
        sd["busy_count"] = status.busy_count;
    } else {
        sd["state"] = "manager_unavailable";
        sd["optional"] = true;
        sd["mounted"] = false;
        sd["attempted"] = false;
        sd["last_error"] = "SD manager unavailable";
        sd["last_error_code"] = 0;
    }

    if (long_history_on(context)) {
        fill_long_status(root, context);
        send_json(*context.server, 200, doc);
        return;
    }

    ArduinoJson::JsonObject daily = root["daily"].to<ArduinoJson::JsonObject>();
    daily["csv_path"] = DailyExtremaHistory::kDailyCsvPath;
    if (context.daily_extrema) {
        daily["current_day"] = context.daily_extrema->currentDayKey();
        daily["current_sample_count"] = context.daily_extrema->currentSampleCount();
        daily["last_write_ok"] = context.daily_extrema->lastWriteOk();
        daily["current_day_units_c"] = context.daily_extrema->currentDayUnitsC();
        daily["preferred_units_c"] = context.daily_extrema->preferredUnitsC();
        daily["pending_day_count"] = context.daily_extrema->pendingDayCount();
        daily["oldest_pending_day"] = context.daily_extrema->oldestPendingDayKey();
        daily["dropped_pending_days"] = context.daily_extrema->droppedPendingDayCount();
        daily["pending_retry_ms"] = context.daily_extrema->pendingRetryRemainingMs(millis());
    } else {
        daily["current_day"] = 0;
        daily["current_sample_count"] = 0;
        daily["last_write_ok"] = false;
        daily["current_day_units_c"] = true;
        daily["preferred_units_c"] = true;
        daily["pending_day_count"] = 0;
        daily["oldest_pending_day"] = 0;
        daily["dropped_pending_days"] = 0;
        daily["pending_retry_ms"] = 0;
    }

    ArduinoJson::JsonObject files = root["files"].to<ArduinoJson::JsonObject>();
    if (context.sd_card && context.sd_card->isReady()) {
        fill_state_file_status(files["current_day_state"].to<ArduinoJson::JsonObject>(),
                               *context.sd_card);
        fill_file_status(files["daily_csv"].to<ArduinoJson::JsonObject>(),
                         *context.sd_card,
                         DailyExtremaHistory::kDailyCsvPath);
    }

    send_json(*context.server, 200, doc);
}

void handleReconnect(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    if (!context.server) return;
    if (ota_busy) {
        WebResponseUtils::sendNoStoreHeaders(*context.server);
        context.server->send(503, "application/json", ota_busy_json);
        return;
    }
    if (!context.sd_card || !context.sd_card->requestRecovery()) {
        send_error(*context.server, 409, "SD reconnect unavailable; check SD status");
        return;
    }
    ArduinoJson::JsonDocument doc;
    doc["success"] = true;
    doc["queued"] = true;
    send_json(*context.server, 202, doc);
}

void handleCsv(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    if (!context.server) {
        return;
    }
    WebRequest &server = *context.server;
    if (ota_busy) {
        WebResponseUtils::sendNoStoreHeaders(server);
        server.send(503, "application/json", ota_busy_json);
        return;
    }
    if (!context.sd_card || !context.sd_card->isReady()) {
        send_error(server, 503, "SD card is not mounted");
        return;
    }
    if (long_history_on(context)) {
        handle_long_csv(context, server);
        return;
    }

    ScopedSdFileLock file_lock(*context.sd_card);
    if (!file_lock.locked()) {
        send_error(server, 503, "SD card is busy");
        return;
    }

    bool exists = false;
    size_t file_size = 0;
    if (!context.sd_card->fileInfoLocked(DailyExtremaHistory::kDailyCsvPath, exists, file_size)) {
        send_error(server, 500, "Failed to inspect daily history CSV");
        return;
    }
    if (!exists) {
        send_error(server, 404, "Daily history CSV is not available yet");
        return;
    }

    FILE *file = context.sd_card->openReadLocked(DailyExtremaHistory::kDailyCsvPath);
    if (!file) {
        send_error(server, 500, "Failed to open daily history CSV");
        return;
    }

    WebResponseUtils::sendNoStoreHeaders(server);
    server.sendHeader("Content-Disposition", "attachment; filename=\"aura-daily-history.csv\"");
    if (!server.beginStreamResponse(200, "text/csv; charset=utf-8", file_size)) {
        fclose(file);
        return;
    }

    uint8_t buffer[kCsvStreamBufferSize];
    size_t sent = 0;
    int last_error = 0;
    bool ok = true;
    bool sd_read_failed = false;
    while (sent < file_size) {
        const size_t remaining = file_size - sent;
        const size_t to_read = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        errno = 0;
        const size_t read = fread(buffer, 1, to_read, file);
        if (read == 0) {
            if (ferror(file)) {
                context.sd_card->noteStreamReadFailure("read", errno);
                sd_read_failed = true;
            }
            ok = false;
            break;
        }
        size_t chunk_sent = 0;
        while (chunk_sent < read) {
            if (!server.waitUntilWritable(250, last_error)) {
                ok = false;
                break;
            }
            const int32_t written =
                server.writeStreamChunk(buffer + chunk_sent, read - chunk_sent, last_error);
            if (written <= 0) {
                ok = false;
                break;
            }
            chunk_sent += static_cast<size_t>(written);
            sent += static_cast<size_t>(written);
        }
        if (!ok || !server.clientConnected()) {
            ok = false;
            break;
        }
    }
    errno = 0;
    if (fclose(file) != 0) {
        context.sd_card->noteStreamReadFailure("close", errno);
        sd_read_failed = true;
        ok = false;
    } else if (!sd_read_failed) {
        context.sd_card->noteStreamReadSuccess();
    }

    if (ok) {
        server.endStreamResponse();
    } else {
        Logger::log(Logger::Warn,
                    "Web",
                    "daily history CSV stream interrupted sent=%u/%u err=%d",
                    static_cast<unsigned>(sent),
                    static_cast<unsigned>(file_size),
                    last_error);
    }
}

void handleCurrentDayCsv(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    if (!context.server) {
        return;
    }
    WebRequest &server = *context.server;
    if (ota_busy) {
        WebResponseUtils::sendNoStoreHeaders(server);
        server.send(503, "application/json", ota_busy_json);
        return;
    }
    String csv;
    if (long_history_on(context)) {
        if (!require_long_history_running(context, server)) return;
        if (!context.long_history->currentDayCsv(long_units_c(context), csv)) {
            send_error(server, 404, "Current day history is not available yet");
            return;
        }
    } else if (!context.daily_extrema) {
        send_error(server, 503, "Daily history is not available");
        return;
    } else if (!context.daily_extrema->currentDayCsv(csv, true)) {
        send_error(server, 404, "Current day history is not available yet");
        return;
    }

    WebResponseUtils::sendNoStoreHeaders(server);
    server.sendHeader("Content-Disposition", "attachment; filename=\"aura-current-day-history.csv\"");
    server.send(200, "text/csv; charset=utf-8", csv);
}

void handleClearHistory(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    WebRequest *server = nullptr;
    if (!require_sd_ready(context, ota_busy, ota_busy_json, server)) {
        return;
    }
    if (long_history_on(context)) {
        LongHistory::OpResults results;
        send_op_results(*server, context.long_history->clearAll(results), results);
        return;
    }

    DailyExtremaHistory::ClearCurrentDayResult clear_result{};
    if (context.daily_extrema) {
        clear_result = context.daily_extrema->clearCurrentDay(true, true);
    }

    bool daily_existed = false;
    const bool daily_ok =
        remove_if_exists(*context.sd_card, DailyExtremaHistory::kDailyCsvPath, daily_existed);
    const bool state_ok = clear_result.ok;
    const bool state_existed = clear_result.state_existed;

    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    const bool ok = daily_ok && state_ok;
    root["success"] = ok;
    ArduinoJson::JsonArray files = root["files"].to<ArduinoJson::JsonArray>();
    add_remove_result(files, DailyExtremaHistory::kDailyCsvPath, daily_existed, daily_ok);
    add_remove_result(files, "/aura/history/current_day.*.bin", state_existed, state_ok);
    send_json(*server, ok ? 200 : 500, doc);
}

void handleClearCurrentDay(WebHandlerContext &context, bool ota_busy, const char *ota_busy_json) {
    WebRequest *server = nullptr;
    if (!require_sd_ready(context, ota_busy, ota_busy_json, server)) {
        return;
    }
    if (long_history_on(context)) {
        LongHistory::OpResults results;
        send_op_results(*server, context.long_history->clearToday(time(nullptr), results), results);
        return;
    }

    DailyExtremaHistory::ClearCurrentDayResult clear_result{};
    if (context.daily_extrema) {
        clear_result = context.daily_extrema->clearCurrentDay(true);
    }
    const bool state_ok = clear_result.ok;
    const bool state_existed = clear_result.state_existed;

    ArduinoJson::JsonDocument doc;
    ArduinoJson::JsonObject root = doc.to<ArduinoJson::JsonObject>();
    root["success"] = state_ok;
    ArduinoJson::JsonArray files = root["files"].to<ArduinoJson::JsonArray>();
    add_remove_result(files, "/aura/history/current_day.*.bin", state_existed, state_ok);
    send_json(*server, state_ok ? 200 : 500, doc);
}

} // namespace WebDailyHistoryHandlers
