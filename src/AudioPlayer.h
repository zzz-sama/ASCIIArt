#pragma once

#include "Common.h"

#include <wrl/client.h>
#include <xaudio2.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Streams the audio track of a media file: Media Foundation decodes PCM in a
// worker thread and XAudio2 plays it. Only a small bounded queue is kept in
// memory, so long audio tracks no longer need to be preloaded.
class AudioPlayer : private IXAudio2VoiceCallback {
public:
    AudioPlayer() = default;
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Returns false when the file has no audio stream (error stays empty) or
    // when audio could not be initialized (error contains a reason).
    bool Open(const std::wstring& path, std::wstring& error);
    bool HasAudio() const { return source_ != nullptr; }
    double Duration() const { return duration_; }

    bool Start(std::wstring& error);
    void Pause();
    void Resume();
    void Restart();
    void Stop();
    void Close();

    // Position feedback, mainly useful for diagnostics.
    unsigned long long SamplesPlayed() const;

private:
    struct Chunk;

    bool ProbeFormat(const std::wstring& path, WAVEFORMATEX& format,
                     double& duration, std::wstring& error);
    void WorkerLoop();
    bool SubmitChunk(std::vector<std::uint8_t>&& data);
    void WaitForQueueSpace(std::unique_lock<std::mutex>& lock);

    void OnVoiceProcessingPassStart(UINT32) override {}
    void OnVoiceProcessingPassEnd() override {}
    void OnStreamEnd() override {}
    void OnBufferStart(void*) override {}
    void OnBufferEnd(void* context) override;
    void OnLoopEnd(void*) override {}
    void OnVoiceError(void*, HRESULT) override {}

    std::wstring path_;
    double duration_ = 0.0;
    WAVEFORMATEX format_{};

    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* mastering_ = nullptr;
    IXAudio2SourceVoice* source_ = nullptr;

    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::set<Chunk*> chunks_;
    std::size_t queuedBytes_ = 0;
    bool stop_ = false;
    bool paused_ = false;
    bool started_ = false;
    bool workerReady_ = false;
    bool workerOk_ = false;
    std::wstring workerError_;
};
