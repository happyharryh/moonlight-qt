#include "streaming/cemuhook.h"

#include "input/input.h"

namespace Cemuhook {

const QMap<SDL_JoystickPowerLevel, SharedResponse::Battery> SharedResponse::k_BatteryMap = {
    {SDL_JOYSTICK_POWER_UNKNOWN, Battery::NOT_APPLICABLE},
    {SDL_JOYSTICK_POWER_EMPTY, Battery::DYING},
    {SDL_JOYSTICK_POWER_LOW, Battery::LOW},
    {SDL_JOYSTICK_POWER_MEDIUM, Battery::MEDIUM},
    {SDL_JOYSTICK_POWER_FULL, Battery::HIGH},
    {SDL_JOYSTICK_POWER_WIRED, Battery::CHARGING},
    {SDL_JOYSTICK_POWER_MAX, Battery::FULL}
};

bool MotionState::updateByControllerSensorEvent(SDL_ControllerSensorEvent* event) {
    // In SDL 2.24.0, the order of the sensor events may be:
    //     T1_gyro, T2_gyro, T3_gyro, T1_accel, T2_accel, T3_accel, ...
    //     (See https://github.com/libsdl-org/SDL/blob/release-2.24.0/src/joystick/hidapi/SDL_hidapi_switch.c#L1999)
    // The below codes are going to change the order to what we need:
    //     (T1_accel, T1_gyro), (T2_accel, T2_gyro), (T3_accel, T3_gyro), ...
    // In the future version of SDL, the order of the sensor events will become:
    //     T1_gyro, T1_accel, T2_gyro, T2_accel, T3_gyro, T3_accel, ...
    //     (See PR: https://github.com/libsdl-org/SDL/pull/6373)
    // After that, the codes here can be simplified.
    if (event->timestamp != inputTimestamp) {
        accelIndex = 0;
        gyroIndex = 0;
        inputTimestamp = event->timestamp;
    }

    DataResponse::MotionData* motion;
    if (event->sensor == SDL_SENSOR_ACCEL) {
        motion = &pendingMotionData[accelIndex % 3];
        constexpr float GRAVITY = 9.80665f;
        motion->accX = - event->data[0] / GRAVITY;
        motion->accY = - event->data[1] / GRAVITY;
        motion->accZ = - event->data[2] / GRAVITY;
        if (++accelIndex > gyroIndex)
            return false;
     } else if (event->sensor == SDL_SENSOR_GYRO) {
        motion = &pendingMotionData[gyroIndex % 3];
        constexpr float PI_FACTOR = 3.1415926535f * 2 / 312.0f;
        motion->pitch = event->data[0] / PI_FACTOR;
        motion->yaw = - event->data[1] / PI_FACTOR;
        motion->roll = - event->data[2] / PI_FACTOR;
        if (++gyroIndex > accelIndex)
            return false;
    } else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Unhandled controller sensor: %d",
                    event->sensor);
        return false;
    }

    // The codes here impliment the feature of this commit in advance:
    // https://github.com/libsdl-org/SDL/commit/18eb319adcba169c73bff27a13abe8a7ea08b0ff
    if (sampleTimestamp == 0)
        sampleTimestamp = SDL_GetTicks();

    constexpr uint32_t SAMPLE_FREQUENCY = 1000;
    if (++sampleCount >= SAMPLE_FREQUENCY) {
        uint32_t now = SDL_GetTicks();
        outputInterval = (now - sampleTimestamp) * 1000 / sampleCount;
        sampleCount = 0;
        sampleTimestamp = now;
    }

    outputTimestamp += outputInterval;
    reinterpret_cast<uint64_t &>(motion->timestamp) = outputTimestamp;
    memcpy(&motionData, motion, sizeof(motionData));

    return true;
}

void Server::init(const QHostAddress& addr, uint16_t port, QObject *parent) {
    if (s_Server)
        destroy();

    s_Server = new Server(parent);
    s_Server->bind(addr, port);

    constexpr int CHECK_INTERVAL = 3000;
    s_Server->startTimer(CHECK_INTERVAL);

    s_Thread = new QThread();
    s_Server->moveToThread(s_Thread);
    s_Thread->start();

    qInfo("[CemuHook Server] Initialized successfully.");
}

void Server::destroy() {
    if (!s_Server)
        return;

    s_Thread->quit();
    s_Thread->wait();
    delete s_Thread;
    s_Thread = nullptr;

    delete s_Server;
    s_Server = nullptr;

    qInfo("[CemuHook Server] Destroyed successfully.");
}

void Server::send(GamepadState* state) {
    if (!s_Server)
        init();

    emit s_Server->sendSignal(*state);
}

Server::Server(QObject *parent) : QUdpSocket(parent),
    m_ServerId(IdentityManager::get()->getUniqueId().toULongLong(nullptr, 16)) {
    connect(this, &Server::readyRead, this, &Server::handleReceive);

    qRegisterMetaType<GamepadState>("GamepadState");
    connect(this, &Server::sendSignal, this, &Server::handleSend);
}

Server::~Server() {
    disconnect(this, &Server::readyRead, this, &Server::handleReceive);
    disconnect(this, &Server::sendSignal, this, &Server::handleSend);
}

void Server::handleReceive() {
    static Request request;
    static QHostAddress inAddress;
    static uint16_t inPort;
    if (readDatagram(reinterpret_cast<char *>(&request), sizeof(request), &inAddress, &inPort) < sizeof(Header))
        return;

    if (strncmp(request.header.magic, "DSUC", 4) || request.header.version != VERSION)
        return;

    uint32_t inCrc32 = request.header.crc32;
    request.header.crc32 = 0;
    if (SDL_crc32(0, &request, request.header.length + 16) != inCrc32)
        return;

    switch (request.header.eventType) {
        case Header::EventType::VERSION_TYPE: {
            static VersionResponse response = {
                {                                       // header
                    {'D', 'S', 'U', 'S'},                   // magic
                    VERSION,                                // version
                    sizeof(VersionResponse) - 16,           // length
                    0,                                      // crc32
                    m_ServerId,                             // id
                    Header::EventType::VERSION_TYPE         // eventType
                },
                VERSION                                 // version
            };

            response.header.crc32 = 0;
            response.header.crc32 = SDL_crc32(0, &response, sizeof(response));
            writeDatagram(reinterpret_cast<char *>(&response), sizeof(response), inAddress, inPort);
            break;
        }

        case Header::EventType::INFO_TYPE: {
            static InfoResponse response {
                {                                                   // header
                    {'D', 'S', 'U', 'S'},                               // magic
                    VERSION,                                            // version
                    sizeof(InfoResponse) - 16,                          // length
                    0,                                                  // crc32
                    m_ServerId,                                         // id
                    Header::EventType::INFO_TYPE                        // eventType
                },
                {                                                   // shared
                    0,                                                  // slot
                    SharedResponse::SlotState::NOT_CONNECTED,           // slotState
                    SharedResponse::DeviceModel::NOT_APPLICABLE,        // deviceModel
                    SharedResponse::Connection::NOT_APPLICABLE,         // connection
                    {},                                                 // mac
                    SharedResponse::Battery::NOT_APPLICABLE,            // battery
                    SharedResponse::Connected::FOR_INFO                 // connected
                }
            };

            for (size_t i = 0; i < request.info.slotNumber; ++i) {
                uint8_t slot = request.info.slot[i];
                if (slot >= MAX_GAMEPADS)
                    continue;

                response.shared.slot = slot;
                if (SDL_Joystick* joystick = SDL_JoystickFromPlayerIndex(slot)) {
                    response.shared.slotState = SharedResponse::SlotState::CONNECTED;

                    SDL_GameController* gameController = SDL_GameControllerFromInstanceID(
                        SDL_JoystickInstanceID(joystick));
                    if (SDL_GameControllerIsSensorEnabled(gameController, SDL_SENSOR_ACCEL) &&
                        SDL_GameControllerIsSensorEnabled(gameController, SDL_SENSOR_GYRO)) {
                        response.shared.deviceModel = SharedResponse::DeviceModel::FULL_GYRO;
                    } else {
                        response.shared.deviceModel = SharedResponse::DeviceModel::DO_NOT_USE;
                    }

                    if (const char* serial = SDL_JoystickGetSerial(joystick)) {
                        sscanf_s(serial, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
                                 &response.shared.mac[0], &response.shared.mac[1], &response.shared.mac[2],
                                 &response.shared.mac[3], &response.shared.mac[4], &response.shared.mac[5]);
                    } else {
                        memset(response.shared.mac, 0, sizeof(response.shared.mac));
                    }

                    response.shared.battery = SharedResponse::k_BatteryMap[SDL_JoystickCurrentPowerLevel(joystick)];
                } else {
                    response.shared.slotState = SharedResponse::SlotState::NOT_CONNECTED;
                    response.shared.deviceModel = SharedResponse::DeviceModel::NOT_APPLICABLE;
                    memset(response.shared.mac, 0, sizeof(response.shared.mac));
                    response.shared.battery = SharedResponse::Battery::NOT_APPLICABLE;
                }

                response.header.crc32 = 0;
                response.header.crc32 = SDL_crc32(0, &response, sizeof(response));
                writeDatagram(reinterpret_cast<char *>(&response), sizeof(response), inAddress, inPort);
            }
            break;
        }

        case Header::EventType::DATA_TYPE: {
            uint32_t curTimestamp = SDL_GetTicks();
            bool isNewClient = true;
            for (Client& client : m_Clients) {
                if (client.address == inAddress && client.port == inPort) {
                    client.lastTimestamp = curTimestamp;
                    isNewClient = false;
                }
            }
            if (isNewClient) {
                m_Clients.append(Client {request.header.id, inAddress, inPort, 0, curTimestamp});

                qInfo("[CemuHook Server] Request for data from new client [%s:%d].",
                      qPrintable(inAddress.toString()), inPort);
            }
            break;
        }
    }
}

void Server::handleSend(const GamepadState& state) {
    if (m_Clients.isEmpty()) {
        return;
    }

    static DataResponse response {
        {                                                   // header
            {'D', 'S', 'U', 'S'},                               // magic
            VERSION,                                            // version
            sizeof(DataResponse) - 16,                          // length
            0,                                                  // crc32
            m_ServerId,                                         // id
            Header::EventType::DATA_TYPE                        // eventType
        },
        {                                                   // shared
            0,                                                  // slot
            SharedResponse::SlotState::CONNECTED,               // slotState
            SharedResponse::DeviceModel::NOT_APPLICABLE,        // deviceModel
            SharedResponse::Connection::NOT_APPLICABLE,         // connection
            {},                                                 // mac
            SharedResponse::Battery::NOT_APPLICABLE,            // battery
            SharedResponse::Connected::CONNECTED                // connected
        }
    };

    response.shared.slot = state.index;

    response.shared.deviceModel = state.motionState.deviceModel;

    SDL_Joystick* joystick = SDL_JoystickFromInstanceID(state.jsId);
    if (const char* serial = SDL_JoystickGetSerial(joystick)) {
        sscanf_s(serial, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx",
                 &response.shared.mac[0], &response.shared.mac[1], &response.shared.mac[2],
                 &response.shared.mac[3], &response.shared.mac[4], &response.shared.mac[5]);
    } else {
        memset(response.shared.mac, 0, sizeof(response.shared.mac));
    }

    response.shared.battery = SharedResponse::k_BatteryMap[SDL_JoystickCurrentPowerLevel(joystick)];

    response.buttons = (state.buttons & BACK_FLAG ? 0x1 : 0) |
                       (state.buttons & LS_CLK_FLAG ? 0x2 : 0) |
                       (state.buttons & RS_CLK_FLAG ? 0x4 : 0) |
                       (state.buttons & PLAY_FLAG ? 0x8 : 0) |
                       (state.buttons & UP_FLAG ? 0x10 : 0) |
                       (state.buttons & RIGHT_FLAG ? 0x20 : 0) |
                       (state.buttons & DOWN_FLAG ? 0x40 : 0) |
                       (state.buttons & LEFT_FLAG ? 0x80 : 0) |
                       (state.lt > 0 ? 0x100 : 0) |
                       (state.rt > 0 ? 0x200 : 0) |
                       (state.buttons & LB_FLAG ? 0x400 : 0) |
                       (state.buttons & RB_FLAG ? 0x800 : 0) |
                       (state.buttons & X_FLAG ? 0x1000 : 0) |
                       (state.buttons & A_FLAG ? 0x2000 : 0) |
                       (state.buttons & B_FLAG ? 0x4000 : 0) |
                       (state.buttons & Y_FLAG ? 0x8000 : 0);
    response.homeButton = state.buttons & SPECIAL_FLAG ? 1 : 0;
    response.lsX = (state.lsX >> 8) + 0x80;
    response.lsY = (state.lsY >> 8) + 0x80;
    response.rsX = (state.rsX >> 8) + 0x80;
    response.rsY = (state.rsY >> 8) + 0x80;
    response.adLeft = state.buttons & LEFT_FLAG ? 0xFF : 0;
    response.adDown = state.buttons & DOWN_FLAG ? 0xFF : 0;
    response.adRight = state.buttons & RIGHT_FLAG ? 0xFF : 0;
    response.adUp = state.buttons & UP_FLAG ? 0xFF : 0;
    response.aY = state.buttons & Y_FLAG ? 0xFF : 0;
    response.aB = state.buttons & B_FLAG ? 0xFF : 0;
    response.aA = state.buttons & A_FLAG ? 0xFF : 0;
    response.aX = state.buttons & X_FLAG ? 0xFF : 0;
    response.aR1 = state.buttons & RB_FLAG ? 0xFF : 0;
    response.aL1 = state.buttons & LB_FLAG ? 0xFF : 0;
    response.aR2 = state.rt;
    response.aL2 = state.lt;

    memcpy(&response.motion, &state.motionState.motionData, sizeof(response.motion));

    for (Client& client : m_Clients) {
        response.packetNumber = client.packetNumber++;
        response.header.crc32 = 0;
        response.header.crc32 = SDL_crc32(0, &response, sizeof(response));
        writeDatagram(reinterpret_cast<char *>(&response), sizeof(response), client.address, client.port);
    }
}

void Server::timerEvent(QTimerEvent*) {
    uint32_t curTimestamp = SDL_GetTicks();

    for (QList<Client>::iterator c = m_Clients.begin(); c != m_Clients.end();) {
        constexpr uint32_t CHECK_TIMEOUT = 5000;
        if (curTimestamp - c->lastTimestamp > CHECK_TIMEOUT) {
            qInfo("[CemuHook Server] No packet from client [%s:%d] for some time.",
                  qPrintable(c->address.toString()), c->port);

            c = m_Clients.erase(c);
        } else {
            ++c;
        }
    }
}

Server* Server::s_Server = nullptr;
QThread* Server::s_Thread = nullptr;

}
