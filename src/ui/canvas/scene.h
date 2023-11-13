#pragma once

#include "../util.h"
#include "../../model/project.h"
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

    namespace canvas {
        class item;
        class manager;
        class node_item;
        class bone_item;
        class skeleton_item;

        using selection_set = std::unordered_set<item*>;

        using item_transform = std::function<void(item*)>;
        using node_transform = std::function<void(node_item*)>;
        using bone_transform = std::function<void(bone_item*)>;

        enum class drag_mode {
            none,
            pan,
            rubber_band
        };

        class scene : public QGraphicsScene {

            Q_OBJECT

                friend class manager;

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

            scene(input_handler& inp_handler);
            void init();
            node_item* top_node(const QPointF& pt) const;
            item* top_item(const QPointF& pt) const;
            std::vector<item*> items_in_rect(const QRectF& pt) const;
            std::vector<item*> canvas_items() const;
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
            void add_to_selection(std::span<item*> itms, bool sync = false);
            void add_to_selection(item* itm, bool sync = false);
            void subtract_from_selection(std::span<item*> itms, bool sync = false);
            void subtract_from_selection(item* itm, bool sync = false);
            void set_selection(std::span<item*> itms, bool sync = false);
            void set_selection(item* itm, bool sync = false);
            void clear_selection();
            void clear();
            void show_status_line(const QString& txt);
            void hide_status_line();
            void filter_selection(std::function<bool(item*)> filter);
            void delete_item(item* item, bool emit_signals);
            QPointF from_global_to_canvas(const QPoint& pt);
            std::string tab_name() const;
            const canvas::manager& manager() const;
            canvas::manager& manager();
            std::optional<sm::point> cursor_pos() const;
        };

        std::optional<mdl::skel_piece> selected_single_model(const scene& canv);
    }
}