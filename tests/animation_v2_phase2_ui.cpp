#include "ui/stick_man.hpp"
#include "ui/canvas/artwork_layer.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/panes/animation_editor.hpp"
#include "ui/panes/animation_pane.hpp"
#include "ui/widgets/pose_strip.hpp"
#include <QtWidgets>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

sm::object_id node_at(const mdl::project& project, sm::point target) {
    for (auto skeleton : project.topology().skeletons()) {
        for (auto node : skeleton->nodes()) {
            if (node->world_pos() == target) return node->id();
        }
    }
    throw std::runtime_error("fixture node missing");
}

std::size_t opaque_pixels(const QPixmap& pixmap) {
    auto image = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);
    std::size_t count = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            if (row[x * 4 + 3]) ++count;
        }
    }
    return count;
}

QImage scene_image(ui::canvas::scene& scene) {
    QImage image(500, 300, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    scene.render(&painter, QRectF(0, 0, 500, 300), QRectF(-20, -10, 160, 100));
    return image;
}

bool same_image(const QImage& a, const QImage& b) {
    return a.size() == b.size() && a.format() == b.format() &&
        std::equal(a.constBits(), a.constBits() + a.sizeInBytes(), b.constBits());
}

sm::image_resource sprite_image() {
    std::vector<std::uint8_t> rgba(18 * 18 * 4, 255);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = 40;
        rgba[i + 1] = 180;
        rgba[i + 2] = 90;
        rgba[i + 3] = 230;
    }
    return sm::image_resource::from_rgba(18, 18, rgba);
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        ui::stick_man window;
        window.resize(1200, 800);
        window.show();
        app.processEvents();
        auto& model = window.project();

        model.add_new_skeleton_root({10, 20});
        model.add_new_skeleton_root({60, 20});
        model.add_new_skeleton_root({110, 20});
        const auto n1 = node_at(model, {10, 20});
        const auto n2 = node_at(model, {60, 20});
        const auto n3 = node_at(model, {110, 20});
        require(model.add_bone(n1, n2) == sm::result::success, "fixture bone 1 failed");
        require(model.add_bone(n2, n3) == sm::result::success, "fixture bone 2 failed");

        auto skeleton = *model.topology().skeletons().begin();
        std::vector<sm::const_skel_ref> members{skeleton};
        const auto character_id = model.make_character(members).value();

        sm::animation animation;
        animation.name = "Phase 2";
        const auto animation_id = animation.id;
        model.edit_animation_data(character_id, [&](auto& data) {
            data.animations.push_back(animation);
        });

        auto* browser = window.findChild<ui::pane::animation*>();
        require(browser && browser->open_animation(character_id, animation_id),
            "open animation failed");
        app.processEvents();

        auto* editor = window.findChild<ui::pane::animation_editor*>("animation_editor");
        auto* strip = editor ? editor->findChild<ui::pose_strip*>() : nullptr;
        require(strip != nullptr, "Pose Strip missing");

        auto* add = editor->findChild<QPushButton*>("new_pose");
        auto* duplicate = editor->findChild<QPushButton*>("duplicate_pose");
        auto* rename = editor->findChild<QPushButton*>("rename_pose");
        auto* remove = editor->findChild<QPushButton*>("delete_pose");
        auto* ghost = editor->findChild<QCheckBox*>("show_previous_pose");
        require(add && duplicate && rename && remove && ghost,
            "Phase 2 pose controls missing");

        add->click();
        app.processEvents();
        const auto first = model.animation_session_keyframe();
        require(first.has_value(), "Add first pose did not select keyframe");

        auto& scene = window.canvases().active_canvas();
        scene.artwork().set_show_artwork(false);
        scene.artwork().set_skeleton_display(ui::canvas::skeleton_display::hidden);
        strip->refresh();

        const auto before = model.topology().get<sm::node>(n1)->get().world_pos();
        const auto unskinned = strip->render_preview(*first);
        require(opaque_pixels(unskinned) > 0,
            "unskinned preview did not force bones visible");
        require(model.topology().get<sm::node>(n1)->get().world_pos() == before,
            "thumbnail generation mutated active pose");

        const sm::point pose1{20, 30};
        model.transform_node_positions({{n1, before}}, {{n1, pose1}});
        add->click();
        const auto second = model.animation_session_keyframe();
        require(second.has_value() && *second != *first, "second keyframe missing");
        const sm::point pose2{45, 30};
        model.transform_node_positions({{n1, pose1}}, {{n1, pose2}});

        ghost->setChecked(false);
        const auto no_ghost = scene_image(scene);
        ghost->setChecked(true);
        const auto with_ghost = scene_image(scene);
        require(!same_image(no_ghost, with_ghost),
            "previous-pose ghost did not affect second-pose rendering");

        require(model.select_animation_keyframe(*first) == sm::result::success,
            "select first failed");
        const auto first_no_ghost = scene_image(scene);
        ghost->setChecked(false);
        const auto first_off = scene_image(scene);
        require(same_image(first_no_ghost, first_off),
            "first keyframe incorrectly wrapped previous-pose ghost");

        browser->leave_animation();
        app.processEvents();

        const auto bone_id = (*model.topology().skeletons().begin())->bones().front()->id();
        const auto image = sprite_image();
        model.edit_artwork(character_id, [&](auto& art) {
            art.insert_frame("part", {image, {}});
            art.add_slot("part", {bone_id});
            art.add_appearance("Look", {{{"part", {{"default", "part"}}}}});
        });

        require(browser->open_animation(character_id, animation_id),
            "reopen animation failed");
        app.processEvents();
        editor = window.findChild<ui::pane::animation_editor*>("animation_editor");
        strip = editor->findChild<ui::pose_strip*>();
        auto& layer = window.canvases().active_canvas().artwork();
        layer.set_active_appearance(character_id, "Look");

        layer.set_show_artwork(false);
        layer.set_skeleton_display(ui::canvas::skeleton_display::visible);
        strip->refresh();
        const auto bones_only = strip->render_preview(*first);

        layer.set_show_artwork(true);
        layer.set_skeleton_display(ui::canvas::skeleton_display::hidden);
        strip->refresh();
        const auto artwork_only = strip->render_preview(*first);

        layer.set_skeleton_display(ui::canvas::skeleton_display::visible);
        strip->refresh();
        const auto combined = strip->render_preview(*first);

        require(opaque_pixels(bones_only) > 0 && opaque_pixels(artwork_only) > 0 &&
            opaque_pixels(combined) > 0, "thumbnail visibility mode produced empty preview");
        require(!same_image(
            bones_only.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),
            artwork_only.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)),
            "bones-only and artwork-only previews are identical");
        require(!same_image(
            combined.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),
            artwork_only.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied)),
            "combined preview did not add skeleton to partially skinned artwork");

        browser->leave_animation();
        std::cout << "PASS Animation V2 Phase 2 UI\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
