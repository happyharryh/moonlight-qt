#pragma once

#include <SDL.h>

#include <QUdpSocket>

#define VERSION 1001

struct GamepadState;

namespace Cemuhook {

struct Header {
    char magic[4];
    uint16_t version;
    uint16_t length;
    uint32_t crc32;
    uint32_t id;

    enum class EventType : uint32_t {
        VERSION_TYPE = 0x100000,
        INFO_TYPE = 0x100001,
        DATA_TYPE = 0x100002
    } eventType;
};

union Request {
    Header header;

    struct InfoRequest {
        Header header;
        int32_t slotNumber;
        uint8_t slot[4];
    } info;

    struct DataRequest {
        Header header;
        uint8_t bitmask;
        uint8_t slot;
        uint8_t mac[6];
    } data;
};

struct SharedResponse {
    uint8_t slot;

    enum class SlotState : uint8_t {
        NOT_CONNECTED, RESERVED, CONNECTED
    } slotState;

    enum class DeviceModel : uint8_t {
        NOT_APPLICABLE, NO_OR_PARTIAL_GYRO, FULL_GYRO, DO_NOT_USE
    } deviceModel;

    enum class Connection : uint8_t {
        NOT_APPLICABLE, USB, BLUETOOTH
    } connection;

    uint8_t mac[6];

    enum class Battery : uint8_t {
        NOT_APPLICABLE, DYING, LOW, MEDIUM, HIGH, FULL, CHARGING = 0xEE, CHARGED = 0xEF
    } battery;

    enum class Connected : uint8_t {
        FOR_INFO, CONNECTED
    } connected;

    static const QMap<SDL_JoystickPowerLevel, Battery> k_BatteryMap;
};

struct VersionResponse {
    Header header;
    uint16_t version;
};

struct InfoResponse {
    Header header;
    SharedResponse shared;
};

struct DataResponse {
    Header header;
    SharedResponse shared;
    uint32_t packetNumber;
    uint16_t buttons;
    uint8_t homeButton;
    uint8_t touchButton;
    uint8_t lsX;
    uint8_t lsY;
    uint8_t rsX;
    uint8_t rsY;
    uint8_t adLeft;
    uint8_t adDown;
    uint8_t adRight;
    uint8_t adUp;
    uint8_t aY;
    uint8_t aB;
    uint8_t aA;
    uint8_t aX;
    uint8_t aR1;
    uint8_t aL1;
    uint8_t aR2;
    uint8_t aL2;

    struct TouchData {
        uint8_t active;
        uint8_t id;
        uint16_t x;
        uint16_t y;
    } touch[2];

    struct MotionData {
        uint32_t timestamp;
        uint32_t timestamp_;
        float accX;
        float accY;
        float accZ;
        float pitch;
        float yaw;
        float roll;
    } motion;
};

struct MotionState {
    SharedResponse::DeviceModel deviceModel = SharedResponse::DeviceModel::NOT_APPLICABLE;
    DataResponse::MotionData motionData;

    DataResponse::MotionData pendingMotionData[3];
    size_t accelIndex = 0;
    size_t gyroIndex = 0;
    uint32_t inputTimestamp = 0;

    uint64_t outputTimestamp = 0;
    uint64_t outputInterval = 5000;
    uint32_t sampleTimestamp = 0;
    uint32_t sampleCount = 0;

    bool updateByControllerSensorEvent(SDL_ControllerSensorEvent* event);
};

class Server : public QUdpSocket {
    Q_OBJECT

public:
    static
    void init(const QHostAddress& addr = QHostAddress::Any, uint16_t port = 26760, QObject *parent = nullptr);

    static
    void destroy();

    static
    void send(GamepadState* state);

signals:
    void sendSignal(const GamepadState& state);

private:
    explicit Server(QObject *parent = nullptr);

    ~Server();

    void handleReceive();

    void handleSend(const GamepadState& state);

    void timerEvent(QTimerEvent*) override;

    uint32_t m_ServerId;

    struct Client {
        uint32_t id;
        QHostAddress address;
        uint16_t port;
        uint32_t packetNumber;
        uint32_t lastTimestamp;
    };
    QList<Client> m_Clients;

    static Server* s_Server;
    static QThread* s_Thread;
};

}
