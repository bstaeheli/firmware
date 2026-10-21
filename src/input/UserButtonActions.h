#pragma once

#include <stdint.h>

// Configurable actions for the user button, mainly for devices without a screen.
// Compiled in only with MESHTASTIC_USER_BUTTON_ACTIONS; the decision logic below is
// hardware-free so it can be unit tested.

enum UserButtonAction : uint8_t {
    BUTTON_ACTION_DEFAULT = 0,    // keep stock behavior for this gesture
    BUTTON_ACTION_NONE,           // do nothing
    BUTTON_ACTION_ADHOC_POSITION, // send position now
    BUTTON_ACTION_SEND_ALERT,     // send alert text (+ position if configured)
    BUTTON_ACTION_ACK_OR_ALERT,   // ack a running notification, otherwise send alert
};

enum UserButtonGesture : uint8_t { USER_BUTTON_SINGLE, USER_BUTTON_DOUBLE, USER_BUTTON_MULTI };

// Fields mirror the proposed UserButtonConfig protobuf message.
struct UserButtonActionsConfig {
    bool enabled;
    UserButtonAction single_press;
    UserButtonAction double_press;
    UserButtonAction multi_press; // three or more presses
    const char *alert_text;
    const char *ack_text; // nullptr = no ack message
    bool alert_with_position;
    uint32_t alert_min_interval_secs;
    uint32_t long_press_shutdown_ms; // 0 = stock value
};

#ifndef UBA_SINGLE_PRESS
#define UBA_SINGLE_PRESS BUTTON_ACTION_DEFAULT
#endif
#ifndef UBA_DOUBLE_PRESS
#define UBA_DOUBLE_PRESS BUTTON_ACTION_DEFAULT
#endif
#ifndef UBA_MULTI_PRESS
#define UBA_MULTI_PRESS BUTTON_ACTION_DEFAULT
#endif
#ifndef UBA_ALERT_TEXT
#define UBA_ALERT_TEXT "\x07HELP"
#endif
#ifndef UBA_ACK_TEXT
#define UBA_ACK_TEXT "\x07OK"
#endif
#ifndef UBA_ALERT_WITH_POSITION
#define UBA_ALERT_WITH_POSITION 1
#endif
#ifndef UBA_ALERT_MIN_INTERVAL_SECS
#define UBA_ALERT_MIN_INTERVAL_SECS 30
#endif
#ifndef UBA_LONG_PRESS_SHUTDOWN_MS
#define UBA_LONG_PRESS_SHUTDOWN_MS 0
#endif
static_assert(UBA_LONG_PRESS_SHUTDOWN_MS <= UINT16_MAX, "UBA_LONG_PRESS_SHUTDOWN_MS must fit ButtonConfig::longLongPressTime");

// Today filled from build flags; could be filled from moduleConfig instead.
inline const UserButtonActionsConfig &userButtonActionsConfig()
{
    static const UserButtonActionsConfig config = {
#if MESHTASTIC_USER_BUTTON_ACTIONS
        true,
#else
        false,
#endif
        UBA_SINGLE_PRESS,
        UBA_DOUBLE_PRESS,
        UBA_MULTI_PRESS,
        UBA_ALERT_TEXT,
        UBA_ACK_TEXT,
        UBA_ALERT_WITH_POSITION != 0,
        UBA_ALERT_MIN_INTERVAL_SECS,
        UBA_LONG_PRESS_SHUTDOWN_MS,
    };
    return config;
}

enum UserButtonOutcome : uint8_t {
    UBA_OUTCOME_STOCK,        // not handled, run stock code
    UBA_OUTCOME_IGNORE,       // handled, do nothing
    UBA_OUTCOME_POSITION,     // send ad-hoc position
    UBA_OUTCOME_ALERT,        // send alert text
    UBA_OUTCOME_ACK,          // stop the notification, send ack text
    UBA_OUTCOME_RATE_LIMITED, // alert suppressed by alert_min_interval_secs
};

struct UserButtonRateLimiter {
    bool sent = false;
    uint32_t lastMs = 0;
    void alertSent(uint32_t nowMs)
    {
        sent = true;
        lastMs = nowMs;
    }
};

inline UserButtonOutcome userButtonActionsResolve(const UserButtonActionsConfig &config, UserButtonGesture gesture, bool nagging,
                                                  uint32_t nowMs, const UserButtonRateLimiter &limiter)
{
    if (!config.enabled)
        return UBA_OUTCOME_STOCK;
    UserButtonAction action = gesture == USER_BUTTON_SINGLE   ? config.single_press
                              : gesture == USER_BUTTON_DOUBLE ? config.double_press
                                                              : config.multi_press;
    switch (action) {
    case BUTTON_ACTION_NONE:
        return UBA_OUTCOME_IGNORE;
    case BUTTON_ACTION_ADHOC_POSITION:
        return UBA_OUTCOME_POSITION;
    case BUTTON_ACTION_ACK_OR_ALERT:
        if (nagging)
            return UBA_OUTCOME_ACK;
        // fall through
    case BUTTON_ACTION_SEND_ALERT:
        if (limiter.sent && nowMs - limiter.lastMs < config.alert_min_interval_secs * 1000)
            return UBA_OUTCOME_RATE_LIMITED;
        return UBA_OUTCOME_ALERT; // caller calls limiter.alertSent() once the alert went out
    default:
        return UBA_OUTCOME_STOCK;
    }
}

// When the long-press shutdown time is overridden, start the stock lead-up melody (4 notes, 400 ms apart) so that it
// ends at the shutdown time: the melody means "release now and the device turns off".
inline uint32_t userButtonActionsLeadUpMs(uint32_t shutdownMs, uint32_t stockLeadUpMs)
{
    const uint32_t leadUpDurationMs = 1300;
    return shutdownMs > leadUpDurationMs ? shutdownMs - leadUpDurationMs : stockLeadUpMs;
}

// Runs the configured action. Returns true if the gesture was handled and stock handling must be skipped.
bool userButtonActionsHandle(UserButtonGesture gesture);
