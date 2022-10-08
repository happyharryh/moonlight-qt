#include "cemuhook.h"

namespace Cemuhook {

const QMap<SDL_JoystickPowerLevel, SharedResponse::Battery> SharedResponse::batteryMap = {
    {SDL_JOYSTICK_POWER_UNKNOWN, Battery::NOT_APPLICABLE},
    {SDL_JOYSTICK_POWER_EMPTY, Battery::DYING},
    {SDL_JOYSTICK_POWER_LOW, Battery::LOW},
    {SDL_JOYSTICK_POWER_MEDIUM, Battery::MEDIUM},
    {SDL_JOYSTICK_POWER_FULL, Battery::HIGH},
    {SDL_JOYSTICK_POWER_WIRED, Battery::CHARGING},
    {SDL_JOYSTICK_POWER_MAX, Battery::FULL}
};

Server::Server(QObject *parent) : QUdpSocket(parent) {
    connect(this, SIGNAL(readyRead()), this, SLOT(handleReceive()));
    startTimer(CHECK_INTERVAL);

    qInfo("[CemuHook Server] Initialized successfully.");
}

void Server::handleSend(SDL_ControllerSensorEvent* event, GamepadState* gState) {
    if (clients.isEmpty()) {
        return;
    }

    SDL_Joystick* joystick = SDL_JoystickFromInstanceID(event->which);
    uint8_t slot = SDL_JoystickGetPlayerIndex(joystick);
    if (slot >= MAX_GAMEPADS)
        return;

    JoystickState* jState = &jStates[slot];
    if (event->timestamp != jState->inputTimestamp) {
        jState->tmpAccelIdx = 0;
        jState->tmpGyroIdx = 0;
        jState->inputTimestamp = event->timestamp;
    }

    DataResponse::MotionData* motion = nullptr;
    if (event->sensor == SDL_SENSOR_ACCEL) {
        constexpr float gravity = 9.80665f;
        DataResponse::MotionData* _motion = &jState->tmpMotionData[jState->tmpAccelIdx % 3];
        _motion->accX = - event->data[0] / gravity;
        _motion->accY = - event->data[1] / gravity;
        _motion->accZ = - event->data[2] / gravity;
        if (++jState->tmpAccelIdx <= jState->tmpGyroIdx)
            motion = _motion;
    } else if (event->sensor == SDL_SENSOR_GYRO) {
        constexpr float PI = 3.1415926535f / 312.0f;
        DataResponse::MotionData* _motion = &jState->tmpMotionData[jState->tmpGyroIdx % 3];
        _motion->pitch = event->data[0] / (PI * 2);
        _motion->yaw = - event->data[1] / (PI * 2);
        _motion->roll = - event->data[2] / (PI * 2);
        if (++jState->tmpGyroIdx <= jState->tmpAccelIdx)
            motion = _motion;
    } else {
        return;
    }

    if (motion) {
        if (jState->sampleTimestamp == 0)
            jState->sampleTimestamp = SDL_GetTicks();

        if (++jState->sampleCount >= SAMPLE_FREQUENCY) {
            uint32_t now = SDL_GetTicks();
            jState->outputInterval = (now - jState->sampleTimestamp) * 1000 / jState->sampleCount;
            jState->sampleCount = 0;
            jState->sampleTimestamp = now;
        }

        jState->outputTimestamp += jState->outputInterval;
        reinterpret_cast<uint64_t &>(motion->timestamp) = jState->outputTimestamp;
    } else {
        return;
    }

    static DataResponse res {
        {                                                                       // header
            {'D', 'S', 'U', 'S'},                                                   // magic
            VERSION,                                                                // version
            sizeof(DataResponse) - 16,                                              // length
            0,                                                                      // crc32
            serverId,                                                               // id
            Header::EventType::DATA_TYPE                                            // eventType
        },
        {                                                                       // response
            0,                                                                      // slot
            SharedResponse::SlotState::CONNECTED,                                   // slotState
            SharedResponse::DeviceModel::FULL_GYRO,                                 // deviceModel
            SharedResponse::Connection::NOT_APPLICABLE,                             // connection
            {},                                                                     // mac
            SharedResponse::Battery::NOT_APPLICABLE,                                // battery
            SharedResponse::Connected::CONNECTED                                    // connected
        }
    };

    res.response.slot = slot;
    sscanf_s(SDL_JoystickGetSerial(joystick), "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
             &res.response.mac[0], &res.response.mac[1], &res.response.mac[2],
             &res.response.mac[3], &res.response.mac[4], &res.response.mac[5]);
    res.response.battery = SharedResponse::batteryMap[SDL_JoystickCurrentPowerLevel(joystick)];
    memcpy(&res.motion, motion, sizeof(res.motion));

    if (gState) {
        res.buttons = (gState->buttons & BACK_FLAG ? 0x1 : 0) |
                      (gState->buttons & LS_CLK_FLAG ? 0x2 : 0) |
                      (gState->buttons & RS_CLK_FLAG ? 0x4 : 0) |
                      (gState->buttons & PLAY_FLAG ? 0x8 : 0) |
                      (gState->buttons & UP_FLAG ? 0x10 : 0) |
                      (gState->buttons & RIGHT_FLAG ? 0x20 : 0) |
                      (gState->buttons & DOWN_FLAG ? 0x40 : 0) |
                      (gState->buttons & LEFT_FLAG ? 0x80 : 0) |
                      (gState->lt > 0 ? 0x100 : 0) |
                      (gState->rt > 0 ? 0x200 : 0) |
                      (gState->buttons & LB_FLAG ? 0x400 : 0) |
                      (gState->buttons & RB_FLAG ? 0x800 : 0) |
                      (gState->buttons & X_FLAG ? 0x1000 : 0) |
                      (gState->buttons & A_FLAG ? 0x2000 : 0) |
                      (gState->buttons & B_FLAG ? 0x4000 : 0) |
                      (gState->buttons & Y_FLAG ? 0x8000 : 0);
        res.homeButton = gState->buttons & SPECIAL_FLAG ? 1 : 0;
        res.lsX = (gState->lsX >> 8) + 0x80;
        res.lsY = (gState->lsY >> 8) + 0x80;
        res.rsX = (gState->rsX >> 8) + 0x80;
        res.rsY = (gState->rsY >> 8) + 0x80;
        res.adLeft = gState->buttons & LEFT_FLAG ? 0xFF : 0;
        res.adDown = gState->buttons & DOWN_FLAG ? 0xFF : 0;
        res.adRight = gState->buttons & RIGHT_FLAG ? 0xFF : 0;
        res.adUp = gState->buttons & UP_FLAG ? 0xFF : 0;
        res.aY = gState->buttons & Y_FLAG ? 0xFF : 0;
        res.aB = gState->buttons & B_FLAG ? 0xFF : 0;
        res.aA = gState->buttons & A_FLAG ? 0xFF : 0;
        res.aX = gState->buttons & X_FLAG ? 0xFF : 0;
        res.aR1 = gState->buttons & RB_FLAG ? 0xFF : 0;
        res.aL1 = gState->buttons & LB_FLAG ? 0xFF : 0;
        res.aR2 = gState->rt;
        res.aR2 = gState->lt;
    } else {
        res.buttons = res.homeButton = 0;
        res.lsX = res.lsY = res.rsX = res.rsY = 0x80;
        res.adLeft = res.adDown = res.adRight = res.adUp = 0;
        res.aY = res.aB = res.aA = res.aX = 0;
        res.aR1 = res.aL1 = res.aR2 = res.aR2 = 0;
    }

    QReadLocker lock(&clientsMutex);
    for(Client& client : clients) {
        res.packetNumber = client.packet++;

        res.header.crc32 = 0;
        res.header.crc32 = SDL_crc32(0, &res, sizeof(res));
        writeDatagram(reinterpret_cast<char *>(&res), sizeof(res), client.address, client.port);
    }
}

void Server::handleReceive() {
    char buf[BUFLEN];
    Client inClient;
    if (readDatagram(buf, BUFLEN, &inClient.address, &inClient.port) < sizeof(Header))
        return;

    Request *req = reinterpret_cast<Request*>(buf);
    if (strncmp(req->header.magic, "DSUC", 4))
        return;

    switch(req->header.eventType) {
        case Header::EventType::VERSION_TYPE: {
            static VersionResponse res = {
                {                                   // header
                    {'D', 'S', 'U', 'S'},               // magic
                    VERSION,                            // version
                    sizeof(VersionResponse) - 16,       // length
                    0,                                  // crc32
                    serverId,                           // id
                    Header::EventType::VERSION_TYPE     // eventType
                },
                VERSION                             // version
            };

            res.header.crc32 = 0;
            res.header.crc32 = SDL_crc32(0, &res, sizeof(res));
            writeDatagram(reinterpret_cast<char *>(&res), sizeof(res), inClient.address, inClient.port);
            break;
        }

        case Header::EventType::INFO_TYPE: {
            static InfoResponse res {
                {                                                   // header
                    {'D', 'S', 'U', 'S'},                               // magic
                    VERSION,                                            // version
                    sizeof(InfoResponse) - 16,                          // length
                    0,                                                  // crc32
                    serverId,                                           // id
                    Header::EventType::INFO_TYPE                        // eventType
                },
                {                                                   // response
                    0,                                                  // slot
                    SharedResponse::SlotState::NOT_CONNECTED,           // slotState
                    SharedResponse::DeviceModel::FULL_GYRO,             // deviceModel
                    SharedResponse::Connection::NOT_APPLICABLE,         // connection
                    {},                                                 // mac
                    SharedResponse::Battery::NOT_APPLICABLE,            // battery
                    SharedResponse::Connected::FOR_INFO                 // connected
                }
            };

            for (size_t i = 0; i < req->info.portCnt; ++i) {
                uint8_t slot = req->info.slot[i];
                if (slot >= MAX_GAMEPADS)
                    continue;

                res.response.slot = slot;
                if (SDL_Joystick* joystick = SDL_JoystickFromPlayerIndex(slot)) {
                    res.response.slotState = SharedResponse::SlotState::CONNECTED;
                    sscanf_s(SDL_JoystickGetSerial(joystick), "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
                             &res.response.mac[0], &res.response.mac[1], &res.response.mac[2],
                             &res.response.mac[3], &res.response.mac[4], &res.response.mac[5]);
                    res.response.battery = SharedResponse::batteryMap[SDL_JoystickCurrentPowerLevel(joystick)];
                } else {
                    res.response.slotState = SharedResponse::SlotState::NOT_CONNECTED;
                    memset(res.response.mac, 0, sizeof(res.response.mac));
                    res.response.battery = SharedResponse::Battery::NOT_APPLICABLE;
                }

                res.header.crc32 = 0;
                res.header.crc32 = SDL_crc32(0, &res, sizeof(res));
                writeDatagram(reinterpret_cast<char *>(&res), sizeof(res), inClient.address, inClient.port);
            }
            break;
        }

        case Header::EventType::DATA_TYPE: {
            uint32_t curTimestamp = SDL_GetTicks();
            bool isNewClient = true;
            {
                QReadLocker lock(&clientsMutex);
                for (Client& client : clients) {
                    if (client.address == inClient.address && client.port == inClient.port) {
                        client.lastTimestamp = curTimestamp;
                        isNewClient = false;
                    }
                }
            }
            if (isNewClient) {
                qInfo("[CemuHook Server] Request for data from new client [%s:%d].",
                      qPrintable(inClient.address.toString()), inClient.port);

                QWriteLocker lock(&clientsMutex);
                clients.append(Client {req->header.id, inClient.address, inClient.port, 0, curTimestamp});
            }
            break;
        }
    }
}

void Server::timerEvent(QTimerEvent*) {
    uint32_t curTimestamp = SDL_GetTicks();

    using IClient = QList<Client>::iterator;
    QList<IClient> clientsToDelete;
    {
        QReadLocker lock(&clientsMutex);
        for (IClient client = clients.begin(); client != clients.end(); ++client) {
            if (curTimestamp - client->lastTimestamp > CHECK_TIMEOUT) {
                clientsToDelete.append(client);

                qInfo("[CemuHook Server] No packet from client [%s:%d] for some time.",
                      qPrintable(client->address.toString()), client->port);
            }
        }
    }

    if (!clientsToDelete.isEmpty()) {
        QWriteLocker lock(&clientsMutex);
        for (IClient& client : clientsToDelete) {
            clients.erase(client);
        }
    }
}

Server* server;

}
