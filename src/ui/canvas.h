#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene>
#include <vector>
#include <any>

/*------------------------------------------------------------------------------------------------*/

namespace sm {
    class joint;
    class bone;
    class ik_sandbox;
}

namespace ui {

    class canvas_view; 
    class stick_man;
    class tool_manager;
    class joint_item;
    class bone_item;

    class canvas : public QGraphicsScene {

        Q_OBJECT

    private:

        constexpr static auto k_grid_line_spacing = 10;
        double scale_ = 1.0;

        tool_manager& tool_mgr();

    public:

        canvas();
        void drawBackground(QPainter* painter, const QRectF& rect) override;
        canvas_view& view();
        joint_item* top_joint(const QPointF& pt) const;
        std::vector<joint_item*> root_joint_items() const;
        std::vector<joint_item*> joint_items() const;
        std::vector<bone_item*> bone_items() const;
        void set_scale(double scale, std::optional<QPointF> pt = {});
        double scale();
        void sync_to_model();

    protected:

        void keyPressEvent(QKeyEvent* event) override;
        void keyReleaseEvent(QKeyEvent* event) override;
        void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;
        void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
        void wheelEvent(QGraphicsSceneWheelEvent* event) override;

    };

    class canvas_view : public QGraphicsView {
    private:
        stick_man* main_window_;
        
    public:
        canvas_view();
        canvas& canvas();
        stick_man& main_window();
    };

    class abstract_stick_man_item {
    public:
        virtual void sync_to_model() = 0;
        canvas* canvas();
    };

    template<typename T, typename U>
    class has_stick_man_model : public abstract_stick_man_item {
    protected:
        U& model_;
    public:
        has_stick_man_model(U& model) : model_(model) {
            auto* derived = static_cast<T*>(this);
            model.set_user_data(std::ref(*derived));
        }
        U& model() { return model_; }
    };

    class joint_item : public has_stick_man_model<joint_item, sm::joint&>, public QGraphicsEllipseItem {
    private:
        QGraphicsEllipseItem* pin_;
    public:
        joint_item(sm::joint& joint, double scale);
        void set_pinned(bool pinned);
        void sync_to_model() override;
    };

    class bone_item : public has_stick_man_model<bone_item, sm::bone&>, public QGraphicsPolygonItem {
    public:
        bone_item(sm::bone& bone, double scale);
        joint_item& parent_joint_item() const;
        joint_item& child_joint_item() const;
        void sync_to_model() override;
    };

    template<typename T, typename U>
    T& item_from_model(U& model_obj) {
        return std::any_cast<std::reference_wrapper<T>>(model_obj.get_user_data()).get();
    }
}