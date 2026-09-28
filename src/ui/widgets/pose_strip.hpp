#pragma once
#include "../../core/sm_animation.hpp"
#include <QPixmap>
#include <QWidget>
#include <optional>
#include <string>
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
    void refresh();
    QPixmap render_preview(sm::object_id keyframe);
    QSize minimumSizeHint() const override;
signals:
    void keyframe_selected(sm::object_id id);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
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

    QRect card_rect(std::size_t index) const;
    QPixmap thumbnail(const sm::pose_keyframe& keyframe);
};
}
