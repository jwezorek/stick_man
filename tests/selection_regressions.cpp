#include "ui/stick_man.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/canvas/skel_item.hpp"
#include "ui/canvas/node_item.hpp"
#include "ui/canvas/bone_item.hpp"
#include "ui/tools/selection_tool.hpp"
#include "ui/panes/tree_view.hpp"
#include "ui/panes/skeleton_properties.hpp"
#include "ui/clipboard.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct fixture {
    ui::stick_man window;
    ui::tool::select tool;
    sm::object_id first, second;

    fixture() {
        auto& project = window.project();
        project.add_new_skeleton_root({0, 0});
        first = (*project.topology().skeletons().begin())->id();
        project.add_new_skeleton_root({80, 0});
        auto end = canvas().node_items().front()->model().id();
        for (auto node : canvas().node_items()) {
            if (node->model().world_pos().x == 80) end = node->model().id();
        }
        project.add_bone(mdl::to_handle(skeleton(first).root_node()), mdl::handle(end));
        project.add_new_skeleton_root({200, 0});
        for (auto skel : project.topology().skeletons()) {
            if (skel->root_node().world_pos().x == 200) second = skel->id();
        }
        tool.init(window.canvases(), project);
        tool.settings_widget();
        canvas().sync_to_model();
    }

    ui::canvas::scene& canvas() { return window.canvases().active_canvas(); }
    const sm::skeleton& skeleton(sm::object_id id) {
        return window.project().topology().skeleton(id)->get();
    }
    ui::canvas::item::skeleton* item(sm::object_id id) {
        return &ui::canvas::item_from_model<ui::canvas::item::skeleton>(skeleton(id));
    }
    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers mods = {}) {
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setScenePos(from);
        tool.mousePressEvent(canvas(), &press);
        QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
        move.setScenePos(from + (to - from) * 0.1);
        tool.mouseMoveEvent(canvas(), &move);
        move.setScenePos(to);
        tool.mouseMoveEvent(canvas(), &move);
        QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
        release.setScenePos(to);
        release.setModifiers(mods);
        tool.mouseReleaseEvent(canvas(), &release);
    }
    void select_both() {
        std::vector<ui::canvas::item::base*> selected{item(first), item(second)};
        canvas().set_selection(selected, true);
        canvas().sync_to_model();
    }
    void check_both() {
        require(canvas().selection().size() == 2 && canvas().selection().contains(item(first)) &&
            canvas().selection().contains(item(second)), "complete components must select both skeletons");
        require(!ui::canvas::selected_single_model(canvas()), "multiple skeletons must not expose a single rename target");
    }
};

void run(const std::string& mode) {
    fixture f;
    if (mode == "rubber_band_directions") {
        const std::vector<std::pair<QPointF, QPointF>> directions{
            {{-40, -40}, {240, 40}}, {{240, -40}, {-40, 40}},
            {{-40, 40}, {240, -40}}, {{240, 40}, {-40, -40}}
        };
        for (const auto& [from, to] : directions) {
            QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
            press.setScenePos(from);
            f.tool.mousePressEvent(f.canvas(), &press);
            // One large move must immediately display the entire band from the press point.
            QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
            move.setScenePos(to);
            f.tool.mouseMoveEvent(f.canvas(), &move);
            ui::canvas::item::rect_rubber_band* band = nullptr;
            for (auto* item : f.canvas().items()) {
                if (auto* candidate = dynamic_cast<ui::canvas::item::rect_rubber_band*>(item)) band = candidate;
            }
            require(band && band->isVisible(), "first move must show the selection rubber band");
            require(band->rect() == QRectF(-40, -40, 280, 80),
                "rubber band must have drawable geometry in every drag direction");
            require(band->boundingRect().contains(QPointF(-40, -40)) &&
                band->boundingRect().contains(QPointF(240, 40)), "paint bounds must cover both drag endpoints");
            QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
            release.setScenePos(to);
            f.tool.mouseReleaseEvent(f.canvas(), &release);
            f.check_both();
            for (auto* item : f.canvas().items()) {
                require(!dynamic_cast<ui::canvas::item::rect_rubber_band*>(item), "release must remove the rubber band");
            }
        }
    } else if (mode == "fast_release" || mode == "release_endpoint") {
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
        press.setScenePos({-40, -40});
        f.tool.mousePressEvent(f.canvas(), &press);
        if (mode == "release_endpoint") {
            QGraphicsSceneMouseEvent move(QEvent::GraphicsSceneMouseMove);
            move.setScenePos({-35, -35});
            f.tool.mouseMoveEvent(f.canvas(), &move);
        }
        QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);
        release.setScenePos({240, 40});
        f.tool.mouseReleaseEvent(f.canvas(), &release);
        f.check_both();
    } else if (mode == "rectangle") {
        f.drag({-40, -40}, {240, 40});
        f.check_both();
    } else if (mode == "partial") {
        f.drag({40, -40}, {240, 40});
        require(f.canvas().selection().size() >= 2, "partial fixture must select topology");
        for (auto* item : f.canvas().selection()) {
            require(!dynamic_cast<ui::canvas::item::skeleton*>(item), "partial skeleton must keep the entire selection as topology");
        }
    } else if (mode == "modifiers") {
        f.drag({-40, -40}, {120, 40});
        f.drag({160, -40}, {240, 40}, Qt::ShiftModifier);
        f.check_both();
        f.drag({160, -40}, {240, 40}, Qt::ControlModifier);
        require(f.canvas().selection().size() == 1 && f.canvas().selection().contains(f.item(f.first)),
            "Ctrl rectangle must subtract only the enclosed skeleton");
    } else if (mode == "tree") {
        ui::pane::tree_view* tree = nullptr;
        for (auto* widget : f.window.findChildren<QTreeView*>()) {
            if (widget->model() == f.item(f.first)->treeview_item()->model()) {
                tree = dynamic_cast<ui::pane::tree_view*>(widget);
            }
        }
        require(tree != nullptr, "missing skeleton tree");
        auto* model = tree->model();
        tree->selectionModel()->select(model->index(0, 0), QItemSelectionModel::Select);
        tree->selectionModel()->select(model->index(1, 0), QItemSelectionModel::Select);
        f.check_both();
        auto child = f.item(f.first)->treeview_item()->child(0)->index();
        tree->selectionModel()->select(child, QItemSelectionModel::Select);
        f.check_both();
        ui::pane::props::skeletons* props = nullptr;
        for (auto* widget : f.window.findChildren<QWidget*>()) {
            if (auto* candidate = dynamic_cast<ui::pane::props::skeletons*>(widget)) props = candidate;
        }
        require(props != nullptr, "missing skeleton properties");
        ui::labeled_field* name = nullptr;
        for (auto* widget : props->findChildren<QWidget*>()) {
            if (auto* field = dynamic_cast<ui::labeled_field*>(widget)) name = field;
        }
        require(name != nullptr && name->isHidden(), "multiple selection must hide individual name");
        bool has_count = false;
        for (auto* label : props->findChildren<QLabel*>()) {
            has_count = has_count || label->text() == "2 skeletons selected";
        }
        require(has_count, "multiple skeleton properties must show count");
        f.canvas().set_selection(f.item(f.first), true);
        require(!name->isHidden() && name->value()->text().toStdString() == f.skeleton(f.first).name(),
            "returning to single selection must restore its name field");
    } else if (mode == "clipboard") {
        f.select_both();
        ui::clipboard::copy(f.window);
        ui::clipboard::paste(f.window, true);
        require(std::ranges::distance(f.window.project().topology().skeletons()) == 4,
            "copy/paste must duplicate both selected skeletons");
        f.window.project().undo();
        require(std::ranges::distance(f.window.project().topology().skeletons()) == 2, "paste undo failed");
        f.select_both();
        ui::clipboard::del(f.window);
        require(f.window.project().topology().empty(), "delete must remove both selected skeletons");
        f.window.project().undo();
        require(std::ranges::distance(f.window.project().topology().skeletons()) == 2, "delete undo failed");
        f.select_both();
        ui::clipboard::cut(f.window);
        require(f.window.project().topology().empty(), "cut must remove both skeletons");
        ui::clipboard::paste(f.window, true);
        require(std::ranges::distance(f.window.project().topology().skeletons()) == 2, "cut clipboard must contain both skeletons");
    } else if (mode == "drag") {
        f.select_both();
        auto* panel = f.tool.settings_widget();
        for (auto* button : panel->findChildren<QAbstractButton*>()) {
            if (button->text() == "drag behaviors on" || button->text() == "translate") button->setChecked(true);
        }
        f.drag({0, 0}, {30, 30});
        auto child_position = [&]() { return (*f.skeleton(f.first).bones().begin())->child_node().world_pos(); };
        require(sm::distance(f.skeleton(f.first).root_node().world_pos(), {30, 30}) < 0.001 &&
            sm::distance(f.skeleton(f.second).root_node().world_pos(), {230, 30}) < 0.001,
            "dragging a selected skeleton's node must translate the whole group");
        require(sm::distance(child_position(), {110, 30}) < 0.001, "group drag must preserve child offset");
        f.window.project().undo();
        require(sm::distance(f.skeleton(f.first).root_node().world_pos(), {0, 0}) < 0.001 &&
            sm::distance(f.skeleton(f.second).root_node().world_pos(), {200, 0}) < 0.001,
            "group translation must undo in one step");
        require(sm::distance(child_position(), {80, 0}) < 0.001, "undo must restore child position");
        f.window.project().redo();
        require(sm::distance(f.skeleton(f.second).root_node().world_pos(), {230, 30}) < 0.001,
            "group translation redo failed");
        require(sm::distance(child_position(), {110, 30}) < 0.001, "redo must restore child position");
    } else {
        throw std::runtime_error("unknown test");
    }
}
}

int main(int argc, char** argv) {
#ifdef _MSC_VER
    // Report assertions to CTest instead of opening a blocking Windows dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    QApplication app(argc, argv);
    try {
        require(argc == 2, "expected test case");
        run(argv[1]);
        std::cout << "PASS " << argv[1] << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
