#include "Arduino.h"
#include "TestUtil.h"
#include "input/UserButtonActions.h"
#include <unity.h>

static UserButtonActionsConfig cfg;
static UserButtonState state;

static const uint32_t SHUTDOWN_MS = 10000;

void setUp(void)
{
    cfg = {};
    cfg.enabled = true;
    cfg.single_press = BUTTON_ACTION_ACK_OR_ALERT;
    cfg.double_press = BUTTON_ACTION_DEFAULT;
    cfg.alert_min_interval_secs = 30;
    cfg.hold_alert_max_ms = 3000;
    cfg.long_press_shutdown_ms = SHUTDOWN_MS;
    state = UserButtonState();
}

void tearDown(void) {}

// Resolve a gesture at nowMs; a resolved alert counts as sent
static UserButtonOutcome resolve(UserButtonGesture gesture, uint32_t nowMs, uint32_t holdMs = 0)
{
    state.nowMs = nowMs;
    UserButtonOutcome outcome = userButtonActionsResolve(cfg, state, gesture, holdMs, SHUTDOWN_MS);
    if (outcome == UBA_OUTCOME_ALERT || outcome == UBA_OUTCOME_ALERT2)
        state.limiters[outcome == UBA_OUTCOME_ALERT2].alertSent(nowMs);
    return outcome;
}

static void test_build_flag_defaults_are_stock()
{
    const UserButtonActionsConfig &c = userButtonActionsConfig();
    TEST_ASSERT_FALSE(c.enabled); // test env does not set MESHTASTIC_USER_BUTTON_ACTIONS
    TEST_ASSERT_EQUAL(BUTTON_ACTION_DEFAULT, c.single_press);
    TEST_ASSERT_EQUAL(BUTTON_ACTION_DEFAULT, c.double_press);
    TEST_ASSERT_EQUAL(BUTTON_ACTION_DEFAULT, c.multi_press);
    TEST_ASSERT_EQUAL_STRING("\x07HELP", c.alert_text);
    TEST_ASSERT_EQUAL_STRING("\x07SOS", c.alert2_text);
    TEST_ASSERT_EQUAL_STRING("\x07OK", c.ack_text);
    TEST_ASSERT_TRUE(c.alert_with_position);
    TEST_ASSERT_EQUAL_UINT32(30, c.alert_min_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(3000, c.hold_alert_max_ms);
    TEST_ASSERT_EQUAL_UINT32(0, c.long_press_shutdown_ms);
}

static void test_disabled_keeps_stock()
{
    cfg.enabled = false;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_SINGLE, 1000));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_SINGLE, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 1000, 6000));
}

static void test_default_action_keeps_stock()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_DOUBLE, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_MULTI, 1000));
}

static void test_none_swallows_gesture()
{
    cfg.double_press = BUTTON_ACTION_NONE;
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_IGNORE, resolve(USER_BUTTON_DOUBLE, 1000));
}

static void test_adhoc_position()
{
    cfg.double_press = BUTTON_ACTION_ADHOC_POSITION;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_POSITION, resolve(USER_BUTTON_DOUBLE, 1000));
}

static void test_ack_or_alert_alerts_when_idle()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000));
}

static void test_ack_or_alert_acks_while_nagging()
{
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, 1000));
    // acks are never rate-limited and do not consume the alert window
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, 1100));
    state.nagging = false;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1200));
}

static void test_ack_inside_alert_window()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, 2000));
}

static void test_send_alert_ignores_nag()
{
    cfg.single_press = BUTTON_ACTION_SEND_ALERT;
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000));
}

static void test_level2_alert_and_ack()
{
    cfg.double_press = BUTTON_ACTION_ACK_OR_ALERT2;
    cfg.multi_press = BUTTON_ACTION_SEND_ALERT2;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT2, resolve(USER_BUTTON_DOUBLE, 1000));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_DOUBLE, 1100));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_MULTI, 1200));
}

static void test_alert_levels_have_separate_windows()
{
    cfg.double_press = BUTTON_ACTION_ACK_OR_ALERT2;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT2, resolve(USER_BUTTON_DOUBLE, 2000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, 3000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_DOUBLE, 4000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 31000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT2, resolve(USER_BUTTON_DOUBLE, 32000));
}

static void test_alert_rate_limited_within_interval()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, 1000 + 29999));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000 + 30000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, 1000 + 30001));
}

static void test_resolve_does_not_start_window()
{
    state.nowMs = 1000;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, userButtonActionsResolve(cfg, state, USER_BUTTON_SINGLE, 0, SHUTDOWN_MS));
    // the window starts only with alertSent(), after the alert was sent
    state.nowMs = 1100;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, userButtonActionsResolve(cfg, state, USER_BUTTON_SINGLE, 0, SHUTDOWN_MS));
}

static void test_first_alert_allowed_right_after_boot()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 0));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, 10));
}

static void test_rate_limit_survives_millis_wraparound()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, UINT32_MAX - 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 30000));
}

static void test_zero_interval_never_limits()
{
    cfg.alert_min_interval_secs = 0;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1000));
}

static void test_short_hold_acts_as_single_press()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_HOLD, 1000, 500));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_HOLD, 2000, 2999));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_HOLD, 3000, 2999));
}

static void test_release_at_2900_ms_alerts()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_HOLD, 1000, 2900));
}

static void test_release_at_3100_ms_plays_hint()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_HINT, resolve(USER_BUTTON_HOLD, 1000, 3100));
}

static void test_medium_hold_plays_hint()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_HINT, resolve(USER_BUTTON_HOLD, 1000, 3000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_HINT, resolve(USER_BUTTON_HOLD, 2000, SHUTDOWN_MS - 1));
    state.nagging = true; // no hint tune over a running notification
    TEST_ASSERT_EQUAL(UBA_OUTCOME_IGNORE, resolve(USER_BUTTON_HOLD, 3000, 5000));
}

static void test_hold_until_shutdown_keeps_stock()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 1000, SHUTDOWN_MS));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 2000, 60000));
}

static void test_hold_with_stock_shutdown_time()
{
    // stock shutdown (3900 ms) comes before hold_alert_max_ms: no hint, holds beyond it shut down
    cfg.hold_alert_max_ms = 5000;
    state.nowMs = 1000;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, userButtonActionsResolve(cfg, state, USER_BUTTON_HOLD, 3000, 3900));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, userButtonActionsResolve(cfg, state, USER_BUTTON_HOLD, 4000, 3900));
}

static void test_hold_alert_disabled()
{
    cfg.hold_alert_max_ms = 0;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 1000, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 1000, 6000));
}

static void test_hold_with_default_single_press_keeps_stock()
{
    cfg.single_press = BUTTON_ACTION_DEFAULT;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 1000, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 1000, 6000));
}

// A press starting while a melody plays only stops it; the rest of that press is consumed
static void press_during_melody_is_consumed(UserButtonOutcome onDown, UserButtonGesture gesture, uint32_t holdMs)
{
    TEST_ASSERT_EQUAL(onDown, resolve(USER_BUTTON_DOWN, 1000));
    state.pressConsumed = true; // as the handler does after a stop/ack on button-down
    state.hintPlaying = false;
    state.nagging = false;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_IGNORE, resolve(gesture, 1500, holdMs));
}

static void test_click_during_hint_only_stops_it()
{
    state.hintPlaying = true;
    press_during_melody_is_consumed(UBA_OUTCOME_STOP_HINT, USER_BUTTON_SINGLE, 0);
}

static void test_double_during_hint_only_stops_it()
{
    state.hintPlaying = true;
    press_during_melody_is_consumed(UBA_OUTCOME_STOP_HINT, USER_BUTTON_DOUBLE, 0);
}

static void test_hold_during_hint_does_not_shut_down()
{
    state.hintPlaying = true;
    press_during_melody_is_consumed(UBA_OUTCOME_STOP_HINT, USER_BUTTON_HOLD, SHUTDOWN_MS + 5000);
}

static void test_press_during_nag_acks_on_button_down()
{
    state.nagging = true;
    press_during_melody_is_consumed(UBA_OUTCOME_ACK, USER_BUTTON_DOUBLE, 0);
}

static void test_hold_during_nag_does_not_shut_down()
{
    state.nagging = true;
    press_during_melody_is_consumed(UBA_OUTCOME_ACK, USER_BUTTON_HOLD, SHUTDOWN_MS + 5000);
}

static void test_hint_beats_nag_on_button_down()
{
    state.hintPlaying = true;
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOP_HINT, resolve(USER_BUTTON_DOWN, 1000));
}

static void test_button_down_without_melody_does_nothing()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_DOWN, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1200));
}

static void test_button_down_acks_only_with_ack_action()
{
    cfg.single_press = BUTTON_ACTION_SEND_ALERT;
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_DOWN, 1000));
    cfg.multi_press = BUTTON_ACTION_ACK_OR_ALERT2;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_DOWN, 1000));
}

static void test_consumed_press_ends_with_its_gesture()
{
    state.pressConsumed = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_IGNORE, resolve(USER_BUTTON_SINGLE, 1000));
}

static void test_consumed_press_without_gesture_does_not_swallow_next_click()
{
    // a press stopped a melody, but no gesture was ever reported for it; last seen pressed at 1000 ms
    state.pressConsumed = true;
    state.lastPressedMs = 1000;
    state.nowMs = 1500; // next button-down within the click series: still the same press
    userButtonActionsPressStart(state);
    TEST_ASSERT_TRUE(state.pressConsumed);
    state.nowMs = 2100; // next button-down later: a new press
    userButtonActionsPressStart(state);
    TEST_ASSERT_FALSE(state.pressConsumed);
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 2400));
}

// Button-down as the handler does it: snapshot the state for this press, then resolve
static UserButtonOutcome buttonDown(uint32_t nowMs)
{
    state.nowMs = nowMs;
    userButtonActionsPressStart(state);
    return resolve(USER_BUTTON_DOWN, nowMs);
}

static void test_text_arriving_during_press_does_not_turn_help_into_ok()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, buttonDown(1000));
    state.nagging = true; // a bell message arrives before the click is reported
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1300));
}

static void test_nag_starting_during_short_hold_still_alerts()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, buttonDown(1000));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_HOLD, 3000, 2000));
}

static void test_no_hint_over_a_nag_starting_during_hold()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, buttonDown(1000));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_IGNORE, resolve(USER_BUTTON_HOLD, 6000, 5000));
}

static void test_hint_ending_during_press_does_not_swallow_it()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, buttonDown(1000));
    state.hintPlaying = true; // stale or late hint state at gesture time
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, 1300));
}

static void test_gesture_without_button_down_uses_current_nag()
{
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, 1000));
}

static void test_hint_tune_ends_right_after_last_note()
{
    TEST_ASSERT_EQUAL(1, userButtonActionsNextHintNote(0));
    TEST_ASSERT_EQUAL(UBA_HINT_NOTES - 1, userButtonActionsNextHintNote(UBA_HINT_NOTES - 2));
    TEST_ASSERT_EQUAL(-1, userButtonActionsNextHintNote(UBA_HINT_NOTES - 1));
    TEST_ASSERT_EQUAL(-1, userButtonActionsNextHintNote(-1));
}

static void test_hold_reaching_shutdown_keeps_stock_shutdown()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_HOLD, 1000, 10000));
}

static void test_lead_up_counts_up_to_shutdown()
{
    // 8 notes of 200 ms from 3 s; the last one starts at 9.8 s and ends at the 10 s shutdown time
    TEST_ASSERT_EQUAL(-1, userButtonActionsLeadUpNote(2999, 3000, 10000));
    TEST_ASSERT_EQUAL(0, userButtonActionsLeadUpNote(3000, 3000, 10000));
    TEST_ASSERT_EQUAL(0, userButtonActionsLeadUpNote(3971, 3000, 10000));
    TEST_ASSERT_EQUAL(1, userButtonActionsLeadUpNote(3972, 3000, 10000));
    TEST_ASSERT_EQUAL(6, userButtonActionsLeadUpNote(9799, 3000, 10000));
    TEST_ASSERT_EQUAL(7, userButtonActionsLeadUpNote(9800, 3000, 10000));
    TEST_ASSERT_EQUAL(7, userButtonActionsLeadUpNote(60000, 3000, 10000));
}

static void test_lead_up_never_on_a_click()
{
    // hold_alert_max_ms = 0: start at the long press time, not at button-down
    TEST_ASSERT_EQUAL(-1, userButtonActionsLeadUpNote(499, 0, 10000));
    TEST_ASSERT_EQUAL(0, userButtonActionsLeadUpNote(500, 0, 10000));
}

static void test_lead_up_with_start_after_last_note()
{
    TEST_ASSERT_EQUAL(-1, userButtonActionsLeadUpNote(2000, 3000, 3100));
    TEST_ASSERT_EQUAL(7, userButtonActionsLeadUpNote(2900, 3000, 3100));
}

void setup()
{
    initializeTestEnvironment();

    UNITY_BEGIN();
    RUN_TEST(test_build_flag_defaults_are_stock);
    RUN_TEST(test_disabled_keeps_stock);
    RUN_TEST(test_default_action_keeps_stock);
    RUN_TEST(test_none_swallows_gesture);
    RUN_TEST(test_adhoc_position);
    RUN_TEST(test_ack_or_alert_alerts_when_idle);
    RUN_TEST(test_ack_or_alert_acks_while_nagging);
    RUN_TEST(test_ack_inside_alert_window);
    RUN_TEST(test_send_alert_ignores_nag);
    RUN_TEST(test_level2_alert_and_ack);
    RUN_TEST(test_alert_levels_have_separate_windows);
    RUN_TEST(test_alert_rate_limited_within_interval);
    RUN_TEST(test_resolve_does_not_start_window);
    RUN_TEST(test_first_alert_allowed_right_after_boot);
    RUN_TEST(test_rate_limit_survives_millis_wraparound);
    RUN_TEST(test_zero_interval_never_limits);
    RUN_TEST(test_short_hold_acts_as_single_press);
    RUN_TEST(test_medium_hold_plays_hint);
    RUN_TEST(test_hold_until_shutdown_keeps_stock);
    RUN_TEST(test_hold_with_stock_shutdown_time);
    RUN_TEST(test_hold_alert_disabled);
    RUN_TEST(test_hold_with_default_single_press_keeps_stock);
    RUN_TEST(test_release_at_2900_ms_alerts);
    RUN_TEST(test_release_at_3100_ms_plays_hint);
    RUN_TEST(test_click_during_hint_only_stops_it);
    RUN_TEST(test_double_during_hint_only_stops_it);
    RUN_TEST(test_hold_during_hint_does_not_shut_down);
    RUN_TEST(test_press_during_nag_acks_on_button_down);
    RUN_TEST(test_hold_during_nag_does_not_shut_down);
    RUN_TEST(test_hint_beats_nag_on_button_down);
    RUN_TEST(test_button_down_without_melody_does_nothing);
    RUN_TEST(test_button_down_acks_only_with_ack_action);
    RUN_TEST(test_consumed_press_ends_with_its_gesture);
    RUN_TEST(test_consumed_press_without_gesture_does_not_swallow_next_click);
    RUN_TEST(test_text_arriving_during_press_does_not_turn_help_into_ok);
    RUN_TEST(test_nag_starting_during_short_hold_still_alerts);
    RUN_TEST(test_no_hint_over_a_nag_starting_during_hold);
    RUN_TEST(test_hint_ending_during_press_does_not_swallow_it);
    RUN_TEST(test_gesture_without_button_down_uses_current_nag);
    RUN_TEST(test_hint_tune_ends_right_after_last_note);
    RUN_TEST(test_hold_reaching_shutdown_keeps_stock_shutdown);
    RUN_TEST(test_lead_up_counts_up_to_shutdown);
    RUN_TEST(test_lead_up_never_on_a_click);
    RUN_TEST(test_lead_up_with_start_after_last_note);
    exit(UNITY_END());
}

void loop() {}
