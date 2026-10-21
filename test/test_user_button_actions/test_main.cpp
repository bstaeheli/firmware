#include "Arduino.h"
#include "TestUtil.h"
#include "input/UserButtonActions.h"
#include <unity.h>

static UserButtonActionsConfig cfg;
static UserButtonRateLimiter limiter;

void setUp(void)
{
    cfg = {};
    cfg.enabled = true;
    cfg.single_press = BUTTON_ACTION_ACK_OR_ALERT;
    cfg.double_press = BUTTON_ACTION_DEFAULT;
    cfg.alert_min_interval_secs = 30;
    limiter = UserButtonRateLimiter();
}

void tearDown(void) {}

// Resolve a press; a resolved alert counts as successfully sent
static UserButtonOutcome resolve(UserButtonGesture gesture, bool nagging, uint32_t nowMs)
{
    UserButtonOutcome outcome = userButtonActionsResolve(cfg, gesture, nagging, nowMs, limiter);
    if (outcome == UBA_OUTCOME_ALERT)
        limiter.alertSent(nowMs);
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
    TEST_ASSERT_EQUAL_STRING("\x07OK", c.ack_text);
    TEST_ASSERT_TRUE(c.alert_with_position);
    TEST_ASSERT_EQUAL_UINT32(30, c.alert_min_interval_secs);
    TEST_ASSERT_EQUAL_UINT32(0, c.long_press_shutdown_ms);
}

static void test_disabled_keeps_stock()
{
    cfg.enabled = false;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_SINGLE, false, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_SINGLE, true, 1000));
}

static void test_default_action_keeps_stock()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_DOUBLE, false, 1000));
}

static void test_none_swallows_gesture()
{
    cfg.double_press = BUTTON_ACTION_NONE;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_IGNORE, resolve(USER_BUTTON_DOUBLE, true, 1000));
}

static void test_adhoc_position()
{
    cfg.double_press = BUTTON_ACTION_ADHOC_POSITION;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_POSITION, resolve(USER_BUTTON_DOUBLE, false, 1000));
}

static void test_ack_or_alert_acks_while_nagging()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, true, 1000));
    // acks are never rate-limited and do not consume the alert window
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, true, 1100));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 1200));
}

static void test_multi_press_uses_its_own_action()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_STOCK, resolve(USER_BUTTON_MULTI, false, 1000));
    cfg.multi_press = BUTTON_ACTION_ACK_OR_ALERT;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_MULTI, false, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_MULTI, true, 1100));
}

static void test_ack_inside_alert_window()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ACK, resolve(USER_BUTTON_SINGLE, true, 2000));
}

static void test_failed_alert_does_not_start_window()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, userButtonActionsResolve(cfg, USER_BUTTON_SINGLE, false, 1000, limiter));
    // not committed via alertSent(), so the next press may retry
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, userButtonActionsResolve(cfg, USER_BUTTON_SINGLE, false, 1100, limiter));
}

static void test_ack_or_alert_alerts_when_idle()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 1000));
}

static void test_send_alert_ignores_nag()
{
    cfg.single_press = BUTTON_ACTION_SEND_ALERT;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, true, 1000));
}

static void test_alert_rate_limited_within_interval()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, false, 1000 + 29999));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 1000 + 30000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, false, 1000 + 30001));
}

static void test_first_alert_allowed_right_after_boot()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 0));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, false, 10));
}

static void test_rate_limit_survives_millis_wraparound()
{
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, UINT32_MAX - 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_RATE_LIMITED, resolve(USER_BUTTON_SINGLE, false, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 30000));
}

static void test_zero_interval_never_limits()
{
    cfg.alert_min_interval_secs = 0;
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 1000));
    TEST_ASSERT_EQUAL(UBA_OUTCOME_ALERT, resolve(USER_BUTTON_SINGLE, false, 1000));
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
    RUN_TEST(test_ack_or_alert_acks_while_nagging);
    RUN_TEST(test_multi_press_uses_its_own_action);
    RUN_TEST(test_ack_inside_alert_window);
    RUN_TEST(test_failed_alert_does_not_start_window);
    RUN_TEST(test_ack_or_alert_alerts_when_idle);
    RUN_TEST(test_send_alert_ignores_nag);
    RUN_TEST(test_alert_rate_limited_within_interval);
    RUN_TEST(test_first_alert_allowed_right_after_boot);
    RUN_TEST(test_rate_limit_survives_millis_wraparound);
    RUN_TEST(test_zero_interval_never_limits);
    exit(UNITY_END());
}

void loop() {}
