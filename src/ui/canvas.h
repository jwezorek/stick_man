#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene>
#include <vector>
#include <any>
#include <unordered_set>
#include <span>
#include <functional>

/*------------------------------------------------------------------------------------------------*/

namespace sm {
    class node;
    class bone;
}

namespace ui {

    class canvas_view; 
    class stick_man;
    class tool_manager;
    class node_item;
    class bone_item;
    class abstract_canvas_item;

    using selection_set = std::unordered_set<ui::abstract_canvas_item*>;
	enum class sel_type {
		none = 0,
		node,
		nodes,
		bone,
		bones,
		mixed
	};

	struct joint_info {
		sm::bone* anchor_bone;
		sm::bone* dependent_bone;
		sm::node* shared_node;
	};

	using item_transform = std::function<void(abstract_canvas_item*)>;
	using node_transform = std::function<void(node_item*)>;
	using bone_transform = std::function<void(bone_item*)>;

    class canvas : public QGraphicsScene {

        Q_OBJECT

    private:

        constexpr static auto k_grid_line_spacing = 10;
        double scale_ = 1.0;
        QString status_line_;
        selection_set selection_;

        tool_manager& tool_mgr();
        void sync_selection();
		void sync_rotation_constraint_to_model();

    protected:

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

        canvas();
        canvas_view& view() const;
        node_item* top_node(const QPointF& pt) const;
        abstract_canvas_item* top_item(const QPointF& pt) const;
        std::vector<abstract_canvas_item*> items_in_rect(const QRectF& pt) const;
        std::vector<node_item*> root_node_items() const;
        std::vector<node_item*> node_items() const;
        std::vector<bone_item*> bone_items() const;
        void set_scale(double scale, std::optional<QPointF> pt = {});
        double scale() const;
        void sync_to_model();

        const selection_set& selection() const;
		sel_type selection_type() const;
		std::optional<joint_info> selected_joint() const;

        std::vector<ui::bone_item*> selected_bones() const;
        std::vector<ui::node_item*> selected_nodes() const;
        bool is_status_line_visible() const;

		void insert_item(sm::node& node);
		void insert_item(sm::bone& bone);

        void transform_selection(item_transform trans);
        void transform_selection(node_transform trans);
        void transform_selection(bone_transform trans);
        void add_to_selection(std::span<abstract_canvas_item*> itms);
        void add_to_selection(abstract_canvas_item* itm);
        void subtract_from_selection(std::span<abstract_canvas_item*> itms);
        void subtract_from_selection(abstract_canvas_item* itm);
        void set_selection(std::span<abstract_canvas_item*> itms);
        void set_selection(abstract_canvas_item* itm);
        void clear_selection();
		void clear();
        void show_status_line(const QString& txt);
        void hide_status_line();

    signals:
        void selection_changed();
		void contents_changed();
    };

    class canvas_view : public QGraphicsView {
    private:
        stick_man* main_window_;
        
    public:
        canvas_view();
        canvas& canvas();
        stick_man& main_window();
    };

}