#include "FpsCounter.hpp"
#include <thread>

FpsCounter::FpsCounter(double targetFps)
    : targetFps_(targetFps)
    , targetFrameTime_(1.0 / targetFps)
    , intervalStart_(Clock::now())
    , frameStart_(Clock::now())
    , lastFrameTime_(Clock::now())
{
}

void FpsCounter::setTargetFps(double target) {
    targetFps_ = target;
    targetFrameTime_ = 1.0 / target;
}

bool FpsCounter::beginFrame() {
    frameStart_ = Clock::now();
    if (firstFrame_) {
        deltaTime_ = targetFrameTime_;
        lastFrameTime_ = frameStart_;
        firstFrame_ = false;
    } else {
        deltaTime_ = std::chrono::duration<double>(frameStart_ - lastFrameTime_).count();
        lastFrameTime_ = frameStart_;
        if (deltaTime_ <= 0.0 || deltaTime_ > 0.5) deltaTime_ = targetFrameTime_;
    }
    frameCount_++;

    auto elapsed = std::chrono::duration<double>(frameStart_ - intervalStart_).count();
    if (elapsed >= kUpdateInterval) {
        fps_ = frameCount_ / elapsed;
        frameTime_ = elapsed / frameCount_ * 1000.0; // ms
        frameCount_ = 0;
        intervalStart_ = frameStart_;
        return true;
    }
    return false;
}

void FpsCounter::endFrame() {
    auto now = Clock::now();
    double elapsed = std::chrono::duration<double>(now - frameStart_).count();
    double remaining = targetFrameTime_ - elapsed;
    if (remaining > 0.001) {
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<int64_t>(remaining * 1e6)));
    }
}
