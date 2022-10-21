#include "vban.h"

namespace Vban {

const QMap<int, Header::SampleRate> Header::k_SampleRateMap {
    {6000, VBAN_SR_6000},
    {12000, VBAN_SR_12000},
    {24000, VBAN_SR_24000},
    {48000, VBAN_SR_48000},
    {96000, VBAN_SR_96000},
    {192000, VBAN_SR_192000},
    {384000, VBAN_SR_384000},
    {8000, VBAN_SR_8000},
    {16000, VBAN_SR_16000},
    {32000, VBAN_SR_32000},
    {64000, VBAN_SR_64000},
    {128000, VBAN_SR_128000},
    {256000, VBAN_SR_256000},
    {512000, VBAN_SR_512000},
    {11025, VBAN_SR_11025},
    {22050, VBAN_SR_22050},
    {44100, VBAN_SR_44100},
    {88200, VBAN_SR_88200},
    {176400, VBAN_SR_176400},
    {352800, VBAN_SR_352800},
    {705600, VBAN_SR_705600}
};

const QMap<SDL_AudioFormat, Header::DataType> Header::k_DataTypeMap {
    {AUDIO_S8, VBAN_DATATYPE_BYTE8},
    {AUDIO_S16, VBAN_DATATYPE_INT16},
    {AUDIO_S32, VBAN_DATATYPE_INT32},
    {AUDIO_F32, VBAN_DATATYPE_FLOAT32}
};

void Emitter::init(const QHostAddress& addr, uint16_t port, QObject *parent) {
    if (s_Emitter)
        destroy();

    SDL_InitSubSystem(SDL_INIT_AUDIO);

    s_Emitter = new Emitter(parent);
    s_Thread = new QThread();
    s_Emitter->moveToThread(s_Thread);
    s_Thread->start();

    if (s_Emitter->bind(addr, port)) {
        qInfo("[VBAN Emitter] Initialized successfully.");
    } else {
        qInfo("[VBAN Emitter] Initialized failed.");
    }
}

void Emitter::destroy() {
    if (!s_Emitter)
        return;

    s_Thread->quit();
    s_Thread->wait();
    delete s_Thread;
    s_Thread = nullptr;

    delete s_Emitter;
    s_Emitter = nullptr;

    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    qInfo("[VBAN Emitter] Destroyed successfully.");
}

Emitter::Emitter(QObject *parent) : QUdpSocket(parent) {
    connect(this, &Emitter::sendSignal, this, &Emitter::handleSend);
}

Emitter::~Emitter() {
    SDL_PauseAudioDevice(m_AudioDeviceId, SDL_TRUE);
    SDL_CloseAudioDevice(m_AudioDeviceId);

    disconnect(this, &Emitter::sendSignal, this, &Emitter::handleSend);
}

bool Emitter::bind(const QHostAddress& addr, uint16_t port) {
    SDL_AudioSpec desired, obtained;

    char* name;
    SDL_GetDefaultAudioInfo(&name, &desired, SDL_TRUE);
    qInfo("[VBAN Emitter] Desired AudioSpec: name[%s] freq[%d] "
          "format[%X] channels[%d] silence[%d] samples[%d] padding[%d] size[%d]",
          name, desired.freq, desired.format, desired.channels,
          desired.silence, desired.samples, desired.padding, desired.size);

    desired.callback = send;
    m_AudioDeviceId = SDL_OpenAudioDevice(name, SDL_TRUE, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    qInfo("[VBAN Emitter] Obtained AudioSpec: AudioDeviceId[%d] freq[%d] "
          "format[%X] channels[%d] silence[%d] samples[%d] padding[%d] size[%d]",
          m_AudioDeviceId, obtained.freq, obtained.format, obtained.channels,
          obtained.silence, obtained.samples, obtained.padding, obtained.size);

    if (m_AudioDeviceId == 0)
        return false;

    memcpy(m_Packet.header.vban, "VBAN", 4);
    m_Packet.header.format_SR = (Header::k_SampleRateMap[obtained.freq] & Header::VBAN_SR_MASK) |
                                (Header::VBAN_PROTOCOL_AUDIO & Header::VBAN_PROTOCOL_MASK);
    m_Packet.header.format_nbc = obtained.channels - 1;
    m_Packet.header.format_bit = (Header::k_DataTypeMap[obtained.format] & Header::VBAN_DATATYPE_MASK) |
                                 (Header::VBAN_CODEC_PCM & Header::VBAN_CODEC_MASK);
    strcpy_s(m_Packet.header.streamname, STREAM_NAME);

    for (uint16_t div = 1; div <= obtained.samples; ++div) {
        if (obtained.samples % div == 0 && obtained.samples / div - 1 <= 0xFF
            && obtained.size % div == 0 && obtained.size / div <= sizeof(m_Packet.data)) {
            m_Packet.header.format_nbs = obtained.samples / div - 1;
            m_PacketDataLen = obtained.size / div;
            break;
        }
    }

    m_ClientAddress = addr;
    m_ClientPort = port;

    SDL_PauseAudioDevice(m_AudioDeviceId, SDL_FALSE);
    return true;
}

void Emitter::handleSend(const QByteArray& data) {
    if (m_ClientAddress.isNull() || m_ClientPort == 0)
        return;

    for (int index = 0; index < data.size(); index += m_PacketDataLen) {
        memcpy(m_Packet.data, data.mid(index), m_PacketDataLen);
        ++m_Packet.header.nuFrame;
        writeDatagram(reinterpret_cast<char *>(&m_Packet), sizeof(Header) + m_PacketDataLen,
                      m_ClientAddress, m_ClientPort);
    }
}

void Emitter::send(void*, uint8_t* stream, int len) {
    if (!s_Emitter)
        return;

    emit s_Emitter->sendSignal(QByteArray(reinterpret_cast<char *>(stream), len));
}

Emitter* Emitter::s_Emitter = nullptr;
QThread* Emitter::s_Thread = nullptr;

}
