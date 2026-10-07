#include "ui/stick_man.hpp"
#include "ui/panes/animation_pane.hpp"
#include "ui/panes/animation_editor.hpp"
#include "ui/widgets/pose_strip.hpp"
#include "ui/widgets/timeline.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/canvas/node_item.hpp"
#include "ui/tools/tool_manager.hpp"
#include "ui/tools/constraint_tool.hpp"
#include "ui/tools/edit_tool_panel.hpp"

#include <QtWidgets>
#include <iostream>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

QPushButton* tool_button(ui::stick_man& window, const QString& tooltip) {
    for (auto* button : window.findChildren<QPushButton*>())
        if (button->toolTip() == tooltip)
            return button;
    return nullptr;
}

void lock_on_existing_frame(QApplication& app) {
    ui::stick_man window;
    window.resize(1200, 800);
    window.show();
    app.processEvents();
    auto& model = window.project();
    const auto add_node = [&](sm::point pos) {
        model.add_new_skeleton_root(pos);
        for (auto skel : model.topology().skeletons())
            if (skel->root_node().world_pos() == pos)
                return skel->root_node().id();
        throw std::runtime_error("missing fixture node");
    };
    const auto hip = add_node({0, 100});
    const auto ankle = add_node({0, 0});
    const auto toe = add_node({50, 0});
    require(model.add_bone(hip, ankle) == sm::result::success, "create leg");
    require(model.add_bone(ankle, toe) == sm::result::success, "create foot");
    auto skel = *model.topology().skeletons().begin();
    const auto character = model.make_character(std::vector<sm::const_skel_ref>{skel}).value();
    sm::animation animation;
    const auto animation_id = animation.id;
    model.edit_animation_data(character, [&](auto& data) { data.animations.push_back(animation); });
    auto* browser = window.findChild<ui::pane::animation*>();
    require(browser->open_animation(character, animation_id), "open fixture animation");
    require(model.add_animation_keyframe() == sm::result::success, "add first frame");
    const auto first = *model.animation_session_keyframe();
    require(model.add_animation_keyframe() == sm::result::success, "add second frame");
    const auto second = *model.animation_session_keyframe();
    require(model.select_animation_keyframe(first) == sm::result::success, "select source frame");
    auto& canvases = window.canvases();
    auto& canvas = canvases.active_canvas();
    window.tool_mgr().set_current_tool(canvases, ui::tool::id::constraint);
    auto& tool = window.tool_mgr().current_tool();
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos({ 0, 0 });
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    tool.mousePressEvent(canvas, &press);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos({ 0, 0 });
    release.setButton(Qt::LeftButton);
    tool.mouseReleaseEvent(canvas, &release);
    require(model.animation_session_pinned_nodes().contains(ankle), "constraint tool did not pin ankle");
    window.tool_mgr().set_current_tool(canvases, ui::tool::id::edit);
    require(model.select_animation_keyframe(second) == sm::result::success, "select destination frame");
    require(canvas.is_node_pinned(ankle), "existing destination frame lost ankle lock");
    canvas.views().first()->centerOn(0, 25);
    app.processEvents();
    QGraphicsPathItem* lock = nullptr;
    for (auto* node : canvas.node_items()) {
        if (node->model().id() != ankle)
            continue;
        for (auto* child : node->childItems())
            if (auto* path = dynamic_cast<QGraphicsPathItem*>(child); path && path->isVisible())
                lock = path;
    }
    require(lock != nullptr, "locked ankle has no lock graphic");
    auto* viewport = canvas.views().first()->viewport();
    const auto with_lock = viewport->grab().toImage();
    lock->hide();
    const auto without_lock = viewport->grab().toImage();
    lock->show();
    // A black outline alone disappears into the filled foot bone. The badge
    // must contribute a contrasting light interior, independent of the bone.
    int contrasting_pixels = 0;
    for (int y = 0; y < with_lock.height(); ++y)
        for (int x = 0; x < with_lock.width(); ++x)
            if (qGray(with_lock.pixel(x, y)) > qGray(without_lock.pixel(x, y)) + 128)
                ++contrasting_pixels;
    require(contrasting_pixels >= 20, "lock indicator disappears over a dark bone");
    browser->leave_animation();
}

void transition_rotation_constraint_tool(QApplication& app) {
    ui::stick_man window;
    window.resize(1200, 800);
    window.show();
    app.processEvents();
    auto& model = window.project();
    const auto add_node = [&](sm::point pos) {
        model.add_new_skeleton_root(pos);
        for (auto skel : model.topology().skeletons())
            if (skel->root_node().world_pos() == pos)
                return skel->root_node().id();
        throw std::runtime_error("missing fixture node");
    };
    const auto root = add_node({0, 0});
    const auto tip = add_node({100, 0});
    require(model.add_bone(root, tip) == sm::result::success, "create tool-test bone");
    auto root_node = model.topology().get<sm::node>(root);
    require(root_node && !root_node->get().child_bones().empty(), "tool-test bone missing");
    const auto bone_id = root_node->get().child_bones().front()->id();
    require(model.add_rotation_constraint(bone_id, sm::rotation_reference::world(), {-1.0, 2.0}).has_value(),
        "create persistent comparison constraint");

    auto skel = *model.topology().skeletons().begin();
    const auto character = model.make_character(std::vector<sm::const_skel_ref>{skel}).value();
    sm::animation animation;
    const auto animation_id = animation.id;
    model.edit_animation_data(character, [&](auto& data) { data.animations.push_back(animation); });
    auto* browser = window.findChild<ui::pane::animation*>();
    require(browser->open_animation(character, animation_id), "open tool-test animation");
    require(model.add_animation_keyframe() == sm::result::success, "add tool-test first frame");
    const auto first = *model.animation_session_keyframe();
    require(model.add_animation_keyframe() == sm::result::success, "add tool-test second frame");
    const auto second = *model.animation_session_keyframe();
    require(model.select_animation_keyframe(first) == sm::result::success, "select tool-test source");

    auto& canvases = window.canvases();
    auto& canvas = canvases.active_canvas();
    window.tool_mgr().set_current_tool(canvases, ui::tool::id::constraint);
    auto* reference = window.findChild<QComboBox*>("constraint_create_reference");
    require(reference != nullptr, "constraint reference combo missing");
    reference->setCurrentIndex(int(sm::rotation_reference_kind::world));
    auto& tool = window.tool_mgr().current_tool();
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setScenePos({50, 0});
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    tool.mousePressEvent(canvas, &press);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos({50, 0});
    release.setButton(Qt::LeftButton);
    tool.mouseReleaseEvent(canvas, &release);
    auto locals = model.animation_session_rotation_constraints();
    require(locals.size() == 1, "Constraint Tool did not create transition-local rotation constraint");
    require(model.core().constraints().size() == 1, "transition-local constraint modified persistent constraints");
    const auto local_id = locals.begin()->first;
    require(locals.begin()->second.rotation()->target_bone == bone_id, "transition-local constraint targeted wrong bone");

    model.undo();
    require(model.animation_session_rotation_constraints().empty(), "transition-local constraint create undo failed");
    require(model.redo() == sm::result::success, "transition-local constraint create redo failed");
    require(model.animation_session_rotation_constraints().contains(local_id), "transition-local constraint create redo lost identity");
    canvas.clear_constraint_selection(false);
    canvas.sync_to_model();

    bool saw_persistent = false, saw_transition = false;
    for (auto* item : canvas.items()) {
        if (auto* path = dynamic_cast<QGraphicsPathItem*>(item)) {
            if (path->pen().color() == QColor("mediumpurple")) saw_persistent = true;
            if (path->pen().color() == QColor("deepskyblue")) saw_transition = true;
        }
    }
    require(saw_persistent, "persistent constraint adornment disappeared in Animation Mode");
    require(saw_transition, "transition-local constraint did not use alternate color");

    auto narrow_local = model.animation_session_rotation_constraint(local_id);
    require(narrow_local.has_value(), "transition-local constraint disappeared before Edit Tool test");
    narrow_local->allowed = {-0.25, 0.5};
    require(model.update_animation_rotation_constraint(local_id, *narrow_local) == sm::result::success,
        "could not narrow transition-local constraint for Edit Tool test");

    // The destination keyframe sees this constraint as an incoming transition
    // constraint. The Edit Tool must honor it during the drag, not repair the
    // pose only when the mouse is released.
    require(model.select_animation_keyframe(second) == sm::result::success,
        "select transition-constraint destination");
    canvas.clear_constraint_selection(false);
    canvas.clear_selection();
    window.tool_mgr().set_current_tool(canvases, ui::tool::id::edit);
    auto* edit_panel = dynamic_cast<ui::tool::edit_tool_panel*>(
        window.tool_mgr().current_tool().settings_widget());
    require(edit_panel != nullptr, "Edit Tool settings panel missing");
    QComboBox* drag_behavior = nullptr;
    for (auto* combo : edit_panel->findChildren<QComboBox*>())
        if (combo->findText("Rotate") >= 0) drag_behavior = combo;
    require(drag_behavior != nullptr, "Edit Tool drag behavior selector missing");
    drag_behavior->setCurrentIndex(drag_behavior->findText("Rotate"));
    canvas.sync_to_model();

    auto& edit_tool = window.tool_mgr().current_tool();
    QGraphicsSceneMouseEvent edit_press(QEvent::GraphicsSceneMousePress);
    edit_press.setScenePos({100, 0});
    edit_press.setButton(Qt::LeftButton);
    edit_press.setButtons(Qt::LeftButton);
    edit_tool.mousePressEvent(canvas, &edit_press);
    QGraphicsSceneMouseEvent edit_move(QEvent::GraphicsSceneMouseMove);
    edit_move.setScenePos({-100, 0});
    edit_move.setButtons(Qt::LeftButton);
    edit_tool.mouseMoveEvent(canvas, &edit_move);
    const auto live_angle = model.topology().get<sm::bone>(bone_id)->get().world_rotation();
    require(live_angle >= -0.25001 && live_angle <= 0.25001,
        "Edit Tool allowed an incoming transition-local constraint to be violated during drag");

    QGraphicsSceneMouseEvent edit_release(QEvent::GraphicsSceneMouseRelease);
    edit_release.setScenePos({-100, 0});
    edit_release.setButton(Qt::LeftButton);
    edit_tool.mouseReleaseEvent(canvas, &edit_release);
    const auto committed_angle = model.topology().get<sm::bone>(bone_id)->get().world_rotation();
    require(std::abs(sm::angular_distance(live_angle, committed_angle)) < 1e-7,
        "transition-local constraint snapped only when the Edit Tool drag was committed");
    browser->leave_animation();
}

}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        lock_on_existing_frame(app);
        transition_rotation_constraint_tool(app);
        ui::stick_man window;
        window.resize(1200, 800);
        window.show();
        app.processEvents();

        auto& model = window.project();
        model.add_new_skeleton_root({10, 20});
        auto skel = *model.topology().skeletons().begin();
        const auto root_id = skel->root_node().id();
        std::vector<sm::const_skel_ref> members{skel};
        const auto character_id = model.make_character(members).value();

        auto* browser = window.findChild<ui::pane::animation*>();
        require(browser != nullptr, "Animation asset pane missing");
        auto* tree = browser->findChild<QTreeWidget*>("animation_asset_tree");
        require(tree && tree->topLevelItemCount() == 1, "Animation pane does not show character assets");

        sm::animation animation;
        animation.name = "Empty";
        const auto animation_id = animation.id;
        model.edit_animation_data(character_id, [&](auto& data) { data.animations.push_back(animation); });
        model.mark_saved();
        require(!model.is_dirty(), "fixture should be clean before Animation Mode");
        require(model.can_undo(), "document history fixture missing");
        auto* delete_action = window.findChild<QAction*>("edit_delete");
        require(delete_action != nullptr, "delete action missing");
        const bool delete_was_enabled = delete_action->isEnabled();

        require(browser->open_animation(character_id, animation_id), "could not open empty Animation V2 asset");
        app.processEvents();
        require(model.animation_mode(), "Animation Mode not active");
        require(window.tool_mgr().current_tool().id() == ui::tool::id::edit,
            "Animation Mode substituted a special Edit tool");
        require(!model.can_undo(), "Animation Mode inherited document undo state");

        auto* editor = window.findChild<ui::pane::animation_editor*>("animation_editor");
        require(editor && editor->isVisible(), "Animation Editor shell is not visible");
        auto* tabs = editor->findChild<QTabWidget*>("animation_tabs");
        require(tabs && tabs->count() == 2, "Animation Editor tabs missing");
        require(tabs->tabText(0) == "Poses" && tabs->tabText(1) == "Artwork",
            "Animation Editor tab names are wrong");
        require(editor->findChild<ui::pose_strip*>() != nullptr, "Pose Strip custom widget missing");
        require(editor->findChild<ui::timeline*>("artwork_timeline") != nullptr,
            "Artwork tab does not contain generic timeline");
        require(editor->findChild<QPushButton*>("new_pose") != nullptr, "New Pose stub missing");
        require(editor->findChild<QToolButton*>("animation_transport_start") != nullptr &&
            editor->findChild<QToolButton*>("animation_transport_play") != nullptr &&
            editor->findChild<QToolButton*>("animation_transport_end") != nullptr,
            "shared animation transport controls missing");

        auto* add_node = tool_button(window, "add node");
        auto* add_bone = tool_button(window, "add bone");
        require(add_node && add_bone && !add_node->isEnabled() && !add_bone->isEnabled(),
            "topology tools remain enabled in Animation Mode");
        require(!delete_action->isEnabled(),
            "topology deletion action remains enabled in Animation Mode");

        auto& constraint = static_cast<ui::tool::constraint&>(
            window.tool_mgr().tool_from_id(ui::tool::id::constraint));
        auto* constraint_settings = constraint.settings_widget();
        auto* operation = constraint_settings->findChild<QComboBox*>("constraint_operation");
        require(operation && !operation->isEnabled(),
            "persistent constraint creation controls remain enabled in Animation Mode");

        auto& canvas = window.canvases().active_canvas();
        auto working_node = model.topology().get<sm::node>(root_id);
        auto persistent_node = model.core().topology().get<sm::node>(root_id);
        require(working_node && persistent_node && &working_node->get() != &persistent_node->get(),
            "canvas model is not detached from persistent topology");

        const auto persistent_position = persistent_node->get().world_pos();
        const sm::point moved{80, 90};
        model.transform_node_positions({{root_id, persistent_position}}, {{root_id, moved}});
        require(model.topology().get<sm::node>(root_id)->get().world_pos() == moved,
            "ordinary pose edit did not target session topology");
        require(model.core().topology().get<sm::node>(root_id)->get().world_pos() == persistent_position,
            "Animation Mode pose changed persistent project");
        require(!model.is_dirty(), "Animation Mode pose marked project dirty");

        require(!canvas.is_node_pinned(root_id), "unexpected initial pin state");
        canvas.toggle_node_pinned_undoable(root_id);
        require(!canvas.is_node_pinned(root_id),
            "Animation Mode allowed pin state without an outgoing transition");

        require(model.add_animation_keyframe() == sm::result::success, "create first frame");
        const auto first_frame = *model.animation_session_keyframe();
        require(model.add_animation_keyframe() == sm::result::success, "create second frame");
        const auto second_frame = *model.animation_session_keyframe();
        auto* strip = editor->findChild<ui::pose_strip*>();
        const auto select_card = [&](std::size_t index) {
            const auto* source = model.core().animation_data(character_id).find_animation(animation_id);
            const auto pos = ui::pose_strip_layout(source).cards[index].rect.center();
            QMouseEvent press(QEvent::MouseButtonPress, pos, strip->mapToGlobal(pos.toPoint()),
                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(strip, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, pos, strip->mapToGlobal(pos.toPoint()),
                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(strip, &release);
            app.processEvents();
        };
        select_card(0);
        auto& edit_tool = window.tool_mgr().current_tool();
        QGraphicsSceneMouseEvent pin_press(QEvent::GraphicsSceneMousePress);
        pin_press.setScenePos({moved.x, moved.y});
        pin_press.setButton(Qt::LeftButton);
        pin_press.setButtons(Qt::LeftButton);
        pin_press.setModifiers(Qt::AltModifier);
        edit_tool.mousePressEvent(canvas, &pin_press);
        QGraphicsSceneMouseEvent pin_release(QEvent::GraphicsSceneMouseRelease);
        pin_release.setScenePos({moved.x, moved.y});
        pin_release.setButton(Qt::LeftButton);
        pin_release.setModifiers(Qt::AltModifier);
        edit_tool.mouseReleaseEvent(canvas, &pin_release);
        require(model.animation_session_pinned_nodes().contains(root_id), "Alt-click did not pin frame 1");
        select_card(1);
        require(model.animation_session_incoming_locked_nodes().contains(root_id),
            "frame 1 pin must lock frame 2");
        bool visible_lock = false;
        for (auto* node : canvas.node_items()) {
            if (node->model().id() != root_id)
                continue;
            for (auto* child : node->childItems())
                if (dynamic_cast<QGraphicsPathItem*>(child) && child->isVisible())
                    visible_lock = true;
        }
        require(visible_lock, "frame 2 must display a lock after pinning existing frame 1");
        // A terminal keyframe has no outgoing transition and therefore no editable
        // transition-local pin state. The existing gesture must simply leave it empty.
        require(model.animation_session_pinned_nodes().empty(), "terminal frame exposed outgoing pins");
        canvas.toggle_node_pinned_undoable(root_id);
        require(model.animation_session_pinned_nodes().empty(), "terminal frame created hidden pin state");

        require(model.add_animation_keyframe() == sm::result::success, "create third frame");
        const auto third_frame = *model.animation_session_keyframe();
        require(model.select_animation_keyframe(second_frame) == sm::result::success, "reselect second frame");
        require(model.animation_session_pinned_nodes().contains(root_id),
            "new outgoing transition did not inherit the current pin behavior");

        // The incoming lock from frame 1 must not prevent editing frame 2's outgoing
        // transition. Removing that outgoing pin must leave the incoming lock visible.
        canvas.toggle_node_pinned_undoable(root_id);
        require(!model.animation_session_pinned_nodes().contains(root_id), "frame 2 unpin failed");
        require(canvas.is_node_pinned(root_id), "unpin must preserve frame 2 incoming movement lock");
        model.transform_node_positions({{root_id, moved}}, {{root_id, {100, 110}}});
        require(model.topology().get<sm::node>(root_id)->get().world_pos() == moved,
            "unpinning frame 2 must not allow moving its locked node");

        canvas.toggle_node_pinned_undoable(root_id);
        require(model.animation_session_pinned_nodes().contains(root_id), "locked node could not be re-pinned");
        model.undo();
        require(!model.animation_session_pinned_nodes().contains(root_id), "re-pin undo failed");
        require(model.redo() == sm::result::success, "re-pin redo failed");
        require(model.animation_session_pinned_nodes().contains(root_id), "re-pin redo lost pin");
        canvas.toggle_node_pinned_undoable(root_id);
        require(model.select_animation_keyframe(third_frame) == sm::result::success, "select third frame");
        require(!model.animation_session_incoming_locked_nodes().contains(root_id) &&
            !canvas.is_node_pinned(root_id), "frame 2 unpin must release frame 3");
        model.transform_node_positions({{root_id, moved}}, {{root_id, {100, 110}}});
        require(model.topology().get<sm::node>(root_id)->get().world_pos() == sm::point{100, 110},
            "released frame 3 node could not move");
        require(model.select_animation_keyframe(first_frame) == sm::result::success, "select first frame");
        require(model.animation_session_pinned_nodes().contains(root_id), "unpin leaked into frame 1");
        require(model.select_animation_keyframe(second_frame) == sm::result::success, "select second frame");
        require(canvas.is_node_pinned(root_id) && !model.animation_session_pinned_nodes().contains(root_id),
            "frame 2 did not retain its incoming lock and outgoing unpin");
        require(model.select_animation_keyframe(third_frame) == sm::result::success, "select third frame");

        browser->leave_animation();
        app.processEvents();
        require(!model.animation_mode(), "Animation Mode did not exit");
        require(model.core().topology().get<sm::node>(root_id)->get().world_pos() == persistent_position,
            "leaving Animation Mode committed scratch pose");
        require(!canvas.is_node_pinned(root_id), "leaving Animation Mode retained session pin");
        require(model.can_undo(), "document history was not restored on leaving Animation Mode");
        require(delete_action->isEnabled() == delete_was_enabled,
            "topology deletion action was not restored after Animation Mode");

        std::cout << "PASS Animation V2 Phase 1 UI\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
