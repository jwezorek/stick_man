#pragma once
#include "../../core/sm_animation.hpp"
#include "pose_strip_layout.hpp"
#include <QPixmap>
#include <QWidget>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>

namespace mdl { class project; }
namespace ui::canvas { class manager; }

namespace ui {
class pose_strip : public QWidget {
    Q_OBJECT
public:
    explicit pose_strip(QWidget* parent = nullptr);
    void set_context(mdl::project* project, canvas::manager* canvases,
        sm::object_id character, sm::object_id animation);
    bool refresh();
    void set_playback_time(double seconds);
    void set_selected_transition(std::optional<sm::object_id> id);
    QPixmap render_preview(sm::object_id keyframe);
    QSize minimumSizeHint() const override;
signals:
    void keyframe_selected(sm::object_id id);
    void transition_selected(sm::object_id id);
    void scrub_requested(double seconds);
    void transition_duration_requested(sm::object_id id, double seconds);
    void playback_focus_changed(QRectF region);
protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
private:
    mdl::project* project_ = nullptr;
    canvas::manager* canvases_ = nullptr;
    sm::object_id character_;
    sm::object_id animation_;
    std::unordered_map<sm::object_id, QPixmap> thumbnails_;
    std::optional<bool> cached_artwork_;
    std::optional<bool> cached_bones_;
    std::string cached_appearance_;
    std::string cached_states_;

    pose_strip_layout layout_;
    double playback_time_ = 0;
    std::optional<sm::object_id> selected_transition_;
    bool scrubbing_ = false;
    struct duration_drag {
        sm::object_id id;
        std::size_t index;
        bool from_left;
        double press_x;
        double original_duration;
        double proposed_duration;
        pose_strip_layout original_layout;
    };
    std::optional<duration_drag> duration_drag_;
    // Which end, if any, is currently under the pointer.
    std::optional<std::pair<std::size_t, bool>> resize_handle_at(QPointF point) const;
    void update_duration_drag(double x);
    void finish_duration_drag(bool commit);
    QPixmap thumbnail(const sm::pose_keyframe& keyframe);
};
}
