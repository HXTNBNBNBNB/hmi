#pragma once

#include <chrono>

class FpsCounter {
public:
    explicit FpsCounter(double targetFps = 60.0);

    // Call at the start of frame; returns true when FPS value is updated
    bool beginFrame();

    // Call at the end of frame; sleeps to enforce frame rate limit
    void endFrame();

    double fps() const { return fps_; }
    double frameTime() const { return frameTime_; }

    void setTargetFps(double target);
    double targetFps() const { return targetFps_; }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    double targetFps_;
    double targetFrameTime_;  // seconds

    double fps_ = 0.0;
    double frameTime_ = 0.0;

    // For FPS calculation (averaged over interval)
    int frameCount_ = 0;
    TimePoint intervalStart_;

    // For frame limiting
    TimePoint frameStart_;

    static constexpr double kUpdateInterval = 0.5; // update FPS display every 0.5s
};
