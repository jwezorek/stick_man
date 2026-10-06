#pragma once
#include <QtWidgets>
#include <array>
#include "../../model/project.hpp"

namespace ui::canvas {
    class manager;
    class artwork_layer;
}
namespace ui::pane {
    std::optional<sm::object_id> artwork_character(const mdl::selection& selection);
    class artwork_browser : public QDockWidget {
        Q_OBJECT
        mdl::project& project_;
        canvas::manager& canvases_;
        std::optional<sm::object_id> character_;
        bool backgrounds_target_ = true;
        std::map<std::string, std::pair<sm::image_resource, QIcon>> thumbnails_;
        std::map<std::string, std::pair<sm::image_resource, QIcon>> background_thumbnails_;
        QWidget* body_;
        QComboBox* target_;
        QTabWidget* tabs_;
        QComboBox* appearances_;
        QListWidget* frames_;
        QTreeWidget* slots_;
        QListWidget* states_;
        QTreeWidget* appearance_structure_;
        QDoubleSpinBox* origin_x_;
        QDoubleSpinBox* origin_y_;
        QToolButton* transform_toggle_;
        QFrame* transform_panel_;
        QPushButton* transform_reset_;
        std::array<QDoubleSpinBox*, 5> transform_;
        std::vector<QToolButton*> order_buttons_;
        QListWidget* backgrounds_;
        QToolButton* background_transform_toggle_;
        QFrame* background_transform_panel_;
        QPushButton* background_transform_reset_;
        std::array<QDoubleSpinBox*, 5> background_transform_;
        std::vector<QToolButton*> background_order_buttons_;
        QPointer<QObject> connected_layer_;
        QMetaObject::Connection layer_selection_, layer_appearance_, layer_preview_, layer_transform_;
        bool refreshing_ = false;
        void reorder_slot(const std::string& slot, int index);
        void move_selected_slot(int direction);
        void move_selected_background(int direction);
        void sync_target_to_canvas();
        void import_backgrounds();
        std::optional<sm::object_id> selected_background_id() const;
        void connect_canvas();
        void update_transform_editing();
        bool edit(const std::function<void(sm::artwork&)>& fn);
        void update_slot_binding_row(const std::string& slot, sm::object_id bone, sm::bone_anchor anchor);
        void refresh_details();
        void import_frames();
        void new_slot_dialog();
        void begin_bone_pick(const std::string& slot);
        QString ask_name(const QString& title, const QString& current = {});
    public:
        artwork_browser(mdl::project& project, canvas::manager& canvases, QWidget* parent);
        void refresh();
        std::optional<sm::object_id> character_id() const { return character_; }
        QString active_appearance() const { return appearances_->currentText(); }
    };
}
