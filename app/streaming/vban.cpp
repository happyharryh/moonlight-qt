#include "vban.h"

namespace Vban {

const QMap<int, Header::VBanSampleRate> Header::sampleRateMap {
    {6000, VBanSampleRate::VBAN_SR_6000}, {12000, VBanSampleRate::VBAN_SR_12000},
    {24000, VBanSampleRate::VBAN_SR_24000}, {48000, VBanSampleRate::VBAN_SR_48000},
    {96000, VBanSampleRate::VBAN_SR_96000}, {192000, VBanSampleRate::VBAN_SR_192000},
    {384000, VBanSampleRate::VBAN_SR_384000}, {8000, VBanSampleRate::VBAN_SR_8000},
    {16000, VBanSampleRate::VBAN_SR_16000}, {32000, VBanSampleRate::VBAN_SR_32000},
    {64000, VBanSampleRate::VBAN_SR_64000}, {128000, VBanSampleRate::VBAN_SR_128000},
    {256000, VBanSampleRate::VBAN_SR_256000}, {512000, VBanSampleRate::VBAN_SR_512000},
    {11025, VBanSampleRate::VBAN_SR_11025}, {22050, VBanSampleRate::VBAN_SR_22050},
    {44100, VBanSampleRate::VBAN_SR_44100}, {88200, VBanSampleRate::VBAN_SR_88200},
    {176400, VBanSampleRate::VBAN_SR_176400}, {352800, VBanSampleRate::VBAN_SR_352800},
    {705600, VBanSampleRate::VBAN_SR_705600},
};

const QMap<SDL_AudioFormat, Header::VBanBitResolution> Header::formatMap {
    {AUDIO_S8, VBanBitResolution::VBAN_BITFMT_8_INT}, {AUDIO_S16, VBanBitResolution::VBAN_BITFMT_16_INT},
    {AUDIO_S32, VBanBitResolution::VBAN_BITFMT_32_INT}, {AUDIO_F32, VBanBitResolution::VBAN_BITFMT_32_FLOAT}
};

void Emitter::init(const QHostAddress& addr, uint16_t port, QObject *parent) {
    if (emitter)
        destroy();

    SDL_InitSubSystem(SDL_INIT_AUDIO);

    emitter = new Emitter(parent);
    thread = new QThread();
    emitter->moveToThread(thread);
    thread->start();

    if (emitter->bind(addr, port)) {
        qInfo("[VBAN Emitter] Initialized successfully.");
    } else {
        qInfo("[VBAN Emitter] Initialized failed.");
    }
}

void Emitter::destroy() {
    if (!emitter)
        return;

    thread->quit();
    thread->wait();
    delete thread;
    thread = nullptr;

    delete emitter;
    emitter = nullptr;

    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    qInfo("[VBAN Emitter] Destroyed successfully.");
}

Emitter::Emitter(QObject *parent) : QUdpSocket(parent) {
    connect(this, &Emitter::sendSignal, this, &Emitter::handleSend);
}

Emitter::~Emitter() {
    SDL_PauseAudioDevice(audioDeviceId, SDL_TRUE);
    SDL_CloseAudioDevice(audioDeviceId);

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
    audioDeviceId = SDL_OpenAudioDevice(name, SDL_TRUE, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    qInfo("[VBAN Emitter] Obtained AudioSpec: AudioDeviceId[%d] freq[%d] "
          "format[%X] channels[%d] silence[%d] samples[%d] padding[%d] size[%d]",
          audioDeviceId, obtained.freq, obtained.format, obtained.channels,
          obtained.silence, obtained.samples, obtained.padding, obtained.size);

    if (audioDeviceId == 0)
        return false;

    memcpy(res.header.vban, "VBAN", 4);
    res.header.format_SR =
        (static_cast<uint8_t>(Header::sampleRateMap[obtained.freq]) & Header::VBAN_SR_MASK) |
        (static_cast<uint8_t>(Header::VBanProtocol::VBAN_PROTOCOL_AUDIO) & Header::VBAN_PROTOCOL_MASK);
    res.header.format_nbc = obtained.channels - 1;
    res.header.format_bit =
        (static_cast<uint8_t>(Header::formatMap[obtained.format]) & Header::VBAN_BIT_RESOLUTION_MASK) |
        (static_cast<uint8_t>(Header::VBanCodec::VBAN_CODEC_PCM) & Header::VBAN_CODEC_MASK);
    strcpy_s(res.header.streamname, STREAM_NAME);

    for (uint16_t div = 1; div <= obtained.samples; ++div) {
        if (obtained.samples % div == 0 && obtained.samples / div - 1 <= 0xFF
            && obtained.size % div == 0 && obtained.size / div <= sizeof(res.data)) {
            res.header.format_nbs = obtained.samples / div - 1;
            bufSize = obtained.size / div;
            break;
        }
    }

    clientAddress = addr;
    clientPort = port;

    SDL_PauseAudioDevice(audioDeviceId, SDL_FALSE);
    return true;
}

void Emitter::handleSend(const QByteArray& data) {
    if (clientAddress.isNull() || clientPort == 0)
        return;

    for (int index = 0; index < data.size(); index += bufSize) {
        memcpy(res.data, data.mid(index), bufSize);
        ++res.header.nuFrame;
        writeDatagram(reinterpret_cast<char *>(&res), sizeof(Header) + bufSize, clientAddress, clientPort);
    }
}

void Emitter::send(void*, uint8_t* stream, int len) {
    if (!emitter)
        return;

    emit emitter->sendSignal(QByteArray(reinterpret_cast<char *>(stream), len));
}

Emitter* Emitter::emitter = nullptr;
QThread* Emitter::thread = nullptr;

}
