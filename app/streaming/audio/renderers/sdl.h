#pragma once

#include "renderer.h"
#include "SDL_compat.h"

class SdlAudioRenderer : public IAudioRenderer
{
public:
    SdlAudioRenderer();

    virtual ~SdlAudioRenderer();

    virtual bool prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig);

    virtual void* getAudioBuffer(int* size);

    virtual bool submitAudio(int bytesWritten);

    virtual AudioFormat getAudioBufferFormat();

private:
    // Number of consecutive submit attempts where SDL's queue failed to drain.
    // Used to detect a dead output path that never reports SDL_AUDIO_STOPPED
    // (common with PipeWire/Pulse when an HDMI/DP sink is destroyed mid-stream).
    static const int kMaxStarvationCount = 5;

    SDL_AudioDeviceID m_AudioDevice;
    void* m_AudioBuffer;
    Uint32 m_FrameSize;
    Uint32 m_FrameDurationMs;
    int m_StarvationCount;
};
