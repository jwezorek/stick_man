#include "canvas_manager.h"
#include "canvas_item.h"
#include <ranges>

namespace r = std::ranges;
namespace rv = std::ranges::views;

namespace {

    ui::skeleton_item* named_skeleton_item(ui::canvas& canv, const std::string& str) {
        auto skels = canv.skeleton_items();
        auto iter = r::find_if(skels,
            [str](auto* skel)->bool {
                return str == skel->model().name();
            }
        );
        if (iter == skels.end()) {
            return nullptr;
        }
        return *iter;
    }

}

/*------------------------------------------------------------------------------------------------*/

void ui::canvas_manager::connect_current_tab_signal() {
    current_tab_conn_ = connect(this, &QTabWidget::currentChanged,
        [this](int i) {
            auto* canv = static_cast<canvas*>(
                static_cast<QGraphicsView*>(widget(i))->scene()
                );
            auto old_active_pane = active_canv_;
            old_active_pane->clear_selection();
            active_canv_ = canv;
            emit active_canvas_changed(*old_active_pane, *canv);
        }
    );
}

void ui::canvas_manager::disconnect_current_tab_signal() {
    disconnect(current_tab_conn_);
}

ui::canvas_manager::canvas_manager(input_handler& inp_handler) :
    drag_mode_(drag_mode::none),
    inp_handler_(inp_handler),
    active_canv_(nullptr) {
    setStyleSheet(
        "QTabBar::tab {"
        "    height: 28px; /* Set the height of tabs */"
        "}"
    );
    add_tab("untitled");
    connect_current_tab_signal();
}

void ui::canvas_manager::init(mdl::project& proj) {
    connect(&proj, &mdl::project::tab_created_or_deleted, this, &canvas_manager::add_or_delete_tab);
    connect(&proj, &mdl::project::pre_new_bone_added, this, &canvas_manager::prepare_to_add_bone);
    connect(&proj, &mdl::project::new_bone_added, this, &canvas_manager::add_new_bone);
    connect(&proj, &mdl::project::new_skeleton_added, this, &canvas_manager::add_new_skeleton);
    connect(&proj, &mdl::project::new_project_opened, this, &canvas_manager::set_contents);
    connect(&proj, &mdl::project::refresh_canvas,
        [this](mdl::project& model, const std::string& canvas, bool clear) {
            if (clear) {
                set_contents_of_canvas(model, canvas);
            }
            else {
                canvas_from_name(canvas)->sync_to_model();
            }
        }
    );
}

void ui::canvas_manager::clear() {
    active_canv_ = nullptr;
    while (count() > 0) {
        QWidget* widget = this->widget(0);
        removeTab(0);
        delete widget;
    }
}

void ui::canvas_manager::add_tab(const std::string& name) {
    QGraphicsView* view = new QGraphicsView();

    view->setRenderHint(QPainter::Antialiasing, true);
    view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    view->scale(1, -1);

    addTab(view, name.c_str());
    canvas* canv = new ui::canvas(inp_handler_);
    view->setScene(canv);
    canv->init();
    canv->set_drag_mode(drag_mode_);

    if (active_canv_ == nullptr) {
        active_canv_ = canv;
    }
    center_active_view();
}

void ui::canvas_manager::add_or_delete_tab(const std::string& name, bool should_add) {
    if (should_add) {
        add_tab(name);
    }
    else {
        int index_of_tab = -1;
        for (int i = 0; i < count(); ++i) {
            if (tabText(i).toStdString() == name) {
                index_of_tab = i;
                break;
            }
        }
        if (index_of_tab == -1) {
            throw std::runtime_error("canvas not found");
        }
        auto view = static_cast<QGraphicsView*>(widget(index_of_tab));
        removeTab(index_of_tab);
        delete view;
    }
}

void ui::canvas_manager::prepare_to_add_bone(sm::node& u, sm::node& v) {
    auto& canv = active_canvas();
    auto* deletee = named_skeleton_item(canv, v.owner().name());
    if (deletee) {
        canv.delete_item(deletee, false);
    }
}

void ui::canvas_manager::add_new_bone(sm::bone& bone) {
    auto& canv = active_canvas();
    canv.insert_item(bone);
    canv.sync_to_model();

    auto& world = bone.owner().owner();
    emit canvas_refresh(world);
}

void ui::canvas_manager::add_new_skeleton(const std::string& canvas, sm::skel_ref skel_ref) {
    auto& canv = *canvas_from_name(canvas);

    auto& skel = skel_ref.get();
    canv.insert_item(skel.root_node());
    canv.insert_item(skel);

    emit canvas_refresh(skel.owner());
}

ui::canvas* ui::canvas_manager::canvas_from_name(const std::string& tab_name) {
    for (int i = 0; i < count(); ++i) {
        if (tabText(i).toStdString() == tab_name) {
            auto view = static_cast<QGraphicsView*>(widget(i));
            return static_cast<ui::canvas*>(view->scene());
        }
    }
    return nullptr;
}

QGraphicsView& ui::canvas_manager::active_view() const {
    return *static_cast<QGraphicsView*>(this->widget(currentIndex()));
}

ui::canvas& ui::canvas_manager::active_canvas() const {

    return *static_cast<ui::canvas*>(active_view().scene());
}

void ui::canvas_manager::center_active_view() {
    active_view().centerOn(0, 0);
}

std::vector<std::string> ui::canvas_manager::tab_names() const {
    std::vector<std::string> names(count());
    for (int i = 0; i < count(); ++i) {
        names[i] = tabText(i).toStdString();
    }
    return names;
}

std::string ui::canvas_manager::tab_name(const canvas& canv) const {
    int index = this->indexOf(&canv.view());
    return (index >= 0) ? tabText(index).toStdString() : "";
}

void ui::canvas_manager::set_contents(mdl::project& model) {

    // clear the old tabs and create new ones based on what is in the project
    disconnect_current_tab_signal();
    clear();
    for (auto tab_name : model.tabs()) {
        add_tab(tab_name.c_str());
    }
    connect_current_tab_signal();

    // set their contents...
    for (auto tab : model.tabs()) {
        canvas_from_name(tab)->set_contents(
            model.skeletons_on_tab(tab) | r::to<std::vector<sm::skel_ref>>()
        );
    }
    emit canvas_refresh(model.world());
}

void ui::canvas_manager::set_contents_of_canvas(mdl::project& model, const std::string& canvas) {
    auto* canv = canvas_from_name(canvas);
    if (!canv) {
        return;
    }

    auto new_contents = model.skeletons_on_tab(canvas) | r::to<std::vector<sm::skel_ref>>();
    canv->set_contents(new_contents);

    emit canvas_refresh(model.world());
}

void ui::canvas_manager::clear_canvas(const std::string& canv)
{
    canvas_from_name(canv)->clear();
}

void ui::canvas_manager::set_drag_mode(drag_mode dm) {
    drag_mode_ = dm;
    for (auto* canv : canvases()) {
        canv->set_drag_mode(dm);
    }
}

void ui::canvas_manager::set_active_canvas(const canvas& c) {
    auto canvases = this->canvases();
    for (auto [index, canv_ptr] : rv::enumerate(canvases)) {
        if (&c == canv_ptr) {
            setCurrentIndex(index);
        }
    }
}