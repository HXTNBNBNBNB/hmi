#ifndef AUDIO_PLAYER_HPP
#define AUDIO_PLAYER_HPP

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <chrono>

/**
 * @brief 音频播放器类
 * 提供后台音频播放功能，支持优先级抢占机制
 */
class AudioPlayer {
public:
    /**
     * @brief 音频优先级枚举
     */
    enum class Priority {
        LOW = 1,      ///< 低优先级
        MEDIUM = 2,   ///< 中优先级
        HIGH = 3      ///< 高优先级
    };

    /**
     * @brief 获取单例实例
     * @return AudioPlayer实例引用
     */
    static AudioPlayer& getInstance();

    /**
     * @brief 初始化音频播放器
     * @return 初始化是否成功
     */
    bool initialize();

    /**
     * @brief 播放指定优先级的音频文件
     * @param priority 音频优先级
     * @param playOnce 是否只播放一次
     * @return 播放请求是否被接受
     */
    bool playAudio(Priority priority, bool playOnce = true);

    /**
     * @brief 播放指定编号的音频文件
     * @param audioNumber 音频文件编号(1-3)
     * @param playOnce 是否只播放一次
     * @return 播放请求是否被接受
     */
    bool playAudio(int audioNumber, bool playOnce = true);

    /**
     * @brief 按顺序播放多个音频文件（拼接播放）
     * @param audioNumbers 音频文件编号序列，如 {4, 6, 8} 表示依次播放 4.ogg、6.ogg、8.ogg
     * @param priority 任务优先级，用于抢占与排队
     * @param playOnce 是否只播放一次
     * @return 播放请求是否被接受
     */
    bool playAudioSequence(const std::vector<int>& audioNumbers,
                          Priority priority = Priority::MEDIUM,
                          bool playOnce = true);

    /**
     * @brief 停止当前播放
     */
    void stop();

    /**
     * @brief 设置音频文件基础路径
     * @param basePath 音频文件所在目录路径
     */
    void setAudioBasePath(const std::string& basePath);

    /**
     * @brief 获取当前是否正在播放
     * @return 是否正在播放
     */
    bool isPlaying() const;

    /**
     * @brief 获取当前播放的音频优先级
     * @return 当前播放的优先级
     */
    Priority getCurrentPriority() const;

    /**
     * @brief 清理资源
     */
    void cleanup();

private:
    /**
     * @brief 音频播放任务结构
     */
    struct AudioTask {
        Priority priority;                   ///< 播放优先级
        std::vector<std::string> filenames;  ///< 待播放文件列表，按顺序播放
        bool playOnce;                       ///< 是否只播放一次

        AudioTask(Priority p, const std::string& f, bool once)
            : priority(p), filenames{f}, playOnce(once) {}

        AudioTask(Priority p, const std::vector<std::string>& files, bool once)
            : priority(p), filenames(files), playOnce(once) {}

        // 优先级比较，数字大的优先级高
        bool operator<(const AudioTask& other) const {
            return priority < other.priority;
        }
    };

    // 单例相关
    AudioPlayer();
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // 内部方法
    void audioThreadFunction();
    bool configurePCMForFormat(void* pcm, unsigned int rate, int channels);
    void reinitPCMHandle();
    void stopPlayback();

    // 成员变量
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::priority_queue<AudioTask> taskQueue_;
    std::thread audioThread_;
    std::atomic<bool> running_;
    std::atomic<bool> playing_;
    std::atomic<Priority> currentPriority_;
    std::string audioBasePath_;

    // ALSA相关
    void* pcmHandle_;  // snd_pcm_t*
};

#endif // AUDIO_PLAYER_HPP