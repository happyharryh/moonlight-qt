#pragma once

#include <QUdpSocket>
#include <QThread>
#include <SDL.h>
#include <QDataStream>

#define STREAM_NAME "Moonlight"

namespace Vban {

struct Header {
    char vban[4];
    uint8_t format_SR;  // VBanSampleRate + VBanProtocol
    uint8_t format_nbs;  // sample per frame
    uint8_t format_nbc;  // channel
    uint8_t format_bit;  // VBanBitResolution + VBanCodec
    char streamname[16];  // stream name
    uint32_t nuFrame;  // growting num

    enum class VBanSampleRate : uint8_t {
        VBAN_SR_6000, VBAN_SR_12000, VBAN_SR_24000, VBAN_SR_48000, VBAN_SR_96000, VBAN_SR_192000, VBAN_SR_384000,
        VBAN_SR_8000, VBAN_SR_16000, VBAN_SR_32000, VBAN_SR_64000, VBAN_SR_128000, VBAN_SR_256000, VBAN_SR_512000,
        VBAN_SR_11025, VBAN_SR_22050, VBAN_SR_44100, VBAN_SR_88200, VBAN_SR_176400, VBAN_SR_352800, VBAN_SR_705600,
        VBAN_SR_UNDEFINED_1, VBAN_SR_UNDEFINED_2, VBAN_SR_UNDEFINED_3, VBAN_SR_UNDEFINED_4, VBAN_SR_UNDEFINED_5,
        VBAN_SR_UNDEFINED_6, VBAN_SR_UNDEFINED_7, VBAN_SR_UNDEFINED_8, VBAN_SR_UNDEFINED_9, VBAN_SR_UNDEFINED_10,
        VBAN_SR_UNDEFINED_11
    };
    static constexpr uint8_t VBAN_SR_MASK = 0x1F;
    static const QMap<int, VBanSampleRate> sampleRateMap;

    enum class VBanProtocol : uint8_t {
        VBAN_PROTOCOL_AUDIO         =   0x00,
        VBAN_PROTOCOL_SERIAL        =   0x20,
        VBAN_PROTOCOL_TXT           =   0x40,
        VBAN_PROTOCOL_UNDEFINED_1   =   0x80,
        VBAN_PROTOCOL_UNDEFINED_2   =   0xA0,
        VBAN_PROTOCOL_UNDEFINED_3   =   0xC0,
        VBAN_PROTOCOL_UNDEFINED_4   =   0xE0
    };
    static constexpr uint8_t VBAN_PROTOCOL_MASK = 0xE0;

    enum class VBanBitResolution : uint8_t {
        VBAN_BITFMT_8_INT, VBAN_BITFMT_16_INT, VBAN_BITFMT_24_INT, VBAN_BITFMT_32_INT,
        VBAN_BITFMT_32_FLOAT, VBAN_BITFMT_64_FLOAT, VBAN_BITFMT_12_INT, VBAN_BITFMT_10_INT
    };
    static constexpr uint8_t VBAN_BIT_RESOLUTION_MASK = 0x07;
    static const QMap<SDL_AudioFormat, VBanBitResolution> formatMap;

    enum class VBanCodec : uint8_t {
        VBAN_CODEC_PCM              =   0x00,
        VBAN_CODEC_VBCA             =   0x10,
        VBAN_CODEC_VBCV             =   0x20,
        VBAN_CODEC_UNDEFINED_3      =   0x30,
        VBAN_CODEC_UNDEFINED_4      =   0x40,
        VBAN_CODEC_UNDEFINED_5      =   0x50,
        VBAN_CODEC_UNDEFINED_6      =   0x60,
        VBAN_CODEC_UNDEFINED_7      =   0x70,
        VBAN_CODEC_UNDEFINED_8      =   0x80,
        VBAN_CODEC_UNDEFINED_9      =   0x90,
        VBAN_CODEC_UNDEFINED_10     =   0xA0,
        VBAN_CODEC_UNDEFINED_11     =   0xB0,
        VBAN_CODEC_UNDEFINED_12     =   0xC0,
        VBAN_CODEC_UNDEFINED_13     =   0xD0,
        VBAN_CODEC_UNDEFINED_14     =   0xE0,
        VBAN_CODEC_USER             =   0xF0
    };
    static constexpr uint8_t VBAN_CODEC_MASK = 0xF0;
};

struct Response {
    Header header;
    uint8_t data[1464 - sizeof(Header)];
};

class Emitter : public QUdpSocket {
    Q_OBJECT

signals:
    void sendSignal(const QByteArray& data);

public:
    static void init(const QHostAddress& addr, uint16_t port = 6980, QObject *parent = nullptr);
    static void destroy();

private:
    Emitter(QObject *parent = nullptr);
    ~Emitter();

    bool bind(const QHostAddress& addr, uint16_t port);
    void handleSend(const QByteArray& data);
    static void send(void*, uint8_t* stream, int len);

    SDL_AudioDeviceID audioDeviceId;

    Response res;
    uint16_t bufSize;

    QHostAddress clientAddress;
    uint16_t clientPort;

    static Emitter* emitter;
    static QThread* thread;
};

}
