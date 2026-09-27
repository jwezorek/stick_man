#pragma once

#include <QDockWidget>
#include <QPointer>
#include <QtWidgets>
#include <optional>
#include <unordered_set>
#include <vector>

#include "../../core/sm_object_id.hpp"

namespace mdl { class project; }
namespace ui::canvas { class manager; class scene; }
namespace ui::pane {
    class animation_editor;

    class animation : public QDockWidget {
        Q_OBJECT
        mdl::project* project_ = nullptr;
        canvas::manager* canvases_ = nullptr;
        QTreeWidget* tree_ = nullptr;
        QPushButton *pose_button_ = nullptr, *animation_button_ = nullptr, *delete_button_ = nullptr;
        animation_editor* editor_ = nullptr;
        QWidget* banner_ = nullptr;
        QLabel* banner_label_ = nullptr;
        sm::object_id active_character_, active_animation_;
        std::vector<std::pair<QPointer<QWidget>, bool>> enabled_before_;
        std::vector<std::pair<QPointer<QAction>, bool>> actions_enabled_before_;
        std::vector<std::pair<canvas::scene*, std::unordered_set<sm::object_id>>> pins_before_;
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
