// SPDX-FileCopyrightText: 2026 Daniel Abramowitz
// SPDX-License-Identifier: GPL-3.0-or-later
#include <unity.h>
#include <math.h>
#include "ui/UiOutdoorCard.h"

using UiOutdoorCard::Message;
using S = WeatherApi::Status;

void setUp() {}
void tearDown() {}

void test_off_hides_the_card() {
    const auto s = UiOutdoorCard::resolve(S::Off);
    TEST_ASSERT_FALSE(s.visible);
    TEST_ASSERT_FALSE(s.show_value);
}

void test_good_check_shows_the_value() {
    const auto s = UiOutdoorCard::resolve(S::Ok);
    TEST_ASSERT_TRUE(s.visible && s.show_value && !s.waiting);
    TEST_ASSERT_EQUAL(static_cast<int>(Message::None), static_cast<int>(s.message));
}

void test_waiting_shows_dashes_not_a_message() {
    const auto s = UiOutdoorCard::resolve(S::Waiting);
    TEST_ASSERT_TRUE(s.visible && s.show_value && s.waiting);
    TEST_ASSERT_EQUAL(static_cast<int>(Message::None), static_cast<int>(s.message));
}

void test_every_failure_shows_its_reason_and_no_value() {
    const struct { S status; Message message; } cases[] = {
        {S::NotSetUp, Message::SetUp},      {S::NoNetwork, Message::NoInternet},
        {S::KeyRejected, Message::BadKey},  {S::ServiceError, Message::Service},
        {S::BadJson, Message::Service},     {S::MissingField, Message::Service},
    };
    for (const auto &c : cases) {
        const auto s = UiOutdoorCard::resolve(c.status);
        TEST_ASSERT_TRUE(s.visible);
        TEST_ASSERT_FALSE(s.show_value);
        TEST_ASSERT_EQUAL(static_cast<int>(c.message), static_cast<int>(s.message));
    }
}

void test_value_uses_the_indoor_unit() {
    char buf[12];
    TEST_ASSERT_TRUE(UiOutdoorCard::formatValue(-12.5f, true, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("-12.5", buf);
    TEST_ASSERT_TRUE(UiOutdoorCard::formatValue(-12.5f, false, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("9.5", buf);
    TEST_ASSERT_FALSE(UiOutdoorCard::formatValue(NAN, true, buf, sizeof(buf)));
    TEST_ASSERT_FALSE(UiOutdoorCard::formatValue(1.0f, true, buf, 3));
}

void test_long_values_use_the_smaller_font() {
    TEST_ASSERT_TRUE(UiOutdoorCard::useLargeValueFont("21.6"));
    TEST_ASSERT_TRUE(UiOutdoorCard::useLargeValueFont("-3.5"));
    TEST_ASSERT_TRUE(UiOutdoorCard::useLargeValueFont("---"));
    TEST_ASSERT_FALSE(UiOutdoorCard::useLargeValueFont("-12.5"));
    TEST_ASSERT_FALSE(UiOutdoorCard::useLargeValueFont("100.4"));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_off_hides_the_card);
    RUN_TEST(test_good_check_shows_the_value);
    RUN_TEST(test_waiting_shows_dashes_not_a_message);
    RUN_TEST(test_every_failure_shows_its_reason_and_no_value);
    RUN_TEST(test_value_uses_the_indoor_unit);
    RUN_TEST(test_long_values_use_the_smaller_font);
    return UNITY_END();
}
