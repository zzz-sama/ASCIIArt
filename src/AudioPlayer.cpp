#include "AudioPlayer.h"

#include "Localization.h"
#include "Util.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kFirstAudioStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr DWORD kMediaSource =
    static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE);
constexpr std::size_t kMaxQueuedChunks = 32;
constexpr std::size_t kMaxQueuedBytes = 16ull * 1024ull * 1024ull;

bool ConfigureAudioReader(IMFSourceReader* reader, WAVEFORMATEX& format,
                          std::wstring& error) {
    error.clear();
    HRESULT hr = reader->SetStreamSelection(
        static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot deselect the other streams in the audio source: ",
                                L"无法关闭音频源中的其他流：")) +
                util::FormatHResult(hr);
        return false;
    }
    hr = reader->SetStreamSelection(kFirstAudioStream, TRUE);
    if (FAILED(hr)) {
        return false;  // no audio stream
    }

    ComPtr<IMFMediaType> native;
    hr = reader->GetNativeMediaType(kFirstAudioStream, 0, &native);
    if (FAILED(hr)) {
        return false;  // no audio stream
    }

    UINT32 channels =
        MFGetAttributeUINT32(native.Get(), MF_MT_AUDIO_NUM_CHANNELS, 2);
    UINT32 sampleRate =
        MFGetAttributeUINT32(native.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND,
                             48000);
    if (channels == 0 || channels > 2 || sampleRate == 0) {
        error = Tr(L"The audio channel count or sample rate is not supported "
                   L"(mono/stereo only).",
                   L"音频声道数或采样率不受支持（当前支持单声道/立体声）。");
        return false;
    }

    ComPtr<IMFMediaType> pcm;
    hr = MFCreateMediaType(&pcm);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot create the audio output format: ",
                                L"无法创建音频输出格式：")) +
                util::FormatHResult(hr);
        return false;
    }
    pcm->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcm->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
    pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                    sampleRate * channels * 2);

    hr = reader->SetCurrentMediaType(kFirstAudioStream, nullptr, pcm.Get());
    if (FAILED(hr)) {
        ComPtr<IMFMediaType> partial;
        MFCreateMediaType(&partial);
        partial->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        partial->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        hr = reader->SetCurrentMediaType(kFirstAudioStream, nullptr,
                                         partial.Get());
    }
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot convert the audio to PCM: ",
                                L"无法把音频转换为 PCM 格式：")) +
                util::FormatHResult(hr);
        return false;
    }

    ComPtr<IMFMediaType> current;
    hr = reader->GetCurrentMediaType(kFirstAudioStream, &current);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot read the audio output format: ",
                                L"无法读取音频输出格式：")) +
                util::FormatHResult(hr);
        return false;
    }

    channels = MFGetAttributeUINT32(current.Get(), MF_MT_AUDIO_NUM_CHANNELS, 0);
    sampleRate =
        MFGetAttributeUINT32(current.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    const UINT32 bits =
        MFGetAttributeUINT32(current.Get(), MF_MT_AUDIO_BITS_PER_SAMPLE, 0);
    const UINT32 blockAlign =
        MFGetAttributeUINT32(current.Get(), MF_MT_AUDIO_BLOCK_ALIGNMENT, 0);
    if (channels == 0 || channels > 2 || sampleRate == 0 || bits != 16 ||
        blockAlign == 0) {
        error = Tr(L"The audio decoder produced an unsupported PCM format.",
                   L"音频解码器输出了不支持的 PCM 格式。");
        return false;
    }

    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(channels);
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(blockAlign);
    format.nAvgBytesPerSec = sampleRate * blockAlign;
    format.cbSize = 0;
    return true;
}

}  // namespace

struct AudioPlayer::Chunk {
    explicit Chunk(std::vector<std::uint8_t>&& bytes) : data(std::move(bytes)) {}
    std::vector<std::uint8_t> data;
};

AudioPlayer::~AudioPlayer() { Close(); }

unsigned long long AudioPlayer::SamplesPlayed() const {
    if (source_ == nullptr) {
        return 0;
    }
    XAUDIO2_VOICE_STATE state{};
    source_->GetState(&state, 0);
    return state.SamplesPlayed;
}

bool AudioPlayer::ProbeFormat(const std::wstring& path, WAVEFORMATEX& format,
                              double& duration, std::wstring& error) {
    error.clear();
    duration = 0.0;

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot open the audio source: ",
                                L"无法打开音频源：")) +
                util::FormatHResult(hr);
        return false;
    }
    if (!ConfigureAudioReader(reader.Get(), format, error)) {
        return false;
    }

    const DWORD durationSources[] = {kFirstAudioStream, kMediaSource};
    for (const DWORD source : durationSources) {
        PROPVARIANT value;
        PropVariantInit(&value);
        hr = reader->GetPresentationAttribute(source, MF_PD_DURATION, &value);
        if (SUCCEEDED(hr)) {
            if (value.vt == VT_UI8) {
                duration =
                    static_cast<double>(value.uhVal.QuadPart) / 10000000.0;
            } else if (value.vt == VT_I8) {
                duration =
                    static_cast<double>(value.hVal.QuadPart) / 10000000.0;
            }
        }
        PropVariantClear(&value);
        if (duration > 0.0) {
            break;
        }
    }
    return true;
}

bool AudioPlayer::Open(const std::wstring& path, std::wstring& error) {
    Close();
    error.clear();
    path_ = path;

    WAVEFORMATEX format{};
    double duration = 0.0;
    if (!ProbeFormat(path, format, duration, error)) {
        if (error.empty()) {
            path_.clear();
        }
        return false;
    }
    format_ = format;
    duration_ = duration;

    HRESULT hr = XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot initialize XAudio2: ",
                                L"无法初始化 XAudio2：")) +
                util::FormatHResult(hr);
        Close();
        return false;
    }
    hr = engine_->CreateMasteringVoice(&mastering_);
    if (FAILED(hr)) {
        error = std::wstring(
                    Tr(L"Cannot create the default audio output device: ",
                       L"无法创建默认音频输出设备：")) +
                util::FormatHResult(hr);
        Close();
        return false;
    }
    hr = engine_->CreateSourceVoice(&source_, &format_, 0,
                                    XAUDIO2_DEFAULT_FREQ_RATIO, this);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot create the audio playback voice: ",
                                L"无法创建音频播放声道：")) +
                util::FormatHResult(hr);
        Close();
        return false;
    }
    return true;
}

void AudioPlayer::OnBufferEnd(void* context) {
    if (context == nullptr) {
        return;
    }
    Chunk* chunk = static_cast<Chunk*>(context);
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = chunks_.find(chunk);
        if (it != chunks_.end()) {
            chunks_.erase(it);
            queuedBytes_ -= chunk->data.size();
            found = true;
        }
    }
    if (found) {
        delete chunk;
    }
    cv_.notify_all();
}

bool AudioPlayer::SubmitChunk(std::vector<std::uint8_t>&& data) {
    if (source_ == nullptr || data.empty()) {
        return false;
    }
    if (data.size() > 0xFFFFFFFFu) {
        return false;
    }

    Chunk* chunk = new Chunk(std::move(data));
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(chunk->data.size());
    buffer.pAudioData = chunk->data.data();
    buffer.pContext = chunk;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        chunks_.insert(chunk);
        queuedBytes_ += chunk->data.size();
    }

    const HRESULT hr = source_->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = chunks_.find(chunk);
            if (it != chunks_.end()) {
                chunks_.erase(it);
                queuedBytes_ -= chunk->data.size();
                found = true;
            }
        }
        if (found) {
            delete chunk;
        }
        return false;
    }
    return true;
}

void AudioPlayer::WaitForQueueSpace(std::unique_lock<std::mutex>& lock) {
    cv_.wait(lock, [this] {
        return stop_ ||
               (!paused_ && chunks_.size() < kMaxQueuedChunks &&
                queuedBytes_ < kMaxQueuedBytes);
    });
}

void AudioPlayer::WorkerLoop() {
    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitialized = SUCCEEDED(comResult);

    auto finishStartup = [this](bool ok, const std::wstring& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        workerReady_ = true;
        workerOk_ = ok;
        workerError_ = error;
        cv_.notify_all();
    };

    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path_.c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        finishStartup(false,
                      std::wstring(Tr(L"Cannot open the audio source: ",
                                      L"无法打开音频源：")) +
                          util::FormatHResult(hr));
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    WAVEFORMATEX actual{};
    std::wstring error;
    if (!ConfigureAudioReader(reader.Get(), actual, error)) {
        finishStartup(false,
                      error.empty() ? std::wstring(Tr(L"Cannot read the audio track.",
                                                      L"无法读取音频轨道。"))
                                    : error);
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }
    if (actual.nChannels != format_.nChannels ||
        actual.nSamplesPerSec != format_.nSamplesPerSec ||
        actual.wBitsPerSample != format_.wBitsPerSample) {
        finishStartup(false, Tr(L"The audio output format changed.",
                                L"音频输出格式发生了变化。"));
        if (comInitialized) {
            CoUninitialize();
        }
        return;
    }

    finishStartup(true, L"");

    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            WaitForQueueSpace(lock);
            if (stop_) {
                break;
            }
            if (paused_) {
                continue;
            }
        }

        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(kFirstAudioStream, 0, nullptr, &flags,
                                &timestamp, &sample);
        if (FAILED(hr)) {
            std::lock_guard<std::mutex> lock(mutex_);
            workerOk_ = false;
            workerError_ = std::wstring(
                               Tr(L"Failed to read an audio frame: ",
                                  L"读取音频帧失败：")) +
                           util::FormatHResult(hr);
            break;
        }
        (void)timestamp;
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
        if (!sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) {
            continue;
        }
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &length))) {
            continue;
        }
        std::vector<std::uint8_t> bytes;
        if (length > 0) {
            bytes.resize(length);
            std::memcpy(bytes.data(), data, length);
        }
        buffer->Unlock();

        if (bytes.empty()) {
            continue;
        }
        if (!SubmitChunk(std::move(bytes))) {
            std::lock_guard<std::mutex> lock(mutex_);
            workerOk_ = false;
            workerError_ = Tr(L"Cannot submit the audio playback buffer.",
                              L"无法提交音频播放缓冲区。");
            break;
        }
    }

    if (comInitialized) {
        CoUninitialize();
    }
}

bool AudioPlayer::Start(std::wstring& error) {
    error.clear();
    if (source_ == nullptr) {
        error = Tr(L"There is no audio to play.", L"没有可播放的音频。");
        return false;
    }

    bool resume = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            if (!paused_) {
                return true;
            }
            paused_ = false;
            resume = true;
        }
    }
    if (resume) {
        if (FAILED(source_->Start(0))) {
            error = Tr(L"Cannot resume audio playback.",
                       L"无法恢复音频播放。");
            return false;
        }
        cv_.notify_all();
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = false;
        paused_ = false;
        workerReady_ = false;
        workerOk_ = false;
        workerError_.clear();
    }
    source_->FlushSourceBuffers();
    worker_ = std::thread(&AudioPlayer::WorkerLoop, this);
    if (FAILED(source_->Start(0))) {
        error = Tr(L"Cannot start audio output.", L"无法启动音频输出。");
        Stop();
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_.wait_for(lock, std::chrono::seconds(3),
                          [this] { return workerReady_; })) {
            lock.unlock();
            error = Tr(L"Audio initialization timed out.",
                       L"音频初始化超时。");
            Stop();
            return false;
        }
        if (!workerOk_) {
            error = workerError_.empty()
                        ? std::wstring(Tr(L"Audio initialization failed.",
                                          L"音频初始化失败。"))
                        : workerError_;
            lock.unlock();
            Stop();
            return false;
        }
    }

    started_ = true;
    return true;
}

void AudioPlayer::Pause() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || paused_) {
            return;
        }
        paused_ = true;
    }
    if (source_ != nullptr) {
        source_->Stop(0);
    }
    cv_.notify_all();
}

void AudioPlayer::Resume() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || !paused_) {
            return;
        }
        paused_ = false;
    }
    if (source_ != nullptr) {
        source_->Start(0);
    }
    cv_.notify_all();
}

void AudioPlayer::Restart() {
    std::wstring ignored;
    Stop();
    Start(ignored);

}

void AudioPlayer::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        paused_ = false;
        started_ = false;
    }
    cv_.notify_all();
    if (source_ != nullptr) {
        source_->Stop(0);
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    if (source_ != nullptr) {
        source_->FlushSourceBuffers();
    }

    std::set<Chunk*> leftovers;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(500),
                     [this] { return chunks_.empty(); });
        leftovers.swap(chunks_);
        queuedBytes_ = 0;
        workerReady_ = false;
        workerOk_ = false;
    }
    for (Chunk* chunk : leftovers) {
        delete chunk;
    }
    started_ = false;
    paused_ = false;
}

void AudioPlayer::Close() {
    Stop();
    if (source_ != nullptr) {
        source_->DestroyVoice();
        source_ = nullptr;
    }
    if (mastering_ != nullptr) {
        mastering_->DestroyVoice();
        mastering_ = nullptr;
    }
    engine_.Reset();
    path_.clear();
    duration_ = 0.0;
    format_ = {};
}
