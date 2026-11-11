// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/StorageStatusMessage.h"

#include <stdio.h>

#include "ui/UiStrings.h"

namespace StorageStatusMessage {

namespace {

using UiStrings::TextId;

void copyText(Message &out, TextId id) {
    // Every translation fits the buffer (test_storage_status_message checks all).
    snprintf(out.text, sizeof(out.text), "%s", UiStrings::text(id));
}

} // namespace

Message build(const StorageHealth::Status &status) {
    Message out;
    out.kind = status.kind;
    switch (status.kind) {
        case StorageHealth::Kind::Off:
        case StorageHealth::Kind::Ok:
            return out;
        case StorageHealth::Kind::NoCard:
            copyText(out, TextId::MsgStorageNoCard);
            break;
        case StorageHealth::Kind::LowSpace:
            snprintf(out.text, sizeof(out.text), UiStrings::text(TextId::MsgStorageDaysLeftFormat),
                     static_cast<unsigned>(status.days_left));
            break;
        case StorageHealth::Kind::Full:
            copyText(out, TextId::MsgStorageFullDeleting);
            break;
        case StorageHealth::Kind::RecordingStopped:
            copyText(out, TextId::MsgStorageFullStopped);
            break;
        case StorageHealth::Kind::ClockBehind:
            copyText(out, TextId::MsgStorageClockBehind);
            break;
        case StorageHealth::Kind::WriteError:
            copyText(out, TextId::MsgStorageWriteError);
            break;
    }
    out.shown = true;
    return out;
}

} // namespace StorageStatusMessage
