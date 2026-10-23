#include "configuration.h"

#if MESHTASTIC_USER_BUTTON_ACTIONS
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Router.h"
#include "TypeConversions.h"
#include "UserButtonActions.h"
#include "buzz/BuzzerFeedbackThread.h"
#include "buzz/buzz.h"
#include "gps/RTC.h"
#include "modules/ExternalNotificationModule.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "modules/PositionModule.h"
#endif

// Same packet construction as CannedMessageModule::sendText, but broadcast on the primary channel without ack
static void sendTextBroadcast(const char *text)
{
    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = NODENUM_BROADCAST;
    p->channel = 0;
    p->want_ack = false;
    p->priority = meshtastic_MeshPacket_Priority_HIGH;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->decoded.payload.size = strnlen(text, sizeof(p->decoded.payload.bytes));
    memcpy(p->decoded.payload.bytes, text, p->decoded.payload.size);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

// Same as the stock double press (INPUT_BROKER_SEND_PING in SystemCommandsModule)
static void sendAdHocPosition(bool wantReplies)
{
    service->refreshLocalMeshNode();
    service->trySendPosition(NODENUM_BROADCAST, wantReplies);
}

static meshtastic_Position lastFix = meshtastic_Position_init_default;

// Last fix since boot; a fixed position counts as one, like in PositionModule (without an age)
static void rememberFix()
{
    meshtastic_Position current = localPosition;
    const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (config.position.fixed_position && !current.latitude_i && !current.longitude_i && node) {
        current = TypeConversions::ConvertToPosition(node->position);
        current.time = current.timestamp = 0;
    }
    userButtonActionsRememberFix(lastFix, current, config.position.fixed_position || nodeDB->hasLocalPositionSinceBoot());
}

// Position packet with the last fix, also after the GPS lost lock (stock would send none). PositionModule builds it
// from localPosition with the channel precision, so swap the last fix in for this one packet.
static void sendLastFix()
{
#if !MESHTASTIC_EXCLUDE_GPS
    if (!positionModule || (!lastFix.latitude_i && !lastFix.longitude_i))
        return;
    meshtastic_Position current = localPosition;
    localPosition = lastFix;
    service->refreshLocalMeshNode();
    positionModule->sendOurPosition(NODENUM_BROADCAST, false, 0);
    current.seq_number = localPosition.seq_number;
    localPosition = current;
#endif
}

// Button feedback as stock would give it for this event (honors buzzer_mode)
static void playFeedback(input_broker_event event)
{
    if (!buzzerFeedbackThread)
        return;
    InputEvent evt = {"UserButton", event, 0, 0, 0};
    buzzerFeedbackThread->handleInputEvent(&evt);
}

static UserButtonState state;
static int hintNote = -1;   // next hint tune note, -1 = not playing
static int leadUpNote = -1; // last count-up note played during this press

bool userButtonActionsTick(bool buttonPressed, uint32_t holdMs)
{
    if (hintNote >= 0) {
        playHintNote(hintNote);
        hintNote = userButtonActionsNextHintNote(hintNote);
    }

    if (buttonPressed)
        state.lastPressedMs = millis();
    rememberFix();

    const UserButtonActionsConfig &cfg = userButtonActionsConfig();
    if (!buttonPressed || state.pressConsumed || !cfg.long_press_shutdown_ms)
        return false;
    int due = userButtonActionsLeadUpNote(holdMs, cfg.hold_alert_max_ms, cfg.long_press_shutdown_ms);
    if (due > leadUpNote) {
        leadUpNote = due;
        playCountUpNote(due);
    }
    return leadUpNote == UBA_LEADUP_NOTES - 1;
}

bool userButtonActionsHandle(UserButtonGesture gesture, uint32_t holdMs, uint32_t shutdownMs)
{
    const UserButtonActionsConfig &cfg = userButtonActionsConfig();
    // Same nag check as InputBroker::handleInputEvent
    state.nagging =
        externalNotificationModule && moduleConfig.external_notification.enabled && externalNotificationModule->nagging();
    state.hintPlaying = hintNote >= 0;
    state.nowMs = millis();
    if (gesture == USER_BUTTON_DOWN)
        userButtonActionsPressStart(state);

    UserButtonOutcome outcome = userButtonActionsResolve(cfg, state, gesture, holdMs, shutdownMs);
    if (gesture == USER_BUTTON_DOWN) {
        leadUpNote = -1;
        if (outcome != UBA_OUTCOME_STOCK)
            state.pressConsumed = true; // until the gesture of this press (or click series) is reported
    } else {
        state.pressConsumed = false; // the gesture ends the press
        state.downSeen = false;
    }
    switch (outcome) {
    case UBA_OUTCOME_STOCK:
        return false;
    case UBA_OUTCOME_ACK:
        LOG_INFO("User button: acknowledge notification");
        externalNotificationModule->stopNow();
        if (cfg.ack_text)
            sendTextBroadcast(cfg.ack_text);
        playFeedback(INPUT_BROKER_USER_PRESS);
        break;
    case UBA_OUTCOME_ALERT:
    case UBA_OUTCOME_ALERT2: {
        LOG_INFO("User button: send alert level %d", outcome == UBA_OUTCOME_ALERT2 ? 2 : 1);
        // Map link only to what the position packet on the primary channel reveals
        char text[meshtastic_Constants_DATA_PAYLOAD_LEN + 1];
        const uint32_t precision = cfg.alert_with_position ? getPositionPrecisionForChannel(0) : 0;
        rememberFix();
        userButtonActionsAlertText(text, sizeof(text), outcome == UBA_OUTCOME_ALERT2 ? cfg.alert2_text : cfg.alert_text, lastFix,
                                   precision, getValidTime(RTCQualityDevice));
        sendTextBroadcast(text);
        state.limiters[outcome == UBA_OUTCOME_ALERT2].alertSent(state.nowMs);
        if (cfg.alert_with_position)
            sendLastFix();
        playFeedback(INPUT_BROKER_USER_PRESS);
        break;
    }
    case UBA_OUTCOME_POSITION:
        LOG_INFO("User button: send position");
        sendAdHocPosition(true);
        playFeedback(INPUT_BROKER_SEND_PING);
        break;
    case UBA_OUTCOME_RATE_LIMITED:
        LOG_DEBUG("User button: alert ignored, sent less than %u s ago", (unsigned)cfg.alert_min_interval_secs);
        playFeedback(INPUT_BROKER_CANCEL);
        break;
    case UBA_OUTCOME_HINT:
        LOG_INFO("User button: held too long for an alert, nothing sent");
        hintNote = 0;
        break;
    case UBA_OUTCOME_STOP_HINT:
        hintNote = -1;
        break;
    case UBA_OUTCOME_IGNORE:
        break;
    }
    // Stock handling would have triggered this via InputBroker
    powerFSM.trigger(EVENT_INPUT);
    return true;
}
#endif
