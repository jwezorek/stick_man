#pragma once

#include "../util.hpp"
#include "../../model/project.hpp"
#include "rubber_band.hpp"
#include "constraint_adornment.hpp"
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
#include <memory>

/*------------------------------------------------------------------------------------------------*/

namespace sm {
    class node;
    class bone;
    class skeleton;
    class topology;
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
        class artwork_layer;

        namespace item {
            class base;
            class node;
            class bone;
            class skeleton;
            class character;
        }

        using selection_set = std::unordered_set<item::base*>;

        using item_transform = std::function<void(item::base*)>;
        using node_transform = std::function<void(item::node*)>;
        using bone_transform = std::function<void(item::bone*)>;

        // Canvas-space editing UI that temporarily gets first chance at input before the
        // active tool. Animation action handles use this now; path/control-point editors
        // can use the same mechanism later.
        class interactive_adornment {
        public:
            virtual bool keyPressEvent(QKeyEvent*) { return false; }
            virtual bool mousePressEvent(QGraphicsSceneMouseEvent*) { return false; }
            virtual bool mouseMoveEvent(QGraphicsSceneMouseEvent*) { return false; }
            virtual bool mouseReleaseEvent(QGraphicsSceneMouseEvent*) { return false; }
            virtual void cancel() {}
            virtual ~interactive_adornment() = default;
        };

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
            std::unordered_set<sm::object_id> pinned_node_ids_;
            mdl::project* model_ = nullptr;
            bool constraint_tool_active_ = false;
            bool show_constraints_in_view_ = true;
            std::optional<sm::object_id> selected_constraint_id_;
            std::unique_ptr<constraint_adornment_layer> constraint_adornments_;
            tool::input_handler& inp_handler_;
            item::rubber_band* rubber_band_;
            std::optional<int> zoom_level_;
            artwork_layer* artwork_ = nullptr; // QObject child, lives with the scene.
            std::shared_ptr<interactive_adornment> interactive_adornment_;

            struct bone_pick_state {
                sm::object_id character;
                item::bone* hovered = nullptr;
                QGraphicsLineItem* highlight = nullptr;
                QCursor previous_cursor;
                QGraphicsView::DragMode previous_drag_mode = QGraphicsView::NoDrag;
                bool view_mouse_tracking = false;
                bool viewport_mouse_tracking = false;
                std::function<void(sm::object_id)> picked;
                std::function<void()> cancelled;
            };
            std::optional<bone_pick_state> bone_pick_;

            
            QGraphicsView& view();
            const QGraphicsView& view() const;
            void set_drag_mode(drag_mode dm);
            void set_contents(mdl::project& model);
            item::bone* bone_pick_target(const QPointF& pt) const;
            void update_bone_pick_hover(const QPointF& pt);
            void finish_bone_pick(std::optional<sm::object_id> bone);

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
            void dragEnterEvent(QGraphicsSceneDragDropEvent* event) override;
            void dragMoveEvent(QGraphicsSceneDragDropEvent* event) override;
            void dropEvent(QGraphicsSceneDragDropEvent* event) override;
            void set_scale_aux(double scale, std::optional<QPointF> pt = {});
        public:

            scene(tool::input_handler& inp_handler);
            void init();
            void init_artwork(mdl::project& project);
            artwork_layer& artwork() const;
            item::node* top_node(const QPointF& pt) const;
            item::base* top_item(const QPointF & pt) const;
            std::vector<item::base*> items_in_rect(const QRectF& pt) const;
            std::vector<item::base*> canvas_items() const;
            std::vector<item::node*> node_items() const;
            std::vector<item::bone*> bone_items() const;
            std::vector<item::skeleton*> skeleton_items() const;

            void set_scale(double scale, std::optional<QPointF> pt = {});
            double scale() const;
            void set_zoom_level(int zoom, std::optional<QPointF> pt = {});
            int closest_zoom_level() const;

            void sync_to_model();

            const selection_set& selection() const;
            //sel_type selection_type() const;

            item::skeleton* selected_skeleton() const;
            std::vector<item::skeleton*> selected_skeletons() const;
            item::character* selected_character() const;
            item::character* character_item(const sm::object_id& id) const;
            mdl::selection selected_objects() const;
            std::vector<item::skeleton*> resolved_skeletons() const;
            std::vector<sm::const_skel_ref> loose_selection() const;
            std::vector<item::bone*> selected_bones() const;
            std::vector<item::node*> selected_nodes() const;

            const std::unordered_set<sm::object_id>& pinned_node_ids() const;
            bool is_node_pinned(const sm::object_id& id) const;
            void set_node_pinned(const sm::object_id& id, bool pinned);
            void toggle_node_pinned(const sm::object_id& id);
            void toggle_node_pinned_undoable(const sm::object_id& id);

            bool constraints_visible() const;
            void set_constraint_tool_active(bool active);
            void set_constraints_view_visible(bool visible);
            std::optional<constraint_hit> constraint_at(const QPointF& point) const;
            std::optional<sm::object_id> selected_constraint_id() const { return selected_constraint_id_; }
            const sm::constraint* selected_constraint() const;
            void select_constraint(sm::object_id id);
            void clear_constraint_selection(bool notify = true);
            void set_hovered_constraint(std::optional<sm::object_id> id);

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
            void sync_selection();
            void clear_selection();
            void clear();
            void set_interactive_adornment(std::shared_ptr<interactive_adornment> adornment);
            void clear_interactive_adornment();
            void show_status_line(const QString& txt);
            void hide_status_line();
            void begin_bone_pick(const sm::object_id& character, const QString& slot,
                std::function<void(sm::object_id)> picked, std::function<void()> cancelled = {});
            void cancel_bone_pick();
            bool bone_pick_active() const { return bone_pick_.has_value(); }
            void filter_selection(std::function<bool(item::base*)> filter);
            void delete_item(item::base* item, bool emit_signals);
            QPointF from_global_to_canvas(const QPoint& pt);
            const canvas::manager& manager() const;
            canvas::manager& manager();
            std::optional<sm::point> cursor_pos() const;
        };

        std::optional<mdl::skel_piece> selected_single_model(const scene& canv);
    }
}
