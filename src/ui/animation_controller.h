#pragma once

#include <QObject>
#include <QTimer>
#include "../core/sm_animation.h"

/*------------------------------------------------------------------------------------------------*/

namespace sm {
    class skeleton;
    class animation;
}

namespace ui {

    class animation_controller : public QObject {
        Q_OBJECT

    public:
        animation_controller(QObject* parent = nullptr);

        void clear();
        void set(sm::skeleton& skel, const sm::animation& anim);

        void start_playback();
        void stop_playback();

    private slots:
        void on_timer_tick();

    private:
        QTimer animation_timer_;
        sm::skeleton* skel_;
        //const sm::animation* anim_;
        sm::baked_animation animation_;
        int current_time_ms_ = 0;
        int timestep_ms_ = 16; // ~60 FPS
    };

} 