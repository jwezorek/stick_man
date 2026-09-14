#pragma once
#include <QtWidgets>
#include "../../model/project.hpp"

namespace ui::canvas { class manager; }
namespace ui::pane {
    std::optional<sm::object_id> artwork_character(const mdl::selection& selection);
    class artwork_browser : public QDockWidget {
        Q_OBJECT
        mdl::project& project_;
        canvas::manager& canvases_;
        std::optional<sm::object_id> character_;
        std::unordered_map<sm::object_id, QString> active_;
        std::map<std::string, std::pair<sm::image_resource, QIcon>> thumbnails_;
        QWidget* body_;
        QLabel* character_label_;
        QComboBox* appearances_;
        QListWidget* frames_;
        QListWidget* slots_;
        QListWidget* states_;
        QComboBox* mapping_;
        QDoubleSpinBox* origin_x_;
        QDoubleSpinBox* origin_y_;
        bool refreshing_ = false;
        void edit(const std::function<void(sm::artwork&)>& fn);
        void refresh_details();
        void import_frames();
        void slot_dialog(bool rebind);
        QString ask_name(const QString& title, const QString& current = {});
    public:
        artwork_browser(mdl::project& project, canvas::manager& canvases, QWidget* parent);
        void refresh();
        std::optional<sm::object_id> character_id() const { return character_; }
        QString active_appearance() const { return appearances_->currentText(); }
    };
}
