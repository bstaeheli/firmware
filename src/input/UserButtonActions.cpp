#include "configuration.h"

#if MESHTASTIC_USER_BUTTON_ACTIONS
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Router.h"
#include "UserButtonActions.h"
#include "buzz/BuzzerFeedbackThread.h"
#include "modules/ExternalNotificationModule.h"

// Same packet construction as CannedMessageModule::sendText, but broadcast on the primary channel without ack
static bool sendTextBroadcast(const char *text)
{
    if (router->getQueueStatus().free == 0) {
        LOG_WARN("User button: TX queue full, text not sent");
        return false;
    }
    meshtastic_MeshPacket *p = router->allocForSending();
    p->to = NODENUM_BROADCAST;
    p->channel = 0;
    p->want_ack = false;
    p->priority = meshtastic_MeshPacket_Priority_HIGH;
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->decoded.payload.size = strnlen(text, sizeof(p->decoded.payload.bytes));
    memcpy(p->decoded.payload.bytes, text, p->decoded.payload.size);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
    return true;
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

bool userButtonActionsHandle(UserButtonGesture gesture)
{
    static UserButtonRateLimiter limiter;
    const UserButtonActionsConfig &cfg = userButtonActionsConfig();
    // Same nag check as InputBroker::handleInputEvent
    bool nagging =
        externalNotificationModule && moduleConfig.external_notification.enabled && externalNotificationModule->nagging();

    uint32_t now = millis();
    switch (userButtonActionsResolve(cfg, gesture, nagging, now, limiter)) {
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
        LOG_INFO("User button: send alert");
        if (!sendTextBroadcast(cfg.alert_text))
            break; // no rate-limit window, the next press retries
        limiter.alertSent(now);
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
    case UBA_OUTCOME_IGNORE:
        break;
    }
    // Stock handling would have triggered this via InputBroker
    powerFSM.trigger(EVENT_INPUT);
    return true;
}
#endif
