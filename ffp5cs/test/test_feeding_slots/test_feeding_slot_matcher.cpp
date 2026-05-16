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

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_same_hour_marks_all_matching_slots);
    RUN_TEST(test_catch_up_window_marks_all_matching_slots);
    RUN_TEST(test_no_slot_outside_current_window);
    return UNITY_END();
}
