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
    class ik_sandbox;
}

namespace ui {

    class canvas_view; 
    class stick_man;
    class tool_manager;
    class node_item;
    class bone_item;
    class abstract_canvas_item;

    using selection_set = std::unordered_set<ui::abstract_canvas_item*>;

    using item_transform = std::function<void(abstract_canvas_item*)>;
    using node_transform = std::function<void(node_item*)>;
    using bone_transform = std::function<void(bone_item*)>;

    class canvas : public QGraphicsScene {

        Q_OBJECT

    private:

        constexpr static auto k_grid_line_spacing = 10;
        double scale_ = 1.0;
        QString status_line_;
        std::unordered_set<ui::abstract_canvas_item*> selection_;

        tool_manager& tool_mgr();
        void sync_selection();

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
        std::vector<ui::abstract_canvas_item*> selection() const;
        std::vector<ui::bone_item*> selected_bones() const;
        std::vector<ui::node_item*> selected_nodes() const;
        bool is_status_line_visible() const;

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

        void show_status_line(const QString& txt);
        void hide_status_line();

    signals:
        void selection_changed(
            const std::unordered_set<ui::abstract_canvas_item*>& sel
        );
    };

    class canvas_view : public QGraphicsView {
    private:
        stick_man* main_window_;
        
    public:
        canvas_view();
        canvas& canvas();
        stick_man& main_window();
    };

    class abstract_canvas_item {
    protected:
        QGraphicsItem* selection_frame_;

        virtual QGraphicsItem* create_selection_frame() const = 0;
        virtual void sync_item_to_model() = 0;
        virtual void sync_sel_frame_to_model() = 0;

    public:
        abstract_canvas_item();
        void sync_to_model();
        canvas* canvas() const;
        bool is_selected() const;
        void set_selected(bool selected);
    };

    template<typename T, typename U>
    class has_stick_man_model : public abstract_canvas_item {
    protected:
        U& model_;
    public:
        has_stick_man_model(U& model) : model_(model) {
            auto* derived = static_cast<T*>(this);
            model.set_user_data(std::ref(*derived));
        }
        U& model() { return model_; }
    };

    class node_item : 
        public has_stick_man_model<node_item, sm::node&>, public QGraphicsEllipseItem {
    private:
        QGraphicsEllipseItem* pin_;
        void sync_item_to_model() override;
        void sync_sel_frame_to_model() override;
        QGraphicsItem* create_selection_frame() const override;
    public:
        using model_type = sm::node;

        node_item(sm::node& node, double scale);
        void set_pinned(bool pinned);
    };

    class bone_item : 
        public has_stick_man_model<bone_item, sm::bone&>, public QGraphicsPolygonItem {
    private:
        void sync_item_to_model() override;
        void sync_sel_frame_to_model() override;
        QGraphicsItem* create_selection_frame() const override;
    public:
        using model_type = sm::bone;

        bone_item(sm::bone& bone, double scale);
        node_item& parent_node_item() const;
        node_item& child_node_item() const;
    };

    template<typename T, typename U>
    T& item_from_model(U& model_obj) {
        return std::any_cast<std::reference_wrapper<T>>(model_obj.get_user_data()).get();
    }
}