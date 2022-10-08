#pragma once

#include <QReadWriteLock>
#include <QUdpSocket>

#include "input/input.h"

#define VERSION 1001
#define BUFLEN 100
#define CHECK_INTERVAL 1000
#define CHECK_TIMEOUT 5000
#define SAMPLE_FREQUENCY 1000
#define PORT 26760

namespace Cemuhook {

struct Header {
    char magic[4]; // DSUS - server, DSUC - client
    uint16_t version; // 1001
    uint16_t length; // without header
    uint32_t crc32; // whole packet with this field = 0
    uint32_t id; // of packet source, constant among one run

    enum class EventType : uint32_t {
        VERSION_TYPE = 0x100000,  // protocol version information
        INFO_TYPE = 0x100001,  // information about connected controllers
        DATA_TYPE = 0x100002,  // actual controllers data
    } eventType; // no part of the header where length is involved
};

union Request {
    Header header;

    struct InfoRequest {
        Header header;
        int32_t portCnt; // amount of ports to report
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

    static const QMap<SDL_JoystickPowerLevel, Battery> batteryMap;
};

struct VersionResponse {
    Header header;
    uint16_t version;
};

struct InfoResponse {
    Header header;
    SharedResponse response;
};

struct DataResponse {
    Header header;
    SharedResponse response;
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

class Server : public QUdpSocket {
    Q_OBJECT

public:
    explicit Server(QObject *parent = nullptr);
    void handleSend(SDL_ControllerSensorEvent* event, GamepadState* state = nullptr);

private slots:
    void handleReceive();

private:
    uint32_t serverId;
    void timerEvent(QTimerEvent*) override;

    struct Client {
        uint32_t id;
        QHostAddress address;
        uint16_t port;
        uint32_t packet;
        uint32_t lastTimestamp;
    };
    QList<Client> clients;
    QReadWriteLock clientsMutex;

    struct JoystickState {
        DataResponse::MotionData tmpMotionData[3];
        uint32_t inputTimestamp = 0;
        size_t tmpAccelIdx = 0;
        size_t tmpGyroIdx = 0;

        uint64_t outputTimestamp = 0;
        uint64_t outputInterval = 5000;
        uint32_t sampleTimestamp = 0;
        uint32_t sampleCount = 0;
    } jStates[MAX_GAMEPADS];
};

extern Server* server;

}
