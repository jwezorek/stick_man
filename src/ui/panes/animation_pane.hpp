#pragma once
#include <QDockWidget>
#include <QPointer>
#include <QtWidgets>
#include "../../core/sm_skeleton.hpp"

namespace mdl { class project; }
namespace ui { namespace canvas { class manager; } class timeline; }
namespace ui::pane {
    class animation : public QDockWidget {
        Q_OBJECT
        mdl::project* project_ = nullptr;
        canvas::manager* canvases_ = nullptr;
        QTreeWidget* tree_;
        QPushButton *pose_button_, *animation_button_, *delete_button_;
        QDockWidget* timeline_pane_ = nullptr;
        QWidget* banner_ = nullptr;
        QLabel* banner_label_ = nullptr;
        ui::timeline* timeline_ = nullptr;
        std::unique_ptr<sm::topology> working_;
        sm::object_id active_character_, active_animation_;
        std::vector<std::pair<QPointer<QWidget>, bool>> enabled_before_;
        std::optional<std::pair<sm::object_id, sm::object_id>> pending_edit_;
        bool syncing_ = false;
        void refresh();
        void update_buttons();
        void context_menu(QPoint point);
        void rename_item(QTreeWidgetItem* item);
        void duplicate_current();
        void delete_current();
        void apply_current();
        void offer_edit();
        sm::object_id selected_character() const;
        QTreeWidgetItem* find_asset(sm::object_id id) const;
        void select_and_rename(sm::object_id id);
    public:
        explicit animation(QWidget* parent);
        ~animation() override;
        void init(canvas::manager& canvases, mdl::project& project);
        void create_pose();
        void create_animation();
        bool open_animation(sm::object_id character, sm::object_id animation);
        void leave_animation();
    };
}
