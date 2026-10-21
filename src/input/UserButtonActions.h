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
    BUTTON_ACTION_SEND_ALERT2,    // send level 2 alert text (+ position if configured)
    BUTTON_ACTION_ACK_OR_ALERT2,  // ack a running notification, otherwise send level 2 alert
};

enum UserButtonGesture : uint8_t {
    USER_BUTTON_SINGLE,
    USER_BUTTON_DOUBLE,
    USER_BUTTON_MULTI, // three or more presses
    USER_BUTTON_HOLD,  // released after a long press
};

// Fields mirror the proposed UserButtonConfig protobuf message.
struct UserButtonActionsConfig {
    bool enabled;
    UserButtonAction single_press;
    UserButtonAction double_press;
    UserButtonAction multi_press; // three or more presses
    const char *alert_text;
    const char *alert2_text;
    const char *ack_text; // nullptr = no ack message
    bool alert_with_position;
    uint32_t alert_min_interval_secs; // per alert level
    uint32_t hold_alert_max_ms;       // shorter holds act as single press, longer ones play a hint; 0 = holds keep stock
    uint32_t long_press_shutdown_ms;  // 0 = stock value
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
#ifndef UBA_ALERT2_TEXT
#define UBA_ALERT2_TEXT "\x07SOS"
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
#ifndef UBA_HOLD_ALERT_MAX_MS
#define UBA_HOLD_ALERT_MAX_MS 5000
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
        UBA_ALERT2_TEXT,
        UBA_ACK_TEXT,
        UBA_ALERT_WITH_POSITION != 0,
        UBA_ALERT_MIN_INTERVAL_SECS,
        UBA_HOLD_ALERT_MAX_MS,
        UBA_LONG_PRESS_SHUTDOWN_MS,
    };
    return config;
}

enum UserButtonOutcome : uint8_t {
    UBA_OUTCOME_STOCK,        // not handled, run stock code
    UBA_OUTCOME_IGNORE,       // handled, do nothing
    UBA_OUTCOME_POSITION,     // send ad-hoc position
    UBA_OUTCOME_ALERT,        // send alert text
    UBA_OUTCOME_ALERT2,       // send level 2 alert text
    UBA_OUTCOME_ACK,          // stop the notification, send ack text
    UBA_OUTCOME_RATE_LIMITED, // alert suppressed by alert_min_interval_secs
    UBA_OUTCOME_HINT,         // nothing sent, play the hint tune
    UBA_OUTCOME_STOP_HINT,    // stop the hint tune, nothing else
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

struct UserButtonState {
    bool nagging = false;     // external notification running
    bool hintPlaying = false; // hint tune running
    uint32_t nowMs = 0;
    UserButtonRateLimiter limiters[2]; // per alert level
};

// holdMs: how long a USER_BUTTON_HOLD lasted; shutdownMs: effective long-press shutdown time
inline UserButtonOutcome userButtonActionsResolve(const UserButtonActionsConfig &config, const UserButtonState &state,
                                                  UserButtonGesture gesture, uint32_t holdMs, uint32_t shutdownMs)
{
    if (!config.enabled)
        return UBA_OUTCOME_STOCK;
    if (state.hintPlaying)
        return UBA_OUTCOME_STOP_HINT;

    UserButtonAction action;
    switch (gesture) {
    case USER_BUTTON_SINGLE:
        action = config.single_press;
        break;
    case USER_BUTTON_DOUBLE:
        action = config.double_press;
        break;
    case USER_BUTTON_MULTI:
        action = config.multi_press;
        break;
    default: // USER_BUTTON_HOLD; holds reaching the shutdown time stay stock (shutdown)
        if (!config.hold_alert_max_ms || config.single_press == BUTTON_ACTION_DEFAULT || holdMs >= shutdownMs)
            return UBA_OUTCOME_STOCK;
        if (holdMs >= config.hold_alert_max_ms)
            return UBA_OUTCOME_HINT;
        action = config.single_press;
        break;
    }

    const bool level2 = action == BUTTON_ACTION_SEND_ALERT2 || action == BUTTON_ACTION_ACK_OR_ALERT2;
    switch (action) {
    case BUTTON_ACTION_NONE:
        return UBA_OUTCOME_IGNORE;
    case BUTTON_ACTION_ADHOC_POSITION:
        return UBA_OUTCOME_POSITION;
    case BUTTON_ACTION_ACK_OR_ALERT:
    case BUTTON_ACTION_ACK_OR_ALERT2:
        if (state.nagging)
            return UBA_OUTCOME_ACK;
        // fall through
    case BUTTON_ACTION_SEND_ALERT:
    case BUTTON_ACTION_SEND_ALERT2: {
        const UserButtonRateLimiter &limiter = state.limiters[level2];
        if (limiter.sent && state.nowMs - limiter.lastMs < config.alert_min_interval_secs * 1000)
            return UBA_OUTCOME_RATE_LIMITED;
        return level2 ? UBA_OUTCOME_ALERT2 : UBA_OUTCOME_ALERT; // caller calls alertSent() once the alert went out
    }
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
bool userButtonActionsHandle(UserButtonGesture gesture, uint32_t holdMs = 0, uint32_t shutdownMs = 0);

// Plays the hint tune one note per call; call regularly from the button thread. A press stops the tune.
void userButtonActionsTick(bool buttonPressed);
