#include "ui/stick_man.hpp"
#include "ui/canvas/canvas_manager.hpp"
#include "ui/canvas/skel_item.hpp"
#include "ui/canvas/node_item.hpp"
#include "ui/canvas/bone_item.hpp"
#include "ui/tools/selection_tool.hpp"
#include "ui/panes/tree_view.hpp"
#include "ui/panes/skeleton_properties.hpp"
#include "ui/clipboard.hpp"
#include "ui/character_actions.hpp"
#include "ui/tools/add_bone_tool.hpp"
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
    sm::object_id make_character(bool both = true) {
        std::vector<sm::const_skel_ref> rig{skeleton(first)};
        if (both) rig.push_back(skeleton(second));
        auto result = window.project().make_character(rig);
        require(result.has_value(), "Make Character failed");
        window.project().rename(*result, "Alice");
        return *result;
    }
    QTreeView* tree() {
        for (auto* tree : window.findChildren<QTreeView*>())
            if (tree->model() == item(first)->treeview_item()->model()) return tree;
        throw std::runtime_error("missing tree");
    }
    void select_row(QStandardItem* row) {
        tree()->selectionModel()->select(row->index(), QItemSelectionModel::ClearAndSelect);
    }
};

void character_test(fixture& f, const std::string& mode) {
    auto& model = f.window.project();
    if (mode == "character_make") {
        f.select_both();
        QPushButton* make = nullptr;
        for (auto* button : f.window.findChildren<QPushButton*>()) if (button->text() == "Make Character") make = button;
        require(make && make->isEnabled(), "complete loose selection must enable Make Character in Properties");
        make->click();
        require(f.canvas().selected_character() && f.canvas().selected_character()->model().rig().size() == 2, "Make Character button must create and select rig");
        model.undo();
        require(f.skeleton(f.first).is_loose() && f.skeleton(f.second).is_loose(), "button creation undo");
        auto* node = &ui::canvas::item_from_model<ui::canvas::item::node>(f.skeleton(f.first).root_node());
        f.canvas().set_selection(node, true);
        require(f.canvas().loose_selection().empty(), "partial topology is not a Make Character candidate");
        ui::character_actions::make(f.canvas(), model);
        require(std::ranges::empty(model.core().characters()), "invalid Make Character must not mutate model");
    } else if (mode == "character_commands") {
        auto id = f.make_character();
        require(f.canvas().selected_character() && f.canvas().selected_character()->id() == id, "creation must select character");
        require(std::holds_alternative<sm::const_character_ref>(f.canvas().selected_objects().front()), "explicit character selection value missing");
        require(f.canvas().selected_skeletons().empty() && f.canvas().resolved_skeletons().size() == 2,
            "character must resolve its rig without being skeleton selection");
        model.undo(); // rename
        model.undo(); // creation
        require(!model.core().character(id) && f.skeleton(f.first).is_loose(), "Make Character undo");
        model.redo(); model.redo();
        require(model.core().character(id)->get().name() == "Alice", "redo must restore same identity/name");
    } else if (mode == "character_pane") {
        auto id = f.make_character(false);
        auto* child = f.item(f.first)->treeview_item();
        require(child->parent() && child->parent()->text() == "Alice", "one-component character needs two distinct rows");
        require(!f.item(f.second)->treeview_item()->parent(), "loose skeleton must remain a root");
        f.select_row(child);
        require(!f.canvas().selected_character() && f.canvas().selected_skeleton() == f.item(f.first), "child selection must never promote");
        f.select_row(child->parent());
        require(f.canvas().selected_character()->id() == id, "root selects character");
        auto* edit = f.window.findChild<QLineEdit*>("characterName");
        require(edit && edit->text() == "Alice", "character properties name missing");
        child->parent()->setText("Renamed");
        require(model.core().character(id)->get().name() == "Renamed" && edit->text() == "Renamed", "pane rename must synchronize properties");
        bool tag = false;
        for (auto* item : f.canvas().items())
            if (auto* text = dynamic_cast<QGraphicsSimpleTextItem*>(item)) tag = tag || text->text() == "Renamed";
        require(tag, "rename must update character tag");
        edit->setText("Properties name");
        QMetaObject::invokeMethod(edit, "editingFinished");
        require(model.core().character(id)->get().name() == "Properties name", "properties rename must reach model");
        for (auto* button : f.window.findChildren<QPushButton*>()) if (button->text() == "Select Component") button->click();
        require(f.canvas().selected_skeleton() == f.item(f.first) && !f.canvas().selected_character(), "Properties component selection must stay explicit skeleton");
    } else if (mode == "character_inference") {
        auto id = f.make_character();
        f.canvas().clear_selection();
        f.drag({-40, -40}, {120, 40});
        require(f.canvas().selected_skeleton() == f.item(f.first), "incomplete rig must remain skeleton selection");
        f.drag({-40, -40}, {240, 40});
        require(f.canvas().selected_character() && f.canvas().selected_character()->id() == id, "complete rig must infer character");
        require(f.tree()->selectionModel()->selectedIndexes().size() == 1 &&
            f.tree()->selectionModel()->selectedIndexes().front() == f.item(f.first)->treeview_item()->parent()->index(), "inference must highlight character root");
        f.drag({160, -40}, {240, 40}, Qt::ControlModifier);
        require(f.canvas().selected_skeleton() == f.item(f.first), "subtracting a component must expand character selection");
        f.drag({160, -40}, {240, 40}, Qt::ShiftModifier);
        require(f.canvas().selected_character(), "adding the final component must restore character inference");
        f.drag({40, -40}, {240, 40});
        require(!f.canvas().selected_character() && f.canvas().selected_skeletons().empty(), "partial component prevents aggregate promotion");
        model.add_new_skeleton_root({300, 0});
        f.drag({-40, -40}, {340, 40});
        require(!f.canvas().selected_character() && f.canvas().selected_skeletons().size() == 3, "rig plus loose must remain skeletons");
        std::vector<sm::const_skel_ref> other;
        for (auto s : model.topology().skeletons()) if (s->is_loose()) other.push_back(s);
        require(model.make_character(other).has_value(), "second character fixture");
        f.drag({-40, -40}, {340, 40});
        require(!f.canvas().selected_character() && f.canvas().selected_skeletons().size() == 3, "two rigs must remain skeletons");
    } else if (mode == "character_drag") {
        auto id = f.make_character();
        auto* frame = f.canvas().selected_character();
        require(frame->rect().contains(QPointF(0, 0)) && frame->rect().contains(QPointF(200, 0)), "frame must enclose disconnected rig");
        require(frame->pen().color() == QColor(133, 77, 181), "character frame must be purple");
        for (auto* button : f.tool.settings_widget()->findChildren<QAbstractButton*>())
            if (button->text() == "drag behaviors on" || button->text() == "rag doll mode") button->setChecked(true);
        ui::canvas::item_from_model<ui::canvas::item::node>(f.skeleton(f.second).root_node()).set_pinned(true);
        f.drag({0, 0}, {30, 30});
        require(sm::distance(f.skeleton(f.first).root_node().world_pos(), {30, 30}) < .001 &&
            sm::distance(f.skeleton(f.second).root_node().world_pos(), {230, 30}) < .001,
            "whole character drag must translate all disconnected components");
        model.undo();
        require(sm::distance(f.skeleton(f.second).root_node().world_pos(), {200, 0}) < .001, "character drag undo");
        model.redo();
        require(sm::distance(f.skeleton(f.second).root_node().world_pos(), {230, 30}) < .001, "character drag redo");
        require(model.core().character(id)->get().rig().size() == 2, "drag must not merge topology");
    } else if (mode == "character_clipboard") {
        auto id = f.make_character();
        ui::clipboard::copy(f.window);
        ui::clipboard::paste(f.window, true);
        auto copy = f.canvas().selected_character()->id();
        require(copy != id && model.core().character(copy)->get().rig().size() == 2, "whole-character paste must create fresh character with rig");
        for (auto s : model.core().character(copy)->get().rig().skeletons()) {
            require(!model.core().character(id)->get().rig().contains(s->id()), "pasted skeleton identity must be fresh");
            for (auto n : s->nodes())
                for (auto original : model.core().character(id)->get().rig().skeletons())
                    require(!original->contains<sm::node>(n->id()), "pasted node identity must be fresh");
        }
        model.undo(); require(!model.core().character(copy), "character paste must undo in one step");
        model.redo(); require(model.core().character(copy).has_value(), "character paste redo must preserve identity");
        ui::clipboard::paste(f.window, true);
        require(f.canvas().selected_character()->model().name() != model.core().character(copy)->get().name(), "repeated paste should choose a distinct cosmetic suffix");
        model.undo();
        f.select_row(f.item(f.first)->treeview_item());
        ui::clipboard::copy(f.window);
        f.canvas().set_selection(f.canvas().character_item(id), true);
        ui::clipboard::paste(f.window, true);
        require(std::ranges::count_if(model.topology().skeletons(), [](auto s) {return s->is_loose();}) == 1,
            "ordinary topology paste must remain loose even with character selected");
        f.canvas().set_selection(f.canvas().character_item(id), true);
        ui::clipboard::cut(f.window);
        require(!model.core().character(id), "whole-character cut must delete original without confirmation");
        ui::clipboard::paste(f.window, true);
        require(f.canvas().selected_character() && f.canvas().selected_character()->id() != id, "cut paste must assign fresh identity");
    } else if (mode == "character_delete") {
        auto id = f.make_character();
        f.select_row(f.item(f.second)->treeview_item());
        ui::clipboard::del(f.window); // must not prompt with another component remaining
        require(model.core().character(id)->get().rig().size() == 1, "nonfinal deletion must retain character");
        f.select_row(f.item(f.first)->treeview_item());
        ui::clipboard::del(f.window);
        require(!model.core().character(id), "final-component deletion must delete character without confirmation");
        model.undo();
        require(model.core().character(id)->get().rig().contains(f.first), "undo must restore character identity and membership");
        model.undo();
        require(model.core().character(id)->get().rig().size() == 2, "undo component deletion must restore full rig");
    } else if (mode == "character_actions") {
        auto id = f.make_character(false);
        f.canvas().set_selection(f.item(f.second), true);
        bool seen = false;
        QTimer::singleShot(0, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->windowTitle() == "Add to Character") {
                    seen = true; dialog->accept(); return;
                }
            }
        });
        ui::character_actions::adopt(f.canvas(), model, &f.window);
        require(seen && model.core().character(id)->get().rig().size() == 2 && f.canvas().selected_character(), "adoption UI must add loose skeleton and select character");
        model.undo(); require(f.skeleton(f.second).is_loose(), "adoption undo must restore loose root");
        model.redo(); require(model.core().character(id)->get().rig().size() == 2, "adoption redo");
    } else if (mode == "character_tag") {
        auto id = f.make_character();
        auto* frame = f.canvas().selected_character();
        QPointF tag_point;
        for (double scale : {.5, 1., 2.}) {
            f.canvas().set_scale(scale);
            require(frame->rect().contains(QPointF(200, 0)), "zoom must not scale model-space frame position");
            QGraphicsSimpleTextItem* label = nullptr;
            for (auto* item : f.canvas().items())
                if (auto* text = dynamic_cast<QGraphicsSimpleTextItem*>(item); text && text->text() == "Alice") label = text;
            require(label, "tag label missing");
            auto* view = f.canvas().views().front();
            auto viewport_point = label->deviceTransform(view->viewportTransform()).map(label->boundingRect().center());
            auto scene_point = view->mapToScene(viewport_point.toPoint());
            tag_point = scene_point;
            require(f.canvas().top_item(scene_point) == frame, "visible tag must hit character at every zoom with Y inversion");
        }
        auto bounds = frame->rect();
        model.rename(id, "A very long character name that must not enlarge the rig geometry bounds");
        require(frame->rect() == bounds, "name tag must not affect character bounding rectangle");
        for (auto* button : f.tool.settings_widget()->findChildren<QAbstractButton*>())
            if (button->text() == "drag behaviors on") button->setChecked(true);
        f.drag(tag_point, tag_point + QPointF(20, 20));
        require(sm::distance(f.skeleton(f.second).root_node().world_pos(), {220, 20}) < .001,
            "dragging the tag must translate every rig component");
    } else if (mode == "character_visual") {
        auto id = f.make_character();
        model.rename(f.first, "body"); model.rename(f.second, "left-eye");
        model.add_new_skeleton_root({110, 80});
        std::vector<sm::const_skel_ref> loose;
        for (auto skel : model.topology().skeletons()) if (skel->is_loose()) loose.push_back(skel);
        model.rename(loose.front()->id(), "right-eye");
        require(model.adopt_skeletons(id, loose) == sm::result::success, "visual adoption");
        model.add_new_skeleton_root({300, 50});
        f.canvas().set_selection(f.canvas().character_item(id), true);
        f.window.resize(1100, 760);
        f.window.show(); QApplication::processEvents();
        f.canvas().views().front()->centerOn(100, 20);
        require(f.window.grab().save("out/character-stage3.png"), "save visual review image");
        f.select_row(f.item(f.first)->treeview_item());
        require(f.window.grab().save("out/character-stage3-skeleton.png"), "save child selection review image");
    } else if (mode == "character_error") {
        auto id = f.make_character(false);
        std::vector<sm::const_skel_ref> other{f.skeleton(f.second)};
        auto second = model.make_character(other);
        require(second.has_value(), "second character fixture");
        model.rename(*second, "Bob"); model.undo(); // keep a redo entry through the rejected command
        const auto before = model.topology().to_json_str();
        ui::tool::add_bone tool;
        tool.init(f.window.canvases(), model);
        QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress), release(QEvent::GraphicsSceneMouseRelease);
        press.setScenePos({0, 0}); release.setScenePos({200, 0});
        tool.mousePressEvent(f.canvas(), &press);
        bool seen = false;
        QTimer::singleShot(0, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                seen = message->text() == "Cannot connect skeletons belonging to different characters.";
                message->done(QMessageBox::Ok); return;
            }
        });
        tool.mouseReleaseEvent(f.canvas(), &release);
        require(seen && model.topology().to_json_str() == before && model.can_redo(), "rejected Add Bone must show normal error, preserve project and redo");
        model.undo();
        require(!model.core().character(*second) && model.core().character(id), "failed Add Bone must not create undo entry");
    } else throw std::runtime_error("unknown character test");
}

void run(const std::string& mode) {
    fixture f;
    if (mode == "character_hierarchy") {
        std::vector<sm::const_skel_ref> rig{f.skeleton(f.first), f.skeleton(f.second)};
        auto character = f.window.project().core().create_character(rig);
        require(character.has_value(), "character fixture creation failed");
        emit f.window.project().refresh_canvas(f.window.project(), true);
        auto* row = f.item(f.first)->treeview_item();
        require(row->parent() && row->parent() == f.item(f.second)->treeview_item()->parent(),
            "character rig components must have a shared character root");
        require(row->parent()->text().toStdString() == character->get().name(), "character root must display its name");
    } else if (mode.starts_with("character_")) {
        character_test(f, mode);
    } else if (mode == "rubber_band_directions") {
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
    // The Windows offscreen platform does not enumerate installed system fonts.
    // Load a real font for the optional rendered UI inspection only.
    if (argc == 2 && std::string(argv[1]) == "character_visual") {
        auto font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
        if (font >= 0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
    }
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
