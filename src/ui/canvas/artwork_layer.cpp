#include "artwork_layer.hpp"
#include "scene.hpp"
#include "bone_item.hpp"
#include "node_item.hpp"
#include "skel_item.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
    QTransform qt_matrix(const sm::matrix& m) {
        return {m(0,0), m(1,0), m(0,1), m(1,1), m(0,2), m(1,2)};
    }
    sm::point point(QPointF p) { return {p.x(), p.y()}; }
    sm::matrix local_matrix(const sm::sprite_transform& t, sm::point origin) {
        return sm::translation_matrix(t.translation) * sm::rotation_matrix(t.rotation) *
            sm::scale_matrix(t.scale.x, t.scale.y) * sm::translation_matrix(-origin);
    }
    bool same(const sm::sprite_transform& a, const sm::sprite_transform& b) {
        return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
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
std::vector<ui::canvas::artwork_layer::drawable> ui::canvas::artwork_layer::drawables() const {
    std::vector<drawable> result;
    if (!show_artwork_) return result;
    // Stable cross-character order; painter order within each appearance is authoritative.
    std::vector<sm::object_id> ids;
    for (auto character : project_.core().characters()) ids.push_back(character->id());
    std::ranges::sort(ids);
    for (const auto& id : ids) {
        auto name = active_appearance(id); if (name.empty()) continue;
        for (auto sprite : project_.core().resolve_artwork(id, name)) {
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
        painter.save(); painter.setWorldTransform(qt_matrix(sprite.transform), true);
        QPen pen(QColor(0, 160, 210), 1, Qt::DashLine); pen.setCosmetic(true);
        painter.setPen(pen); painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(-sprite.image.width()/2.0, -sprite.image.height()/2.0, sprite.image.width(), sprite.image.height()));
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
    if (!selected_ || !project_.core().character(selected_->character)) return {};
    const auto& apps = project_.core().artwork(selected_->character).appearances();
    auto it = apps.find(selected_->appearance); if (it == apps.end()) return {};
    for (const auto& slot : it->second.appearance_slots) if (slot.slot == selected_->slot) return slot.transform;
    return {};
}
void ui::canvas::artwork_layer::refresh() {
    std::erase_if(active_, [&](const auto& entry) { return !project_.core().character(entry.first); });
    if (selected_ && !selected_transform()) { selected_.reset(); emit selection_changed(); }
    refresh_guides(); scene_.update();
}
void ui::canvas::artwork_layer::reset() {
    drag_.reset(); selected_.reset(); active_.clear(); refresh(); emit selection_changed(); emit appearance_changed();
}
void ui::canvas::artwork_layer::set_show_artwork(bool show) { cancel_transform(); show_artwork_ = show; scene_.update(); }
void ui::canvas::artwork_layer::set_show_skeleton(bool show) { show_skeleton_ = show; refresh_guides(); scene_.update(); }
void ui::canvas::artwork_layer::set_wireframe(bool value) { wireframe_ = value; refresh_guides(); scene_.update(); }
void ui::canvas::artwork_layer::refresh_guides() {
    // Opacity preserves the existing selection visibility semantics of rig items.
    for (auto* item : scene_.items()) if (!item->parentItem()) item->setOpacity(show_skeleton_ ? 1 : 0);
    for (auto* bone : scene_.bone_items()) bone->setBrush(wireframe_ ? QBrush(Qt::NoBrush) : QBrush(Qt::black));
    for (auto* node : scene_.node_items()) node->setBrush(wireframe_ ? QBrush(Qt::NoBrush) : QBrush(Qt::white));
}
bool ui::canvas::artwork_layer::begin_transform(QPointF position, sprite_drag mode) {
    cancel_transform();
    auto hit = hit_test(position);
    if (!hit) { selected_.reset(); scene_.update(); emit selection_changed(); return false; }
    if (auto* character = scene_.character_item(hit->character)) scene_.set_selection(character, true);
    set_active_appearance(hit->character, hit->appearance);
    set_selected_slot(hit->character, hit->slot);
    auto transform = selected_transform(); if (!transform) return false;
    for (const auto& sprite : drawables()) if (sprite.selection == *hit) {
        sm::matrix inverse = sprite.bone_transform.inverse();
        drag_ = drag_state{*hit, *transform, *transform, inverse, sm::transform(point(position), inverse), mode};
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
            if (d.mode == sprite_drag::scale_x || d.mode == sprite_drag::scale_xy) d.preview.scale.x = scale_axis(d.before.scale.x,a.x,b.x);
            if (d.mode == sprite_drag::scale_y || d.mode == sprite_drag::scale_xy) d.preview.scale.y = scale_axis(d.before.scale.y,a.y,b.y);
        }
    }
    scene_.update();
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
    scene_.update();
}
void ui::canvas::artwork_layer::cancel_transform() { if (drag_) { drag_.reset(); scene_.update(); } }
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
