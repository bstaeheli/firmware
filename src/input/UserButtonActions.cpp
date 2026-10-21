#include "configuration.h"

#if MESHTASTIC_USER_BUTTON_ACTIONS
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Router.h"
#include "UserButtonActions.h"
#include "buzz/BuzzerFeedbackThread.h"
#include "buzz/buzz.h"
#include "modules/ExternalNotificationModule.h"

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

// Button feedback as stock would give it for this event (honors buzzer_mode)
static void playFeedback(input_broker_event event)
{
    if (!buzzerFeedbackThread)
        return;
    InputEvent evt = {"UserButton", event, 0, 0, 0};
    buzzerFeedbackThread->handleInputEvent(&evt);
}

static UserButtonState state;
static const int HINT_OFF = -1, HINT_INTERRUPTED = -2; // otherwise: next hint tune note
static int hintNote = HINT_OFF;

void userButtonActionsTick(bool buttonPressed)
{
    if (hintNote >= 0 && buttonPressed)
        hintNote = HINT_INTERRUPTED; // stop now, the gesture of this press only stops the tune
    else if (hintNote >= 0 && !playHintNote(hintNote++))
        hintNote = HINT_OFF;
}

bool userButtonActionsHandle(UserButtonGesture gesture, uint32_t holdMs, uint32_t shutdownMs)
{
    const UserButtonActionsConfig &cfg = userButtonActionsConfig();
    // Same nag check as InputBroker::handleInputEvent
    state.nagging =
        externalNotificationModule && moduleConfig.external_notification.enabled && externalNotificationModule->nagging();
    state.hintPlaying = hintNote != HINT_OFF;
    state.nowMs = millis();

    UserButtonOutcome outcome = userButtonActionsResolve(cfg, state, gesture, holdMs, shutdownMs);
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
    case UBA_OUTCOME_ALERT2:
        LOG_INFO("User button: send alert level %d", outcome == UBA_OUTCOME_ALERT2 ? 2 : 1);
        sendTextBroadcast(outcome == UBA_OUTCOME_ALERT2 ? cfg.alert2_text : cfg.alert_text);
        state.limiters[outcome == UBA_OUTCOME_ALERT2].alertSent(state.nowMs);
        if (cfg.alert_with_position)
            sendAdHocPosition(false);
        playFeedback(INPUT_BROKER_USER_PRESS);
        break;
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
        hintNote = HINT_OFF;
        break;
    case UBA_OUTCOME_IGNORE:
        break;
    }
    // Stock handling would have triggered this via InputBroker
    powerFSM.trigger(EVENT_INPUT);
    return true;
}
#endif
