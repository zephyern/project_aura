// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string.h>
#include <unity.h>

#include "ui/StorageStatusMessage.h"
#include "ui/UiStrings.h"

using StorageHealth::Kind;

namespace {

StorageHealth::Status status(Kind kind, uint32_t days_left = 0) {
    StorageHealth::Status s;
    s.kind = kind;
    s.days_left = days_left;
    return s;
}

void assertText(Kind kind, const char *expected, uint32_t days_left = 0) {
    const StorageStatusMessage::Message m = StorageStatusMessage::build(status(kind, days_left));
    TEST_ASSERT_TRUE(m.shown);
    TEST_ASSERT_EQUAL(static_cast<int>(kind), static_cast<int>(m.kind));
    TEST_ASSERT_EQUAL_STRING(expected, m.text);
}

} // namespace

void setUp() { UiStrings::setLanguage(Config::Language::EN); }
void tearDown() {}

void test_off_and_ok_show_nothing() {
    TEST_ASSERT_FALSE(StorageStatusMessage::build(status(Kind::Off)).shown);
    TEST_ASSERT_FALSE(StorageStatusMessage::build(status(Kind::Ok)).shown);
}

void test_english_texts_are_the_decided_wording() {
    assertText(Kind::NoCard, "No SD card \xE2\x80\x93 history off");
    assertText(Kind::LowSpace, "SD card: about 12 days left", 12);
    assertText(Kind::Full, "SD card full \xE2\x80\x93 deleting oldest detail");
    assertText(Kind::RecordingStopped, "SD card full \xE2\x80\x93 recording stopped");
    assertText(Kind::ClockBehind, "Clock behind \xE2\x80\x93 recording paused");
    assertText(Kind::WriteError, "SD card error \xE2\x80\x93 history not saved");
}

void test_every_language_has_every_text_and_fits() {
    const Kind kinds[] = {Kind::NoCard, Kind::LowSpace, Kind::Full, Kind::RecordingStopped,
                          Kind::ClockBehind, Kind::WriteError};
    for (int lang = 0; lang < static_cast<int>(Config::Language::COUNT); ++lang) {
        UiStrings::setLanguage(static_cast<Config::Language>(lang));
        for (Kind kind : kinds) {
            // The largest count a 32-bit days_left can print.
            const StorageStatusMessage::Message m = StorageStatusMessage::build(status(kind, UINT32_MAX));
            TEST_ASSERT_TRUE(m.shown);
            const size_t len = strlen(m.text);
            TEST_ASSERT_TRUE(len > 0);
            TEST_ASSERT_TRUE_MESSAGE(len < StorageStatusMessage::kTextCapacity - 1, m.text);
        }
        const StorageStatusMessage::Message low = StorageStatusMessage::build(status(Kind::LowSpace, 7));
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(low.text, "7"), low.text);
        TEST_ASSERT_NULL_MESSAGE(strchr(low.text, '%'), low.text);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_off_and_ok_show_nothing);
    RUN_TEST(test_english_texts_are_the_decided_wording);
    RUN_TEST(test_every_language_has_every_text_and_fits);
    return UNITY_END();
}
