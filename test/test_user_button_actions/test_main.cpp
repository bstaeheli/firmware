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
    cfg.hold_alert_max_ms = 5000;
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
    TEST_ASSERT_EQUAL_UINT32(5000, c.hold_alert_max_ms);
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
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_HOLD, 2000, 4999));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_HOLD, 3000, 4999));
}

static void test_medium_hold_plays_hint()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_HINT, resolve(USER_BUTTON_HOLD, 1000, 5000));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_HINT, resolve(USER_BUTTON_HOLD, 2000, SHUTDOWN_MS - 1));
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

static void test_press_during_hint_only_stops_it()
{
    state.hintPlaying = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOP_HINT, resolve(USER_BUTTON_SINGLE, 1000));
    state.nagging = true;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOP_HINT, resolve(USER_BUTTON_SINGLE, 1100));
    state.hintPlaying = false;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, 1200));
}

static void test_lead_up_ends_at_shutdown_time()
{
    TEST_ASSERT_EQUAL_UINT32(2200, userButtonActionsLeadUpMs(0, 2200)); // no override: stock
    TEST_ASSERT_EQUAL_UINT32(8700, userButtonActionsLeadUpMs(10000, 2200));
    TEST_ASSERT_EQUAL_UINT32(2200, userButtonActionsLeadUpMs(1300, 2200)); // too short to shift
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
    RUN_TEST(test_press_during_hint_only_stops_it);
    RUN_TEST(test_lead_up_ends_at_shutdown_time);
    exit(UNITY_END());
}

void loop() {}
