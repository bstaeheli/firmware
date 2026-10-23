#pragma once

#include "PositionPrecision.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    USER_BUTTON_DOWN,  // button went down (start of any gesture)
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
#define UBA_ALERT_TEXT                                                                                                           \
    "\x07"                                                                                                                       \
    "HELP"
#endif
#ifndef UBA_ALERT2_TEXT
#define UBA_ALERT2_TEXT                                                                                                          \
    "\x07"                                                                                                                       \
    "SOS"
#endif
#ifndef UBA_ACK_TEXT
#define UBA_ACK_TEXT                                                                                                             \
    "\x07"                                                                                                                       \
    "OK"
#endif
#ifndef UBA_ALERT_WITH_POSITION
#define UBA_ALERT_WITH_POSITION 1
#endif
#ifndef UBA_ALERT_MIN_INTERVAL_SECS
#define UBA_ALERT_MIN_INTERVAL_SECS 30
#endif
#ifndef UBA_HOLD_ALERT_MAX_MS
#define UBA_HOLD_ALERT_MAX_MS 3000
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
    bool nagging = false;       // external notification running
    bool hintPlaying = false;   // hint tune running
    bool pressConsumed = false; // this press stopped a melody, ignore its gesture
    uint32_t lastPressedMs = 0; // last time the button was seen pressed
    bool downSeen = false;      // button-down of the current press was observed
    bool nagAtDown = false;     // nag state at that button-down
    uint32_t nowMs = 0;
    UserButtonRateLimiter limiters[2]; // per alert level
};

static const int UBA_HINT_NOTES = 5;

// Hint tune note after the given one, -1 = tune over
inline int userButtonActionsNextHintNote(int note)
{
    return note >= 0 && note + 1 < UBA_HINT_NOTES ? note + 1 : -1;
}

// OneButton reports a gesture 250 ms (BUTTON_CLICK_MS) after the last release; margin included
static const uint32_t UBA_GESTURE_TIMEOUT_MS = 1000;

// Call at button-down: a consumed press whose gesture never arrived must not swallow a later press
inline void userButtonActionsPressStart(UserButtonState &state)
{
    if (state.pressConsumed && state.nowMs - state.lastPressedMs > UBA_GESTURE_TIMEOUT_MS)
        state.pressConsumed = false;
    state.downSeen = true;
    state.nagAtDown = state.nagging;
}

// holdMs: how long a USER_BUTTON_HOLD lasted; shutdownMs: effective long-press shutdown time
inline UserButtonOutcome userButtonActionsResolve(const UserButtonActionsConfig &config, const UserButtonState &state,
                                                  UserButtonGesture gesture, uint32_t holdMs, uint32_t shutdownMs)
{
    if (!config.enabled)
        return UBA_OUTCOME_STOCK;
    // While a melody plays, the button only stops it (nag: with ack) right at button-down; the handler then sets
    // pressConsumed so the gesture of that press does nothing else (no alert, hint or shutdown)
    if (gesture == USER_BUTTON_DOWN) {
        if (state.hintPlaying)
            return UBA_OUTCOME_STOP_HINT;
        auto acks = [](UserButtonAction a) { return a == BUTTON_ACTION_ACK_OR_ALERT || a == BUTTON_ACTION_ACK_OR_ALERT2; };
        const bool ackConfigured = acks(config.single_press) || acks(config.double_press) || acks(config.multi_press);
        return state.nagging && ackConfigured ? UBA_OUTCOME_ACK : UBA_OUTCOME_STOCK;
    }
    if (state.pressConsumed)
        return UBA_OUTCOME_IGNORE;
    // Decide on the state at button-down: a nag or hint tune starting later does not change this press. Without an
    // observed button-down, fall back to the current state.
    const bool nagging = state.downSeen ? state.nagAtDown : state.nagging;
    if (!state.downSeen && state.hintPlaying)
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
            return state.nagging ? UBA_OUTCOME_IGNORE : UBA_OUTCOME_HINT; // no hint tune over a notification
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
        if (nagging)
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

// Count-up melody that replaces the stock lead-up when long_press_shutdown_ms is set: rising notes from
// hold_alert_max_ms (at the earliest from the long press time), the last one ending at the shutdown time.
static const int UBA_LEADUP_NOTES = 8;
static const uint32_t UBA_LEADUP_NOTE_MS = 200;

// Index of the count-up note due after holding for holdMs, -1 = none yet
inline int userButtonActionsLeadUpNote(uint32_t holdMs, uint32_t holdAlertMaxMs, uint32_t shutdownMs)
{
    const uint32_t startMs = holdAlertMaxMs > 500 ? holdAlertMaxMs : 500; // never on a click
    const uint32_t lastNoteMs = shutdownMs > UBA_LEADUP_NOTE_MS ? shutdownMs - UBA_LEADUP_NOTE_MS : 0;
    if (holdMs >= lastNoteMs)
        return UBA_LEADUP_NOTES - 1;
    if (holdMs < startMs)
        return -1;
    return (holdMs - startMs) * (UBA_LEADUP_NOTES - 1) / (lastNoteMs - startMs);
}

// OSM shortlink code (openstreetmap-website lib/short_link.rb); code needs UBA_OSM_CODE_SIZE bytes for zoom <= 18
static const size_t UBA_OSM_CODE_SIZE = 12;
inline void osmShortLink(char *code, int32_t lat_i, int32_t lon_i, uint8_t zoom)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_~";
    const uint32_t x = (uint64_t)((int64_t)lon_i + 1800000000) * (1ull << 32) / 3600000000u;
    const uint32_t y = (uint64_t)((int64_t)lat_i + 900000000) * (1ull << 32) / 1800000000u;
    uint64_t bits = 0;
    for (int i = 31; i >= 0; i--)
        bits = (bits << 2) | ((x >> i) & 1) << 1 | ((y >> i) & 1);
    int n = 0;
    for (int i = 0; i < (zoom + 8 + 2) / 3; i++)
        code[n++] = alphabet[(bits >> (58 - 6 * i)) & 0x3f];
    for (int i = 0; i < (zoom + 8) % 3; i++)
        code[n++] = '-';
    code[n] = 0;
}

// Zoom at which the map shows a few times the accuracy radius (meters, 0 = unknown)
inline uint8_t userButtonActionsMapZoom(uint32_t accuracyM)
{
    if (!accuracyM)
        return 16;
    uint8_t zoom = 18; // a phone screen spans roughly 2^(26 - zoom) m
    while (zoom > 12 && (1u << (23 - zoom)) < accuracyM)
        zoom--;
    return zoom;
}

// Text bytes that always fit a packet: Router adds the Data fields (port, length, bitfield) to the payload
static const size_t UBA_MAX_TEXT_LEN = 200;

// Keeps the last fix since boot: GPS.cpp/MeshService clear localPosition when the GPS loses lock
inline void userButtonActionsRememberFix(meshtastic_Position &lastFix, const meshtastic_Position &current, bool fixSinceBoot)
{
    if (!fixSinceBoot)
        lastFix = meshtastic_Position_init_default;
    else if (current.latitude_i || current.longitude_i)
        lastFix = current;
}

// Alert text plus a map link to the position as the position packet would reveal it, e.g.
// "\aHELP https://osm.org/go/0C0WCb6VB--?m ±25m 4min old", or "\aHELP no GPS fix" without a position.
// precision: channel position_precision, 0 = plain text. nowSecs: valid time, 0 = unknown.
// The suffix is left out if the text would exceed UBA_MAX_TEXT_LEN; the alert is never cut. Returns the text length.
inline size_t userButtonActionsAlertText(char *buf, size_t size, const char *alert, meshtastic_Position pos, uint32_t precision,
                                         uint32_t nowSecs)
{
    const int len = snprintf(buf, size, "%s", alert);
    auto append = [&](const char *suffix) {
        const size_t total = len + strlen(suffix);
        if (total < size && total <= UBA_MAX_TEXT_LEN)
            strcpy(buf + len, suffix);
        return strlen(buf);
    };
    if (len < 0 || (size_t)len >= size || !precision)
        return len < 0 ? 0 : strlen(buf);
    if (!pos.latitude_i && !pos.longitude_i)
        return append(" no GPS fix");

    // Accuracy ~ 5 m per DOP (1/100 units) rounded to 5 m, at least the radius of the precision cell
    const uint32_t dop = pos.HDOP ? pos.HDOP : pos.PDOP;
    uint32_t accuracyM = dop ? (dop + 50) / 100 * 5 : 0;
    if (dop && !accuracyM)
        accuracyM = 5;
    applyPositionPrecision(pos, precision);
    if (pos.precision_bits < 32) {
        const uint32_t radiusM = (uint32_t)(((1ull << (31 - pos.precision_bits)) * 11132 + 999999) / 1000000);
        if (accuracyM < radiusM)
            accuracyM = (radiusM + 4) / 5 * 5;
    }

    char code[UBA_OSM_CODE_SIZE];
    osmShortLink(code, pos.latitude_i, pos.longitude_i, userButtonActionsMapZoom(accuracyM));
    char link[64];
    int n = snprintf(link, sizeof(link), " https://osm.org/go/%s?m", code);
    if (accuracyM)
        n += snprintf(link + n, sizeof(link) - n, " \xC2\xB1%um", (unsigned)accuracyM);
    const uint32_t fixSecs = pos.timestamp ? pos.timestamp : pos.time;
    const uint32_t ageSecs = nowSecs && fixSecs && nowSecs > fixSecs ? nowSecs - fixSecs : 0;
    if (ageSecs > 120) {
        const uint32_t mins = ageSecs / 60;
        if (mins < 60)
            snprintf(link + n, sizeof(link) - n, " %umin old", (unsigned)mins);
        else if (mins < 48 * 60)
            snprintf(link + n, sizeof(link) - n, " %uh old", (unsigned)(mins / 60));
        else
            snprintf(link + n, sizeof(link) - n, " %ud old", (unsigned)(mins / 1440));
    }
    return append(link);
}

// Runs the configured action. Returns true if the gesture was handled and stock handling must be skipped.
bool userButtonActionsHandle(UserButtonGesture gesture, uint32_t holdMs = 0, uint32_t shutdownMs = 0);

// Plays the hint tune and the count-up lead-up melody one note per call; call regularly from the button thread.
// Returns true once the lead-up melody has finished.
bool userButtonActionsTick(bool buttonPressed, uint32_t holdMs);
