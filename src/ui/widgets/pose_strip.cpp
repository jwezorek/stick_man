#include "pose_strip.hpp"
#include "../canvas/artwork_layer.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../util.hpp"
#include "../../model/project.hpp"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPolygonF>
#include <QHelpEvent>
#include <QToolTip>
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <cmath>
#include <charconv>

namespace {
QTransform qt_matrix(const sm::matrix& m) {
    return {m(0, 0), m(1, 0), m(0, 1), m(1, 1), m(0, 2), m(1, 2)};
}

struct bounds {
    double left = 1e100;
    double right = -1e100;
    double bottom = 1e100;
    double top = -1e100;

    void add(sm::point p) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        bottom = std::min(bottom, p.y);
        top = std::max(top, p.y);
    }

    bool valid() const {
        return left <= right && bottom <= top;
    }
};

sm::topology posed_topology(const sm::character& character,
        const sm::pose_keyframe& keyframe) {
    sm::topology result;
    for (auto skeleton : character.rig().skeletons()) {
        auto copy = skeleton->copy_to(result);
        if (!copy) {
            throw std::runtime_error("thumbnail rig copy failed");
        }
    }
    sm::apply_skeletal_pose(keyframe.pose, result, character.rig().skeleton_ids());
    return result;
}
}

ui::pose_strip::pose_strip(QWidget* parent) : QWidget(parent) {
    setObjectName("pose_strip");
    setMinimumHeight(130);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ui::pose_strip::set_context(mdl::project* project, canvas::manager* canvases,
        sm::object_id character, sm::object_id animation) {
    project_ = project;
    canvases_ = canvases;
    character_ = character;
    animation_ = animation;
    playback_time_ = 0;
    playback_active_ = false;
    scrubbing_ = false;
    duration_drag_.reset();
    unsetCursor();
    refresh();
}

bool ui::pose_strip::refresh() {
    const sm::animation* animation = nullptr;
    if (project_) {
        auto character = project_->core().character(character_);
        if (character)
            animation = character->get().animation_data().find_animation(animation_);
    }
    pose_strip_layout next(animation);
    const bool timing_changed = !layout_.same_timing(next);
    layout_ = std::move(next);
    if (timing_changed)
        playback_time_ = 0;
    thumbnails_.clear();
    setMinimumWidth(int(std::ceil(layout_.width)));
    updateGeometry();
    update();
    return timing_changed;
}

void ui::pose_strip::set_playback_time(double seconds) {
    const auto previous = layout_.at_time(playback_time_);
    playback_time_ = seconds;
    update();
    if (const auto position = layout_.at_time(seconds)) {
        auto focus = QRectF(position->x, 4, 1, 122);
        if (!previous || previous->current_pose != position->current_pose || seconds == 0) {
            focus = focus.united(layout_.cards[position->current_pose].rect);
        }
        emit playback_focus_changed(focus);
    }
}

void ui::pose_strip::set_playback_active(bool active) {
    if (playback_active_ == active)
        return;
    playback_active_ = active;
    update();
}

void ui::pose_strip::set_selected_transition(std::optional<sm::object_id> id) {
    if (selected_transition_ == id)
        return;
    selected_transition_ = id;
    update();
}

QPixmap ui::pose_strip::render_preview(sm::object_id id) {
    if (!project_)
        return {};

    auto character = project_->core().character(character_);
    auto* animation = character ?
        character->get().animation_data().find_animation(animation_) : nullptr;
    auto* keyframe = animation ? animation->find_keyframe(id) : nullptr;
    return keyframe ? thumbnail(*keyframe) : QPixmap{};
}

QSize ui::pose_strip::minimumSizeHint() const {
    return {int(std::ceil(layout_.width)), pose_strip_layout::height};
}

QPixmap ui::pose_strip::thumbnail(const sm::pose_keyframe& keyframe) {
    if (auto it = thumbnails_.find(keyframe.id); it != thumbnails_.end()) {
        return it->second;
    }

    QPixmap pixmap(136, 82);
    pixmap.fill(Qt::transparent);
    if (!project_ || !canvases_)
        return pixmap;

    auto character_ref = project_->core().character(character_);
    if (!character_ref)
        return pixmap;
    const auto& character = character_ref->get();

    auto* animation = character.animation_data().find_animation(animation_);
    if (!animation)
        return pixmap;

    auto& layer = canvases_->active_canvas().artwork();
    const bool has_artwork = !character.artwork().appearances().empty();
    const bool show_artwork = layer.show_artwork() && has_artwork;
    const bool show_bones = layer.show_skeleton() || !has_artwork;
    const auto appearance = layer.active_appearance(character_);

    std::map<std::string, std::string> states;
    for (const auto& [slot, _] : character.artwork().slot_definitions()) {
        states.emplace(slot, layer.preview_state(character_, slot));
    }

    // All cards use one framing rectangle, independent of live canvas zoom or selection.
    bounds frame_bounds;
    for (const auto& frame : animation->keyframes) {
        auto topology = posed_topology(character, frame);
        for (auto skeleton : topology.skeletons()) {
            for (auto node : skeleton->nodes()) {
                frame_bounds.add(node->world_pos());
            }
        }

        if (show_artwork && !appearance.empty()) {
            for (const auto& sprite : project_->core().resolve_artwork(
                    character_, appearance, states, &topology)) {
                const double half_width = sprite.image.width() / 2.0;
                const double half_height = sprite.image.height() / 2.0;
                const std::array corners{
                    sm::point{-half_width, -half_height},
                    sm::point{half_width, -half_height},
                    sm::point{half_width, half_height},
                    sm::point{-half_width, half_height}
                };
                for (auto point : corners) {
                    frame_bounds.add(sm::transform(point, sprite.transform));
                }
            }
        }
    }

    if (!frame_bounds.valid()) {
        thumbnails_[keyframe.id] = pixmap;
        return pixmap;
    }

    const double width = std::max(1.0, frame_bounds.right - frame_bounds.left);
    const double height = std::max(1.0, frame_bounds.top - frame_bounds.bottom);
    const double scale = std::min(120.0 / (width * 1.15), 70.0 / (height * 1.15));
    const sm::point center{
        (frame_bounds.left + frame_bounds.right) / 2.0,
        (frame_bounds.bottom + frame_bounds.top) / 2.0
    };

    auto topology = posed_topology(character, keyframe);
    std::vector<sm::resolved_sprite> sprites;
    if (show_artwork && !appearance.empty()) {
        sprites = project_->core().resolve_artwork(character_, appearance, states, &topology);
    }

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QTransform world;
    world.translate(pixmap.width() / 2.0, pixmap.height() / 2.0);
    world.scale(scale, -scale);
    world.translate(-center.x, -center.y);
    painter.setWorldTransform(world);

    for (const auto& sprite : sprites) {
        const auto& image_resource = sprite.image;
        const auto stride = image_resource.height() > 1 ?
            image_resource.row(1).data() - image_resource.row(0).data() :
            image_resource.width() * 4;
        QImage image(image_resource.row(0).data(), image_resource.width(),
            image_resource.height(), qsizetype(stride), QImage::Format_RGBA8888);
        const auto transform = sprite.transform
            * sm::translation_matrix(-image_resource.width() / 2.0,
                image_resource.height() / 2.0)
            * sm::scale_matrix(1, -1);

        painter.save();
        painter.setWorldTransform(qt_matrix(transform), true);
        painter.drawImage(QPointF(0, 0), image);
        painter.restore();
    }

    if (show_bones) {
        painter.setPen(QPen(QColor(225, 225, 235), 1.5 / scale));
        painter.setBrush(QColor(225, 225, 235));
        for (auto skeleton : topology.skeletons()) {
            for (auto bone : skeleton->bones()) {
                auto [u, v] = bone->line_segment();
                painter.drawLine(QPointF(u.x, u.y), QPointF(v.x, v.y));
            }
            for (auto node : skeleton->nodes()) {
                painter.drawEllipse(QPointF(node->world_x(), node->world_y()),
                    2.3 / scale, 2.3 / scale);
            }
        }
    }

    painter.end();
    thumbnails_[keyframe.id] = pixmap;
    return pixmap;
}

bool ui::pose_strip::scrub_hit(QPointF point) const {
    if (layout_.cards.empty() || point.y() < 0)
        return false;
    // The entire ruler supports seeking. Give the playhead's triangular grip
    // an additional hit area below the ruler so it is easy to pick up.
    if (point.y() <= 20.0)
        return true;
    const auto position = layout_.at_time(playback_time_);
    return position && point.y() <= 26.0 &&
        std::abs(point.x() - position->x) <= 11.0;
}

std::optional<std::pair<std::size_t, bool>> ui::pose_strip::resize_handle_at(QPointF point) const {
    for (std::size_t i = 0; i < layout_.transitions.size(); ++i) {
        const auto& rect = layout_.transitions[i].rect;
        const double handle_width = std::min(7.0, rect.width() / 3.0);
        if (!rect.contains(point))
            continue;
        const double left_distance = point.x() - rect.left();
        const double right_distance = rect.right() - point.x();
        if (std::min(left_distance, right_distance) <= handle_width)
            return std::pair{i, left_distance <= right_distance};
    }
    return {};
}

void ui::pose_strip::update_duration_drag(double x) {
    if (!duration_drag_)
        return;
    const auto& drag = *duration_drag_;
    const double signed_delta = (x - drag.press_x) * (drag.from_left ? -1.0 : 1.0);
    const double scale = std::max(1.e-9, drag.original_layout.pixels_per_second);
    const double duration = std::clamp(drag.original_duration + signed_delta / scale, 0.000001, 1.0e9);
    duration_drag_->proposed_duration = duration;
    layout_ = drag.original_layout;
    layout_.preview_duration(drag.index, duration, drag.from_left);
    setMinimumWidth(int(std::ceil(layout_.width)));
    updateGeometry();
    update();
}

void ui::pose_strip::finish_duration_drag(bool commit) {
    if (!duration_drag_)
        return;
    auto drag = std::move(*duration_drag_);
    duration_drag_.reset();
    layout_ = std::move(drag.original_layout);
    setMinimumWidth(int(std::ceil(layout_.width)));
    updateGeometry();
    QToolTip::hideText();
    unsetCursor();
    update();
    if (commit && drag.proposed_duration != drag.original_duration)
        emit transition_duration_requested(drag.id, drag.proposed_duration);
}

void ui::pose_strip::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    if (!project_)
        return;

    auto character = project_->core().character(character_);
    auto* animation = character ?
        character->get().animation_data().find_animation(animation_) : nullptr;

    if (canvases_ && character) {
        auto& layer = canvases_->active_canvas().artwork();
        const bool has_artwork = !character->get().artwork().appearances().empty();
        const bool show_artwork = layer.show_artwork() && has_artwork;
        const bool show_bones = layer.show_skeleton() || !has_artwork;
        const auto appearance = layer.active_appearance(character_);

        std::string state_signature;
        for (const auto& [slot, _] : character->get().artwork().slot_definitions()) {
            state_signature += slot;
            state_signature.push_back('=');
            state_signature += layer.preview_state(character_, slot);
            state_signature.push_back(';');
        }

        if (!cached_artwork_ || *cached_artwork_ != show_artwork ||
                !cached_bones_ || *cached_bones_ != show_bones ||
                cached_appearance_ != appearance || cached_states_ != state_signature) {
            thumbnails_.clear();
            cached_artwork_ = show_artwork;
            cached_bones_ = show_bones;
            cached_appearance_ = appearance;
            cached_states_ = std::move(state_signature);
        }
    }

    if (!animation || animation->keyframes.empty()) {
        auto frame = rect().adjusted(10, 10, -10, -10);
        QPen pen(palette().mid().color());
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawRoundedRect(frame, 5, 5);
        painter.setPen(palette().text().color());
        painter.drawText(frame, Qt::AlignCenter, tr("Pose Strip\nNo animation poses yet"));
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing);
    // Dedicated scrub ruler: cards are deliberately below this lane.
    painter.setPen(palette().mid().color());
    painter.drawLine(QPointF(10, 12), QPointF(std::max(10.0, layout_.width - 10), 12));

    for (const auto& transition : layout_.transitions) {
        const bool selected_transition = selected_transition_ && *selected_transition_ == transition.id;
        painter.setPen(QPen(selected_transition ? palette().highlight().color() : palette().mid().color(),
            selected_transition ? 3 : 1));
        painter.setBrush(palette().alternateBase());
        painter.drawRect(transition.rect);
        // Narrow grip marks make the resizable ends discoverable without
        // obscuring the duration label on wider bars.
        painter.setPen(QPen(palette().mid().color(), 1.5));
        for (double edge : {transition.rect.left() + 3.0, transition.rect.right() - 3.0})
            painter.drawLine(QPointF(edge, transition.rect.top() + 7.0),
                QPointF(edge, transition.rect.bottom() - 7.0));
        const auto label = transition_duration_label(transition.duration);
        if (painter.fontMetrics().horizontalAdvance(label) + 12 <= transition.rect.width()) {
            painter.setPen(palette().text().color());
            painter.drawText(transition.rect, Qt::AlignCenter, label);
        }
    }

    const auto selected = project_->animation_session_keyframe();
    const auto position = layout_.at_time(playback_time_);
    const auto accent = palette().link().color();
    for (std::size_t i = 0; i < animation->keyframes.size(); ++i) {
        const auto& keyframe = animation->keyframes[i];
        const auto card = layout_.cards[i].rect;
        const bool is_selected = selected && *selected == keyframe.id;
        // Playback and scrubbing use a transient highlight, independent of
        // the persistent selection border used for editing a keyframe.
        const bool is_current = (playback_active_ || scrubbing_) &&
            position && position->current_pose == i;

        painter.setPen(QPen(is_selected ? palette().highlight().color() :
            palette().mid().color(), is_selected ? 3 : 1));
        painter.setBrush(palette().window());
        painter.drawRoundedRect(card, 5, 5);
        if (is_current) {
            auto tint = accent;
            tint.setAlpha(35);
            painter.setPen(QPen(accent, 2));
            painter.setBrush(tint);
            painter.drawRoundedRect(card.adjusted(4, 4, -4, -4), 3, 3);
            painter.fillRect(QRectF(card.x() + 8, card.y() + 5, card.width() - 16, 3), accent);
        }
        painter.drawPixmap(QPointF(card.x() + 7, card.y() + 7), thumbnail(keyframe));

        painter.setPen(palette().text().color());
        const auto label = painter.fontMetrics().elidedText(pose_keyframe_label(*animation, i),
            Qt::ElideRight, int(card.width() - 16));
        painter.drawText(QRectF(card.x() + 8, card.bottom() - 23, card.width() - 16, 19),
            Qt::AlignCenter, label);
    }
    if (position) {
        // The filled downward-pointing grip makes the draggable playhead
        // visible in the otherwise sparse ruler lane.
        painter.setPen(QPen(accent, 2));
        painter.drawLine(QPointF(position->x, 18), QPointF(position->x, 131));
        // The line remains tied to animation time. While dragging, let only
        // the triangle follow the pointer across time-collapsed pose cards.
        const double grip_x = scrubbing_ ? scrub_grip_x_ : position->x;
        painter.setPen(QPen(palette().base().color(), 1));
        painter.setBrush(accent);
        painter.drawPolygon(QPolygonF{
            QPointF(grip_x - 7, 3),
            QPointF(grip_x + 7, 3),
            QPointF(grip_x, 18)
        });
    }
}

bool ui::pose_strip::event(QEvent* event) {
    // A lost mouse grab (e.g. switching windows mid-drag) must not leave
    // the transient card highlight latched on.
    if ((event->type() == QEvent::UngrabMouse ||
            event->type() == QEvent::WindowDeactivate ||
            event->type() == QEvent::Hide) && scrubbing_) {
        scrubbing_ = false;
        unsetCursor();
        update();
    }
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        for (const auto& transition : layout_.transitions) {
            if (transition.rect.contains(help->pos())) {
                char duration[64];
                const auto result = std::to_chars(std::begin(duration), std::end(duration),
                    transition.duration);
                QToolTip::showText(help->globalPos(), tr("%1 s").arg(
                    QString::fromLatin1(duration, result.ptr - duration)), this);
                return true;
            }
        }
        if (scrub_hit(help->pos())) {
            QToolTip::showText(help->globalPos(), tr("Drag playhead to scrub"), this);
            return true;
        }
        if (project_) {
            auto character = project_->core().character(character_);
            const auto* animation = character ?
                character->get().animation_data().find_animation(animation_) : nullptr;
            for (std::size_t i = 0; animation && i < layout_.cards.size(); ++i) {
                const auto& card = layout_.cards[i];
                if (card.rect.contains(help->pos())) {
                    QToolTip::showText(help->globalPos(), tr("%1\nReached at %2 s")
                        .arg(pose_keyframe_label(*animation, i))
                        .arg(QString::number(card.time, 'g', 12)), this);
                    return true;
                }
            }
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}

void ui::pose_strip::mousePressEvent(QMouseEvent* event) {
    if (!project_ || event->button() != Qt::LeftButton)
        return;

    auto character = project_->core().character(character_);
    auto* animation = character ?
        character->get().animation_data().find_animation(animation_) : nullptr;
    if (!animation)
        return;

    if (scrub_hit(event->position())) {
        scrubbing_ = true;
        scrub_grip_x_ = event->position().x();
        setFocus();
        setCursor(Qt::ClosedHandCursor);
        update();
        if (auto time = layout_.time_at_x(event->position().x())) emit scrub_requested(*time);
        event->accept();
        return;
    }
    if (auto handle = resize_handle_at(event->position())) {
        const auto& transition = layout_.transitions[handle->first];
        duration_drag_ = duration_drag{transition.id, handle->first, handle->second,
            event->position().x(), transition.duration, transition.duration, layout_};
        setFocus();
        setCursor(Qt::SizeHorCursor);
        emit transition_selected(transition.id);
        event->accept();
        return;
    }
    for (std::size_t i = 0; i < animation->keyframes.size(); ++i) {
        if (layout_.cards[i].rect.contains(event->position())) {
            emit keyframe_selected(animation->keyframes[i].id);
            return;
        }
    }
    for (const auto& transition : layout_.transitions) {
        if (transition.rect.contains(event->position())) {
            emit transition_selected(transition.id);
            return;
        }
    }
}

void ui::pose_strip::mouseMoveEvent(QMouseEvent* event) {
    if (duration_drag_) {
        update_duration_drag(event->position().x());
        QToolTip::showText(event->globalPosition().toPoint(),
            tr("%1 s").arg(QString::number(duration_drag_->proposed_duration, 'g', 6)), this);
        event->accept();
        return;
    }
    if (scrubbing_) {
        scrub_grip_x_ = event->position().x();
        update(); // The pointer can move while keyframe time stays unchanged.
        if (auto time = layout_.time_at_x(event->position().x())) emit scrub_requested(*time);
        event->accept();
        return;
    }
    if (scrub_hit(event->position()))
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(resize_handle_at(event->position()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void ui::pose_strip::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    if (duration_drag_) {
        update_duration_drag(event->position().x());
        finish_duration_drag(true);
        event->accept();
        return;
    }
    if (scrubbing_) {
        if (auto time = layout_.time_at_x(event->position().x())) emit scrub_requested(*time);
        scrubbing_ = false;
        setCursor(scrub_hit(event->position()) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        update();
        // Releasing at an exact keyframe returns to editing that pose.
        // Interior times remain read-only previews with no selected keyframe.
        if (const auto position = layout_.at_time(playback_time_);
                position && position->at_keyframe)
            emit keyframe_selected(layout_.cards[position->current_pose].id);
        event->accept();
    }
}

void ui::pose_strip::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && scrubbing_) {
        scrubbing_ = false;
        unsetCursor();
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && duration_drag_) {
        finish_duration_drag(false);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
