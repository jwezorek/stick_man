#include "pose_strip.hpp"
#include "../canvas/artwork_layer.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../util.hpp"
#include "../../model/project.hpp"
#include <QMouseEvent>
#include <QPainter>
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
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ui::pose_strip::set_context(mdl::project* project, canvas::manager* canvases,
        sm::object_id character, sm::object_id animation) {
    project_ = project;
    canvases_ = canvases;
    character_ = character;
    animation_ = animation;
    playback_time_ = 0;
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
        const bool is_current = position && position->current_pose == i;

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
        painter.setPen(QPen(accent, 2));
        painter.drawLine(QPointF(position->x, 5), QPointF(position->x, 131));
    }
}

bool ui::pose_strip::event(QEvent* event) {
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

    if (event->position().y() <= 20.0) {
        scrubbing_ = true;
        if (auto time = layout_.time_at_x(event->position().x())) emit scrub_requested(*time);
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
    if (!scrubbing_)
        return;
    if (auto time = layout_.time_at_x(event->position().x())) emit scrub_requested(*time);
}

void ui::pose_strip::mouseReleaseEvent(QMouseEvent* event) {
    if (!scrubbing_ || event->button() != Qt::LeftButton)
        return;
    if (auto time = layout_.time_at_x(event->position().x())) emit scrub_requested(*time);
    scrubbing_ = false;
}
