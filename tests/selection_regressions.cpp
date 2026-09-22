#include "ui/stick_man.hpp"
#include "ui/panes/artwork_browser.hpp"
#include "ui/canvas/artwork_layer.hpp"
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
#include <numbers>
#include <iostream>
#include <stdexcept>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

QTreeWidgetItem* appearance_state_item(QTreeWidget* tree, const std::string& slot, const std::string& state) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        auto* top = tree->topLevelItem(i);
        if (top->data(0, Qt::UserRole).toString().toStdString() != slot) continue;
        for (int j = 0; j < top->childCount(); ++j) {
            auto* child = top->child(j);
            if (child->data(0, Qt::UserRole + 1).toString().toStdString() == state) return child;
        }
    }
    return nullptr;
}

void choose_mapping(QTreeWidget* tree, QTreeWidgetItem* state_item, const QString& choice) {
    QStyleOptionViewItem option;
    auto* editor = qobject_cast<QComboBox*>(
        tree->itemDelegate()->createEditor(tree->viewport(), option, tree->indexFromItem(state_item, 1)));
    require(editor != nullptr, "appearance mapping editor missing");
    auto index = editor->findText(choice);
    require(index >= 0, "appearance mapping choice missing");
    editor->setCurrentIndex(index);
    QMetaObject::invokeMethod(editor, "activated", Qt::DirectConnection, Q_ARG(int, index));
    delete editor;
}

void choose_preview_state(QTreeWidget* tree, QTreeWidgetItem* state_item) {
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 200, 24);
    option.widget = tree;
    auto width = tree->style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, tree);
    QPointF pos(option.rect.left() + 2 + width / 2.0, option.rect.center().y());
    QMouseEvent release(QEvent::MouseButtonRelease, pos, pos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    require(tree->itemDelegate()->editorEvent(&release, tree->model(), option, tree->indexFromItem(state_item, 0)),
        "appearance preview radio did not handle click");
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

void artwork_canvas_test(fixture& f, bool visual) {
    auto& model = f.window.project();
    auto id = f.make_character(false);
    auto bone = (*f.skeleton(f.first).bones().begin())->id();
    sm::image_buffer stripe(20 * 20 * 4), blue(20 * 20 * 4);
    for (int y=0; y<20; ++y) for (int x=0; x<20; ++x) {
        auto i = (y*20+x)*4;
        stripe[i] = y < 10 ? 255 : 0; stripe[i+1] = y < 10 ? 0 : 255; stripe[i+3] = 255;
        blue[i+2] = 255; blue[i+3] = 255;
    }
    model.edit_artwork(id, [&](auto& a) {
        a.insert_frame("stripe", {sm::image_resource::from_rgba(20,20,stripe), {}});
        a.insert_frame("blue", {sm::image_resource::from_rgba(20,20,blue), {}});
        a.add_slot("z_back", {bone}); a.add_slot("a_front", {bone});
        a.add_appearance("Default", {{{"z_back", {{"default","blue"}}, {{30,30},0,{1,1}}},
            {"a_front", {{"default","stripe"}}, {{30,30},0,{1,1}}}}});
    });
    auto& layer = f.canvas().artwork();
    auto render = [&] {
        QImage result(240,240,QImage::Format_RGBA8888); result.fill(Qt::white);
        QPainter painter(&result); painter.translate(120,120); painter.scale(1,-1);
        layer.paint(painter); painter.end(); return result;
    };
    auto image = render();
    require(image.pixelColor(150,85) == QColor(Qt::red) && image.pixelColor(150,95) == QColor(Qt::green), "sprite orientation or painter order incorrect");
    require(layer.hit_test({35,35})->slot == "a_front", "sprite hit test must select front layer");
    auto pose = model.topology().to_json_str();
    auto current = [&]() { return model.core().artwork(id).appearances().at("Default").appearance_slots[1].transform; };
    require(layer.begin_transform({35,35}, ui::canvas::sprite_drag::translate), "sprite translate did not start");
    layer.update_transform({45,55});
    require(current().translation == sm::point{30,30}, "drag preview prematurely mutated Core");
    layer.end_transform({45,55});
    require(current().translation == sm::point{40,50}, "sprite translation incorrect");
    require(model.topology().to_json_str() == pose, "sprite tool changed rig");
    model.undo(); require(current().translation == sm::point{30,30}, "drag did not undo in one step");
    model.redo(); require(current().translation == sm::point{40,50}, "sprite drag redo failed"); model.undo();
    require(layer.begin_transform({35,30}, ui::canvas::sprite_drag::rotate), "rotate start failed");
    layer.end_transform({30,35});
    require(std::abs(current().rotation - std::numbers::pi/2) < 1e-8, "sprite rotation incorrect"); model.undo();
    for (auto mode : {ui::canvas::sprite_drag::scale_x, ui::canvas::sprite_drag::scale_y, ui::canvas::sprite_drag::scale_xy}) {
        require(layer.begin_transform({35,35},mode), "scale start failed"); layer.end_transform({40,40});
        auto expected = mode == ui::canvas::sprite_drag::scale_x ? sm::point{2,1} : mode == ui::canvas::sprite_drag::scale_y ? sm::point{1,2} : sm::point{2,2};
        require(current().scale == expected, "sprite scale incorrect"); model.undo();
    }
    layer.begin_transform({35,35},ui::canvas::sprite_drag::translate); layer.update_transform({100,100}); layer.cancel_transform();
    require(current().translation == sm::point{30,30} && !layer.dragging(), "cancelled drag changed Core");
    auto* browser = f.window.findChild<ui::pane::artwork_browser*>();
    auto* tree = browser->findChild<QTreeWidget*>("artwork_appearance_structure");
    require(tree->topLevelItem(0)->data(0,Qt::UserRole).toString() == "z_back", "browser uses name order instead of painter order");
    layer.set_selected_slot(id,"a_front");
    auto* transform = browser->findChild<QToolButton*>("artwork_transform_toggle");
    auto* transform_panel = browser->findChild<QFrame*>("artwork_transform_panel");
    require(transform && transform_panel, "sprite transform popup missing");
    transform->click();
    require(transform->isChecked() && !transform_panel->isHidden(), "sprite transform popup did not open");
    auto* x = browser->findChild<QDoubleSpinBox*>("artwork_translation_x");
    require(x && x->isEnabled(), "sprite numeric controls disabled");
    x->setValue(44); QMetaObject::invokeMethod(x,"editingFinished",Qt::DirectConnection);
    require(current().translation.x == 44, "numeric transform not applied"); model.undo();
    auto* send_to_back = browser->findChild<QToolButton*>("artwork_send_to_back");
    require(send_to_back && send_to_back->isEnabled(), "Send to Back tool button missing or disabled");
    send_to_back->click();
    require(model.core().artwork(id).appearances().at("Default").appearance_slots.front().slot == "a_front", "painter order button failed");
    require(render().pixelColor(150,85) == QColor(Qt::blue), "reordering did not change rendered overlap"); model.undo();
    layer.set_show_artwork(false); require(!layer.hit_test({35,35}) && render().pixelColor(150,85) == QColor(Qt::white), "Show Artwork did not hide sprites"); layer.set_show_artwork(true);
    layer.set_show_skeleton(false); require(f.canvas().bone_items().front()->effectiveOpacity() == 0, "Show Skeleton did not hide guides");
    require(f.canvas().selected_character(), "hiding guides lost semantic selection"); layer.set_show_skeleton(true);
    layer.set_wireframe(true); require(f.canvas().bone_items().front()->brush().style() == Qt::NoBrush, "wireframe not applied"); layer.set_wireframe(false);
    layer.assign_frame(id,bone,"stripe");
    require(model.core().artwork(id).slot_definitions().contains("stripe"), "frame drop did not create channel");
    require(model.core().artwork(id).resolve_frame("Default","stripe") == "stripe", "frame drop did not map image");
    model.undo(); require(!model.core().artwork(id).slot_definitions().contains("stripe"), "compound frame assignment undo failed");
    QMimeData mime; mime.setData(ui::canvas::frame_mime_type, QJsonDocument(QJsonObject{{"character",QString::fromStdString(id.to_string())},{"frame","stripe"}}).toJson());
    require(layer.can_drop(&mime,{30,0}) && !layer.can_drop(&mime,{200,0}), "frame drag character/bone validation failed");
    // Exercise real tool dispatch and Escape, with no topology edits.
    //f.window.tool_mgr().set_current_tool(f.window.canvases(),ui::tool::id::sprite_transform);
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress); press.setButton(Qt::LeftButton); press.setScenePos({35,35});
    f.window.tool_mgr().mousePressEvent(f.canvas(),&press);
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); f.window.tool_mgr().keyPressEvent(f.canvas(),&escape);
    require(!layer.dragging() && model.topology().to_json_str() == pose, "Sprite Transform Escape changed rig");
    layer.begin_transform({500,500},ui::canvas::sprite_drag::translate);
    require(!layer.selected_slot(), "empty canvas click should deselect the sprite");
    QTimer::singleShot(0, [] {
        for (auto* widget : QApplication::topLevelWidgets()) if (auto* dialog = qobject_cast<QInputDialog*>(widget)) dialog->accept();
    });
    require(layer.drop_frame(&mime,{30,0}), "frame MIME drop failed");
    require(model.core().artwork(id).slot_definitions().contains("stripe"), "drop did not create slot");
    model.undo(); require(!model.core().artwork(id).slot_definitions().contains("stripe"), "drop was not one undo command");
    if (visual) {
        layer.set_selected_slot(id,"a_front");
        auto* tabs = browser->findChild<QTabWidget*>(); tabs->setCurrentIndex(1);
        f.window.resize(1400,950); f.window.show(); QApplication::processEvents();
        f.canvas().set_scale(3); f.canvas().views().first()->centerOn(40,20);
        require(f.window.grab().save("out/appearances-phase2.png"), "phase2 visual capture failed");
    }
    // Artwork selection notifications must run only after obsolete canvas items
    // have been removed, including while a character's Core object is destroyed.
    layer.set_selected_slot(id,"a_front");
    model.delete_character(id);
    require(!layer.selected_slot(), "deleted character kept sprite selection");
    model.undo();
    layer.set_selected_slot(id,"a_front");
    auto saved = model.serialize(); require(saved.has_value(), "phase2 save failed");
    require(model.deserialize(*saved), "reopen with sprite selected failed");
    require(!layer.selected_slot(), "new project retained stale sprite selection");
}
void character_test(fixture& f, const std::string& mode) {
    auto& model = f.window.project();
    if (mode == "character_artwork_phase3" || mode == "character_artwork_phase3_visual") {
        auto id = f.make_character(false);
        auto bone = (*f.skeleton(f.first).bones().begin())->id();
        model.edit_artwork(id, [&](auto& a) {
            a.insert_frame("red", {sm::image_resource::from_rgba(1,1,{255,0,0,255}), {}});
            a.insert_frame("blue", {sm::image_resource::from_rgba(1,1,{0,0,255,255}), {}});
            a.add_slot("eyes", {bone});
            a.add_state("eyes", "closed"); a.add_state("eyes", "half");
            a.add_appearance("Human", {{{"eyes", {{"default","red"}, {"closed","blue"}}}}});
            a.add_appearance("Robot", {{{"eyes", {{"default","blue"}, {"closed",std::nullopt}}}}});
        });
        auto& layer = f.canvas().artwork();
        layer.set_active_appearance(id,"Human"); layer.set_selected_slot(id,"eyes");
        auto* browser = f.window.findChild<ui::pane::artwork_browser*>();
        auto* tree = browser->findChild<QTreeWidget*>("artwork_appearance_structure");
        require(tree, "missing appearance structure tree");
        auto choose = [&](const char* name) {
            auto* state_item = appearance_state_item(tree, "eyes", name);
            require(state_item, "preview state missing");
            choose_preview_state(tree, state_item);
            require(layer.preview_state(id, "eyes") == name, "preview state radio did not update canvas state");
        };
        auto color = [&] {
            QImage image(8,8,QImage::Format_RGBA8888); image.fill(Qt::white);
            QPainter painter(&image); painter.translate(4,4); painter.scale(4,-4); layer.paint(painter); painter.end();
            return image.pixelColor(4,4);
        };
        choose("closed"); require(color() == QColor(Qt::blue), "explicit state did not render");
        require(layer.hit_test({0,0}).has_value(), "visible preview not selectable");
        layer.set_active_appearance(id,"Robot");
        require(layer.preview_state(id, "eyes") == "closed" && color() == QColor(Qt::white),
            "appearance switch lost semantic state or hidden mapping");
        require(!layer.hit_test({0,0}), "hidden preview remained selectable");
        auto* closed = appearance_state_item(tree, "eyes", "closed");
        require(closed && closed->text(1) == "Hidden", "hidden mapping not represented in browser");
        choose_mapping(tree, closed, "Use default (unmapped)");
        require(color() == QColor(Qt::blue), "editing unmapped fallback did not refresh preview");
        model.undo(); require(color() == QColor(Qt::white), "mapping undo lost preview state");
        choose("half"); require(color() == QColor(Qt::blue), "unmapped state did not fall back");
        layer.set_active_appearance(id,"Human"); require(color() == QColor(Qt::red), "fallback used other appearance");
        choose("default");
        require(layer.preview_state(id, "eyes") == "default", "preview reset failed");
        choose("closed");
        model.edit_artwork(id, [](auto& a) { a.rename_state("eyes","closed","shut"); });
        require(layer.preview_state(id, "eyes") == "default", "renamed preview state did not reset safely");
        require(model.core().artwork(id).resolve_frame("Human","eyes","shut") == "blue" &&
            !model.core().artwork(id).resolve_frame("Robot","eyes","shut"), "rename did not propagate across appearances");
        choose("shut"); model.edit_artwork(id, [](auto& a) { a.delete_state("eyes","shut"); });
        require(layer.preview_state(id, "eyes") == "default" && color() == QColor(Qt::red), "deleted state left stale preview");
        model.undo(); require(appearance_state_item(tree, "eyes", "shut"), "undo did not restore vocabulary");
        choose("shut");
        if (mode.ends_with("visual")) {
            browser->findChild<QTabWidget*>()->setCurrentIndex(1);
            f.window.resize(1400,950); f.window.show(); QApplication::processEvents();
            require(f.window.grab().save("out/appearances-phase3.png"), "phase3 visual capture failed");
        }
        auto saved = model.serialize(); require(saved.has_value() && model.deserialize(*saved), "state preview reopen failed");
        require(color() == QColor(Qt::red), "preview state leaked into saved project");
        return;
    }
    if (mode == "character_artwork_phase2" || mode == "character_artwork_phase2_visual") { artwork_canvas_test(f, mode.ends_with("visual")); return; }
    if (mode == "character_artwork" || mode == "character_artwork_visual") {
        auto id = f.make_character(false);
        auto* browser = f.window.findChild<ui::pane::artwork_browser*>();
        require(browser && browser->character_id() == id, "browser must follow character selection");
        auto bone = (*f.skeleton(f.first).bones().begin())->id();
        model.edit_artwork(id, [&](auto& art) {
            art.insert_frame("head", {sm::image_resource::from_rgba(1, 1, {4, 5, 6, 77}), {2, -3}});
            art.add_slot("head", {bone});
            art.add_appearance("Human", {{{"head", {{"default", "head"}}}}});
            art.add_appearance("Robot");
        });
        require(browser->findChild<QListWidget*>("artwork_frames")->count() == 1, "browser thumbnail missing");
        model.undo(); require(model.core().artwork(id).frames().empty(), "artwork undo failed");
        model.redo(); require(model.core().artwork(id).frames().contains("head"), "artwork redo failed");
        auto* choices = browser->findChild<QComboBox*>("artwork_appearance");
        choices->setCurrentText("Robot");
        require(browser->active_appearance() == "Robot", "appearance switch failed");
        f.canvas().set_selection(f.item(f.first), true);
        require(browser->character_id() == id, "browser must follow member topology");
        f.canvas().set_selection(f.item(f.second), true);
        require(!browser->character_id(), "loose topology must clear artwork context");
        f.canvas().set_selection(f.canvas().character_item(id), true);
        require(browser->active_appearance() == "Robot", "active appearance session state lost");
        if (mode == "character_artwork_visual") {
            choices->setCurrentText("Human");
            f.window.resize(1280, 900); f.window.show(); QApplication::processEvents();
            require(f.window.grab().save("out/appearances-browser.png"), "artwork browser screenshot failed");
            return;
        }
        choices->setCurrentText("Human");
        auto* origin = browser->findChild<QDoubleSpinBox*>("artwork_origin_x");
        origin->setValue(-7); QMetaObject::invokeMethod(origin, "editingFinished", Qt::DirectConnection);
        require(model.core().artwork(id).frames().at("head").registration_origin.x == -7, "origin UI did not edit model");
        model.undo(); require(model.core().artwork(id).frames().at("head").registration_origin.x == 2, "origin UI undo failed");
        bool rejected = false;
        try { model.edit_artwork(id, [](auto& a) { a.add_appearance("temporary"); a.add_appearance("Human"); }); }
        catch (const std::exception&) { rejected = true; }
        require(rejected && model.can_redo() && !model.core().artwork(id).appearances().contains("temporary"), "failed edit was not atomic");
        auto* tree = browser->findChild<QTreeWidget*>("artwork_appearance_structure");
        auto* default_state = appearance_state_item(tree, "head", "default");
        require(default_state, "default appearance state missing");
        choose_mapping(tree, default_state, "Hidden (none)");
        require(!model.core().artwork(id).resolve_frame("Human", "head"), "mapping UI did not hide frame");
        model.undo(); require(model.core().artwork(id).resolve_frame("Human", "head") == "head", "mapping UI undo failed");
        ui::clipboard::copy(f.window); ui::clipboard::paste(f.window, true);
        auto copied = f.canvas().selected_character()->id();
        require(copied != id && model.core().artwork(copied).frames().contains("head"), "character copy lost artwork");
        require(model.core().slot_resolved(copied, "head"), "copied artwork bone was not remapped");
        require(model.core().artwork(copied).slot_definitions().at("head").bone != bone, "copy retained source bone");
        model.edit_artwork(copied, [](auto& art) { art.rename_frame("head", "copy-head"); });
        require(model.core().artwork(id).frames().contains("head"), "copy edits changed original artwork");
        model.undo(); model.undo();
        require(!model.core().character(copied), "character paste undo failed");
        model.delete_character(id); model.undo();
        require(model.core().artwork(id).frames().contains("head"), "character delete undo lost artwork");
        require(model.core().slot_resolved(id, "head"), "restored artwork bone unresolved");
        model.undo(); // Undo original artwork edit after topology restoration.
        require(model.core().artwork(id).frames().empty(), "artwork command invalid after character restoration");
        model.redo();
        auto saved = model.serialize(); require(saved.has_value(), "editor artwork save failed");
        require(model.deserialize(*saved), "editor artwork reopen failed");
        f.canvas().set_selection(f.canvas().character_item(id), true);
        require(browser->character_id() == id && browser->findChild<QListWidget*>("artwork_frames")->count() == 1,
            "browser did not restore reopened frames");
    } else if (mode == "character_make") {
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
        ui::hyperlink_button* parent_link = nullptr;
        for (auto* button : f.window.findChildren<QPushButton*>())
            if (auto* link = dynamic_cast<ui::hyperlink_button*>(button);
                link && !link->isHidden() && link->text() == "Alice") parent_link = link;
        require(parent_link, "parented skeleton properties must expose its character as a hyperlink");
        parent_link->click();
        require(f.canvas().selected_character() && f.canvas().selected_character()->id() == id,
            "skeleton character hyperlink must select the parent character");
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
        require(!f.window.findChild<QComboBox*>("characterComponents"),
            "character properties must not retain the component combo box");
        for (auto* button : f.window.findChildren<QPushButton*>())
            require(button->text() != "Select Component", "character properties must not retain Select Component button");
        auto* component_scroller = f.window.findChild<QScrollArea*>("characterSkeletons");
        require(component_scroller, "character properties must expose a skeleton scroll view");
        ui::hyperlink_button* component_link = nullptr;
        for (auto* button : component_scroller->findChildren<QPushButton*>())
            if (auto* link = dynamic_cast<ui::hyperlink_button*>(button);
                link && link->text().toStdString() == f.skeleton(f.first).name()) component_link = link;
        require(component_link, "character properties must show each skeleton as a hyperlink");
        component_link->click();
        require(f.canvas().selected_skeleton() == f.item(f.first) && !f.canvas().selected_character(),
            "character skeleton hyperlink must select the skeleton explicitly");
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
        f.canvas().set_node_pinned(f.skeleton(f.second).root_node().id(), true);
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
        const auto source_bone = (*f.skeleton(f.first).bones().begin())->id();
        model.edit_animation_data(id, [&](auto& data) {
            auto pose = sm::capture_pose(model.topology(), model.core().character(id)->get().rig().skeleton_ids(), "Wave");
            const auto pose_id = pose.id;
            data.poses.push_back(std::move(pose));
            sm::animation animation; animation.name = "Wave animation"; animation.base_pose = pose_id;
            sm::animation_action action; action.data = sm::rigid_rotation{source_bone};
            animation.layers.push_back({{action}});
            data.animations.push_back(std::move(animation));
        });
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
        const auto& copied_character = model.core().character(copy)->get();
        const auto& copied_assets = copied_character.animation_data();
        require(copied_assets.poses.size() == model.core().animation_data(id).poses.size() &&
            copied_assets.animations.size() == 1, "character copy lost poses or animations");
        copied_assets.validate(model.topology(), copied_character.character_root_bone());
        const auto& copied_action = copied_assets.animations.front().layers.front().actions.front();
        const auto copied_bone = std::get<sm::rigid_rotation>(copied_action.data).bone;
        require(copied_bone != source_bone, "character copy retained source animation bone reference");
        auto copied_bone_ref = model.topology().get<sm::bone>(copied_bone);
        require(copied_bone_ref && copied_bone_ref->get().owner().parent_character() &&
            copied_bone_ref->get().owner().parent_character()->get().id() == copy,
            "character copy animation reference was not remapped into copied rig");
        for (const auto& pose : copied_assets.poses) for (const auto& [node_id, pt] : pose.node_positions) {
            auto node = model.topology().get<sm::node>(node_id);
            require(node && node->get().owner().parent_character() &&
                node->get().owner().parent_character()->get().id() == copy,
                "character copy pose retained a source node ID");
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
    } else if (mode == "pane_structure") {
        QDockWidget* skeleton_dock = nullptr;
        QDockWidget* animation_dock = nullptr;
        for (auto* dock : f.window.findChildren<QDockWidget*>()) {
            if (dock->windowTitle() == "Skeleton") skeleton_dock = dock;
            if (dock->windowTitle() == "Animation") animation_dock = dock;
        }
        require(skeleton_dock != nullptr, "missing Skeleton dock");
        require(animation_dock != nullptr, "missing Animation dock");
        require(qobject_cast<QTabWidget*>(skeleton_dock->widget()) == nullptr,
            "Skeleton dock must no longer be a tab control");
        require(skeleton_dock->findChild<ui::pane::tree_view*>() != nullptr,
            "Skeleton dock lost the skeleton tree");
        require(animation_dock->findChild<QTreeWidget*>("animation_asset_tree") != nullptr,
            "Animation dock must own the character animation asset browser");
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
    } else if (mode == "pin_state") {
        auto id = f.skeleton(f.first).root_node().id();
        f.canvas().set_node_pinned(id, true);
        require(f.canvas().is_node_pinned(id), "scene must own semantic pin state by node id");
        require(ui::canvas::item_from_model<ui::canvas::item::node>(f.skeleton(f.first).root_node()).pin_visible(),
            "pinning through the scene must update the node indicator");

        emit f.window.project().refresh_canvas(f.window.project(), true);
        require(f.canvas().is_node_pinned(id), "canvas rebuild must preserve pin state for the same node id");
        require(ui::canvas::item_from_model<ui::canvas::item::node>(f.skeleton(f.first).root_node()).pin_visible(),
            "recreated node item must initialize its pin indicator from scene state");

        f.canvas().set_selection(f.item(f.first), true);
        ui::clipboard::del(f.window);
        require(!f.canvas().is_node_pinned(id), "deleted nodes must be pruned from scene pin state");
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
    if (argc == 2 && std::string(argv[1]).ends_with("visual")) {
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
