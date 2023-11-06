#pragma once

#include "util.h"
#include "../model/project.h"
#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene>
#include <vector>
#include <any>
#include <unordered_set>
#include <span>
#include <functional>
#include <variant>
#include <optional>
#include <ranges>

/*------------------------------------------------------------------------------------------------*/

namespace sm {
    class node;
    class bone;
    class skeleton;
    class world;
}

namespace mdl {
    class project;
}

namespace ui {

    class stick_man;
    class tool_manager;
    class input_handler;
    class canvas_manager;
    class node_item;
    class bone_item;
    class skeleton_item;
    class abstract_canvas_item;

    using selection_set = std::unordered_set<ui::abstract_canvas_item*>;

    using item_transform = std::function<void(abstract_canvas_item*)>;
    using node_transform = std::function<void(node_item*)>;
    using bone_transform = std::function<void(bone_item*)>;

    enum class drag_mode {
        none,
        pan,
        rubber_band
    };

    class canvas : public QGraphicsScene {

        Q_OBJECT

        friend class canvas_manager;

    private:

        constexpr static auto k_grid_line_spacing = 10;
        double scale_ = 1.0;
        QString status_line_;
        selection_set selection_;
        input_handler& inp_handler_;

        void sync_selection();
        QGraphicsView& view();
        const QGraphicsView& view() const;
        void set_drag_mode(drag_mode dm);
        void set_contents(const std::vector<sm::skel_ref>& contents);

        void keyPressEvent(QKeyEvent* event) override;
        void keyReleaseEvent(QKeyEvent* event) override;
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
        void wheelEvent(QGraphicsSceneWheelEvent* event) override;
        void drawBackground(QPainter* painter, const QRectF& rect) override;
        void drawForeground(QPainter* painter, const QRectF& rect) override;
        void focusOutEvent(QFocusEvent* focusEvent) override;

    public:

        canvas(input_handler& inp_handler);
        void init();
        node_item* top_node(const QPointF& pt) const;
        abstract_canvas_item* top_item(const QPointF& pt) const;
        std::vector<abstract_canvas_item*> items_in_rect(const QRectF& pt) const;
        std::vector<abstract_canvas_item*> canvas_items() const;
        std::vector<node_item*> root_node_items() const;
        std::vector<node_item*> node_items() const;
        std::vector<bone_item*> bone_items() const;
        std::vector<skeleton_item*> skeleton_items() const;

        void set_scale(double scale, std::optional<QPointF> pt = {});
        double scale() const;
        void sync_to_model();

        const selection_set& selection() const;
        //sel_type selection_type() const;

        skeleton_item* selected_skeleton() const;
        std::vector<bone_item*> selected_bones() const;
        std::vector<node_item*> selected_nodes() const;

        bool is_status_line_visible() const;

        node_item* insert_item(sm::node& node);
        bone_item* insert_item(sm::bone& bone);
        skeleton_item* insert_item(sm::skeleton& skel);

        void transform_selection(item_transform trans);
        void transform_selection(node_transform trans);
        void transform_selection(bone_transform trans);
        void add_to_selection(std::span<abstract_canvas_item*> itms, bool sync = false);
        void add_to_selection(abstract_canvas_item* itm, bool sync = false);
        void subtract_from_selection(std::span<abstract_canvas_item*> itms, bool sync = false);
        void subtract_from_selection(abstract_canvas_item* itm, bool sync = false);
        void set_selection(std::span<abstract_canvas_item*> itms, bool sync = false);
        void set_selection(abstract_canvas_item* itm, bool sync = false);
        void clear_selection();
        void clear();
        void show_status_line(const QString& txt);
        void hide_status_line();
        void filter_selection(std::function<bool(abstract_canvas_item*)> filter);
        void delete_item(abstract_canvas_item* item, bool emit_signals);
        QPointF from_global_to_canvas(const QPoint& pt);
        std::string tab_name() const;
        const canvas_manager& manager() const;
        canvas_manager& manager(); 
        std::optional<sm::point> cursor_pos() const;
    };

    class canvas_manager : public QTabWidget {

        Q_OBJECT

    private:
        QGraphicsView& active_view() const;
        canvas* active_canv_;
        QMetaObject::Connection current_tab_conn_;
        input_handler& inp_handler_;
        drag_mode drag_mode_;

        void connect_current_tab_signal();
        void disconnect_current_tab_signal();
        void add_new_tab(const std::string& name);
        void prepare_to_add_bone(sm::node& u, sm::node& v);
        void add_new_bone(sm::bone& bone);
        void add_new_skeleton(sm::skel_ref skel);
        void set_contents(mdl::project& model);
        void set_contents_of_canvas(mdl::project& model, const std::string& canvas);
        void clear_canvas(const std::string& canv);

    public:
        canvas_manager(input_handler& inp_handler);
        void init(mdl::project& proj);
        void clear();
        void center_active_view();
        canvas& active_canvas() const;
        canvas* canvas_from_tab(const std::string& tab_name);
        void set_drag_mode(drag_mode dm);
        void set_active_canvas(const canvas& c);
        std::vector<std::string> tab_names() const;
        std::string tab_name(const canvas& canv) const;

        auto canvases() {
            namespace r = std::ranges;
            namespace rv = std::ranges::views;
            return rv::iota(0, count()) |
                rv::transform(
                    [this](int i)->ui::canvas* {
                        return static_cast<ui::canvas*>(
                            static_cast<QGraphicsView*>(widget(i))->scene()
                            );
                    }
                );
        }

    signals:
        void active_canvas_changed(ui::canvas& old_canv, ui::canvas& canv);
        void selection_changed(ui::canvas& canv);
        void rubber_band_change(ui::canvas& canv, QRect rbr, QPointF from, QPointF to);
        void canvas_refresh(sm::world& proj);
    };

    std::optional<mdl::skel_piece> selected_single_model(const ui::canvas& canv);
}