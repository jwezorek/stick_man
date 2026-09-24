#include "artwork_layer.hpp"
#include "scene.hpp"
#include "bone_item.hpp"
#include "node_item.hpp"
#include "skel_item.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {
    constexpr double handle_radius_pixels = 5.0;
    constexpr double handle_hit_radius_pixels = 8.0;
    constexpr double rotation_handle_offset_pixels = 24.0;

    QTransform qt_matrix(const sm::matrix& m) {
        return {m(0,0), m(1,0), m(0,1), m(1,1), m(0,2), m(1,2)};
    }
    sm::point point(QPointF p) { return {p.x(), p.y()}; }
    QPointF qpoint(sm::point p) { return {p.x, p.y}; }
    sm::matrix local_matrix(const sm::sprite_transform& t, sm::point origin) {
        return sm::translation_matrix(t.translation) * sm::rotation_matrix(t.rotation) *
            sm::scale_matrix(t.scale.x, t.scale.y) * sm::translation_matrix(-origin);
    }
    bool same(const sm::sprite_transform& a, const sm::sprite_transform& b) {
        return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
    }
    double length(sm::point p) { return std::hypot(p.x, p.y); }
    sm::point normalized(sm::point p, sm::point fallback = {0, 1}) {
        auto len = length(p);
        return len > 1e-9 ? sm::point{p.x / len, p.y / len} : fallback;
    }
    double squared_distance(QPointF a, QPointF b) {
        auto dx = a.x() - b.x(), dy = a.y() - b.y();
        return dx * dx + dy * dy;
    }
    struct transform_handle_geometry {
        QPointF center, left, right, top, bottom, rotate;
        std::array<QPointF, 4> corners;
        double radius = 0, hit_radius = 0;
    };
    transform_handle_geometry handle_geometry(const sm::matrix& transform, double width, double height, double scene_scale) {
        const auto half_width = width / 2.0, half_height = height / 2.0;
        const auto to_scene = [&](sm::point p) { return qpoint(sm::transform(p, transform)); };
        transform_handle_geometry result;
        result.center = to_scene({0, 0});
        result.left = to_scene({-half_width, 0});
        result.right = to_scene({half_width, 0});
        result.top = to_scene({0, half_height});
        result.bottom = to_scene({0, -half_height});
        result.corners = {
            to_scene({-half_width, half_height}), to_scene({half_width, half_height}),
            to_scene({half_width, -half_height}), to_scene({-half_width, -half_height})
        };
        auto up = normalized(point(result.top - result.center));
        auto scale = std::max(std::abs(scene_scale), 1e-9);
        result.rotate = result.top + QPointF(up.x, up.y) * (rotation_handle_offset_pixels / scale);
        result.radius = handle_radius_pixels / scale;
        result.hit_radius = handle_hit_radius_pixels / scale;
        return result;
    }
    struct frame_drag { sm::object_id character; std::string frame; };
    std::optional<frame_drag> parse_drag(const QMimeData* mime) {
        if (!mime || !mime->hasFormat(ui::canvas::frame_mime_type)) return {};
        auto object = QJsonDocument::fromJson(mime->data(ui::canvas::frame_mime_type)).object();
        auto id = sm::object_id::from_string(object["character"].toString().toStdString());
        auto frame = object["frame"].toString().toStdString();
        if (!id || frame.empty()) return {};
        return frame_drag{*id, frame};
    }
    ui::canvas::item::bone* bone_at(ui::canvas::scene& scene, QPointF pos) {
        for (auto* item : scene.items(pos))
            if (auto* bone = dynamic_cast<ui::canvas::item::bone*>(item); bone && bone->effectiveOpacity() > 0) return bone;
        return nullptr;
    }
}
ui::canvas::artwork_layer::artwork_layer(scene& scene, mdl::project& project) : QObject(&scene), scene_(scene), project_(project) {
    connect(&project, &mdl::project::project_changed, this, [this] { cancel_transform(); refresh(); });
    connect(&project, &mdl::project::artwork_changed, this, [this](mdl::project&, sm::object_id) { cancel_transform(); refresh(); });
    connect(&project, &mdl::project::new_project_opened, this, [this] { reset(); });
    connect(&project, &mdl::project::refresh_canvas, this, [this] { cancel_transform(); refresh(); });
}
std::string ui::canvas::artwork_layer::active_appearance(const sm::object_id& id) const {
    auto character = project_.core().character(id);
    if (!character) return {};
    const auto& apps = character->get().artwork().appearances();
    auto it = active_.find(id);
    if (it != active_.end() && apps.contains(it->second)) return it->second;
    return apps.empty() ? "" : apps.begin()->first;
}
void ui::canvas::artwork_layer::set_active_appearance(const sm::object_id& id, const std::string& name) {
    if (active_appearance(id) == name && active_.contains(id)) return;
    cancel_transform(); active_[id] = name;
    if (selected_ && selected_->character == id && selected_->appearance != name) {
        selected_.reset(); emit selection_changed();
    }
    scene_.update(); emit appearance_changed();
}
void ui::canvas::artwork_layer::set_selected_slot(const sm::object_id& id, const std::string& slot) {
    sprite_selection value{id, active_appearance(id), slot};
    if (selected_ && *selected_ == value) return;
    cancel_transform(); selected_ = value; scene_.update(); emit selection_changed();
}
void ui::canvas::artwork_layer::clear_selected_slot() {
    if (!selected_) return;
    cancel_transform(); selected_.reset(); scene_.update(); emit selection_changed();
}
std::string ui::canvas::artwork_layer::preview_state(const sm::object_id& id, const std::string& slot) const {
    if (!project_.core().character(id)) return "default";
    const auto& definitions = project_.core().artwork(id).slot_definitions();
    auto definition = definitions.find(slot);
    auto character = preview_states_.find(id);
    if (definition == definitions.end() || character == preview_states_.end()) return "default";
    auto state = character->second.find(slot);
    if (state == character->second.end() || std::ranges::find(definition->second.states, state->second) == definition->second.states.end()) return "default";
    return state->second;
}
void ui::canvas::artwork_layer::set_preview_state(const sm::object_id& id, const std::string& slot, const std::string& state) {
    const auto& vocabulary = project_.core().artwork(id).slot_definitions().at(slot).states;
    if (std::ranges::find(vocabulary, state) == vocabulary.end()) throw std::invalid_argument("Unknown preview state.");
    if (preview_state(id, slot) == state) return;
    cancel_transform();
    if (state == "default") preview_states_[id].erase(slot);
    else preview_states_[id][slot] = state;
    scene_.update(); emit preview_changed();
}
void ui::canvas::artwork_layer::reset_preview_states(const sm::object_id& id) {
    cancel_transform(); preview_states_.erase(id); scene_.update(); emit preview_changed();
}
std::vector<ui::canvas::artwork_layer::drawable> ui::canvas::artwork_layer::drawables() const {
    std::vector<drawable> result;
    if (!show_artwork_) return result;
    // Stable cross-character order; painter order within each appearance is authoritative.
    std::vector<sm::object_id> ids;
    for (auto character : project_.core().characters()) ids.push_back(character->id());
    std::ranges::sort(ids);
    for (const auto& id : ids) {
        auto name = active_appearance(id); if (name.empty()) continue;
        std::map<std::string, std::string> states;
        for (const auto& [slot, _] : project_.core().artwork(id).slot_definitions())
            states.emplace(slot, preview_state(id, slot));
        for (auto sprite : project_.core().resolve_artwork(id, name, states, preview_topology_)) {
            sprite_selection selection{id, name, sprite.slot};
            if (drag_ && drag_->selection == selection)
                sprite.transform = sprite.bone_transform * local_matrix(drag_->preview, sprite.registration_origin);
            result.push_back({selection, sprite.image, sprite.transform, sprite.bone_transform});
        }
    }
    return result;
}
void ui::canvas::artwork_layer::paint(QPainter& painter) const {
    for (const auto& sprite : drawables()) {
        const auto& img = sprite.image;
        auto stride = img.height() > 1 ? img.row(1).data() - img.row(0).data() : img.width() * 4;
        QImage image(img.row(0).data(), img.width(), img.height(), qsizetype(stride), QImage::Format_RGBA8888);
        // Core coordinates are Cartesian. Pixel memory begins at the top left.
        const sm::matrix pixels_to_world = sprite.transform * sm::translation_matrix(-img.width() / 2.0, img.height() / 2.0) * sm::scale_matrix(1, -1);
        painter.save(); painter.setWorldTransform(qt_matrix(pixels_to_world), true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(QPointF(0, 0), image); painter.restore();
    }
}
void ui::canvas::artwork_layer::paint_selection(QPainter& painter) const {
    if (!selected_ || !show_artwork_) return;
    for (const auto& sprite : drawables()) if (sprite.selection == *selected_) {
        const auto half_width = sprite.image.width() / 2.0;
        const auto half_height = sprite.image.height() / 2.0;
        painter.save(); painter.setWorldTransform(qt_matrix(sprite.transform), true);
        QPen pen(QColor(0, 160, 210), 1, Qt::DashLine); pen.setCosmetic(true);
        painter.setPen(pen); painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(-half_width, -half_height, sprite.image.width(), sprite.image.height()));
        painter.restore();

        if (!transform_editing_) continue;

        const auto handles = handle_geometry(sprite.transform, sprite.image.width(), sprite.image.height(), scene_.scale());

        painter.save();
        QPen handle_pen(QColor(0, 160, 210), 1); handle_pen.setCosmetic(true);
        painter.setPen(handle_pen);
        painter.setBrush(QColor(245, 245, 245));
        painter.drawLine(handles.top, handles.rotate);
        const auto square = [&](QPointF p) {
            painter.drawRect(QRectF(p.x() - handles.radius, p.y() - handles.radius,
                2 * handles.radius, 2 * handles.radius));
        };
        square(handles.left); square(handles.right); square(handles.top); square(handles.bottom);
        for (auto corner : handles.corners) square(corner);
        painter.drawEllipse(handles.rotate, handles.radius, handles.radius);
        painter.restore();
    }
}
std::optional<ui::canvas::sprite_selection> ui::canvas::artwork_layer::hit_test(QPointF position) const {
    auto sprites = drawables();
    for (auto it = sprites.rbegin(); it != sprites.rend(); ++it) {
        if (std::abs(it->transform.determinant()) < 1e-12) continue;
        auto p = sm::transform(point(position), it->transform.inverse());
        auto x = p.x + it->image.width()/2.0, y = it->image.height()/2.0 - p.y;
        if (x >= 0 && y >= 0 && x < it->image.width() && y < it->image.height() && it->image.row(int(y))[int(x)*4+3] != 0)
            return it->selection;
    }
    return {};
}
std::optional<sm::sprite_transform> ui::canvas::artwork_layer::selected_transform() const {
    if (drag_ && selected_ && drag_->selection == *selected_) return drag_->preview;
    if (!selected_ || !project_.core().character(selected_->character)) return {};
    const auto& apps = project_.core().artwork(selected_->character).appearances();
    auto it = apps.find(selected_->appearance); if (it == apps.end()) return {};
    for (const auto& slot : it->second.appearance_slots) if (slot.slot == selected_->slot) return slot.transform;
    return {};
}

std::optional<ui::canvas::artwork_layer::transform_target> ui::canvas::artwork_layer::transform_target_at(QPointF position) const {
    if (selected_) {
        for (const auto& sprite : drawables()) if (sprite.selection == *selected_) {
            const auto half_width = sprite.image.width() / 2.0;
            const auto half_height = sprite.image.height() / 2.0;
            const auto handles = handle_geometry(sprite.transform, sprite.image.width(), sprite.image.height(), scene_.scale());
            auto hit_radius_squared = handles.hit_radius * handles.hit_radius;
            if (squared_distance(position, handles.rotate) <= hit_radius_squared) return transform_target{*selected_, sprite_drag::rotate};
            for (auto corner : handles.corners)
                if (squared_distance(position, corner) <= hit_radius_squared) return transform_target{*selected_, sprite_drag::scale_xy};
            if (squared_distance(position, handles.left) <= hit_radius_squared || squared_distance(position, handles.right) <= hit_radius_squared)
                return transform_target{*selected_, sprite_drag::scale_x};
            if (squared_distance(position, handles.top) <= hit_radius_squared || squared_distance(position, handles.bottom) <= hit_radius_squared)
                return transform_target{*selected_, sprite_drag::scale_y};

            if (std::abs(sprite.transform.determinant()) >= 1e-12) {
                auto local = sm::transform(point(position), sprite.transform.inverse());
                if (local.x >= -half_width && local.x <= half_width && local.y >= -half_height && local.y <= half_height)
                    return transform_target{*selected_, sprite_drag::translate};
            }
        }
    }
    if (auto hit = hit_test(position)) return transform_target{*hit, sprite_drag::translate};
    return {};
}
void ui::canvas::artwork_layer::refresh() {
    std::erase_if(active_, [&](const auto& entry) { return !project_.core().character(entry.first); });
    for (auto& [id, states] : preview_states_)
        std::erase_if(states, [&](const auto& state) { return preview_state(id, state.first) == "default"; });
    std::erase_if(preview_states_, [](const auto& entry) { return entry.second.empty(); });
    if (selected_ && !selected_transform()) { selected_.reset(); emit selection_changed(); }
    refresh_guides(); scene_.update();
}
void ui::canvas::artwork_layer::reset() {
    drag_.reset(); selected_.reset(); active_.clear(); preview_states_.clear(); refresh(); emit selection_changed(); emit appearance_changed(); emit preview_changed(); emit transform_changed();
}
void ui::canvas::artwork_layer::set_show_artwork(bool show) { cancel_transform(); show_artwork_ = show; scene_.update(); }
void ui::canvas::artwork_layer::set_skeleton_display(skeleton_display display) {
    if (skeleton_display_ == display) return;
    skeleton_display_ = display;
    refresh_guides();
    scene_.update();
}
void ui::canvas::artwork_layer::refresh_guides() {
    const bool show_nodes = skeleton_display_ != skeleton_display::hidden;
    const bool show_bones = skeleton_display_ == skeleton_display::wireframe ||
        skeleton_display_ == skeleton_display::visible;
    const bool wireframe = skeleton_display_ == skeleton_display::wireframe_nodes ||
        skeleton_display_ == skeleton_display::wireframe;

    for (auto* bone : scene_.bone_items()) {
        bone->set_wireframe(wireframe);
        bone->setVisible(show_bones);
    }
    for (auto* node : scene_.node_items()) {
        node->set_wireframe(wireframe);
        node->setVisible(show_nodes);
    }

    // Skeleton/character aggregate frames are selection guides rather than nodes or bones.
    // Keep their selection state intact, but hide them whenever bones are not part of the view.
    for (auto* item : scene_.canvas_items()) {
        if (dynamic_cast<ui::canvas::item::skeleton*>(item) ||
            dynamic_cast<ui::canvas::item::character*>(item)) {
            if (auto* graphics = dynamic_cast<QGraphicsItem*>(item))
                graphics->setOpacity(show_bones ? 1.0 : 0.0);
        }
    }
}
void ui::canvas::artwork_layer::set_transform_editing(bool enabled) {
    if (transform_editing_ == enabled) return;
    cancel_transform();
    transform_editing_ = enabled;
    scene_.update();
    emit transform_changed();
}
bool ui::canvas::artwork_layer::begin_transform(QPointF position) {
    cancel_transform();
    auto target = transform_target_at(position);
    if (!target) return false;
    return begin_transform(position, *target);
}
bool ui::canvas::artwork_layer::begin_transform(QPointF position, sprite_drag mode) {
    cancel_transform();
    auto hit = hit_test(position);
    if (!hit) return false;
    return begin_transform(position, transform_target{*hit, mode});
}
bool ui::canvas::artwork_layer::begin_transform(QPointF position, const transform_target& target) {
    if (project_.animation_mode()) return false;
    if (auto* character = scene_.character_item(target.selection.character)) scene_.set_selection(character, true);
    set_active_appearance(target.selection.character, target.selection.appearance);
    set_selected_slot(target.selection.character, target.selection.slot);
    auto transform = selected_transform(); if (!transform) return false;
    for (const auto& sprite : drawables()) if (sprite.selection == target.selection) {
        sm::matrix inverse = sprite.bone_transform.inverse();
        drag_ = drag_state{target.selection, *transform, *transform, inverse, sm::transform(point(position), inverse), target.mode};
        emit transform_changed();
        return true;
    }
    return false;
}
void ui::canvas::artwork_layer::update_transform(QPointF position) {
    if (!drag_) return;
    auto& d = *drag_;
    auto current = sm::transform(point(position), d.bone_inverse);
    d.preview = d.before;
    if (d.mode == sprite_drag::translate) d.preview.translation += current - d.start;
    else {
        auto a = d.start - d.before.translation, b = current - d.before.translation;
        if (d.mode == sprite_drag::rotate) {
            if (std::hypot(a.x,a.y) > 1e-6 && std::hypot(b.x,b.y) > 1e-6)
                d.preview.rotation += std::atan2(b.y,b.x) - std::atan2(a.y,a.x);
        } else {
            a = sm::transform(a, sm::rotation_matrix(-d.before.rotation));
            b = sm::transform(b, sm::rotation_matrix(-d.before.rotation));
            auto scale_axis = [](double original, double start, double now) {
                return std::abs(start) > 1e-6 ? original * now / start : original + (now-start)/50.0;
            };
            if (d.mode == sprite_drag::scale_xy) {
                auto a_length = std::hypot(a.x, a.y);
                auto b_length = std::hypot(b.x, b.y);
                if (a_length > 1e-6) {
                    auto factor = b_length / a_length;
                    if (a.x * b.x + a.y * b.y < 0) factor = -factor;
                    d.preview.scale.x = d.before.scale.x * factor;
                    d.preview.scale.y = d.before.scale.y * factor;
                }
            } else {
                if (d.mode == sprite_drag::scale_x) d.preview.scale.x = scale_axis(d.before.scale.x,a.x,b.x);
                if (d.mode == sprite_drag::scale_y) d.preview.scale.y = scale_axis(d.before.scale.y,a.y,b.y);
            }
        }
    }
    scene_.update(); emit transform_changed();
}
void ui::canvas::artwork_layer::end_transform(QPointF position) {
    if (!drag_) return;
    update_transform(position);
    auto d = *drag_; drag_.reset();
    if (!same(d.before,d.preview)) project_.edit_artwork(d.selection.character, [&](auto& art) {
        auto app = art.appearances().at(d.selection.appearance);
        for (auto& slot : app.appearance_slots) if (slot.slot == d.selection.slot) slot.transform = d.preview;
        art.set_appearance(d.selection.appearance, std::move(app));
    });
    scene_.update(); emit transform_changed();
}
void ui::canvas::artwork_layer::cancel_transform() {
    if (drag_) { drag_.reset(); scene_.update(); emit transform_changed(); }
}
void ui::canvas::artwork_layer::assign_frame(const sm::object_id& character, const sm::object_id& bone,
        const std::string& frame, const std::optional<std::string>& slot) {
    const auto& art = project_.core().artwork(character);
    if (!art.frames().contains(frame)) throw std::invalid_argument("Unknown frame.");
    bool owned = false;
    for (auto skeleton : project_.core().character(character)->get().rig().skeletons())
        for (auto candidate : skeleton->bones()) if (candidate->id() == bone) owned = true;
    if (!owned) throw std::invalid_argument("Drop images on a bone in their character.");
    auto name = active_appearance(character);
    if (name.empty()) name = "Appearance";
    std::string channel = slot.value_or(frame);
    if (!slot) for (int suffix=2; art.slot_definitions().contains(channel); ++suffix) channel = frame + "_" + std::to_string(suffix);
    project_.edit_artwork(character, [&](auto& a) {
        if (!slot) a.add_slot(channel, {bone});
        else if (a.slot_definitions().at(channel).bone != bone) throw std::invalid_argument("Slot belongs to another bone.");
        if (!a.appearances().contains(name)) a.add_appearance(name);
        auto app = a.appearances().at(name);
        auto it = std::ranges::find_if(app.appearance_slots, [&](const auto& s) { return s.slot == channel; });
        if (it == app.appearance_slots.end()) { app.appearance_slots.push_back({channel}); it = std::prev(app.appearance_slots.end()); }
        it->states["default"] = frame;
        a.set_appearance(name, std::move(app));
    });
    if (auto* item = scene_.character_item(character)) scene_.set_selection(item, true);
    set_active_appearance(character,name); set_selected_slot(character,channel);
}
bool ui::canvas::artwork_layer::can_drop(const QMimeData* mime, QPointF position) const {
    if (project_.animation_mode()) return false;
    auto data = parse_drag(mime); auto* bone = bone_at(scene_,position);
    if (!data || !bone || !project_.core().character(data->character)) return false;
    auto parent = bone->model().owner().parent_character();
    return parent && parent->get().id() == data->character && project_.core().artwork(data->character).frames().contains(data->frame);
}
bool ui::canvas::artwork_layer::drop_frame(const QMimeData* mime, QPointF position) {
    if (!can_drop(mime,position)) return false;
    auto data = *parse_drag(mime); auto bone = bone_at(scene_,position)->model().id();
    QStringList choices{"New slot"}; std::vector<std::string> names;
    for (const auto& [name, def] : project_.core().artwork(data.character).slot_definitions()) if (def.bone == bone) {
        names.push_back(name); choices.push_back(QString::fromStdString(name));
    }
    std::optional<std::string> selected;
    if (!names.empty()) {
        bool ok = false;
        auto choice = QInputDialog::getItem(scene_.views().first(), "Assign image", "Visual channel", choices, 0, false, &ok);
        if (!ok) return false;
        auto index = choices.indexOf(choice); if (index > 0) selected = names.at(index-1);
    }
    assign_frame(data.character,bone,data.frame,selected); return true;
}
