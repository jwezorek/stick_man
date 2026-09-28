#include "ui/stick_man.hpp"
#include "ui/panes/animation_pane.hpp"
#include "ui/panes/animation_editor.hpp"
#include "ui/widgets/pose_strip.hpp"
#include "ui/widgets/timeline.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/canvas/node_item.hpp"
#include "ui/tools/tool_manager.hpp"
#include "ui/tools/constraint_tool.hpp"

#include <QtWidgets>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

QPushButton* tool_button(ui::stick_man& window, const QString& tooltip) {
    for (auto* button : window.findChildren<QPushButton*>())
        if (button->toolTip() == tooltip) return button;
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
            if (skel->root_node().world_pos() == pos) return skel->root_node().id();
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
    press.setScenePos({0, 0}); press.setButton(Qt::LeftButton); press.setButtons(Qt::LeftButton);
    tool.mousePressEvent(canvas, &press);
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
    release.setScenePos({0, 0}); release.setButton(Qt::LeftButton);
    tool.mouseReleaseEvent(canvas, &release);
    require(model.animation_session_pinned_nodes().contains(ankle), "constraint tool did not pin ankle");
    window.tool_mgr().set_current_tool(canvases, ui::tool::id::selection);
    require(model.select_animation_keyframe(second) == sm::result::success, "select destination frame");
    require(canvas.is_node_pinned(ankle), "existing destination frame lost ankle lock");
    canvas.views().first()->centerOn(0, 25);
    app.processEvents();
    QGraphicsPathItem* lock = nullptr;
    for (auto* node : canvas.node_items()) {
        if (node->model().id() != ankle) continue;
        for (auto* child : node->childItems())
            if (auto* path = dynamic_cast<QGraphicsPathItem*>(child); path && path->isVisible()) lock = path;
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
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    try {
        lock_on_existing_frame(app);
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
        require(window.tool_mgr().current_tool().id() == ui::tool::id::selection,
            "Animation Mode substituted a special Selection tool");
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
        require(editor->findChild<QPushButton*>("edit_pose_domain") != nullptr,
            "Edit Pose Domain stub missing");
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
            "Animation Mode allowed pin state without an edited keyframe owner");

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
        auto& selection_tool = window.tool_mgr().current_tool();
        QGraphicsSceneMouseEvent pin_press(QEvent::GraphicsSceneMousePress);
        pin_press.setScenePos({moved.x, moved.y});
        pin_press.setButton(Qt::LeftButton);
        pin_press.setButtons(Qt::LeftButton);
        pin_press.setModifiers(Qt::AltModifier);
        selection_tool.mousePressEvent(canvas, &pin_press);
        QGraphicsSceneMouseEvent pin_release(QEvent::GraphicsSceneMouseRelease);
        pin_release.setScenePos({moved.x, moved.y});
        pin_release.setButton(Qt::LeftButton);
        pin_release.setModifiers(Qt::AltModifier);
        selection_tool.mouseReleaseEvent(canvas, &pin_release);
        require(model.animation_session_pinned_nodes().contains(root_id), "Alt-click did not pin frame 1");
        select_card(1);
        require(model.animation_session_incoming_locked_nodes().contains(root_id),
            "frame 1 pin must lock frame 2");
        bool visible_lock = false;
        for (auto* node : canvas.node_items()) {
            if (node->model().id() != root_id) continue;
            for (auto* child : node->childItems())
                if (dynamic_cast<QGraphicsPathItem*>(child) && child->isVisible()) visible_lock = true;
        }
        require(visible_lock, "frame 2 must display a lock after pinning existing frame 1");
        // Frame 2 was created before frame 1 was pinned, so it has no outgoing pin yet.
        canvas.toggle_node_pinned_undoable(root_id);
        canvas.toggle_node_pinned_undoable(root_id);
        require(!model.animation_session_pinned_nodes().contains(root_id), "frame 2 unpin failed");
        require(canvas.is_node_pinned(root_id), "unpin must preserve frame 2 incoming movement lock");
        model.transform_node_positions({{root_id, moved}}, {{root_id, {100, 110}}});
        require(model.topology().get<sm::node>(root_id)->get().world_pos() == moved,
            "unpinning frame 2 must not allow moving its locked node");

        // A lock from frame 1 must not prevent toggling frame 2's outgoing pin back on.
        canvas.toggle_node_pinned_undoable(root_id);
        require(model.animation_session_pinned_nodes().contains(root_id), "locked node could not be re-pinned");
        model.undo();
        require(!model.animation_session_pinned_nodes().contains(root_id), "re-pin undo failed");
        require(model.redo() == sm::result::success, "re-pin redo failed");
        require(model.animation_session_pinned_nodes().contains(root_id), "re-pin redo lost pin");
        canvas.toggle_node_pinned_undoable(root_id);
        require(model.add_animation_keyframe() == sm::result::success, "create third frame");
        const auto third_frame = *model.animation_session_keyframe();
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
