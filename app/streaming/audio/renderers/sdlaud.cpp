#include "sdl.h"

#include <Limelight.h>

SdlAudioRenderer::SdlAudioRenderer()
    : m_AudioDevice(0),
      m_AudioBuffer(nullptr),
      m_FrameSize(0),
      m_FrameDurationMs(0),
      m_StarvationCount(0)
{
    SDL_assert(!SDL_WasInit(SDL_INIT_AUDIO));

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s",
                     SDL_GetError());
        SDL_assert(SDL_WasInit(SDL_INIT_AUDIO));
    }
}

bool SdlAudioRenderer::prepareForPlayback(const OPUS_MULTISTREAM_CONFIGURATION* opusConfig)
{
    SDL_AudioSpec want, have;

    SDL_zero(want);
    want.freq = opusConfig->sampleRate;
    want.format = AUDIO_F32SYS;
    want.channels = opusConfig->channelCount;

    // On PulseAudio systems, setting a value too small can cause underruns for other
    // applications sharing this output device. We impose a floor of 480 samples (10 ms)
    // to mitigate this issue. Otherwise, we will buffer up to 3 frames of audio which
    // is 15 ms at regular 5 ms frames and 30 ms at 10 ms frames for slow connections.
    // The buffering helps avoid audio underruns due to network jitter.
    want.samples = SDL_max(480, opusConfig->samplesPerFrame * 3);

    m_FrameDurationMs = opusConfig->samplesPerFrame / (opusConfig->sampleRate / 1000);
    m_FrameSize = opusConfig->samplesPerFrame *
                  opusConfig->channelCount *
                  getAudioBufferSampleSize();

    // Always open the current default device. Do not pin a specific sink index —
    // HDMI/DP sinks are destroyed and recreated with new IDs when monitors power-cycle.
    m_AudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (m_AudioDevice == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to open audio device: %s",
                     SDL_GetError());
        return false;
    }

    m_AudioBuffer = SDL_malloc(m_FrameSize);
    if (m_AudioBuffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to allocate audio buffer");
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Desired audio buffer: %u samples (%u bytes)",
                want.samples,
                want.samples * want.channels * getAudioBufferSampleSize());

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Obtained audio buffer: %u samples (%u bytes)",
                have.samples,
                have.size);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "SDL audio driver: %s (device id %u)",
                SDL_GetCurrentAudioDriver(),
                (unsigned)m_AudioDevice);

    // Start playback
    SDL_PauseAudioDevice(m_AudioDevice, 0);

    m_StarvationCount = 0;
    return true;
}

SdlAudioRenderer::~SdlAudioRenderer()
{
    if (m_AudioDevice != 0) {
        // Stop playback
        SDL_PauseAudioDevice(m_AudioDevice, 1);
        SDL_CloseAudioDevice(m_AudioDevice);
        m_AudioDevice = 0;
    }

    if (m_AudioBuffer != nullptr) {
        SDL_free(m_AudioBuffer);
        m_AudioBuffer = nullptr;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_assert(!SDL_WasInit(SDL_INIT_AUDIO));
}

void* SdlAudioRenderer::getAudioBuffer(int*)
{
    return m_AudioBuffer;
}

bool SdlAudioRenderer::submitAudio(int bytesWritten)
{
    if (bytesWritten == 0) {
        // Nothing to do
        return true;
    }

    // Our device may enter a permanent error status upon removal (WASAPI, and
    // Pulse/PipeWire when SDL detects stream failure). Recreate the renderer so
    // we pick up the new default output device.
    SDL_AudioStatus status = SDL_GetAudioDeviceStatus(m_AudioDevice);
    if (status == SDL_AUDIO_STOPPED) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL audio device stopped; requesting reopen");
        return false;
    }

    // Don't queue if there's already more than 30 ms of audio data waiting
    // in Moonlight's audio queue.
    if (LiGetPendingAudioDuration() > 30) {
        return true;
    }

    // Provide backpressure on the queue to ensure too many frames don't build up
    // in SDL's audio queue, but don't wait forever to avoid a deadlock if the
    // audio device fails.
    //
    // On PipeWire/Pulse, destroying the HDMI/DP sink under a default-device open
    // often leaves the SDL device in PLAYING state with no live sink-input: the
    // queue stops draining but SDL never reports STOPPED. Detect that by watching
    // for a non-draining queue and force reopen.
    bool queueHasSpace = false;
    for (int i = 0; i < 100; i++) {
        if (SDL_GetAudioDeviceStatus(m_AudioDevice) == SDL_AUDIO_STOPPED) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SDL audio device stopped while waiting for queue space");
            return false;
        }

        // Only queue more samples where there is 50 ms or less in SDL's queue
        if (SDL_GetQueuedAudioSize(m_AudioDevice) / m_FrameSize * m_FrameDurationMs <= 50) {
            queueHasSpace = true;
            break;
        }

        SDL_Delay(1);
    }

    if (!queueHasSpace) {
        m_StarvationCount++;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SDL audio device not consuming data (starvation %d/%d, queued=%u bytes)",
                    m_StarvationCount,
                    kMaxStarvationCount,
                    (unsigned)SDL_GetQueuedAudioSize(m_AudioDevice));
        if (m_StarvationCount >= kMaxStarvationCount) {
            // Device is gone or wedged — Session will tear us down and reopen.
            return false;
        }
        // Drop this frame rather than grow the queue unboundedly while we wait
        // to confirm starvation.
        return true;
    }

    m_StarvationCount = 0;

    if (SDL_QueueAudio(m_AudioDevice, m_AudioBuffer, bytesWritten) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to queue audio sample: %s",
                     SDL_GetError());
        // Queue failure is treated as fatal so we re-open the output path.
        return false;
    }

    return true;
}

IAudioRenderer::AudioFormat SdlAudioRenderer::getAudioBufferFormat()
{
    return AudioFormat::Float32NE;
}
