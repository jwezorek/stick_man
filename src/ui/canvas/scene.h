#pragma once

#include "../util.h"
#include "../../model/project.h"
#include "rubber_band.h"
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

    namespace tool {
        class manager;
        class input_handler;
    }

    namespace canvas {
        class manager;

        namespace item {
            class base;
            class node;
            class bone;
            class skeleton;
        }

        using selection_set = std::unordered_set<item::base*>;

        using item_transform = std::function<void(item::base*)>;
        using node_transform = std::function<void(item::node*)>;
        using bone_transform = std::function<void(item::bone*)>;

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
            QString status_line_;
            selection_set selection_;
            tool::input_handler& inp_handler_;
            item::rubber_band* rubber_band_;
            std::optional<int> zoom_level_;

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
            void set_scale_aux(double scale, std::optional<QPointF> pt = {});
        public:

            scene(tool::input_handler& inp_handler);
            void init();
            item::node* top_node(const QPointF& pt) const;
            item::base* top_item(const QPointF & pt) const;
            std::vector<item::base*> items_in_rect(const QRectF& pt) const;
            std::vector<item::base*> canvas_items() const;
            std::vector<item::node*> root_node_items() const;
            std::vector<item::node*> node_items() const;
            std::vector<item::bone*> bone_items() const;
            std::vector<item::skeleton*> skeleton_items() const;

            void set_scale(double scale, std::optional<QPointF> pt = {});
            double scale() const;
            void set_zoom_level(int zoom, std::optional<QPointF> pt = {});
            std::optional<int> zoom_level() const;
            int closest_zoom_level() const;

            void sync_to_model();

            const selection_set& selection() const;
            //sel_type selection_type() const;

            item::skeleton* selected_skeleton() const;
            std::vector<item::bone*> selected_bones() const;
            std::vector<item::node*> selected_nodes() const;

            bool is_status_line_visible() const;

            item::node* insert_item(sm::node& node);
            item::bone* insert_item(sm::bone& bone);
            item::skeleton* insert_item(sm::skeleton& skel);

            void transform_selection(item_transform trans);
            void transform_selection(node_transform trans);
            void transform_selection(bone_transform trans);
            void add_to_selection(std::span<item::base*> itms, bool sync = false);
            void add_to_selection(item::base* itm, bool sync = false);
            void subtract_from_selection(std::span<item::base*> itms, bool sync = false);
            void subtract_from_selection(item::base* itm, bool sync = false);
            void set_selection(std::span<item::base*> itms, bool sync = false);
            void set_selection(item::base* itm, bool sync = false);
            void clear_selection();
            void clear();
            void show_status_line(const QString& txt);
            void hide_status_line();
            void filter_selection(std::function<bool(item::base*)> filter);
            void delete_item(item::base* item, bool emit_signals);
            QPointF from_global_to_canvas(const QPoint& pt);
            std::string tab_name() const;
            const canvas::manager& manager() const;
            canvas::manager& manager();
            std::optional<sm::point> cursor_pos() const;
        };

        std::optional<mdl::skel_piece> selected_single_model(const scene& canv);
    }
}