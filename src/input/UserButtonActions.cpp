#include "configuration.h"

#if MESHTASTIC_USER_BUTTON_ACTIONS
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "Router.h"
#include "UserButtonActions.h"
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
static void sendAdHocPosition()
{
    service->refreshLocalMeshNode();
    service->trySendPosition(NODENUM_BROADCAST, true);
}

bool userButtonActionsHandle(UserButtonGesture gesture)
{
    static UserButtonRateLimiter limiter;
    const UserButtonActionsConfig &cfg = userButtonActionsConfig();
    // Same nag check as InputBroker::handleInputEvent
    bool nagging =
        externalNotificationModule && moduleConfig.external_notification.enabled && externalNotificationModule->nagging();

    switch (userButtonActionsResolve(cfg, gesture, nagging, millis(), limiter)) {
    case UBA_OUTCOME_STOCK:
        return false;
    case UBA_OUTCOME_ACK:
        LOG_INFO("User button: acknowledge notification");
        externalNotificationModule->stopNow();
        if (cfg.ack_text)
            sendTextBroadcast(cfg.ack_text);
        break;
    case UBA_OUTCOME_ALERT:
        LOG_INFO("User button: send alert");
        sendTextBroadcast(cfg.alert_text);
        if (cfg.alert_with_position)
            sendAdHocPosition();
        break;
    case UBA_OUTCOME_POSITION:
        LOG_INFO("User button: send position");
        sendAdHocPosition();
        break;
    case UBA_OUTCOME_RATE_LIMITED:
        LOG_DEBUG("User button: alert ignored, sent less than %u s ago", (unsigned)cfg.alert_min_interval_secs);
        break;
    case UBA_OUTCOME_IGNORE:
        break;
    }
    // Stock handling would have triggered this via InputBroker
    powerFSM.trigger(EVENT_INPUT);
    return true;
}
#endif
