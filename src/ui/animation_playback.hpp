#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace ui {
// Editor-only transport clock. It has no reference to the model or any canvas.
// Evaluation subscribes to time_changed without changing strip timing.
class animation_playback : public QObject {
    Q_OBJECT
public:
    explicit animation_playback(QObject* parent = nullptr);
    void set_duration(double seconds);
    void play();
    void pause();
    // Absolute seek: reject nonfinite values; clamp finite values to the clip.
    // Seeking always pauses and publishes exactly one requested-time update.
    bool seek(double seconds);
    // Stop at the last published time without emitting a tick (failure/edit exit).
    void hold();
    void stop();
    void go_to_end();
    double time() const { return time_; }
    double duration() const { return duration_; }
    bool playing() const { return timer_.isActive(); }
signals:
    void time_changed(double seconds);
    void playing_changed(bool playing);
    // Emitted only when running playback reaches its natural end (after last sample).
    void finished();
private:
    QTimer timer_;
    QElapsedTimer elapsed_;
    double duration_ = 0;
    double time_ = 0;
    double anchor_ = 0;
    void tick();
};
}
