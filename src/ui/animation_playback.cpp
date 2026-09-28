#include "animation_playback.hpp"
#include <algorithm>
#include <cmath>

ui::animation_playback::animation_playback(QObject* parent) : QObject(parent) {
    timer_.setInterval(16);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &animation_playback::tick);
}

void ui::animation_playback::set_duration(double seconds) {
    duration_ = std::isfinite(seconds) ? std::max(0.0, seconds) : 0;
    stop();
}

void ui::animation_playback::play() {
    if (playing() || duration_ <= 0) return;
    if (time_ >= duration_) time_ = 0;
    anchor_ = time_;
    elapsed_.start();
    timer_.start();
    emit playing_changed(true);
    emit time_changed(time_);
}

void ui::animation_playback::tick() {
    // Sample a monotonic clock, never accumulate timer intervals or paint calls.
    time_ = std::min(duration_, anchor_ + elapsed_.nsecsElapsed() / 1e9);
    if (time_ >= duration_) {
        timer_.stop();
        emit playing_changed(false);
    }
    emit time_changed(time_);
}


bool ui::animation_playback::seek(double seconds) {
    if (!std::isfinite(seconds)) return false;
    const bool was_playing = playing();
    timer_.stop();
    if (was_playing) emit playing_changed(false);
    time_ = std::clamp(seconds, 0.0, duration_);
    anchor_ = time_;
    emit time_changed(time_);
    return true;
}

void ui::animation_playback::pause() {
    if (!playing()) return;
    tick();
    if (playing()) {
        timer_.stop();
        emit playing_changed(false);
    }
}

void ui::animation_playback::hold() {
    const bool was_playing = playing();
    timer_.stop();
    if (was_playing) emit playing_changed(false);
}

void ui::animation_playback::stop() {
    const bool was_playing = playing();
    timer_.stop();
    time_ = 0;
    if (was_playing) emit playing_changed(false);
    emit time_changed(time_);
}

void ui::animation_playback::go_to_end() {
    stop();
    time_ = duration_;
    emit time_changed(time_);
}
