#pragma once

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

/*------------------------------------------------------------------------------------------------*/

namespace sm {
    class node;
    class bone;
    class skeleton;
    class world;
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

        canvas(input_handler& inp_handler);
        void init();
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
        //sel_type selection_type() const;

        ui::skeleton_item* selected_skeleton() const;
        std::vector<ui::bone_item*> selected_bones() const;
        std::vector<ui::node_item*> selected_nodes() const;
        bool is_status_line_visible() const;

        ui::node_item* insert_item(sm::node& node);
        ui::bone_item* insert_item(sm::bone& bone);
        ui::skeleton_item* insert_item(sm::skeleton& skel);

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
    };

    class canvas_manager : public QTabWidget {

        Q_OBJECT

    private:
        QGraphicsView& active_view() const;
        canvas* active_canv_;
        QMetaObject::Connection current_tab_conn_;
        input_handler& inp_handler_;

        static std::string tab_from_skeleton(const sm::skeleton& skel);
        static std::vector<std::string> tab_names_from_model(const sm::world& w);

        void connect_current_tab_signal();
        void disconnect_current_tab_signal();

    public:
        canvas_manager(input_handler& inp_handler);
        void clear();;
        void center_active_view();
        canvas& active_canvas() const;
        canvas* add_new_tab(QString name);
        canvas* canvas_from_skeleton(sm::skeleton& skel);
        canvas* canvas_from_tab(const std::string& tab_name);
        std::vector<canvas*> canvases();
        void set_drag_mode(drag_mode dm);
        void set_active_canvas(const canvas& c);
        std::vector<std::string> tab_names() const;
        std::string tab_name(const canvas& canv) const;
        void sync_to_model(sm::world& model);
    signals:
        void active_canvas_changed(ui::canvas& old_canv, ui::canvas& canv);
        void selection_changed();
        void contents_changed();
        void rubber_band_change(QRect rbr, QPointF from, QPointF to);
    };

    using model_variant = std::variant<std::reference_wrapper<sm::skeleton>,
        std::reference_wrapper<sm::node>,
        std::reference_wrapper<sm::bone>>;
    std::optional<model_variant> selected_single_model(const ui::canvas& canv);
}