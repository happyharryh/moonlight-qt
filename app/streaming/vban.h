#pragma once

#include <SDL.h>

#include <QUdpSocket>
#include <QThread>

#define STREAM_NAME "Moonlight"

namespace Vban {

struct Header {
    char vban[4];           // contains 'V' 'B', 'A', 'N'
    uint8_t format_SR;      // SR index (SampleRate + Protocol)
    uint8_t format_nbs;     // nb sample per frame (1 to 256)
    uint8_t format_nbc;     // nb channel (1 to 256)
    uint8_t format_bit;     // mask = 0x07 (DataType + Codec)
    char streamname[16];    // stream name
    uint32_t nuFrame;       // growing frame number

    enum SampleRate : uint8_t {
        VBAN_SR_6000,
        VBAN_SR_12000,
        VBAN_SR_24000,
        VBAN_SR_48000,
        VBAN_SR_96000,
        VBAN_SR_192000,
        VBAN_SR_384000,
        VBAN_SR_8000,
        VBAN_SR_16000,
        VBAN_SR_32000,
        VBAN_SR_64000,
        VBAN_SR_128000,
        VBAN_SR_256000,
        VBAN_SR_512000,
        VBAN_SR_11025,
        VBAN_SR_22050,
        VBAN_SR_44100,
        VBAN_SR_88200,
        VBAN_SR_176400,
        VBAN_SR_352800,
        VBAN_SR_705600,
        VBAN_SR_UNDEFINED_1,
        VBAN_SR_UNDEFINED_2,
        VBAN_SR_UNDEFINED_3,
        VBAN_SR_UNDEFINED_4,
        VBAN_SR_UNDEFINED_5,
        VBAN_SR_UNDEFINED_6,
        VBAN_SR_UNDEFINED_7,
        VBAN_SR_UNDEFINED_8,
        VBAN_SR_UNDEFINED_9,
        VBAN_SR_UNDEFINED_10,
        VBAN_SR_UNDEFINED_11
    };
    static constexpr uint8_t VBAN_SR_MASK = 0x1F;
    static const QMap<int, SampleRate> k_SampleRateMap;

    enum Protocol : uint8_t {
        VBAN_PROTOCOL_AUDIO         =   0x00,
        VBAN_PROTOCOL_SERIAL        =   0x20,
        VBAN_PROTOCOL_TXT           =   0x40,
        VBAN_PROTOCOL_SERVICE       =   0x60,
        VBAN_PROTOCOL_UNDEFINED_1   =   0x80,
        VBAN_PROTOCOL_UNDEFINED_2   =   0xA0,
        VBAN_PROTOCOL_UNDEFINED_3   =   0xC0,
        VBAN_PROTOCOL_USER          =   0xE0
    };
    static constexpr uint8_t VBAN_PROTOCOL_MASK = 0xE0;

    enum DataType : uint8_t {
        VBAN_DATATYPE_BYTE8,
        VBAN_DATATYPE_INT16,
        VBAN_DATATYPE_INT24,
        VBAN_DATATYPE_INT32,
        VBAN_DATATYPE_FLOAT32,
        VBAN_DATATYPE_FLOAT64,
        VBAN_DATATYPE_12BITS,
        VBAN_DATATYPE_10BITS
    };
    static constexpr uint8_t VBAN_DATATYPE_MASK = 0x07;
    static const QMap<SDL_AudioFormat, DataType> k_DataTypeMap;

    enum Codec : uint8_t {
        VBAN_CODEC_PCM              =   0x00,
        VBAN_CODEC_VBCA             =   0x10,
        VBAN_CODEC_VBCV             =   0x20,
        VBAN_CODEC_UNDEFINED_1      =   0x30,
        VBAN_CODEC_UNDEFINED_2      =   0x40,
        VBAN_CODEC_UNDEFINED_3      =   0x50,
        VBAN_CODEC_UNDEFINED_4      =   0x60,
        VBAN_CODEC_UNDEFINED_5      =   0x70,
        VBAN_CODEC_UNDEFINED_6      =   0x80,
        VBAN_CODEC_UNDEFINED_7      =   0x90,
        VBAN_CODEC_UNDEFINED_8      =   0xA0,
        VBAN_CODEC_UNDEFINED_9      =   0xB0,
        VBAN_CODEC_UNDEFINED_10     =   0xC0,
        VBAN_CODEC_UNDEFINED_11     =   0xD0,
        VBAN_CODEC_UNDEFINED_12     =   0xE0,
        VBAN_CODEC_USER             =   0xF0
    };
    static constexpr uint8_t VBAN_CODEC_MASK = 0xF0;
};

class Emitter : public QUdpSocket {
    Q_OBJECT

public:
    static
    void init(const QHostAddress& addr, uint16_t port = 6980, QObject *parent = nullptr);

    static
    void destroy();

signals:
    void sendSignal(const QByteArray& data);

private:
    explicit Emitter(QObject *parent = nullptr);

    ~Emitter();

    bool bind(const QHostAddress& addr, uint16_t port);

    void handleSend(const QByteArray& data);

    static
    void send(void*, uint8_t* stream, int len);

    SDL_AudioDeviceID m_AudioDeviceId;

    struct {
        Header header;
        uint8_t data[1464 - sizeof(Header)];  // 1464 = 1500 (UDP Packet) - 36 (UDP/IP Header)
    } m_Packet;
    uint16_t m_PacketDataLen;

    QHostAddress m_ClientAddress;
    uint16_t m_ClientPort;

    static Emitter* s_Emitter;
    static QThread* s_Thread;
};

}
