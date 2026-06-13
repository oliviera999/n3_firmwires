#include <unity.h>
#include "automatism/feeding_slot_matcher.h"

void setUp(void) {
}

void tearDown(void) {
}

void test_same_hour_marks_all_matching_slots() {
    const FeedingSlotMatcher::SlotMatches matches =
        FeedingSlotMatcher::slotsForCurrentWindow(8, 8, 12, 8);

    TEST_ASSERT_TRUE(matches.morning);
    TEST_ASSERT_FALSE(matches.noon);
    TEST_ASSERT_TRUE(matches.evening);
    TEST_ASSERT_TRUE(matches.any());
}

void test_catch_up_window_marks_all_matching_slots() {
    const FeedingSlotMatcher::SlotMatches matches =
        FeedingSlotMatcher::slotsForCurrentWindow(9, 8, 9, 20);

    TEST_ASSERT_TRUE(matches.morning);
    TEST_ASSERT_TRUE(matches.noon);
    TEST_ASSERT_FALSE(matches.evening);
    TEST_ASSERT_TRUE(matches.any());
}

void test_no_slot_outside_current_window() {
    const FeedingSlotMatcher::SlotMatches matches =
        FeedingSlotMatcher::slotsForCurrentWindow(15, 8, 12, 20);

    TEST_ASSERT_FALSE(matches.morning);
    TEST_ASSERT_FALSE(matches.noon);
    TEST_ASSERT_FALSE(matches.evening);
    TEST_ASSERT_FALSE(matches.any());
}

void test_slot_missed_after_catch_up_window() {
    // Créneau 8 h : fenêtre 8 h, rattrapage 9 h -> manqué à partir de 10 h
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isSlotMissed(8, 8));
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isSlotMissed(9, 8));
    TEST_ASSERT_TRUE(FeedingSlotMatcher::isSlotMissed(10, 8));
    TEST_ASSERT_TRUE(FeedingSlotMatcher::isSlotMissed(23, 8));
}

void test_slot_not_missed_before_window() {
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isSlotMissed(7, 8));
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isSlotMissed(0, 8));
}

void test_evening_slot_never_missed_same_day() {
    // Créneau 23 h : rattrapage à 0 h le jour suivant, jamais "manqué" le même jour
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isSlotMissed(23, 23));
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isSlotMissed(0, 23));
}

void test_schedule_config_valid() {
    TEST_ASSERT_TRUE(FeedingSlotMatcher::isScheduleConfigValid(8, 12, 20));
    TEST_ASSERT_TRUE(FeedingSlotMatcher::isScheduleConfigValid(0, 1, 23));
}

void test_schedule_config_invalid_duplicates() {
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isScheduleConfigValid(8, 8, 20));
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isScheduleConfigValid(8, 12, 12));
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isScheduleConfigValid(8, 8, 8));
}

void test_schedule_config_invalid_order_or_range() {
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isScheduleConfigValid(12, 8, 20));
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isScheduleConfigValid(8, 12, 24));
    TEST_ASSERT_FALSE(FeedingSlotMatcher::isScheduleConfigValid(25, 26, 27));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_same_hour_marks_all_matching_slots);
    RUN_TEST(test_catch_up_window_marks_all_matching_slots);
    RUN_TEST(test_no_slot_outside_current_window);
    RUN_TEST(test_slot_missed_after_catch_up_window);
    RUN_TEST(test_slot_not_missed_before_window);
    RUN_TEST(test_evening_slot_never_missed_same_day);
    RUN_TEST(test_schedule_config_valid);
    RUN_TEST(test_schedule_config_invalid_duplicates);
    RUN_TEST(test_schedule_config_invalid_order_or_range);
    return UNITY_END();
}
