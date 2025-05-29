#include "animation_controller.h"
#include "../core/sm_skeleton.h"
#include "../core/sm_animation.h"
#include "canvas/canvas_item.h"
#include "canvas/scene.h"
#include "canvas/skel_item.h"

ui::animation_controller::animation_controller(QObject* parent) : 
        QObject(parent), skel_(nullptr), anim_(nullptr) {
    connect(&animation_timer_, &QTimer::timeout, this, &animation_controller::on_timer_tick);
}

void ui::animation_controller::clear() {
    skel_ = nullptr;
    anim_ = nullptr;
}

void ui::animation_controller::set(sm::skeleton& skel, const sm::animation& anim) {
    skel_ = &skel;
    anim_ = &anim;
}

void ui::animation_controller::start_playback() {
    current_time_ms_ = 0;
    animation_timer_.start(timestep_ms_);
}

void ui::animation_controller::stop_playback() {
    animation_timer_.stop();
}

void ui::animation_controller::on_timer_tick() {
    anim_->perform_timestep( *skel_, current_time_ms_, timestep_ms_);
    current_time_ms_ += timestep_ms_;

    auto& skel_item = canvas::item_from_model<canvas::item::skeleton>( *skel_ );
    skel_item.canvas()->sync_to_model();
}