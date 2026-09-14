#include "artwork_browser.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../../core/sm_bone.hpp"
#include <type_traits>

namespace {
    std::string selected(QListWidget* list) { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString().toStdString() : ""; }
    void restore(QListWidget* list, const std::string& name) {
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString().toStdString() == name) { list->setCurrentRow(i); return; }
        if (list->count()) list->setCurrentRow(0);
    }
    void item(QListWidget* list, const std::string& name, const QString& label) {
        auto* row = new QListWidgetItem(label, list); row->setData(Qt::UserRole, QString::fromStdString(name));
    }
    QDoubleSpinBox* coordinate(QWidget* parent) {
        auto* spin = new QDoubleSpinBox(parent); spin->setRange(-1e9, 1e9); spin->setDecimals(4); return spin;
    }
    std::pair<std::string, std::string> selected_appearance_item(QTreeWidget* tree) {
        auto* current = tree->currentItem();
        if (!current) return {};
        return {
            current->data(0, Qt::UserRole).toString().toStdString(),
            current->data(0, Qt::UserRole + 1).toString().toStdString()
        };
    }
    void restore_appearance_item(QTreeWidget* tree, const std::string& slot, const std::string& state) {
        QTreeWidgetItem* fallback = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* top = tree->topLevelItem(i);
            if (!fallback) fallback = top->childCount() ? top->child(0) : top;
            if (top->data(0, Qt::UserRole).toString().toStdString() != slot) continue;
            if (state.empty()) { tree->setCurrentItem(top); return; }
            for (int j = 0; j < top->childCount(); ++j) {
                auto* child = top->child(j);
                if (child->data(0, Qt::UserRole + 1).toString().toStdString() == state) {
                    tree->setCurrentItem(child); return;
                }
            }
            tree->setCurrentItem(top); return;
        }
        if (fallback) tree->setCurrentItem(fallback);
    }
    const sm::appearance_slot* appearance_slot(const sm::appearance& appearance, const std::string& slot) {
        for (const auto& candidate : appearance.appearance_slots) if (candidate.slot == slot) return &candidate;
        return nullptr;
    }
    QString mapping_text(const sm::appearance_slot* implementation, const std::string& state) {
        if (!implementation) return QStringLiteral("—");
        auto mapping = implementation->states.find(state);
        if (mapping == implementation->states.end()) return QStringLiteral("Use default");
        if (!mapping->second) return QStringLiteral("Hidden");
        return QString::fromStdString(*mapping->second);
    }
}
std::optional<sm::object_id> ui::pane::artwork_character(const mdl::selection& selection) {
    std::optional<sm::object_id> result;
    for (const auto& object : selection) {
        auto id = std::visit([](auto ref) -> std::optional<sm::object_id> {
            using T = std::remove_cvref_t<decltype(ref.get())>;
            if constexpr (std::is_same_v<T, sm::character>) return ref->id();
            else {
                const sm::skeleton* skeleton;
                if constexpr (std::is_same_v<T, sm::skeleton>) skeleton = &ref.get();
                else skeleton = &ref->owner();
                auto parent = skeleton->parent_character();
                return parent ? std::optional(parent->get().id()) : std::nullopt;
            }
        }, object);
        if (!id || (result && result != id)) return std::nullopt;
        result = id;
    }
    return result;
}
ui::pane::artwork_browser::artwork_browser(mdl::project& project, canvas::manager& canvases, QWidget* parent) :
    QDockWidget("Artwork Browser", parent), project_(project), canvases_(canvases) {
    setObjectName("artwork_browser");
    body_ = new QWidget(this); setWidget(body_);
    auto* layout = new QVBoxLayout(body_);
    character_label_ = new QLabel(body_); layout->addWidget(character_label_);
    auto buttons = [this](QVBoxLayout* target, std::initializer_list<std::pair<QString, std::function<void()>>> actions) {
        std::vector<QPushButton*> result;
        auto* row = new QHBoxLayout;
        for (const auto& [label, fn] : actions) {
            auto* button = new QPushButton(label, body_); row->addWidget(button); result.push_back(button);
            connect(button, &QPushButton::clicked, this, [this, fn] {
                if (!character_) return;
                try { fn(); } catch (const std::exception& e) { QMessageBox::warning(this, "Artwork", e.what()); }
            });
        }
        target->addLayout(row);
        return result;
    };

    auto* tabs = new QTabWidget(body_); layout->addWidget(tabs);

    // Character-wide artwork structure. Nothing on this page is scoped to an appearance.
    auto* structure_tab = new QWidget(tabs); auto* structure_layout = new QVBoxLayout(structure_tab); tabs->addTab(structure_tab, "Structure");
    auto* structure_help = new QLabel("Slots and states define the artwork structure shared by every appearance.", structure_tab);
    structure_help->setWordWrap(true); structure_layout->addWidget(structure_help);
    structure_layout->addWidget(new QLabel("Slots", structure_tab));
    slots_ = new QListWidget(structure_tab); slots_->setObjectName("artwork_slots"); structure_layout->addWidget(slots_);
    buttons(structure_layout, {{"New slot…", [this] { slot_dialog(false); }}, {"Rebind…", [this] { slot_dialog(true); }}, {"Rename", [this] {
        auto old = selected(slots_); if (old.empty()) return;
        auto name = ask_name("Rename slot", QString::fromStdString(old)); if (name.isEmpty()) return;
        edit([&](auto& a) { a.rename_slot(old, name.toStdString()); }); restore(slots_, name.toStdString());
    }}, {"Delete", [this] {
        auto name = selected(slots_); if (!name.empty()) edit([&](auto& a) { a.delete_slot(name); });
    }}});
    structure_layout->addWidget(new QLabel("States for selected slot", structure_tab));
    states_ = new QListWidget(structure_tab); states_->setObjectName("artwork_states"); structure_layout->addWidget(states_);
    buttons(structure_layout, {{"New state", [this] {
        auto slot = selected(slots_); if (slot.empty()) return;
        auto name = ask_name("New state"); if (!name.isEmpty()) edit([&](auto& a) { a.add_state(slot, name.toStdString()); });
    }}, {"Rename", [this] {
        auto slot = selected(slots_), state = selected(states_); if (state.empty()) return;
        auto name = ask_name("Rename state", QString::fromStdString(state)); if (name.isEmpty()) return;
        edit([&](auto& a) { a.rename_state(slot, state, name.toStdString()); });
    }}, {"Delete", [this] {
        auto slot = selected(slots_), state = selected(states_); if (!state.empty()) edit([&](auto& a) { a.delete_state(slot, state); });
    }}});

    // One concrete implementation of the shared structure.
    auto* appearance_tab = new QWidget(tabs); auto* appearance_layout = new QVBoxLayout(appearance_tab); tabs->addTab(appearance_tab, "Appearances");
    auto* appearance_help = new QLabel("Each appearance maps the shared structure to concrete images.", appearance_tab);
    appearance_help->setWordWrap(true); appearance_layout->addWidget(appearance_help);
    auto* appearance_selector = new QFormLayout;
    appearances_ = new QComboBox(appearance_tab); appearances_->setObjectName("artwork_appearance");
    appearance_selector->addRow("Appearance", appearances_); appearance_layout->addLayout(appearance_selector);
    buttons(appearance_layout, {{"New appearance", [this] {
        auto name = ask_name("New appearance"); if (name.isEmpty()) return;
        edit([&](auto& a) { a.add_appearance(name.toStdString()); });
        appearances_->setCurrentText(name);
    }}, {"Rename", [this] {
        auto old = active_appearance(); if (old.isEmpty()) return;
        auto name = ask_name("Rename appearance", old); if (name.isEmpty()) return;
        edit([&](auto& a) { a.rename_appearance(old.toStdString(), name.toStdString()); }); appearances_->setCurrentText(name);
    }}, {"Delete", [this] {
        auto name = active_appearance().toStdString(); if (!name.empty()) edit([&](auto& a) { a.delete_appearance(name); });
    }}});
    appearance_structure_ = new QTreeWidget(appearance_tab);
    appearance_structure_->setObjectName("artwork_appearance_structure");
    appearance_structure_->setHeaderLabels({"Slot / state", "Image"});
    appearance_structure_->setRootIsDecorated(true);
    appearance_layout->addWidget(appearance_structure_);
    auto membership = buttons(appearance_layout, {{"Add to appearance", [this] {
        auto [slot, _] = selected_appearance_item(appearance_structure_);
        auto name = active_appearance().toStdString(); if (slot.empty() || name.empty()) return;
        edit([&](auto& a) {
            auto app = a.appearances().at(name);
            if (!appearance_slot(app, slot)) app.appearance_slots.push_back({slot});
            a.set_appearance(name, std::move(app));
        });
    }}, {"Remove from appearance", [this] {
        auto [slot, _] = selected_appearance_item(appearance_structure_);
        auto name = active_appearance().toStdString(); if (slot.empty() || name.empty()) return;
        edit([&](auto& a) {
            auto app = a.appearances().at(name);
            std::erase_if(app.appearance_slots, [&](const auto& s) { return s.slot == slot; });
            a.set_appearance(name, std::move(app));
        });
    }}});
    add_to_appearance_ = membership.at(0); remove_from_appearance_ = membership.at(1);
    mapping_ = new QComboBox(appearance_tab); mapping_->setObjectName("artwork_mapping");
    appearance_layout->addWidget(new QLabel("Image for selected state", appearance_tab)); appearance_layout->addWidget(mapping_);

    // Character-local image resources shared by all appearances.
    auto* image_tab = new QWidget(tabs); auto* image_layout = new QVBoxLayout(image_tab); tabs->addTab(image_tab, "Images");
    auto* image_help = new QLabel("Images are character resources shared by all appearances.", image_tab);
    image_help->setWordWrap(true); image_layout->addWidget(image_help);
    frames_ = new QListWidget(image_tab); frames_->setObjectName("artwork_frames"); frames_->setIconSize({64, 64}); image_layout->addWidget(frames_);
    buttons(image_layout, {{"Import…", [this] { import_frames(); }}, {"Rename", [this] {
        auto old = selected(frames_); if (old.empty()) return;
        auto name = ask_name("Rename image", QString::fromStdString(old)); if (name.isEmpty()) return;
        edit([&](auto& a) { a.rename_frame(old, name.toStdString()); }); restore(frames_, name.toStdString());
    }}, {"Delete", [this] {
        auto name = selected(frames_); if (!name.empty()) edit([&](auto& a) { a.delete_frame(name); });
    }}});
    auto* origin = new QFormLayout;
    origin_x_ = coordinate(image_tab); origin_y_ = coordinate(image_tab);
    origin_x_->setObjectName("artwork_origin_x"); origin_y_->setObjectName("artwork_origin_y");
    origin->addRow("Origin X", origin_x_); origin->addRow("Origin Y (up)", origin_y_); image_layout->addLayout(origin);
    auto save_origin = [this] {
        if (refreshing_ || !character_) return;
        auto name = selected(frames_); if (name.empty()) return;
        sm::point value{origin_x_->value(), origin_y_->value()};
        if (project_.core().artwork(*character_).frames().at(name).registration_origin == value) return;
        edit([&](auto& a) { a.set_registration_origin(name, value); });
    };
    connect(origin_x_, &QDoubleSpinBox::editingFinished, this, save_origin);
    connect(origin_y_, &QDoubleSpinBox::editingFinished, this, save_origin);

    connect(mapping_, &QComboBox::activated, this, [this](int index) {
        if (refreshing_ || !character_) return;
        auto [slot, state] = selected_appearance_item(appearance_structure_);
        auto name = active_appearance().toStdString();
        if (slot.empty() || state.empty() || name.empty()) return;
        auto frame = mapping_->currentData().toString().toStdString();
        edit([&](auto& a) {
            auto app = a.appearances().at(name);
            for (auto& s : app.appearance_slots) if (s.slot == slot) {
                if (index == 0) s.states.erase(state);
                else s.states[state] = index == 1 ? sm::frame_target{} : sm::frame_target{frame};
            }
            a.set_appearance(name, std::move(app));
        });
    });
    connect(appearances_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (!refreshing_ && character_) { active_[*character_] = name; refresh(); }
    });
    connect(frames_, &QListWidget::currentRowChanged, this, [this] { if (!refreshing_) refresh_details(); });
    connect(slots_, &QListWidget::currentRowChanged, this, [this] { if (!refreshing_) refresh(); });
    connect(appearance_structure_, &QTreeWidget::currentItemChanged, this, [this] { if (!refreshing_) refresh_details(); });
    connect(&canvases_, &canvas::manager::selection_changed, this, [this] { refresh(); });
    connect(&project_, &mdl::project::project_changed, this, [this] { refresh(); });
    connect(&project_, &mdl::project::new_project_opened, this, [this] { active_.clear(); refresh(); });
    refresh();
}
QString ui::pane::artwork_browser::ask_name(const QString& title, const QString& current) {
    bool ok = false; auto name = QInputDialog::getText(this, title, "Name", QLineEdit::Normal, current, &ok);
    return ok ? name.trimmed() : QString{};
}
void ui::pane::artwork_browser::edit(const std::function<void(sm::artwork&)>& fn) {
    if (!character_) return;
    try { project_.edit_artwork(*character_, fn); }
    catch (const std::exception& e) { QMessageBox::warning(this, "Artwork", e.what()); }
}
void ui::pane::artwork_browser::refresh() {
    if (refreshing_) return;
    refreshing_ = true;
    auto context = artwork_character(canvases_.active_canvas().selected_objects());
    if (context != character_) thumbnails_.clear();
    character_ = context;
    auto frame = selected(frames_), slot = selected(slots_), state = selected(states_);
    auto [appearance_slot_name, appearance_state] = selected_appearance_item(appearance_structure_);
    appearances_->clear(); frames_->clear(); slots_->clear(); states_->clear(); appearance_structure_->clear(); mapping_->clear();
    body_->setEnabled(character_.has_value());
    character_label_->setText("Select a character or one of its members");
    if (character_) {
        character_label_->setText("Character: " + QString::fromStdString(project_.core().character(*character_)->get().name()));
        const auto& art = project_.core().artwork(*character_);
        for (const auto& [name, _] : art.appearances()) appearances_->addItem(QString::fromStdString(name));
        if (appearances_->findText(active_[*character_]) >= 0) appearances_->setCurrentText(active_[*character_]);
        active_[*character_] = appearances_->currentText();

        std::erase_if(thumbnails_, [&](const auto& entry) { return !art.frames().contains(entry.first); });
        for (const auto& [name, f] : art.frames()) {
            auto cached = thumbnails_.find(name);
            if (cached == thumbnails_.end() || cached->second.first.row(0).data() != f.image.row(0).data() ||
                cached->second.first.width() != f.image.width() || cached->second.first.height() != f.image.height()) {
                // Read directly from immutable Core rows. Only the small thumbnail is copied;
                // retaining the resource in the cache keeps its backing identity alive.
                auto stride = f.image.height() > 1 ? f.image.row(1).data() - f.image.row(0).data() : f.image.width() * 4;
                QImage view(f.image.row(0).data(), f.image.width(), f.image.height(), qsizetype(stride), QImage::Format_RGBA8888);
                auto icon = QIcon(QPixmap::fromImage(view.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                thumbnails_.insert_or_assign(name, std::make_pair(f.image, std::move(icon)));
            }
            item(frames_, name, QString::fromStdString(name) + QString(" (%1 × %2)").arg(f.image.width()).arg(f.image.height()));
            frames_->item(frames_->count() - 1)->setIcon(thumbnails_.at(name).second);
        }
        for (const auto& [name, definition] : art.slot_definitions()) {
            auto label = QString::fromStdString(name) + (definition.anchor == sm::bone_anchor::root ? " [root]" : " [tip]");
            if (!project_.core().slot_resolved(*character_, name)) label += " — unresolved";
            item(slots_, name, label);
        }
        restore(frames_, frame); restore(slots_, slot);
        slot = selected(slots_);
        if (!slot.empty()) for (const auto& name : art.slot_definitions().at(slot).states) item(states_, name, QString::fromStdString(name));
        restore(states_, state);

        const sm::appearance* active = nullptr;
        if (auto found = art.appearances().find(active_appearance().toStdString()); found != art.appearances().end()) active = &found->second;
        for (const auto& [name, definition] : art.slot_definitions()) {
            auto* implementation = active ? appearance_slot(*active, name) : nullptr;
            auto* top = new QTreeWidgetItem(appearance_structure_, QStringList{
                QString::fromStdString(name), implementation ? QStringLiteral("Included") : QStringLiteral("Not in this appearance")
            });
            top->setData(0, Qt::UserRole, QString::fromStdString(name));
            top->setData(0, Qt::UserRole + 1, QString{});
            for (const auto& semantic_state : definition.states) {
                auto* child = new QTreeWidgetItem(top, QStringList{
                    QString::fromStdString(semantic_state), mapping_text(implementation, semantic_state)
                });
                child->setData(0, Qt::UserRole, QString::fromStdString(name));
                child->setData(0, Qt::UserRole + 1, QString::fromStdString(semantic_state));
            }
            top->setExpanded(true);
        }
        restore_appearance_item(appearance_structure_, appearance_slot_name, appearance_state);
        appearance_structure_->resizeColumnToContents(0);
    }
    refreshing_ = false; refresh_details();
}
void ui::pane::artwork_browser::refresh_details() {
    refreshing_ = true;
    auto frame = selected(frames_);
    origin_x_->setEnabled(!frame.empty()); origin_y_->setEnabled(!frame.empty());
    mapping_->clear(); mapping_->setEnabled(false);
    add_to_appearance_->setEnabled(false); remove_from_appearance_->setEnabled(false);
    if (character_) {
        const auto& art = project_.core().artwork(*character_);
        if (!frame.empty()) { auto p = art.frames().at(frame).registration_origin; origin_x_->setValue(p.x); origin_y_->setValue(p.y); }
        auto app = art.appearances().find(active_appearance().toStdString());
        auto [slot, state] = selected_appearance_item(appearance_structure_);
        if (app != art.appearances().end() && !slot.empty()) {
            auto* implementation = appearance_slot(app->second, slot);
            add_to_appearance_->setEnabled(!implementation);
            remove_from_appearance_->setEnabled(implementation);
            if (implementation && !state.empty()) {
                mapping_->addItem("Use default (unmapped)"); mapping_->addItem("Hidden (none)");
                for (const auto& [name, _] : art.frames()) mapping_->addItem(QString::fromStdString(name), QString::fromStdString(name));
                auto it = implementation->states.find(state);
                int index = it == implementation->states.end() ? 0 : !it->second ? 1 : mapping_->findData(QString::fromStdString(*it->second));
                mapping_->setCurrentIndex(index); mapping_->setEnabled(true);
                if (state == "default") qobject_cast<QStandardItemModel*>(mapping_->model())->item(0)->setEnabled(false);
            }
        }
    }
    refreshing_ = false;
}
void ui::pane::artwork_browser::import_frames() {
    auto files = QFileDialog::getOpenFileNames(this, "Import sprite images", {}, "Images (*.png *.jpg *.jpeg *.bmp *.tga *.gif *.psd *.pic *.pnm)");
    if (files.empty()) return;
    // All file access belongs to the editor; Core receives encoded memory buffers.
    std::vector<std::pair<std::string, sm::image_buffer>> imports;
    for (const auto& path : files) {
        QFile file(path); if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Could not read image file.");
        auto bytes = file.readAll(); if (file.error() != QFileDevice::NoError) throw std::runtime_error("Could not read complete image file.");
        const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.constData());
        imports.emplace_back(QFileInfo(path).completeBaseName().toStdString(), sm::image_buffer(data, data + bytes.size()));
    }
    edit([&](auto& art) {
        for (const auto& [base, bytes] : imports) {
            auto name = base;
            for (int suffix = 2; art.frames().contains(name); ++suffix) name = base + "_" + std::to_string(suffix);
            art.insert_frame(name, bytes);
        }
    });
}
void ui::pane::artwork_browser::slot_dialog(bool rebind) {
    auto old = selected(slots_); if (rebind && old.empty()) return;
    QDialog dialog(this); dialog.setWindowTitle(rebind ? "Rebind slot" : "New slot"); QFormLayout layout(&dialog);
    QLineEdit name; name.setText(QString::fromStdString(old)); if (!rebind) layout.addRow("Name", &name);
    QComboBox bones, anchor; std::vector<sm::object_id> ids;
    for (auto skeleton : project_.core().character(*character_)->get().rig().skeletons()) for (auto bone : skeleton->bones()) {
        ids.push_back(bone->id()); bones.addItem(QString::fromStdString(skeleton->name() + "/" + bone->name()));
    }
    if (ids.empty()) { QMessageBox::information(this, "Artwork", "Add a bone to this character before creating a slot."); return; }
    anchor.addItems({"Root", "Tip"});
    if (rebind) {
        const auto& definition = project_.core().artwork(*character_).slot_definitions().at(old);
        for (std::size_t i = 0; i < ids.size(); ++i) if (ids[i] == definition.bone) bones.setCurrentIndex(int(i));
        anchor.setCurrentIndex(definition.anchor == sm::bone_anchor::root ? 0 : 1);
    } else for (const auto& object : canvases_.active_canvas().selected_objects()) if (auto bone = std::get_if<sm::const_bone_ref>(&object))
        for (std::size_t i = 0; i < ids.size(); ++i) if (ids[i] == bone->get().id()) bones.setCurrentIndex(int(i));
    layout.addRow("Bone", &bones); layout.addRow("Anchor", &anchor);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    edit([&](auto& art) {
        auto bone = ids.at(bones.currentIndex()); auto endpoint = anchor.currentIndex() == 0 ? sm::bone_anchor::root : sm::bone_anchor::tip;
        if (rebind) art.bind_slot(old, bone, endpoint); else art.add_slot(name.text().trimmed().toStdString(), {bone, endpoint});
    });
}
