#include "animation_action_editor.hpp"

#include "canvas/scene.hpp"
#include "../core/sm_skeleton.hpp"
#include "tools/motion_path_fit.hpp"
#include "util.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>
#include <type_traits>

namespace {
    constexpr double degrees = 180.0 / std::numbers::pi;

    template<class... Ts> struct overload : Ts... { using Ts::operator()...; };
    template<class> inline constexpr bool always_false_v = false;

    QString text(sm::object_id value) { return QString::fromStdString(value.to_string()); }
    sm::object_id id(const QString& value) {
        return sm::object_id::from_string(value.toStdString()).value_or(sm::object_id{});
    }

    QString path_name(sm::motion_path_kind kind) {
        switch (kind) {
        case sm::motion_path_kind::straight: return "Straight";
        case sm::motion_path_kind::curve: return "Curve";
        case sm::motion_path_kind::spline: return "Spline";
        }
        return "Path";
    }

    bool same_path(const sm::motion_path& lhs, const sm::motion_path& rhs) {
        const auto& a = lhs.geometry();
        const auto& b = rhs.geometry();
        if (a.index() != b.index()) return false;
        return std::visit(overload{
            [](const sm::line_path& x, const sm::line_path& y) {
                return x.start == y.start && x.end == y.end;
            },
            [](const sm::cubic_bezier_path& x, const sm::cubic_bezier_path& y) {
                return x.start == y.start && x.control1 == y.control1 &&
                    x.control2 == y.control2 && x.end == y.end;
            },
            [](const sm::spline_path& x, const sm::spline_path& y) {
                if (x.segments.size() != y.segments.size()) return false;
                for (std::size_t i = 0; i < x.segments.size(); ++i) {
                    const auto& p = x.segments[i];
                    const auto& q = y.segments[i];
                    if (!(p.start == q.start && p.control1 == q.control1 &&
                        p.control2 == q.control2 && p.end == q.end)) return false;
                }
                return true;
            },
            [](const auto&, const auto&) { return false; }
        }, a, b);
    }

    template<class Action> struct action_editor;

    template<>
    struct action_editor<sm::rigid_rotation> {
        static ui::animation_editing::timeline_presentation timeline(const sm::rigid_rotation& action,
            const sm::topology& topology, sm::object_id) {
            auto bone = topology.get<sm::bone>(action.bone);
            const auto propagation = action.propagation == sm::rotation_propagation::bone_only ? "bone only" : "hierarchy";
            return {
                QString("Rotate %1 (%2°, %3)")
                    .arg(bone ? QString::fromStdString(bone->get().name()) : "missing bone")
                    .arg(action.angle * degrees, 0, 'f', 1).arg(propagation),
                ui::timeline_color::blue,
                !bone
            };
        }
        static QString selection_text(const sm::rigid_rotation& action, const sm::topology& topology) {
            auto bone = topology.get<sm::bone>(action.bone);
            return QString("Selected action — Rotate %1")
                .arg(bone ? QString::fromStdString(bone->get().name()) : "missing bone");
        }
        static constexpr bool uses_easing = true;
        static sm::easing authoring_easing(sm::easing requested) { return requested; }
        static QString begin_message() { return "Drag to author the rotation; release to create the action. Escape cancels."; }
        static ui::animation_editing::authoring_presentation authoring_update(const sm::rigid_rotation& action) {
            return {std::abs(action.angle) > 1e-8,
                QString("Rotation preview: %1°").arg(action.angle * degrees, 0, 'f', 1)};
        }
        static QString preview_message(const sm::rigid_rotation& action) {
            return QString("Rotation angle: %1° — release to commit; Escape cancels.")
                .arg(action.angle * degrees, 0, 'f', 1);
        }
        static bool equivalent(const sm::rigid_rotation& a, const sm::rigid_rotation& b) {
            return a.bone == b.bone && a.pivot == b.pivot && a.angle == b.angle && a.propagation == b.propagation;
        }
    };

    template<>
    struct action_editor<sm::ik_rotation> {
        static ui::animation_editing::timeline_presentation timeline(const sm::ik_rotation& action,
            const sm::topology& topology, sm::object_id) {
            auto effector = topology.get<sm::node>(action.effector);
            auto pivot = topology.get<sm::node>(action.pivot_node);
            return {
                QString("IK rotate %1 (%2°)")
                    .arg(effector ? QString::fromStdString(effector->get().name()) : "missing effector")
                    .arg(action.angle * degrees, 0, 'f', 1),
                ui::timeline_color::purple,
                !effector || !pivot
            };
        }
        static QString selection_text(const sm::ik_rotation& action, const sm::topology& topology) {
            auto effector = topology.get<sm::node>(action.effector);
            return QString("Selected action — IK rotate %1")
                .arg(effector ? QString::fromStdString(effector->get().name()) : "missing effector");
        }
        static constexpr bool uses_easing = true;
        static sm::easing authoring_easing(sm::easing requested) { return requested; }
        static QString begin_message() { return "Drag to author the rotation; release to create the action. Escape cancels."; }
        static ui::animation_editing::authoring_presentation authoring_update(const sm::ik_rotation& action) {
            return {std::abs(action.angle) > 1e-8,
                QString("Rotation preview: %1°").arg(action.angle * degrees, 0, 'f', 1)};
        }
        static QString preview_message(const sm::ik_rotation& action) {
            return QString("Rotation angle: %1° — release to commit; Escape cancels.")
                .arg(action.angle * degrees, 0, 'f', 1);
        }
        static bool equivalent(const sm::ik_rotation& a, const sm::ik_rotation& b) {
            return a.effector == b.effector && a.pivot_node == b.pivot_node && a.angle == b.angle;
        }
    };

    template<>
    struct action_editor<sm::rigid_translation> {
        static ui::animation_editing::timeline_presentation timeline(const sm::rigid_translation& action,
            const sm::topology& topology, sm::object_id character_root_bone) {
            bool invalid = action.skeletons.empty();
            for (auto skeleton : action.skeletons) invalid = invalid || !topology.contains_skeleton(skeleton);
            if (action.reference == sm::translation_reference::bone)
                invalid = invalid || !topology.get<sm::bone>(action.reference_bone);
            else
                invalid = invalid || !topology.get<sm::bone>(character_root_bone);
            return {
                QString("Translate %1 skeleton%2 (%3)")
                    .arg(action.skeletons.size()).arg(action.skeletons.size() == 1 ? "" : "s")
                    .arg(path_name(action.path.kind())),
                ui::timeline_color::green,
                invalid
            };
        }
        static QString selection_text(const sm::rigid_translation& action, const sm::topology&) {
            return QString("Selected action — Translate %1 skeleton%2")
                .arg(action.skeletons.size()).arg(action.skeletons.size() == 1 ? "" : "s");
        }
        static constexpr bool uses_easing = false;
        static sm::easing authoring_easing(sm::easing) { return sm::easing::linear; }
        static QString begin_message() { return "Drag to author the translation path; release to create the action. Escape cancels."; }
        static ui::animation_editing::authoring_presentation authoring_update(const sm::rigid_translation& action) {
            return {action.path.length() > 1e-6,
                QString("Translation preview: %1 units (%2)")
                    .arg(action.path.length(), 0, 'f', 1).arg(path_name(action.path.kind()))};
        }
        static QString preview_message(const sm::rigid_translation& action) {
            return QString("Motion path length: %1 — release to commit; Escape cancels.")
                .arg(action.path.length(), 0, 'f', 1);
        }
        static bool equivalent(const sm::rigid_translation& a, const sm::rigid_translation& b) {
            return a.skeletons == b.skeletons && a.reference == b.reference &&
                a.reference_bone == b.reference_bone && same_path(a.path, b.path);
        }
    };

    template<>
    struct action_editor<sm::ik_translation> {
        static ui::animation_editing::timeline_presentation timeline(const sm::ik_translation& action,
            const sm::topology& topology, sm::object_id character_root_bone) {
            auto effector = topology.get<sm::node>(action.effector);
            bool invalid = !effector;
            for (auto pin : action.pins) invalid = invalid || !topology.get<sm::node>(pin);
            if (action.reference == sm::translation_reference::bone)
                invalid = invalid || !topology.get<sm::bone>(action.reference_bone);
            else
                invalid = invalid || !topology.get<sm::bone>(character_root_bone);
            return {
                QString("IK translate %1 (%2)")
                    .arg(effector ? QString::fromStdString(effector->get().name()) : "missing effector")
                    .arg(path_name(action.path.kind())),
                ui::timeline_color::orange,
                invalid
            };
        }
        static QString selection_text(const sm::ik_translation& action, const sm::topology& topology) {
            auto effector = topology.get<sm::node>(action.effector);
            return QString("Selected action — IK translate %1")
                .arg(effector ? QString::fromStdString(effector->get().name()) : "missing effector");
        }
        static constexpr bool uses_easing = false;
        static sm::easing authoring_easing(sm::easing) { return sm::easing::linear; }
        static QString begin_message() { return "Drag to author the translation path; release to create the action. Escape cancels."; }
        static ui::animation_editing::authoring_presentation authoring_update(const sm::ik_translation& action) {
            return {action.path.length() > 1e-6,
                QString("IK translation preview: %1 units (%2)")
                    .arg(action.path.length(), 0, 'f', 1).arg(path_name(action.path.kind()))};
        }
        static QString preview_message(const sm::ik_translation& action) {
            return QString("Motion path length: %1 — release to commit; Escape cancels.")
                .arg(action.path.length(), 0, 'f', 1);
        }
        static bool equivalent(const sm::ik_translation& a, const sm::ik_translation& b) {
            return a.effector == b.effector && a.pins == b.pins && a.reference == b.reference &&
                a.reference_bone == b.reference_bone && same_path(a.path, b.path);
        }
    };

    template<class F>
    decltype(auto) dispatch(const sm::action_data& data, F&& fn) {
        return std::visit([&](const auto& action) -> decltype(auto) {
            using action_type = std::remove_cvref_t<decltype(action)>;
            // No primary action_editor implementation exists intentionally. Adding
            // an action_data alternative therefore requires an explicit Editor decision.
            return fn(action_editor<action_type>{}, action);
        }, data);
    }

    class rotation_action_adornment final : public ui::canvas::interactive_adornment {
        ui::canvas::scene& scene_;
        QPointF pivot_;
        double radius_ = 0.0;
        double start_theta_ = 0.0;
        double angle_ = 0.0;
        double drag_angle_ = 0.0;
        double previous_pointer_theta_ = 0.0;
        bool dragging_ = false;
        QGraphicsEllipseItem* arc_ = nullptr;
        QGraphicsLineItem* radius_line_ = nullptr;
        QGraphicsEllipseItem* pivot_handle_ = nullptr;
        QGraphicsEllipseItem* angle_handle_ = nullptr;
        std::function<void(double)> preview_;
        std::function<void(double)> commit_;
        std::function<void()> cancel_;

        QPointF start_point() const {
            return pivot_ + QPointF(radius_ * std::cos(start_theta_), radius_ * std::sin(start_theta_));
        }
        QPointF end_point() const {
            const auto theta = start_theta_ + angle_;
            return pivot_ + QPointF(radius_ * std::cos(theta), radius_ * std::sin(theta));
        }
        double hit_tolerance() const { return 9.0 / std::max(0.001, std::abs(scene_.scale())); }
        bool hits_angle_handle(const QPointF& point) const { return ui::distance(point, end_point()) <= hit_tolerance(); }
        void set_cursor(Qt::CursorShape cursor) {
            if (!scene_.views().isEmpty()) scene_.views().first()->viewport()->setCursor(cursor);
        }
        void clear_cursor() {
            if (!scene_.views().isEmpty()) scene_.views().first()->viewport()->unsetCursor();
        }
        void update_graphics() {
            ui::set_arc(arc_, pivot_, radius_, start_theta_, angle_);
            radius_line_->setLine(QLineF(pivot_, start_point()));
            pivot_handle_->setPos(pivot_);
            angle_handle_->setPos(end_point());
        }
    public:
        rotation_action_adornment(ui::canvas::scene& scene, QPointF pivot, double radius,
            double start_theta, double angle, std::function<void(double)> preview,
            std::function<void(double)> commit, std::function<void()> cancel)
            : scene_(scene), pivot_(pivot), radius_(radius), start_theta_(start_theta), angle_(angle),
              preview_(std::move(preview)), commit_(std::move(commit)), cancel_(std::move(cancel)) {
            const QColor accent("#35d0c5");
            arc_ = new QGraphicsEllipseItem;
            QPen arc_pen(accent, 2.5, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin); arc_pen.setCosmetic(true);
            arc_->setPen(arc_pen); arc_->setBrush(Qt::NoBrush); arc_->setZValue(20000); scene_.addItem(arc_);
            radius_line_ = new QGraphicsLineItem;
            QPen radius_pen(accent); radius_pen.setWidthF(1.0); radius_pen.setCosmetic(true); radius_pen.setStyle(Qt::DashLine);
            radius_pen.setColor(QColor(accent.red(), accent.green(), accent.blue(), 150));
            radius_line_->setPen(radius_pen); radius_line_->setZValue(19999); scene_.addItem(radius_line_);
            auto make_handle = [&](double diameter, bool filled) {
                auto* handle = new QGraphicsEllipseItem(-diameter / 2.0, -diameter / 2.0, diameter, diameter);
                handle->setFlag(QGraphicsItem::ItemIgnoresTransformations);
                QPen pen(accent, 2.0); pen.setCosmetic(true); handle->setPen(pen);
                handle->setBrush(filled ? QBrush(accent) : QBrush(Qt::NoBrush)); handle->setZValue(20001); scene_.addItem(handle);
                return handle;
            };
            pivot_handle_ = make_handle(8.0, false);
            angle_handle_ = make_handle(12.0, true);
            update_graphics();
        }
        ~rotation_action_adornment() override {
            clear_cursor(); delete arc_; delete radius_line_; delete pivot_handle_; delete angle_handle_;
        }
        bool keyPressEvent(QKeyEvent* event) override {
            if (!dragging_ || event->key() != Qt::Key_Escape) return false;
            dragging_ = false; clear_cursor(); if (cancel_) cancel_(); return true;
        }
        bool mousePressEvent(QGraphicsSceneMouseEvent* event) override {
            if (event->button() != Qt::LeftButton || !hits_angle_handle(event->scenePos())) return false;
            dragging_ = true; drag_angle_ = angle_;
            previous_pointer_theta_ = ui::angle_through_points(pivot_, event->scenePos());
            set_cursor(Qt::ClosedHandCursor); return true;
        }
        bool mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
            if (!dragging_) {
                if (hits_angle_handle(event->scenePos())) { set_cursor(Qt::OpenHandCursor); return true; }
                clear_cursor(); return false;
            }
            const auto theta = ui::angle_through_points(pivot_, event->scenePos());
            drag_angle_ += sm::angular_distance(previous_pointer_theta_, theta);
            previous_pointer_theta_ = theta; angle_ = drag_angle_; update_graphics();
            if (preview_) preview_(angle_); return true;
        }
        bool mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
            if (!dragging_ || event->button() != Qt::LeftButton) return false;
            mouseMoveEvent(event); dragging_ = false; clear_cursor();
            if (commit_) commit_(angle_); return true;
        }
        void cancel() override {
            if (!dragging_) return;
            dragging_ = false; clear_cursor(); if (cancel_) cancel_();
        }
    };

    class translation_action_adornment final : public ui::canvas::interactive_adornment {
        enum class role { control1, control2, endpoint };
        struct handle { std::size_t segment = 0; role kind = role::endpoint; QGraphicsEllipseItem* item = nullptr; };
        ui::canvas::scene& scene_;
        sm::motion_path path_;
        sm::reference_frame frame_;
        sm::point local_offset_{};
        QGraphicsPathItem* curve_ = nullptr;
        QGraphicsEllipseItem* origin_handle_ = nullptr;
        std::vector<QGraphicsLineItem*> guides_;
        std::vector<handle> handles_;
        std::optional<std::size_t> dragging_;
        std::function<void(const sm::motion_path&)> preview_;
        std::function<void(const sm::motion_path&)> commit_;
        std::function<void()> cancel_;

        QPointF origin() const { return ui::to_qt_pt(frame_.local_to_world(local_offset_)); }
        QPointF world(sm::point p) const { return ui::to_qt_pt(frame_.local_to_world(local_offset_ + p)); }
        sm::point local(QPointF p) const { return frame_.world_to_local(ui::from_qt_pt(p)) - local_offset_; }
        double hit_tolerance() const { return 9.0 / std::max(0.001, std::abs(scene_.scale())); }
        void set_cursor(Qt::CursorShape c) { if (!scene_.views().isEmpty()) scene_.views().first()->viewport()->setCursor(c); }
        void clear_cursor() { if (!scene_.views().isEmpty()) scene_.views().first()->viewport()->unsetCursor(); }
        static sm::point direction(sm::point p) {
            double d = std::sqrt(p.x * p.x + p.y * p.y); return d > 1e-9 ? (1.0 / d) * p : sm::point{1, 0};
        }
        QGraphicsEllipseItem* make_handle(double diameter, bool filled) {
            auto* h = new QGraphicsEllipseItem(-diameter / 2, -diameter / 2, diameter, diameter);
            h->setFlag(QGraphicsItem::ItemIgnoresTransformations);
            QPen pen(QColor("#35d0c5"), 2.0); pen.setCosmetic(true); h->setPen(pen);
            h->setBrush(filled ? QBrush(QColor("#35d0c5")) : QBrush(Qt::NoBrush)); h->setZValue(20002); scene_.addItem(h); return h;
        }
        void rebuild_handles() {
            for (auto& h : handles_) delete h.item; handles_.clear();
            for (auto* g : guides_) delete g; guides_.clear();
            auto add = [&](std::size_t segment, role kind, sm::point p, bool filled) {
                auto* item = make_handle(filled ? 11 : 9, filled); item->setPos(world(p)); handles_.push_back({segment, kind, item});
            };
            auto guide = [&](sm::point a, sm::point b) {
                auto* line = new QGraphicsLineItem(QLineF(world(a), world(b)));
                QPen pen(QColor(53, 208, 197, 150), 1, Qt::DashLine); pen.setCosmetic(true);
                line->setPen(pen); line->setZValue(19999); scene_.addItem(line); guides_.push_back(line);
            };
            std::visit([&](const auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, sm::line_path>) add(0, role::endpoint, g.end, true);
                else if constexpr (std::is_same_v<T, sm::cubic_bezier_path>) {
                    guide(g.start, g.control1); guide(g.control2, g.end);
                    add(0, role::control1, g.control1, false); add(0, role::control2, g.control2, false); add(0, role::endpoint, g.end, true);
                } else {
                    for (std::size_t i = 0; i < g.segments.size(); ++i) {
                        const auto& c = g.segments[i]; guide(c.start, c.control1); guide(c.control2, c.end);
                        add(i, role::control1, c.control1, false); add(i, role::control2, c.control2, false); add(i, role::endpoint, c.end, true);
                    }
                }
            }, path_.geometry());
        }
        void update_graphics() {
            QPainterPath qp(origin());
            std::visit([&](const auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, sm::line_path>) qp.lineTo(world(g.end));
                else if constexpr (std::is_same_v<T, sm::cubic_bezier_path>) qp.cubicTo(world(g.control1), world(g.control2), world(g.end));
                else for (const auto& c : g.segments) qp.cubicTo(world(c.control1), world(c.control2), world(c.end));
            }, path_.geometry());
            curve_->setPath(qp); rebuild_handles(); origin_handle_->setPos(origin());
        }
        void set_handle(std::size_t index, sm::point p) {
            auto geometry = path_.geometry(); const auto h = handles_[index];
            std::visit([&](auto& g) {
                using T = std::decay_t<decltype(g)>;
                if constexpr (std::is_same_v<T, sm::line_path>) g.end = p;
                else if constexpr (std::is_same_v<T, sm::cubic_bezier_path>) {
                    if (h.kind == role::control1) g.control1 = p;
                    else if (h.kind == role::control2) g.control2 = p;
                    else { auto d = p - g.end; g.end = p; g.control2 += d; }
                } else {
                    auto& c = g.segments[h.segment];
                    if (h.kind == role::control1) {
                        c.control1 = p;
                        if (h.segment > 0) {
                            auto& prev = g.segments[h.segment - 1];
                            double l = sm::distance(prev.control2, c.start); prev.control2 = c.start - l * direction(p - c.start);
                        }
                    } else if (h.kind == role::control2) {
                        c.control2 = p;
                        if (h.segment + 1 < g.segments.size()) {
                            auto& next = g.segments[h.segment + 1];
                            double l = sm::distance(next.control1, c.end); next.control1 = c.end - l * direction(p - c.end);
                        }
                    } else {
                        auto old = c.end, d = p - old; c.end = p; c.control2 += d;
                        if (h.segment + 1 < g.segments.size()) {
                            auto& next = g.segments[h.segment + 1]; next.start = p; next.control1 += d;
                        }
                    }
                }
            }, geometry);
            path_.set_geometry(std::move(geometry)); update_graphics();
        }
        std::optional<std::size_t> hit(QPointF p) const {
            double best = hit_tolerance(); std::optional<std::size_t> result;
            for (std::size_t i = 0; i < handles_.size(); ++i) {
                double d = ui::distance(p, handles_[i].item->pos());
                if (d <= best) { best = d; result = i; }
            }
            return result;
        }
    public:
        translation_action_adornment(ui::canvas::scene& scene, sm::motion_path path, sm::reference_frame frame,
            sm::point local_offset, std::function<void(const sm::motion_path&)> preview,
            std::function<void(const sm::motion_path&)> commit, std::function<void()> cancel)
            : scene_(scene), path_(std::move(path)), frame_(frame), local_offset_(local_offset),
              preview_(std::move(preview)), commit_(std::move(commit)), cancel_(std::move(cancel)) {
            curve_ = new QGraphicsPathItem;
            QPen pen(QColor("#35d0c5"), 2.5, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin); pen.setCosmetic(true);
            curve_->setPen(pen); curve_->setBrush(Qt::NoBrush); curve_->setZValue(20000); scene_.addItem(curve_);
            origin_handle_ = make_handle(8, false); update_graphics();
        }
        ~translation_action_adornment() override {
            clear_cursor(); for (auto& h : handles_) delete h.item; for (auto* g : guides_) delete g;
            delete curve_; delete origin_handle_;
        }
        bool keyPressEvent(QKeyEvent* event) override {
            if (!dragging_ || event->key() != Qt::Key_Escape) return false;
            dragging_.reset(); clear_cursor(); if (cancel_) cancel_(); return true;
        }
        bool mousePressEvent(QGraphicsSceneMouseEvent* event) override {
            if (event->button() != Qt::LeftButton) return false;
            auto h = hit(event->scenePos()); if (!h) return false; dragging_ = h; set_cursor(Qt::ClosedHandCursor); return true;
        }
        bool mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
            if (!dragging_) {
                if (hit(event->scenePos())) { set_cursor(Qt::OpenHandCursor); return true; }
                clear_cursor(); return false;
            }
            auto index = *dragging_; // rebuild preserves descriptor ordering.
            set_handle(index, local(event->scenePos())); if (preview_) preview_(path_); return true;
        }
        bool mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
            if (!dragging_ || event->button() != Qt::LeftButton) return false;
            mouseMoveEvent(event); dragging_.reset(); clear_cursor(); if (commit_) commit_(path_); return true;
        }
        void cancel() override {
            if (!dragging_) return; dragging_.reset(); clear_cursor(); if (cancel_) cancel_();
        }
    };

    template<class Rotation>
    void install_rotation_adornment(ui::canvas::scene& scene, const Rotation& rotation,
        const sm::action_evaluation_context& context, ui::animation_editing::adornment_callbacks callbacks) {
        if (!context.rotation) return;
        const QPointF pivot = ui::to_qt_pt(context.rotation->pivot);
        const QPointF rotating = ui::to_qt_pt(context.rotation->rotating);
        const auto radius = ui::distance(pivot, rotating);
        if (!(radius > 0.0) || !std::isfinite(radius)) return;
        const auto start_theta = ui::angle_through_points(pivot, rotating);
        scene.set_interactive_adornment(std::make_shared<rotation_action_adornment>(scene, pivot, radius,
            start_theta, rotation.angle,
            [rotation, callback = callbacks.preview](double angle) mutable {
                auto edited = rotation; edited.angle = angle; if (callback) callback(sm::action_data{std::move(edited)});
            },
            [rotation, callback = callbacks.commit](double angle) mutable {
                auto edited = rotation; edited.angle = angle; if (callback) callback(sm::action_data{std::move(edited)});
            }, std::move(callbacks.cancel)));
    }

    template<class Translation>
    void install_translation_adornment(ui::canvas::scene& scene, const Translation& translation,
        const sm::action_evaluation_context& context, ui::animation_editing::adornment_callbacks callbacks,
        bool ik) {
        // The evaluation context captures the selected action's reference frame
        // immediately before that action is applied. This is the authoritative
        // frame for rendering/editing its stored local motion_path.
        if (!context.translation_reference_frame) return;
        sm::point local_offset{};
        if (ik) {
            if (!context.translation_anchor_world) return;
            local_offset = context.translation_reference_frame->world_to_local(*context.translation_anchor_world);
        }
        scene.set_interactive_adornment(std::make_shared<translation_action_adornment>(scene, translation.path,
            *context.translation_reference_frame, local_offset,
            [translation, callback = callbacks.preview](const sm::motion_path& path) mutable {
                auto edited = translation; edited.path = path; if (callback) callback(sm::action_data{std::move(edited)});
            },
            [translation, callback = callbacks.commit](const sm::motion_path& path) mutable {
                auto edited = translation; edited.path = path; if (callback) callback(sm::action_data{std::move(edited)});
            }, std::move(callbacks.cancel)));
    }

    std::vector<sm::object_id> skeleton_ids_from_nodes(const std::vector<sm::node_ref>& nodes) {
        std::vector<sm::object_id> result;
        result.reserve(nodes.size());
        for (auto node : nodes) result.push_back(node->owner().id());
        std::ranges::sort(result);
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
}

ui::animation_editing::timeline_presentation ui::animation_editing::timeline_item_for(
    const sm::action_data& data, const sm::topology& topology, sm::object_id character_root_bone) {
    return dispatch(data, [&](auto editor, const auto& action) { return editor.timeline(action, topology, character_root_bone); });
}

QString ui::animation_editing::selection_text_for(const sm::action_data& data, const sm::topology& topology) {
    return dispatch(data, [&](auto editor, const auto& action) { return editor.selection_text(action, topology); });
}

bool ui::animation_editing::uses_easing(const sm::action_data& data) {
    return dispatch(data, [](auto editor, const auto&) { return decltype(editor)::uses_easing; });
}

sm::easing ui::animation_editing::authoring_easing(const sm::action_data& data, sm::easing requested) {
    return dispatch(data, [&](auto editor, const auto&) { return editor.authoring_easing(requested); });
}

QString ui::animation_editing::authoring_begin_message(const sm::action_data& data) {
    return dispatch(data, [](auto editor, const auto&) { return editor.begin_message(); });
}

ui::animation_editing::authoring_presentation ui::animation_editing::authoring_update(const sm::action_data& data) {
    return dispatch(data, [](auto editor, const auto& action) { return editor.authoring_update(action); });
}

QString ui::animation_editing::interactive_preview_message(const sm::action_data& data) {
    return dispatch(data, [](auto editor, const auto& action) { return editor.preview_message(action); });
}

bool ui::animation_editing::editor_equivalent(const sm::action_data& lhs, const sm::action_data& rhs) {
    if (lhs.index() != rhs.index()) return false;
    return std::visit([](const auto& a, const auto& b) {
        using A = std::remove_cvref_t<decltype(a)>;
        using B = std::remove_cvref_t<decltype(b)>;
        if constexpr (!std::is_same_v<A, B>) return false;
        else return action_editor<A>::equivalent(a, b);
    }, lhs, rhs);
}

void ui::animation_editing::install_adornment(canvas::scene& scene, const sm::animation_action& action,
    const sm::action_evaluation_context& context, adornment_callbacks callbacks) {
    dispatch(action.data, [&](auto, const auto& concrete) {
        using T = std::remove_cvref_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, sm::rigid_rotation> || std::is_same_v<T, sm::ik_rotation>)
            install_rotation_adornment(scene, concrete, context, std::move(callbacks));
        else if constexpr (std::is_same_v<T, sm::rigid_translation>)
            install_translation_adornment(scene, concrete, context, std::move(callbacks), false);
        else if constexpr (std::is_same_v<T, sm::ik_translation>)
            install_translation_adornment(scene, concrete, context, std::move(callbacks), true);
        else static_assert(always_false_v<T>, "Add an explicit canvas-adornment decision for the new action type");
    });
}

void ui::animation_editing::sync_translation_tool_properties(tool::select_tool_panel& panel,
    const sm::action_data* selected) {
    if (!selected) {
        panel.set_animation_translation(panel.animation_translation(), false);
        return;
    }
    dispatch(*selected, [&](auto, const auto& action) {
        using T = std::remove_cvref_t<decltype(action)>;
        if constexpr (std::is_same_v<T, sm::rigid_translation>)
            panel.set_animation_translation({action.path.kind(), action.reference, action.reference_bone}, false);
        else if constexpr (std::is_same_v<T, sm::ik_translation>)
            panel.set_animation_translation({action.path.kind(), action.reference, action.reference_bone}, true);
        else if constexpr (std::is_same_v<T, sm::rigid_rotation> || std::is_same_v<T, sm::ik_rotation>)
            panel.set_animation_translation(panel.animation_translation(), false);
        else static_assert(always_false_v<T>, "Add an explicit Tool Properties decision for the new action type");
    });
}

std::optional<sm::action_data> ui::animation_editing::apply_translation_tool_properties(
    const sm::action_data& selected, tool::animation_translation_settings settings) {
    return dispatch(selected, [&](auto, const auto& action) -> std::optional<sm::action_data> {
        using T = std::remove_cvref_t<decltype(action)>;
        if constexpr (std::is_same_v<T, sm::rigid_translation> || std::is_same_v<T, sm::ik_translation>) {
            auto edited = action;
            if (settings.reference == sm::translation_reference::bone && settings.reference_bone.is_nil() &&
                action.reference == sm::translation_reference::bone)
                settings.reference_bone = action.reference_bone;
            if (edited.path.kind() != settings.path) edited.path = tool::convert_motion_path(edited.path, settings.path);
            edited.reference = settings.reference;
            edited.reference_bone = settings.reference_bone;
            return sm::action_data{std::move(edited)};
        } else if constexpr (std::is_same_v<T, sm::rigid_rotation> || std::is_same_v<T, sm::ik_rotation>) {
            return std::nullopt;
        } else {
            static_assert(always_false_v<T>, "Add an explicit translation-properties decision for the new action type");
        }
    });
}

ui::animation_editing::action_edit_result ui::animation_editing::capture_pins(
    const sm::action_data& selected, const sm::topology& topology,
    const std::vector<sm::object_id>& pinned_nodes) {
    return dispatch(selected, [&](auto, const auto& action) -> action_edit_result {
        using T = std::remove_cvref_t<decltype(action)>;
        if constexpr (std::is_same_v<T, sm::ik_translation>) {
            auto effector = topology.get<sm::node>(action.effector);
            if (!effector) return {{}, "The IK effector is missing."};
            std::vector<sm::object_id> pins;
            const auto owner = effector->get().owner().id();
            for (auto pin : pinned_nodes) {
                auto node = topology.get<sm::node>(pin);
                if (node && pin != action.effector && node->get().owner().id() == owner) pins.push_back(pin);
            }
            std::ranges::sort(pins);
            pins.erase(std::unique(pins.begin(), pins.end()), pins.end());
            auto edited = action; edited.pins = std::move(pins);
            return {sm::action_data{std::move(edited)}, {}};
        } else if constexpr (std::is_same_v<T, sm::rigid_rotation> || std::is_same_v<T, sm::ik_rotation> ||
                             std::is_same_v<T, sm::rigid_translation>) {
            return {};
        } else {
            static_assert(always_false_v<T>, "Add an explicit pin-capture decision for the new action type");
        }
    });
}

sm::action_data ui::animation_editing::authored_action_for(const tool::rotation_state& state) {
    if (state.mode() == tool::sel_drag_mode::rag_doll)
        return sm::ik_rotation{state.rotating().id(), state.axis().id(), state.gesture_angle()};
    const auto pivot = &state.axis() == &state.bone().parent_node() ?
        sm::rotation_pivot::root : sm::rotation_pivot::tip;
    const auto propagation = state.mode() == tool::sel_drag_mode::unique ?
        sm::rotation_propagation::bone_only : sm::rotation_propagation::hierarchy;
    return sm::rigid_rotation{state.bone().id(), pivot, state.gesture_angle(), propagation};
}

std::optional<sm::action_data> ui::animation_editing::authored_action_for(const tool::translation_state& state) {
    if (state.mode == tool::sel_drag_mode::rubber_band || state.gesture_samples.size() < 2) return {};
    std::vector<sm::point> local_samples; local_samples.reserve(state.gesture_samples.size());
    const sm::reference_frame frame{state.reference_origin, state.reference_angle};
    const auto origin = frame.world_to_local(state.gesture_samples.front());
    for (auto p : state.gesture_samples) local_samples.push_back(frame.world_to_local(p) - origin);
    auto path = tool::fit_motion_path(local_samples, state.path_kind);
    if (state.mode == tool::sel_drag_mode::rigid) {
        auto skeletons = skeleton_ids_from_nodes(state.moving);
        if (skeletons.empty()) return {};
        return sm::action_data{sm::rigid_translation{std::move(skeletons), std::move(path), state.reference, state.reference_bone}};
    }
    if (state.mode == tool::sel_drag_mode::rag_doll) {
        if (state.moving.size() != 1) return {};
        std::vector<sm::object_id> pins; pins.reserve(state.pinned.size());
        for (auto pin : state.pinned)
            if (&pin->owner() == &state.moving.front()->owner() && pin->id() != state.moving.front()->id()) pins.push_back(pin->id());
        std::ranges::sort(pins); pins.erase(std::unique(pins.begin(), pins.end()), pins.end());
        return sm::action_data{sm::ik_translation{state.moving.front()->id(), std::move(pins), std::move(path), state.reference, state.reference_bone}};
    }
    return {};
}

ui::animation_editing::action_properties::action_properties(QWidget* parent) : QWidget(parent) {
    auto* specific = new QGridLayout(this);
    specific->setContentsMargins(0, 0, 0, 0);
    bone_ = new QComboBox(this); bone_->setObjectName("rotation_bone"); bone_->setMinimumContentsLength(12);
    pivot_ = new QComboBox(this); pivot_->setObjectName("rotation_pivot"); pivot_->addItems({"Root", "Tip"});
    propagation_ = new QComboBox(this); propagation_->setObjectName("rotation_propagation"); propagation_->addItems({"Hierarchy", "Bone only"});
    effector_ = new QComboBox(this); effector_->setObjectName("ik_rotation_effector"); effector_->setMinimumContentsLength(12);
    pivot_node_ = new QComboBox(this); pivot_node_->setObjectName("ik_rotation_pivot_node"); pivot_node_->setMinimumContentsLength(12);
    angle_ = new QDoubleSpinBox(this); angle_->setObjectName("rotation_angle"); angle_->setRange(-360000, 360000);
    angle_->setDecimals(3); angle_->setSuffix("°"); angle_->setValue(90); angle_->setKeyboardTracking(false);
    bone_label_ = new QLabel("Bone", this); pivot_label_ = new QLabel("Pivot", this);
    propagation_label_ = new QLabel("Propagation", this); effector_label_ = new QLabel("Effector", this);
    pivot_node_label_ = new QLabel("Pivot node", this); angle_label_ = new QLabel("Angle", this);
    QList<QLabel*> labels{bone_label_, pivot_label_, propagation_label_, effector_label_, pivot_node_label_, angle_label_};
    QList<QWidget*> fields{bone_, pivot_, propagation_, effector_, pivot_node_, angle_};
    for (int col = 0; col < fields.size(); ++col) { specific->addWidget(labels[col], 0, col); specific->addWidget(fields[col], 1, col); }

    connect(bone_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) edit_as<sm::rigid_rotation>([&](auto& action) { action.bone = id(bone_->itemData(index).toString()); });
    });
    connect(pivot_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) edit_as<sm::rigid_rotation>([&](auto& action) { action.pivot = sm::rotation_pivot(index); });
    });
    connect(propagation_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) edit_as<sm::rigid_rotation>([&](auto& action) { action.propagation = sm::rotation_propagation(index); });
    });
    connect(effector_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) edit_as<sm::ik_rotation>([&](auto& action) { action.effector = id(effector_->itemData(index).toString()); });
    });
    connect(pivot_node_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) edit_as<sm::ik_rotation>([&](auto& action) { action.pivot_node = id(pivot_node_->itemData(index).toString()); });
    });
    connect(angle_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (updating_ || !current_) return;
        std::visit([&](const auto& action) {
            using T = std::remove_cvref_t<decltype(action)>;
            if constexpr (std::is_same_v<T, sm::rigid_rotation>)
                edit_as<sm::rigid_rotation>([&](auto& edited) { edited.angle = value / degrees; });
            else if constexpr (std::is_same_v<T, sm::ik_rotation>)
                edit_as<sm::ik_rotation>([&](auto& edited) { edited.angle = value / degrees; });
            else if constexpr (std::is_same_v<T, sm::rigid_translation> || std::is_same_v<T, sm::ik_translation>) {
            } else static_assert(always_false_v<T>, "Add an explicit angle-control decision for the new action type");
        }, *current_);
    });
    update_visibility();
}

void ui::animation_editing::action_properties::set_topology(const sm::topology* topology) {
    topology_ = topology;
    updating_ = true;
    bone_->clear(); effector_->clear(); pivot_node_->clear();
    if (topology_) for (auto skeleton : topology_->skeletons()) {
        for (auto bone : skeleton->bones()) bone_->addItem(QString::fromStdString(bone->name()), text(bone->id()));
        for (auto node : skeleton->nodes()) {
            const auto name = QString::fromStdString(node->name());
            effector_->addItem(name, text(node->id())); pivot_node_->addItem(name, text(node->id()));
        }
    }
    updating_ = false;
}

void ui::animation_editing::action_properties::set_action(const sm::animation_action* action) {
    updating_ = true;
    if (action) current_ = action->data; else current_.reset();
    if (current_) dispatch(*current_, [&](auto, const auto& concrete) {
        using T = std::remove_cvref_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, sm::rigid_rotation>) {
            bone_->setCurrentIndex(bone_->findData(text(concrete.bone)));
            pivot_->setCurrentIndex(int(concrete.pivot));
            propagation_->setCurrentIndex(int(concrete.propagation));
            angle_->setValue(concrete.angle * degrees);
        } else if constexpr (std::is_same_v<T, sm::ik_rotation>) {
            effector_->setCurrentIndex(effector_->findData(text(concrete.effector)));
            pivot_node_->setCurrentIndex(pivot_node_->findData(text(concrete.pivot_node)));
            angle_->setValue(concrete.angle * degrees);
        } else if constexpr (std::is_same_v<T, sm::rigid_translation> || std::is_same_v<T, sm::ik_translation>) {
            // Translation-specific controls live in the Selection/Animate Tool Properties panel.
        } else static_assert(always_false_v<T>, "Add an explicit property-panel decision for the new action type");
    });
    update_visibility();
    updating_ = false;
}

void ui::animation_editing::action_properties::preview(const sm::action_data& data) {
    if (!current_ || current_->index() != data.index()) return;
    current_ = data;
    updating_ = true;
    dispatch(data, [&](auto, const auto& concrete) {
        using T = std::remove_cvref_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, sm::rigid_rotation> || std::is_same_v<T, sm::ik_rotation>)
            angle_->setValue(concrete.angle * degrees);
        else if constexpr (std::is_same_v<T, sm::rigid_translation> || std::is_same_v<T, sm::ik_translation>) {
        } else static_assert(always_false_v<T>, "Add an explicit property-preview decision for the new action type");
    });
    updating_ = false;
}

void ui::animation_editing::action_properties::focus_primary_editor() {
    if (!current_) return;
    dispatch(*current_, [&](auto, const auto& concrete) {
        using T = std::remove_cvref_t<decltype(concrete)>;
        if constexpr (std::is_same_v<T, sm::rigid_rotation> || std::is_same_v<T, sm::ik_rotation>) {
            angle_->setFocus(Qt::MouseFocusReason); angle_->selectAll();
        } else if constexpr (std::is_same_v<T, sm::rigid_translation> || std::is_same_v<T, sm::ik_translation>) {
        } else static_assert(always_false_v<T>, "Add an explicit primary-editor decision for the new action type");
    });
}

void ui::animation_editing::action_properties::update_visibility() {
    bool rigid_rotation = false, ik_rotation = false;
    if (current_) dispatch(*current_, [&](auto, const auto& concrete) {
        using T = std::remove_cvref_t<decltype(concrete)>;
        static_assert(std::is_same_v<T, sm::rigid_rotation> || std::is_same_v<T, sm::ik_rotation> ||
                      std::is_same_v<T, sm::rigid_translation> || std::is_same_v<T, sm::ik_translation>,
                      "Add an explicit property-visibility decision for the new action type");
        rigid_rotation = std::is_same_v<T, sm::rigid_rotation>;
        ik_rotation = std::is_same_v<T, sm::ik_rotation>;
    });
    bone_label_->setVisible(rigid_rotation); bone_->setVisible(rigid_rotation);
    pivot_label_->setVisible(rigid_rotation); pivot_->setVisible(rigid_rotation);
    propagation_label_->setVisible(rigid_rotation); propagation_->setVisible(rigid_rotation);
    effector_label_->setVisible(ik_rotation); effector_->setVisible(ik_rotation);
    pivot_node_label_->setVisible(ik_rotation); pivot_node_->setVisible(ik_rotation);
    angle_label_->setVisible(rigid_rotation || ik_rotation); angle_->setVisible(rigid_rotation || ik_rotation);
}
