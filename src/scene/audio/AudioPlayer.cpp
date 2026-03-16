#include "AudioPlayer.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <unistd.h>

// ALSA头文件
#include <alsa/asoundlib.h>

// OGG Vorbis头文件
#define STB_VORBIS_IMPLEMENTATION
#include "stb_vorbis.c"

AudioPlayer& AudioPlayer::getInstance() {
    static AudioPlayer instance;
    return instance;
}

AudioPlayer::AudioPlayer()
    : running_(false), playing_(false), currentPriority_(Priority::LOW), pcmHandle_(nullptr) {
    audioBasePath_ = "../resources/audio/";
}

AudioPlayer::~AudioPlayer() {
    cleanup();
}

bool AudioPlayer::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return true; // 已经初始化
    }

    // 初始化ALSA PCM设备
    int err;
    snd_pcm_t* handle;

    // 打开PCM设备用于回放（阻塞模式确保正确的播放节奏）
    err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "无法打开PCM设备: " << snd_strerror(err) << std::endl;
        return false;
    }

    // 设置初始硬件参数
    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    // 设置访问类型为交错模式
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);

    // 设置采样格式为16位有符号整数
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);

    // 设置声道数为立体声
    snd_pcm_hw_params_set_channels(handle, params, 2);

    // 设置采样率为48000Hz
    unsigned int sampleRate = 48000;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(handle, params, &sampleRate, &dir);

    // 设置合理的缓冲区参数
    snd_pcm_uframes_t period_size = 1024;
    snd_pcm_uframes_t buffer_size = 4096;
    snd_pcm_hw_params_set_period_size_near(handle, params, &period_size, &dir);
    snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_size);

    // 写入参数
    err = snd_pcm_hw_params(handle, params);
    if (err < 0) {
        std::cerr << "无法设置PCM硬件参数: " << snd_strerror(err) << std::endl;
        snd_pcm_close(handle);
        return false;
    }

    // 设置软件参数
    snd_pcm_sw_params_t* sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(handle, sw_params);
    snd_pcm_sw_params_set_avail_min(handle, sw_params, period_size);
    snd_pcm_sw_params_set_start_threshold(handle, sw_params, period_size);
    snd_pcm_sw_params(handle, sw_params);

    pcmHandle_ = handle;

    // 启动音频播放线程
    running_ = true;
    audioThread_ = std::thread(&AudioPlayer::audioThreadFunction, this);

    std::cout << "音频播放器初始化成功" << std::endl;
    return true;
}

void AudioPlayer::cleanup() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }

    condition_.notify_all();

    if (audioThread_.joinable()) {
        audioThread_.join();
    }

    if (pcmHandle_) {
        snd_pcm_close(static_cast<snd_pcm_t*>(pcmHandle_));
        pcmHandle_ = nullptr;
    }

    std::cout << "音频播放器已清理" << std::endl;
}

bool AudioPlayer::playAudio(Priority priority, bool playOnce) {
    int audioNumber = static_cast<int>(priority);
    return playAudio(audioNumber, playOnce);
}

bool AudioPlayer::playAudio(int audioNumber, bool playOnce) {
    std::string filename = audioBasePath_ + std::to_string(audioNumber) + ".ogg";

    // 检查文件是否存在
    std::ifstream file(filename);
    if (!file.good()) {
        std::cerr << "音频文件不存在: " << filename << std::endl;
        return false;
    }
    file.close();

    Priority priority = static_cast<Priority>(audioNumber);

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 拒绝条件（与 playAudioSequence 保持一致）
        if ((playing_ || !taskQueue_.empty()) && priority <= currentPriority_.load()) {
            return false;
        }

        // 高优先级抚占：打断当前播放并清空队列
        if (playing_) {
            stopPlayback();
            while (!taskQueue_.empty()) taskQueue_.pop();
        }

        // 添加新的播放任务
        taskQueue_.emplace(priority, std::vector<std::string>{filename}, playOnce);
        currentPriority_.store(priority);
    }

    condition_.notify_one();

    return true;
}

bool AudioPlayer::playAudioSequence(const std::vector<int>& audioNumbers,
                                   Priority priority,
                                   bool playOnce) {
    if (audioNumbers.empty()) {
        std::cerr << "[AudioPlayer] playAudioSequence: empty seq, skip" << std::endl;
        return false;
    }

    std::vector<std::string> filenames;
    std::string basePath;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        basePath = audioBasePath_;
    }

    for (int num : audioNumbers) {
        if(num == -1) continue; // -1表示这个位置没有有效的音频，跳过
        std::string filename = basePath + std::to_string(num) + ".ogg";
        std::ifstream file(filename);
        if (!file.good()) {
            std::cerr << "[AudioPlayer] 音频文件不存在，跳过: " << filename << std::endl;
            continue;  // 跳过缺失文件，继续播放其余文件
        }
        file.close();
        filenames.push_back(std::move(filename));
    }

    if (filenames.empty()) {
        std::cerr << "[AudioPlayer] playAudioSequence: 所有文件都不存在或被跳过" << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 拒绝条件：当前正在播放或队列中已有任务，且新任务优先级不高于当前优先级
        // 解决竞争： playing_=false 但 taskQueue_ 非空时，次帧同优先级报警不再重复入队
        if ((playing_ || !taskQueue_.empty()) && priority <= currentPriority_.load()) {
            return false;
        }

        if (playing_) {
            // 高优先级抚占：打断当前播放，同时清空尚未开始的低优先级任务
            stopPlayback();
            while (!taskQueue_.empty()) taskQueue_.pop();
        }

        taskQueue_.emplace(priority, filenames, playOnce);
        currentPriority_.store(priority);
    }

    condition_.notify_one();
    return true;
}

void AudioPlayer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (playing_) {
        stopPlayback();
    }
}

void AudioPlayer::setAudioBasePath(const std::string& basePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    audioBasePath_ = basePath;
    // 确保路径以斜杠结尾
    if (!audioBasePath_.empty() && audioBasePath_.back() != '/') {
        audioBasePath_ += '/';
    }
}

bool AudioPlayer::isPlaying() const {
    return playing_;
}

AudioPlayer::Priority AudioPlayer::getCurrentPriority() const {
    return currentPriority_;
}

void AudioPlayer::audioThreadFunction() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this] {
            return !running_ || !taskQueue_.empty();
        });
        if (!running_) break;
        if (taskQueue_.empty()) continue;

        AudioTask task = taskQueue_.top();
        taskQueue_.pop();
        lock.unlock();

        playing_ = true;

        snd_pcm_t* pcm = static_cast<snd_pcm_t*>(pcmHandle_);
        unsigned int configuredRate    = 0;
        int          configuredChannels = 0;
        bool         alsaReady         = false;

        for (size_t fileIdx = 0; fileIdx < task.filenames.size(); ++fileIdx) {
            // 应用层退出：!running_ 要清理 PCM；!playing_ 是 stopPlayback() 打断，它已经做了 drop+prepare
            // 不要再额外 drop，否则会把 PREPARED 状态破坏为 SETUP，导致下一任务写入时再次 EBADFD
            if (!running_) {
                if (alsaReady && pcm) snd_pcm_drop(pcm);
                alsaReady = false;
                break;
            }
            if (!playing_) {
                // stopPlayback() 已调用 snd_pcm_drop + snd_pcm_prepare
                // PCM 处于 PREPARED 状态，下一任务可直接写入，不需重配置
                break;
            }

            const std::string& filename  = task.filenames[fileIdx];
            const bool         isLastFile = (fileIdx == task.filenames.size() - 1);

            if (!pcm) {
                std::cerr << "[AudioPlayer] pcm handle is null!" << std::endl;
                break;
            }

            // --- 打开 vorbis ---
            stb_vorbis* vorbis = stb_vorbis_open_filename(filename.c_str(), nullptr, nullptr);
            if (!vorbis) {
                std::cerr << "[AudioPlayer] 跳过（无法打开）: " << filename << std::endl;
                continue;
            }

            stb_vorbis_info info = stb_vorbis_get_info(vorbis);
            if (info.channels <= 0 || info.channels > 8) {
                std::cerr << "[AudioPlayer] 非法声道数: " << info.channels << std::endl;
                stb_vorbis_close(vorbis);
                continue;
            }

            const unsigned int thisRate     = info.sample_rate;
            const int          thisChannels = info.channels;
            const bool         fmtChanged   = (thisRate != configuredRate || thisChannels != configuredChannels);

            // --- 仅当格式发生变化时（或首次）才重新配置 ALSA ---
            if (!alsaReady || fmtChanged) {
                if (alsaReady && fmtChanged) {
                    // 切换格式前先冲洗已缓冲的数据
                    snd_pcm_drain(pcm);
                }
                if (!configurePCMForFormat(pcm, thisRate, thisChannels)) {
                    std::cerr << "[AudioPlayer] ALSA 配置失败，尝试重初始化设备" << std::endl;
                    stb_vorbis_close(vorbis);
                    reinitPCMHandle();
                    pcm = static_cast<snd_pcm_t*>(pcmHandle_);
                    if (!pcm || !configurePCMForFormat(pcm, thisRate, thisChannels)) {
                        std::cerr << "[AudioPlayer] 重初始化后仍配置失败，跳过该文件" << std::endl;
                        alsaReady = false;
                        continue;
                    }
                }
                configuredRate     = thisRate;
                configuredChannels = thisChannels;
                alsaReady          = true;
            }
            // 同格式文件：直接写入，不停止/不重配置 → 无缝连接

            // --- 解码 & 写入 PCM ---
            constexpr int BUFFER_SAMPLES = 2048;
            constexpr int MAX_CH         = 8;
            short buffer[BUFFER_SAMPLES * MAX_CH];
            float fbuf[BUFFER_SAMPLES * MAX_CH];
            memset(buffer, 0, sizeof(buffer));
            memset(fbuf,   0, sizeof(fbuf));

            int  frames_decoded = 0;
            bool fileHadError   = false;

            while (running_ && playing_ &&
                   (frames_decoded = stb_vorbis_get_samples_float_interleaved(
                       vorbis, thisChannels, fbuf, BUFFER_SAMPLES * thisChannels)) > 0) {

                const int total_samples = frames_decoded * thisChannels;
                for (int j = 0; j < total_samples; ++j) {
                    float s = fbuf[j];
                    if (s >  1.0f) s =  1.0f;
                    if (s < -1.0f) s = -1.0f;
                    buffer[j] = (short)(s * 32767.0f);
                }

                int written = snd_pcm_writei(pcm, buffer, frames_decoded);

                // stopPlayback() 从外部打断导致写入失败：直接退出，不做任何恢复操作
                // （如果做 drop/prepare 重试，会把现当旧序列的音频拼进 PREPARED 的 PCM）
                if (written < 0 && !playing_) break;
                if (written == -EPIPE) {
                    snd_pcm_prepare(pcm);
                    written = snd_pcm_writei(pcm, buffer, frames_decoded);
                } else if (written == -ESTRPIPE) {
                    int r;
                    while ((r = snd_pcm_resume(pcm)) == -EAGAIN) sleep(1);
                    if (r < 0) snd_pcm_prepare(pcm);
                    written = snd_pcm_writei(pcm, buffer, frames_decoded);
                } else if (written == -EBADFD) {
                    // PCM 处于错误状态（PulseAudio 流异常）
                    // 不要 close/reopen（会导致 PulseAudio 断言崩溃）
                    // 改为 drop+prepare 就地重置状态
                    std::cerr << "[AudioPlayer] PCM写入错误: " << snd_strerror(written) << std::endl;
                    snd_pcm_drop(pcm);
                    snd_pcm_prepare(pcm);
                    written = snd_pcm_writei(pcm, buffer, frames_decoded);
                    if (written < 0) {
                        // 仍失败：跳过该文件，下个文件重新配置 ALSA
                        stb_vorbis_close(vorbis);
                        alsaReady = false;
                        fileHadError = true;
                        break;
                    }
                } else if (written < 0) {
                    std::cerr << "[AudioPlayer] PCM写入错误: " << snd_strerror(written) << std::endl;
                    written = snd_pcm_recover(pcm, written, 0);
                    if (written < 0) {
                        // 标准恢复失败：跳过该文件，下个文件重新配置
                        // 不调用 reinitPCMHandle()，避免 PulseAudio 断言崩溃
                        std::cerr << "[AudioPlayer] 恢复失败，跳过当前文件，下个文件将重新配置 PCM" << std::endl;
                        stb_vorbis_close(vorbis);
                        alsaReady = false;
                        fileHadError = true;
                        break;
                    }
                    written = snd_pcm_writei(pcm, buffer, frames_decoded);
                }
            }

            if (!fileHadError) {
                stb_vorbis_close(vorbis);
                // 序列自然结束（playing_ 仍为 true）才 drain
                // stopPlayback() 打断时不 drain：PCM 已处于 PREPARED 状态，下一任务可直接写入
                if (isLastFile && alsaReady && pcm && playing_) {
                    snd_pcm_drain(pcm);
                }
            }
        }

        playing_ = false;

        if (task.playOnce) {
            std::lock_guard<std::mutex> lock2(mutex_);
            if (currentPriority_.load() == task.priority) {
                currentPriority_.store(Priority::LOW);
            }
        }
    }
}

bool AudioPlayer::configurePCMForFormat(void* pcmVoid, unsigned int rate, int channels) {
    snd_pcm_t* pcm = static_cast<snd_pcm_t*>(pcmVoid);
    snd_pcm_drop(pcm);
    snd_pcm_hw_free(pcm);

    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_any(pcm, params) < 0) return false;

    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, params, (unsigned int)channels);

    int dir = 0;
    snd_pcm_hw_params_set_rate_near(pcm, params, &rate, &dir);

    snd_pcm_uframes_t period_size = 1024;
    snd_pcm_uframes_t buffer_size = period_size * 8;
    snd_pcm_hw_params_set_period_size_near(pcm, params, &period_size, &dir);
    snd_pcm_hw_params_set_buffer_size_near(pcm, params, &buffer_size);

    int err = snd_pcm_hw_params(pcm, params);
    if (err < 0) {
        std::cerr << "[AudioPlayer] ALSA hw_params 失败: " << snd_strerror(err) << std::endl;
        return false;
    }

    snd_pcm_uframes_t actual_period = 0, actual_buffer = 0;
    snd_pcm_hw_params_get_period_size(params, &actual_period, &dir);
    snd_pcm_hw_params_get_buffer_size(params, &actual_buffer);

    // start_threshold 设为接近满缓冲区，保证 PulseAudio 稳定性
    // 序列内各文件无缝连接，可阶段性降低此阈値
    snd_pcm_sw_params_t* sw_params;
    snd_pcm_sw_params_alloca(&sw_params);
    snd_pcm_sw_params_current(pcm, sw_params);
    snd_pcm_sw_params_set_avail_min(pcm, sw_params, actual_period);
    snd_pcm_sw_params_set_start_threshold(pcm, sw_params, actual_buffer - actual_period);
    snd_pcm_sw_params(pcm, sw_params);

    snd_pcm_prepare(pcm);
    return true;
}

void AudioPlayer::reinitPCMHandle() {
    // 先置 null，防止并发的 stopPlayback() 使用已关闭的句柄
    void* oldHandle = pcmHandle_;
    pcmHandle_ = nullptr;

    if (oldHandle) {
        snd_pcm_close(static_cast<snd_pcm_t*>(oldHandle));
    }

    snd_pcm_t* handle = nullptr;
    int err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        std::cerr << "[AudioPlayer] reinit: 重新打开 PCM 失败: " << snd_strerror(err) << std::endl;
        return;
    }
    pcmHandle_ = handle;
    std::cout << "[AudioPlayer] PCM 设备重初始化成功" << std::endl;
}

void AudioPlayer::stopPlayback() {
    // 先置 playing_=false，再执行 PCM drop
    // 这样 snd_pcm_drop 打断阴塞的 writei 时，音频线程的
    // "if(written<0 && !playing_) break" 能立即捕捉到它，不会进入错误处理器
    playing_ = false;
    if (pcmHandle_) {
        snd_pcm_drop(static_cast<snd_pcm_t*>(pcmHandle_));
        snd_pcm_prepare(static_cast<snd_pcm_t*>(pcmHandle_));
    }
}