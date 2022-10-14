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

void Server::init(const QHostAddress& addr, uint16_t port, QObject *parent) {
    if (server)
        return;

    server = new Server(parent);
    server->bind(addr, port);

    constexpr int CHECK_INTERVAL = 3000;
    server->startTimer(CHECK_INTERVAL);

    thread = new QThread();
    server->moveToThread(thread);
    thread->start();

    qInfo("[CemuHook Server] Initialized successfully.");
}

void Server::send(SDL_ControllerSensorEvent* event, GamepadState* state) {
    if (!server)
        init();

    if (state) {
        emit server->sendSignal(*event, *state);
    } else {
        static const GamepadState nullState {};
        emit server->sendSignal(*event, nullState);
    }
}

Server::Server(QObject *parent) : QUdpSocket(parent),
    serverId(IdentityManager::get()->getUniqueId().toULongLong(nullptr, 16)) {
    connect(this, &Server::readyRead, this, &Server::handleReceive);

    qRegisterMetaType<SDL_ControllerSensorEvent>("SDL_ControllerSensorEvent");
    qRegisterMetaType<GamepadState>("GamepadState");
    connect(this, &Server::sendSignal, this, &Server::handleSend);
}

void Server::handleReceive() {
    static Request req;
    static QHostAddress inAddress;
    static uint16_t inPort;
    if (readDatagram(reinterpret_cast<char *>(&req), sizeof(req), &inAddress, &inPort) < sizeof(Header))
        return;

    if (strncmp(req.header.magic, "DSUC", 4) || req.header.version != VERSION)
        return;

    uint32_t inCrc = req.header.crc32;
    req.header.crc32 = 0;
    if (SDL_crc32(0, &req, req.header.length + 16) != inCrc)
        return;

    switch (req.header.eventType) {
        case Header::EventType::VERSION_TYPE: {
            static VersionResponse res = {
                {                                       // header
                    {'D', 'S', 'U', 'S'},                   // magic
                    VERSION,                                // version
                    sizeof(VersionResponse) - 16,           // length
                    0,                                      // crc32
                    serverId,                               // id
                    Header::EventType::VERSION_TYPE         // eventType
                },
                VERSION                                 // version
            };

            res.header.crc32 = 0;
            res.header.crc32 = SDL_crc32(0, &res, sizeof(res));
            writeDatagram(reinterpret_cast<char *>(&res), sizeof(res), inAddress, inPort);
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
                    SharedResponse::DeviceModel::NOT_APPLICABLE,        // deviceModel
                    SharedResponse::Connection::NOT_APPLICABLE,         // connection
                    {},                                                 // mac
                    SharedResponse::Battery::NOT_APPLICABLE,            // battery
                    SharedResponse::Connected::FOR_INFO                 // connected
                }
            };

            for (size_t i = 0; i < req.info.portCnt; ++i) {
                uint8_t slot = req.info.slot[i];
                if (slot >= MAX_GAMEPADS)
                    continue;

                res.response.slot = slot;
                if (SDL_Joystick* joystick = SDL_JoystickFromPlayerIndex(slot)) {
                    res.response.slotState = SharedResponse::SlotState::CONNECTED;

                    SDL_GameController* gameController = SDL_GameControllerFromInstanceID(
                        SDL_JoystickInstanceID(joystick));
                    if (SDL_GameControllerIsSensorEnabled(gameController, SDL_SENSOR_ACCEL) &&
                        SDL_GameControllerIsSensorEnabled(gameController, SDL_SENSOR_GYRO)) {
                        res.response.deviceModel = SharedResponse::DeviceModel::FULL_GYRO;
                    } else {
                        res.response.deviceModel = SharedResponse::DeviceModel::DO_NOT_USE;
                    }

                    if (const char* serial = SDL_JoystickGetSerial(joystick)) {
                        sscanf_s(serial, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
                                 &res.response.mac[0], &res.response.mac[1], &res.response.mac[2],
                                 &res.response.mac[3], &res.response.mac[4], &res.response.mac[5]);
                    } else {
                        memset(res.response.mac, 0, sizeof(res.response.mac));
                    }

                    res.response.battery = SharedResponse::batteryMap[SDL_JoystickCurrentPowerLevel(joystick)];
                } else {
                    res.response.slotState = SharedResponse::SlotState::NOT_CONNECTED;
                    res.response.deviceModel = SharedResponse::DeviceModel::NOT_APPLICABLE;
                    memset(res.response.mac, 0, sizeof(res.response.mac));
                    res.response.battery = SharedResponse::Battery::NOT_APPLICABLE;
                }

                res.header.crc32 = 0;
                res.header.crc32 = SDL_crc32(0, &res, sizeof(res));
                writeDatagram(reinterpret_cast<char *>(&res), sizeof(res), inAddress, inPort);
            }
            break;
        }

        case Header::EventType::DATA_TYPE: {
            uint32_t curTimestamp = SDL_GetTicks();
            bool isNewClient = true;
            for (Client& client : clients) {
                if (client.address == inAddress && client.port == inPort) {
                    client.lastTimestamp = curTimestamp;
                    isNewClient = false;
                }
            }
            if (isNewClient) {
                clients.append(Client {req.header.id, inAddress, inPort, 0, curTimestamp});

                qInfo("[CemuHook Server] Request for data from new client [%s:%d].",
                      qPrintable(inAddress.toString()), inPort);
            }
            break;
        }
    }
}

void Server::handleSend(const SDL_ControllerSensorEvent& event, const GamepadState& gState) {
    if (clients.isEmpty()) {
        return;
    }

    SDL_Joystick* joystick = SDL_JoystickFromInstanceID(event.which);
    uint8_t slot = SDL_JoystickGetPlayerIndex(joystick);
    if (slot >= MAX_GAMEPADS)
        return;

    JoystickState* jState = &jStates[slot];
    if (event.timestamp != jState->inputTimestamp) {
        jState->tmpAccelIdx = 0;
        jState->tmpGyroIdx = 0;
        jState->inputTimestamp = event.timestamp;
    }

    DataResponse::MotionData* motion = nullptr;
    if (event.sensor == SDL_SENSOR_ACCEL) {
        DataResponse::MotionData* _motion = &jState->tmpMotionData[jState->tmpAccelIdx % 3];
        constexpr float GRAVITY = 9.80665f;
        _motion->accX = - event.data[0] / GRAVITY;
        _motion->accY = - event.data[1] / GRAVITY;
        _motion->accZ = - event.data[2] / GRAVITY;
        if (++jState->tmpAccelIdx <= jState->tmpGyroIdx)
            motion = _motion;
    } else if (event.sensor == SDL_SENSOR_GYRO) {
        DataResponse::MotionData* _motion = &jState->tmpMotionData[jState->tmpGyroIdx % 3];
        constexpr float PI = 3.1415926535f / 312.0f;
        _motion->pitch = event.data[0] / (PI * 2);
        _motion->yaw = - event.data[1] / (PI * 2);
        _motion->roll = - event.data[2] / (PI * 2);
        if (++jState->tmpGyroIdx <= jState->tmpAccelIdx)
            motion = _motion;
    } else {
        return;
    }

    if (motion) {
        if (jState->sampleTimestamp == 0)
            jState->sampleTimestamp = SDL_GetTicks();

        constexpr uint32_t SAMPLE_FREQUENCY = 1000;
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
        {                                                   // header
            {'D', 'S', 'U', 'S'},                               // magic
            VERSION,                                            // version
            sizeof(DataResponse) - 16,                          // length
            0,                                                  // crc32
            serverId,                                           // id
            Header::EventType::DATA_TYPE                        // eventType
        },
        {                                                   // response
            0,                                                  // slot
            SharedResponse::SlotState::CONNECTED,               // slotState
            SharedResponse::DeviceModel::FULL_GYRO,             // deviceModel
            SharedResponse::Connection::NOT_APPLICABLE,         // connection
            {},                                                 // mac
            SharedResponse::Battery::NOT_APPLICABLE,            // battery
            SharedResponse::Connected::CONNECTED                // connected
        }
    };

    res.response.slot = slot;

    if (const char* serial = SDL_JoystickGetSerial(joystick)) {
        sscanf_s(serial, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
                 &res.response.mac[0], &res.response.mac[1], &res.response.mac[2],
                 &res.response.mac[3], &res.response.mac[4], &res.response.mac[5]);
    } else {
        memset(res.response.mac, 0, sizeof(res.response.mac));
    }

    res.response.battery = SharedResponse::batteryMap[SDL_JoystickCurrentPowerLevel(joystick)];

    memcpy(&res.motion, motion, sizeof(res.motion));

    if (gState.controller) {
        res.buttons = (gState.buttons & BACK_FLAG ? 0x1 : 0) |
                      (gState.buttons & LS_CLK_FLAG ? 0x2 : 0) |
                      (gState.buttons & RS_CLK_FLAG ? 0x4 : 0) |
                      (gState.buttons & PLAY_FLAG ? 0x8 : 0) |
                      (gState.buttons & UP_FLAG ? 0x10 : 0) |
                      (gState.buttons & RIGHT_FLAG ? 0x20 : 0) |
                      (gState.buttons & DOWN_FLAG ? 0x40 : 0) |
                      (gState.buttons & LEFT_FLAG ? 0x80 : 0) |
                      (gState.lt > 0 ? 0x100 : 0) |
                      (gState.rt > 0 ? 0x200 : 0) |
                      (gState.buttons & LB_FLAG ? 0x400 : 0) |
                      (gState.buttons & RB_FLAG ? 0x800 : 0) |
                      (gState.buttons & X_FLAG ? 0x1000 : 0) |
                      (gState.buttons & A_FLAG ? 0x2000 : 0) |
                      (gState.buttons & B_FLAG ? 0x4000 : 0) |
                      (gState.buttons & Y_FLAG ? 0x8000 : 0);
        res.homeButton = gState.buttons & SPECIAL_FLAG ? 1 : 0;
        res.lsX = (gState.lsX >> 8) + 0x80;
        res.lsY = (gState.lsY >> 8) + 0x80;
        res.rsX = (gState.rsX >> 8) + 0x80;
        res.rsY = (gState.rsY >> 8) + 0x80;
        res.adLeft = gState.buttons & LEFT_FLAG ? 0xFF : 0;
        res.adDown = gState.buttons & DOWN_FLAG ? 0xFF : 0;
        res.adRight = gState.buttons & RIGHT_FLAG ? 0xFF : 0;
        res.adUp = gState.buttons & UP_FLAG ? 0xFF : 0;
        res.aY = gState.buttons & Y_FLAG ? 0xFF : 0;
        res.aB = gState.buttons & B_FLAG ? 0xFF : 0;
        res.aA = gState.buttons & A_FLAG ? 0xFF : 0;
        res.aX = gState.buttons & X_FLAG ? 0xFF : 0;
        res.aR1 = gState.buttons & RB_FLAG ? 0xFF : 0;
        res.aL1 = gState.buttons & LB_FLAG ? 0xFF : 0;
        res.aR2 = gState.rt;
        res.aL2 = gState.lt;
    } else {
        res.buttons = res.homeButton = 0;
        res.lsX = res.lsY = res.rsX = res.rsY = 0x80;
        res.adLeft = res.adDown = res.adRight = res.adUp = 0;
        res.aY = res.aB = res.aA = res.aX = 0;
        res.aR1 = res.aL1 = res.aR2 = res.aL2 = 0;
    }

    for (Client& client : clients) {
        res.packetNumber = client.packet++;
        res.header.crc32 = 0;
        res.header.crc32 = SDL_crc32(0, &res, sizeof(res));
        writeDatagram(reinterpret_cast<char *>(&res), sizeof(res), client.address, client.port);
    }
}

void Server::timerEvent(QTimerEvent*) {
    uint32_t curTimestamp = SDL_GetTicks();

    for (QList<Client>::iterator c = clients.begin(); c != clients.end();) {
        constexpr uint32_t CHECK_TIMEOUT = 5000;
        if (curTimestamp - c->lastTimestamp > CHECK_TIMEOUT) {
            qInfo("[CemuHook Server] No packet from client [%s:%d] for some time.",
                  qPrintable(c->address.toString()), c->port);

            c = clients.erase(c);
        } else {
            ++c;
        }
    }
}

Server* Server::server = nullptr;
QThread* Server::thread = nullptr;

}
