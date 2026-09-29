#include "pose_strip_layout.hpp"
#include <QObject>
#include <QWidget>
#include <algorithm>
#include <cmath>

ui::pose_strip_layout::pose_strip_layout(const sm::animation* animation, double card_width) {
    if (!animation || animation->keyframes.empty())
        return;
    constexpr double margin = 10;
    constexpr double pixels_per_second = 160;
    constexpr double minimum_transition_width = 12;
    // Qt cannot create a widget wider than QWIDGETSIZE_MAX. Only clips beyond
    // that platform limit reduce the scale; the viewport never determines it.
    const double fixed_width = margin * 2 + animation->keyframes.size() * card_width +
        animation->transitions.size() * minimum_transition_width;
    const double available = std::max(0.0, QWIDGETSIZE_MAX - fixed_width);
    const double duration = animation->duration_seconds();
    const double scale = duration > 0 ? std::min(pixels_per_second, available / duration) :
        pixels_per_second;
    double x = margin;
    double time = 0;
    for (std::size_t i = 0; i < animation->keyframes.size(); ++i) {
        cards.push_back({animation->keyframes[i].id, {x, 22, card_width, 104}, time});
        x += card_width;
        if (i < animation->transitions.size()) {
            const auto& source = animation->transitions[i];
            const double w = std::max(minimum_transition_width,
                source.duration_seconds * scale);
            transitions.push_back({source.id, {x, 55, w, 38}, time, source.duration_seconds});
            x += w;
            time += source.duration_seconds;
        }
    }
    width = x + margin;
}

std::optional<ui::pose_strip_layout::position> ui::pose_strip_layout::at_time(double seconds) const {
    if (cards.empty())
        return {};
    seconds = std::clamp(seconds, 0.0, cards.back().time);
    const auto next = std::upper_bound(cards.begin(), cards.end(), seconds,
        [](double time, const card& value) { return time < value.time; });
    const auto index = std::size_t(next - cards.begin() - 1);
    double x = cards[index].rect.right(); // Instantaneously jump across the current card.
    if (index < transitions.size()) {
        const auto& t = transitions[index];
        x = t.rect.left() + t.rect.width() * ((seconds - t.start) / t.duration);
    }
    return position{index, x, seconds == cards[index].time};
}

std::optional<double> ui::pose_strip_layout::time_at_x(double x) const {
    if (cards.empty() || !std::isfinite(x))
        return {};
    if (x <= cards.front().rect.left())
        return cards.front().time;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].rect.contains(QPointF(x, cards[i].rect.center().y())) ||
                (x >= cards[i].rect.left() && x <= cards[i].rect.right())) {
            // Collapsed timestamps use the same last-key convention as sampling.
            auto j = i;
            while (j + 1 < cards.size() && cards[j + 1].time == cards[i].time) ++j;
            return cards[j].time;
        }
        if (i < transitions.size()) {
            const auto& t = transitions[i];
            if (x >= t.rect.left() && x <= t.rect.right()) {
                const double u = std::clamp((x - t.rect.left()) / t.rect.width(), 0.0, 1.0);
                return t.start + u * t.duration;
            }
        }
    }
    return cards.back().time;
}

bool ui::pose_strip_layout::same_timing(const pose_strip_layout& other) const {
    if (cards.size() != other.cards.size() || transitions.size() != other.transitions.size())
        return false;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].id != other.cards[i].id)
            return false;
    }
    for (std::size_t i = 0; i < transitions.size(); ++i) {
        if (transitions[i].id != other.transitions[i].id ||
            transitions[i].duration != other.transitions[i].duration) return false;
    }
    return true;
}

QString ui::pose_keyframe_label(const sm::animation& animation, std::size_t index) {
    const auto& keyframe = animation.keyframes.at(index);
    return keyframe.name ? QString::fromStdString(*keyframe.name) :
        QObject::tr("Pose %1").arg(index + 1);
}

QString ui::transition_duration_label(double seconds) {
    return QObject::tr("%1 s").arg(QString::number(seconds, 'g', 3));
}
