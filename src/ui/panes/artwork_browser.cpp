#include "artwork_browser.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/artwork_layer.hpp"
#include "../../core/sm_bone.hpp"
#include <type_traits>
#include <algorithm>
#include <numbers>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

namespace {
    class frame_list : public QListWidget {
    public:
        std::function<std::optional<sm::object_id>()> character;
        using QListWidget::QListWidget;
    protected:
        QStringList mimeTypes() const override { return {ui::canvas::frame_mime_type}; }
        QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
            auto* mime = new QMimeData;
            auto id = character();
            if (id && !items.empty()) {
                QJsonObject data{{"character", QString::fromStdString(id->to_string())},
                    {"frame", items.front()->data(Qt::UserRole).toString()}};
                mime->setData(ui::canvas::frame_mime_type, QJsonDocument(data).toJson(QJsonDocument::Compact));
            }
            return mime;
        }
    };
    // Drops are translated directly to painter-vector edits. Qt must never move
    // tree items itself, since semantic-state children cannot be reparented.
    class appearance_tree : public QTreeWidget {
    public:
        using QTreeWidget::QTreeWidget;
        std::function<void(const std::string&, int)> reorder;
    protected:
        void mousePressEvent(QMouseEvent* event) override {
            if (event->button() == Qt::LeftButton && !itemAt(event->position().toPoint())) {
                clearSelection();
                setCurrentItem(nullptr);
                event->accept();
                return;
            }
            QTreeWidget::mousePressEvent(event);
        }
        void startDrag(Qt::DropActions) override {
            if (!currentItem() || currentItem()->parent() || !currentItem()->data(0, Qt::UserRole + 2).toBool()) return;
            QDrag drag(this);
            auto* mime = new QMimeData;
            mime->setData("application/x-stickman-appearance-slot", currentItem()->data(0, Qt::UserRole).toString().toUtf8());
            drag.setMimeData(mime);
            drag.exec(Qt::MoveAction);
        }
        void dragEnterEvent(QDragEnterEvent* event) override {
            if (event->source() == this && event->mimeData()->hasFormat("application/x-stickman-appearance-slot")) event->acceptProposedAction();
            else event->ignore();
        }
        void dragMoveEvent(QDragMoveEvent* event) override {
            auto* target = itemAt(event->position().toPoint());
            if (event->source() != this || (target && target->parent())) { event->ignore(); return; }
            event->acceptProposedAction();
        }
        void dropEvent(QDropEvent* event) override {
            auto* source = currentItem();
            auto* target = itemAt(event->position().toPoint());
            if (event->source() != this || !source || source->parent() || (target && target->parent()) ||
                !source->data(0, Qt::UserRole + 2).toBool()) { event->ignore(); return; }
            int index = target ? indexOfTopLevelItem(target) : topLevelItemCount();
            if (target && event->position().y() > visualItemRect(target).center().y()) ++index;
            if (indexOfTopLevelItem(source) < index) --index;
            auto slot = QString::fromUtf8(event->mimeData()->data("application/x-stickman-appearance-slot")).toStdString();
            event->setDropAction(Qt::MoveAction); event->accept();
            reorder(slot, index);
        }
    };
    std::string selected(QListWidget* list) { return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString().toStdString() : ""; }
    std::string selected(QTreeWidget* tree) { return tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString().toStdString() : ""; }
    void restore(QListWidget* list, const std::string& name) {
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString().toStdString() == name) { list->setCurrentRow(i); return; }
        if (list->count()) list->setCurrentRow(0);
    }
    void restore(QTreeWidget* tree, const std::string& name) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString() == name) { tree->setCurrentItem(tree->topLevelItem(i)); return; }
        if (tree->topLevelItemCount()) tree->setCurrentItem(tree->topLevelItem(0));
    }
    QListWidgetItem* item(QListWidget* list, const std::string& name, const QString& label) {
        auto* row = new QListWidgetItem(label, list); row->setData(Qt::UserRole, QString::fromStdString(name)); return row;
    }
    QDoubleSpinBox* coordinate(QWidget* parent) {
        auto* spin = new QDoubleSpinBox(parent); spin->setRange(-1e9, 1e9); spin->setDecimals(4); return spin;
    }
    constexpr int slot_role = Qt::UserRole;
    constexpr int state_role = Qt::UserRole + 1;
    constexpr int included_role = Qt::UserRole + 2;
    constexpr int mapping_kind_role = Qt::UserRole + 3;
    constexpr int mapping_frame_role = Qt::UserRole + 4;
    constexpr int frame_names_role = Qt::UserRole + 5;
    constexpr int bone_id_role = Qt::UserRole + 6;
    constexpr int bone_names_role = Qt::UserRole + 7;
    constexpr int bone_ids_role = Qt::UserRole + 8;
    constexpr int anchor_role = Qt::UserRole + 9;
    constexpr int bone_pick_command_role = Qt::UserRole + 10;
    constexpr int preview_visible_role = Qt::UserRole + 11;
    constexpr int preview_checked_role = Qt::UserRole + 12;
    enum class mapping_choice { inherited, hidden, frame };

    QIcon appearance_membership_icon(bool included) {
        QPixmap pixmap(12, 12);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        if (included) {
            painter.setPen(QPen(QColor(82, 190, 104), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath path;
            path.moveTo(1.5, 6.0);
            path.lineTo(4.5, 9.0);
            path.lineTo(10.5, 2.5);
            painter.drawPath(path);
        } else {
            painter.setPen(QPen(QColor(210, 70, 70), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawEllipse(QRectF(1.5, 1.5, 9.0, 9.0));
            painter.drawLine(QPointF(3.0, 9.0), QPointF(9.0, 3.0));
        }
        return QIcon(pixmap);
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
        if (slot.empty()) { tree->setCurrentItem(nullptr); return; }
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
    std::pair<mapping_choice, QString> mapping_value(const sm::appearance_slot* implementation, const std::string& state) {
        if (!implementation) return {mapping_choice::hidden, {}};
        auto mapping = implementation->states.find(state);
        if (mapping == implementation->states.end()) return {mapping_choice::inherited, {}};
        if (!mapping->second) return {mapping_choice::hidden, {}};
        return {mapping_choice::frame, QString::fromStdString(*mapping->second)};
    }

    class slot_item_delegate : public QStyledItemDelegate {
    public:
        std::function<void(const std::string&, sm::object_id, sm::bone_anchor)> binding_changed;
        std::function<void(const std::string&)> bone_pick_requested;
        using QStyledItemDelegate::QStyledItemDelegate;

        QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            if (index.column() == 0) return QStyledItemDelegate::createEditor(parent, option, index);
            if (index.column() != 1 && index.column() != 2) return nullptr;

            auto* combo = new QComboBox(parent);
            combo->setFrame(false);
            if (index.column() == 1) {
                const auto names = index.data(bone_names_role).toStringList();
                const auto ids = index.data(bone_ids_role).toStringList();
                for (int i = 0; i < std::min(names.size(), ids.size()); ++i) combo->addItem(names[i], ids[i]);
                combo->insertSeparator(combo->count());
                combo->addItem(QStringLiteral("Pick on canvas..."));
                combo->setItemData(combo->count() - 1, true, bone_pick_command_role);
                const auto current = index.data(bone_id_role).toString();
                for (int i = 0; i < combo->count(); ++i)
                    if (combo->itemData(i).toString() == current) { combo->setCurrentIndex(i); break; }
                combo->setProperty("slot", index.siblingAtColumn(0).data(slot_role));
                combo->setProperty("anchor", index.siblingAtColumn(2).data(anchor_role));
            } else {
                combo->addItem(QStringLiteral("Root"), int(sm::bone_anchor::root));
                combo->addItem(QStringLiteral("Tip"), int(sm::bone_anchor::tip));
                const auto current = index.data(anchor_role).toInt();
                for (int i = 0; i < combo->count(); ++i)
                    if (combo->itemData(i).toInt() == current) { combo->setCurrentIndex(i); break; }
                combo->setProperty("slot", index.siblingAtColumn(0).data(slot_role));
                combo->setProperty("bone", index.siblingAtColumn(1).data(bone_id_role));
            }

            auto* self = const_cast<slot_item_delegate*>(this);
            connect(combo, &QComboBox::activated, self, [self, combo, column = index.column()](int choice) {
                auto slot = combo->property("slot").toString().toStdString();
                if (column == 1 && combo->itemData(choice, bone_pick_command_role).toBool()) {
                    emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
                    if (self->bone_pick_requested) self->bone_pick_requested(slot);
                    return;
                }
                auto bone_text = column == 1 ? combo->itemData(choice).toString() : combo->property("bone").toString();
                auto bone = sm::object_id::from_string(bone_text.toStdString());
                if (!bone) return;
                auto anchor = column == 1 ? sm::bone_anchor(combo->property("anchor").toInt())
                                          : sm::bone_anchor(combo->itemData(choice).toInt());
                emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
                if (self->binding_changed) self->binding_changed(slot, *bone, anchor);
            });
            QTimer::singleShot(0, combo, &QComboBox::showPopup);
            return combo;
        }
        void setEditorData(QWidget* editor, const QModelIndex& index) const override {
            if (index.column() == 0) QStyledItemDelegate::setEditorData(editor, index);
        }
        void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
            if (index.column() == 0) QStyledItemDelegate::setModelData(editor, model, index);
        }
        void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
            editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
        }
        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            auto result = QStyledItemDelegate::sizeHint(option, index);
            if (index.column() == 1 || index.column() == 2) result.setHeight(std::max(result.height(), 24));
            return result;
        }
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            if (index.column() == 0) { QStyledItemDelegate::paint(painter, option, index); return; }
            QStyleOptionViewItem background(option);
            initStyleOption(&background, index);
            auto* style = option.widget ? option.widget->style() : QApplication::style();
            style->drawPrimitive(QStyle::PE_PanelItemViewItem, &background, painter, option.widget);

            QStyleOptionComboBox combo;
            combo.rect = option.rect.adjusted(2, 1, -2, -1);
            combo.palette = option.palette;
            combo.currentText = index.data(Qt::DisplayRole).toString();
            combo.state = QStyle::State_Active | QStyle::State_Enabled;
            style->drawComplexControl(QStyle::CC_ComboBox, &combo, painter, option.widget);
            style->drawControl(QStyle::CE_ComboBoxLabel, &combo, painter, option.widget);
        }
    };

    class appearance_item_delegate : public QStyledItemDelegate {
        static QRect preview_radio_rect(const QStyle* style, const QStyleOptionViewItem& option) {
            const int width = style->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, option.widget);
            const int height = style->pixelMetric(QStyle::PM_ExclusiveIndicatorHeight, nullptr, option.widget);
            return {option.rect.left() + 2, option.rect.center().y() - height / 2, width, height};
        }
    public:
        std::function<void(const std::string&, bool)> membership_changed;
        std::function<void(const std::string&, const std::string&, mapping_choice, const std::string&)> mapping_changed;
        std::function<void(const std::string&, const std::string&)> preview_changed;
        using QStyledItemDelegate::QStyledItemDelegate;

        QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override {
            auto metadata = index.siblingAtColumn(0);
            auto state = metadata.data(state_role).toString();
            if (index.column() != 1 || state.isEmpty() || !metadata.data(included_role).toBool()) return nullptr;

            auto* combo = new QComboBox(parent);
            combo->setFrame(false);
            if (state != QStringLiteral("default")) combo->addItem(QStringLiteral("Use default (unmapped)"), int(mapping_choice::inherited));
            combo->addItem(QStringLiteral("Hidden (none)"), int(mapping_choice::hidden));
            for (const auto& frame : index.data(frame_names_role).toStringList()) {
                combo->addItem(frame, int(mapping_choice::frame));
                combo->setItemData(combo->count() - 1, frame, Qt::UserRole + 1);
            }

            auto kind = mapping_choice(index.data(mapping_kind_role).toInt());
            auto frame = index.data(mapping_frame_role).toString();
            for (int i = 0; i < combo->count(); ++i) {
                if (mapping_choice(combo->itemData(i).toInt()) != kind) continue;
                if (kind == mapping_choice::frame && combo->itemData(i, Qt::UserRole + 1).toString() != frame) continue;
                combo->setCurrentIndex(i);
                break;
            }
            combo->setProperty("slot", metadata.data(slot_role));
            combo->setProperty("state", state);

            auto* self = const_cast<appearance_item_delegate*>(this);
            connect(combo, &QComboBox::activated, self, [self, combo](int index) {
                auto slot = combo->property("slot").toString().toStdString();
                auto state = combo->property("state").toString().toStdString();
                auto kind = mapping_choice(combo->itemData(index).toInt());
                auto frame = kind == mapping_choice::frame ? combo->itemData(index, Qt::UserRole + 1).toString().toStdString() : std::string{};
                emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
                if (self->mapping_changed) self->mapping_changed(slot, state, kind, frame);
            });
            QTimer::singleShot(0, combo, &QComboBox::showPopup);
            return combo;
        }
        void setEditorData(QWidget*, const QModelIndex&) const override {}
        void setModelData(QWidget*, QAbstractItemModel*, const QModelIndex&) const override {}
        void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
            editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
        }
        QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            auto result = QStyledItemDelegate::sizeHint(option, index);
            if (index.column() == 1 && !index.siblingAtColumn(0).data(state_role).toString().isEmpty()) result.setHeight(std::max(result.height(), 24));
            return result;
        }
        void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            auto metadata = index.siblingAtColumn(0);
            const auto state = metadata.data(state_role).toString();
            if (index.column() == 0 && !state.isEmpty() && metadata.data(preview_visible_role).toBool()) {
                QStyleOptionViewItem background(option);
                initStyleOption(&background, index);
                const auto text = background.text;
                background.text.clear();
                background.icon = {};
                auto* style = option.widget ? option.widget->style() : QApplication::style();
                style->drawControl(QStyle::CE_ItemViewItem, &background, painter, option.widget);

                QStyleOptionButton radio;
                radio.rect = preview_radio_rect(style, option);
                radio.palette = option.palette;
                radio.state = QStyle::State_Active | QStyle::State_Enabled |
                    (metadata.data(preview_checked_role).toBool() ? QStyle::State_On : QStyle::State_Off);
                if (option.state & QStyle::State_MouseOver) radio.state |= QStyle::State_MouseOver;
                style->drawPrimitive(QStyle::PE_IndicatorRadioButton, &radio, painter, option.widget);

                auto text_rect = option.rect;
                text_rect.setLeft(radio.rect.right() + 5);
                const auto role = option.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text;
                style->drawItemText(painter, text_rect, Qt::AlignVCenter | Qt::AlignLeft, option.palette,
                    option.state & QStyle::State_Enabled, text, role);
                return;
            }
            if (index.column() != 1 || state.isEmpty()) {
                QStyledItemDelegate::paint(painter, option, index);
                return;
            }
            QStyleOptionViewItem background(option);
            initStyleOption(&background, index);
            auto* style = option.widget ? option.widget->style() : QApplication::style();
            style->drawPrimitive(QStyle::PE_PanelItemViewItem, &background, painter, option.widget);

            QStyleOptionComboBox combo;
            combo.rect = option.rect.adjusted(2, 1, -2, -1);
            combo.palette = option.palette;
            combo.currentText = index.data(Qt::DisplayRole).toString();
            combo.state = QStyle::State_Active;
            if (metadata.data(included_role).toBool()) combo.state |= QStyle::State_Enabled;
            style->drawComplexControl(QStyle::CC_ComboBox, &combo, painter, option.widget);
            style->drawControl(QStyle::CE_ComboBoxLabel, &combo, painter, option.widget);
        }
        bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
            const QModelIndex& index) override {
            auto slot = index.data(slot_role).toString();
            if (index.column() == 0 && !slot.isEmpty() && event->type() == QEvent::MouseButtonRelease) {
                auto* mouse = static_cast<QMouseEvent*>(event);
                if (mouse->button() == Qt::LeftButton) {
                    const auto state = index.data(state_role).toString();
                    auto* style = option.widget ? option.widget->style() : QApplication::style();
                    if (!state.isEmpty() && index.data(preview_visible_role).toBool() &&
                        preview_radio_rect(style, option).contains(mouse->position().toPoint())) {
                        if (preview_changed) preview_changed(slot.toStdString(), state.toStdString());
                        return true;
                    }
                    if (state.isEmpty()) {
                        QStyleOptionViewItem item_option(option);
                        initStyleOption(&item_option, index);
                        auto icon_rect = style->subElementRect(QStyle::SE_ItemViewItemDecoration, &item_option, option.widget);
                        if (icon_rect.contains(mouse->position().toPoint())) {
                            if (membership_changed) membership_changed(slot.toStdString(), index.data(included_role).toBool());
                            return true;
                        }
                    }
                }
            }
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        }
    };
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
            auto* button = new QPushButton(label, body_);
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            row->addWidget(button); result.push_back(button);
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
    structure_layout->addWidget(new QLabel("Slots", structure_tab));
    slots_ = new QTreeWidget(structure_tab);
    slots_->setObjectName("artwork_slots");
    slots_->setHeaderLabels({"Slot", "Bone", "Anchor"});
    slots_->setRootIsDecorated(false);
    slots_->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    slots_->header()->setSectionResizeMode(QHeaderView::Interactive);
    slots_->setColumnWidth(2, 70);
    auto* slot_delegate = new slot_item_delegate(slots_);
    slot_delegate->binding_changed = [this](const std::string& slot, sm::object_id bone, sm::bone_anchor endpoint) {
        if (refreshing_ || !character_) return;
        if (edit([&](auto& art) { art.bind_slot(slot, bone, endpoint); })) restore(slots_, slot);
    };
    slot_delegate->bone_pick_requested = [this](const std::string& slot) { begin_bone_pick(slot); };
    slots_->setItemDelegate(slot_delegate);
    structure_layout->addWidget(slots_);
    buttons(structure_layout, {{"New slot…", [this] { new_slot_dialog(); }}, {"Delete", [this] {
        auto name = selected(slots_); if (!name.empty()) edit([&](auto& a) { a.delete_slot(name); });
    }}});
    structure_layout->addWidget(new QLabel("States for selected slot", structure_tab));
    states_ = new QListWidget(structure_tab);
    states_->setObjectName("artwork_states");
    states_->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    structure_layout->addWidget(states_);
    buttons(structure_layout, {{"New state", [this] {
        auto slot = selected(slots_); if (slot.empty()) return;
        auto name = ask_name("New state"); if (!name.isEmpty()) edit([&](auto& a) { a.add_state(slot, name.toStdString()); });
    }}, {"Delete", [this] {
        auto slot = selected(slots_), state = selected(states_); if (!state.empty()) edit([&](auto& a) { a.delete_state(slot, state); });
    }}});

    // One concrete implementation of the shared structure.
    auto* appearance_tab = new QWidget(tabs); auto* appearance_layout = new QVBoxLayout(appearance_tab); tabs->addTab(appearance_tab, "Appearances");
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
    auto* order_panel = new QFrame(appearance_tab);
    order_panel->setFrameShape(QFrame::StyledPanel);
    auto* order_panel_layout = new QVBoxLayout(order_panel);
    order_panel_layout->setContentsMargins(0, 0, 0, 0);
    order_panel_layout->setSpacing(0);

    auto* ordered_tree = new appearance_tree(order_panel);
    appearance_structure_ = ordered_tree;
    ordered_tree->reorder = [this](const std::string& slot, int index) { reorder_slot(slot, index); };
    appearance_structure_->setDragDropMode(QAbstractItemView::InternalMove);
    appearance_structure_->setDefaultDropAction(Qt::MoveAction);
    appearance_structure_->setDropIndicatorShown(false);
    appearance_structure_->setObjectName("artwork_appearance_structure");
    appearance_structure_->setHeaderLabels({"Slot / state", "Image"});
    appearance_structure_->setRootIsDecorated(true);
    appearance_structure_->setIconSize({12, 12});
    appearance_structure_->setFrameShape(QFrame::NoFrame);
    appearance_structure_->setEditTriggers(QAbstractItemView::CurrentChanged | QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    auto* appearance_delegate = new appearance_item_delegate(appearance_structure_);
    appearance_delegate->membership_changed = [this](const std::string& slot, bool included) {
        if (refreshing_ || !character_) return;
        auto name = active_appearance().toStdString();
        if (name.empty()) return;
        edit([&](auto& art) {
            auto app = art.appearances().at(name);
            if (included) std::erase_if(app.appearance_slots, [&](const auto& candidate) { return candidate.slot == slot; });
            else if (!appearance_slot(app, slot)) app.appearance_slots.push_back({slot});
            art.set_appearance(name, std::move(app));
        });
    };
    appearance_delegate->mapping_changed = [this](const std::string& slot, const std::string& state,
        mapping_choice choice, const std::string& frame) {
        if (refreshing_ || !character_) return;
        auto name = active_appearance().toStdString();
        if (name.empty()) return;
        edit([&](auto& art) {
            auto app = art.appearances().at(name);
            for (auto& implementation : app.appearance_slots) if (implementation.slot == slot) {
                switch (choice) {
                case mapping_choice::inherited: implementation.states.erase(state); break;
                case mapping_choice::hidden: implementation.states[state] = sm::frame_target{}; break;
                case mapping_choice::frame: implementation.states[state] = sm::frame_target{frame}; break;
                }
            }
            art.set_appearance(name, std::move(app));
        });
    };
    appearance_delegate->preview_changed = [this](const std::string& slot, const std::string& state) {
        if (refreshing_ || !character_) return;
        canvases_.active_canvas().artwork().set_preview_state(*character_, slot, state);
    };
    appearance_structure_->setItemDelegate(appearance_delegate);
    order_panel_layout->addWidget(appearance_structure_);

    auto* order_toolbar = new QWidget(order_panel);
    order_toolbar->setObjectName("artwork_order_toolbar");
    auto* order_toolbar_layout = new QHBoxLayout(order_toolbar);
    order_toolbar_layout->setContentsMargins(4, 2, 4, 2);
    order_toolbar_layout->setSpacing(2);
    order_toolbar_layout->addStretch();
    auto order_button = [this, order_toolbar, order_toolbar_layout](const QString& text, const QString& tooltip, int direction) {
        auto* button = new QToolButton(order_toolbar);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setAccessibleName(tooltip);
        button->setAutoRaise(true);
        button->setFixedSize(24, 22);
        connect(button, &QToolButton::clicked, this, [this, direction] {
            if (!character_) return;
            try { move_selected_slot(direction); } catch (const std::exception& e) { QMessageBox::warning(this, "Artwork", e.what()); }
        });
        order_toolbar_layout->addWidget(button);
        order_buttons_.push_back(button);
        return button;
    };
    order_button(QStringLiteral("⇈"), QStringLiteral("Send to Back"), -2)->setObjectName("artwork_send_to_back");
    order_button(QStringLiteral("↑"), QStringLiteral("Send Backward"), -1)->setObjectName("artwork_send_backward");
    order_button(QStringLiteral("↓"), QStringLiteral("Bring Forward"), 1)->setObjectName("artwork_bring_forward");
    order_button(QStringLiteral("⇊"), QStringLiteral("Bring to Front"), 2)->setObjectName("artwork_bring_to_front");
    order_panel_layout->addWidget(order_toolbar);
    appearance_layout->addWidget(order_panel);

    transform_toggle_ = new QToolButton(appearance_tab);
    transform_toggle_->setObjectName("artwork_transform_toggle");
    transform_toggle_->setText(QStringLiteral("Transform ▸"));
    transform_toggle_->setCheckable(true);
    transform_toggle_->setAutoRaise(true);
    transform_toggle_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    transform_toggle_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    appearance_layout->addWidget(transform_toggle_, 0, Qt::AlignLeft);

    transform_panel_ = new QFrame(appearance_tab);
    transform_panel_->setObjectName("artwork_transform_panel");
    transform_panel_->setFrameShape(QFrame::StyledPanel);
    auto* transform_layout = new QVBoxLayout(transform_panel_);
    transform_layout->setContentsMargins(8, 6, 8, 8);
    transform_layout->setSpacing(6);
    auto* transform_form = new QFormLayout;
    const QStringList transform_labels{"Translation X", "Translation Y (up)", "Rotation (degrees)", "Scale X", "Scale Y"};
    const QStringList transform_names{"artwork_translation_x", "artwork_translation_y", "artwork_rotation", "artwork_scale_x", "artwork_scale_y"};
    for (int i = 0; i < 5; ++i) {
        transform_[i] = coordinate(transform_panel_);
        transform_[i]->setKeyboardTracking(false);
        transform_[i]->setObjectName(transform_names[i]);
        transform_form->addRow(transform_labels[i], transform_[i]);
        connect(transform_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
            if (refreshing_ || !character_) return;
            auto [slot, state] = selected_appearance_item(appearance_structure_);
            auto name = active_appearance().toStdString();
            const auto& art = project_.core().artwork(*character_);
            auto app = art.appearances().find(name);
            if (app == art.appearances().end()) return;
            auto* current = appearance_slot(app->second, slot);
            if (!current) return;
            const auto& t = current->transform;
            const std::array<double, 5> values{t.translation.x, t.translation.y, t.rotation * 180 / std::numbers::pi, t.scale.x, t.scale.y};
            // Display rounding on another field must never rewrite the transform.
            if (QString::number(value, 'f', transform_[i]->decimals()) == QString::number(values[i], 'f', transform_[i]->decimals())) return;
            edit([&](auto& a) {
                auto changed = a.appearances().at(name);
                for (auto& s : changed.appearance_slots) if (s.slot == slot) {
                    switch (i) {
                    case 0: s.transform.translation.x = value; break;
                    case 1: s.transform.translation.y = value; break;
                    case 2: s.transform.rotation = value * std::numbers::pi / 180; break;
                    case 3: s.transform.scale.x = value; break;
                    case 4: s.transform.scale.y = value; break;
                    }
                }
                a.set_appearance(name, std::move(changed));
            });
        });
    }
    transform_layout->addLayout(transform_form);
    transform_reset_ = new QPushButton(QStringLiteral("Reset transform"), transform_panel_);
    transform_reset_->setObjectName("artwork_reset_transform");
    transform_layout->addWidget(transform_reset_, 0, Qt::AlignLeft);
    connect(transform_reset_, &QPushButton::clicked, this, [this] {
        if (refreshing_ || !character_) return;
        auto [slot, state] = selected_appearance_item(appearance_structure_);
        auto name = active_appearance().toStdString();
        const auto& art = project_.core().artwork(*character_);
        auto app = art.appearances().find(name);
        if (app == art.appearances().end()) return;
        auto* current = appearance_slot(app->second, slot);
        if (!current) return;
        const sm::sprite_transform identity;
        if (current->transform.translation == identity.translation && current->transform.rotation == identity.rotation &&
            current->transform.scale == identity.scale) return;
        edit([&](auto& a) {
            auto changed = a.appearances().at(name);
            for (auto& s : changed.appearance_slots) if (s.slot == slot) s.transform = identity;
            a.set_appearance(name, std::move(changed));
        });
    });
    transform_panel_->setVisible(false);
    appearance_layout->addWidget(transform_panel_);
    connect(transform_toggle_, &QToolButton::toggled, this, [this](bool enabled) {
        transform_panel_->setVisible(enabled);
        transform_toggle_->setText(enabled ? QStringLiteral("Transform ▾") : QStringLiteral("Transform ▸"));
        update_transform_editing();
        refresh_details();
    });

    // Character-local image resources shared by all appearances.
    auto* image_tab = new QWidget(tabs); auto* image_layout = new QVBoxLayout(image_tab); tabs->addTab(image_tab, "Images");
    auto* image_help = new QLabel("Images are character resources shared by all appearances.", image_tab);
    image_help->setWordWrap(true); image_layout->addWidget(image_help);
    auto* draggable_frames = new frame_list(image_tab);
    draggable_frames->character = [this] { return character_; };
    frames_ = draggable_frames; frames_->setDragEnabled(true); frames_->setDragDropMode(QAbstractItemView::DragOnly);
    frames_->setSupportedDragActions(Qt::CopyAction);
    frames_->setObjectName("artwork_frames"); frames_->setIconSize({64, 64}); image_layout->addWidget(frames_);
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

    connect(appearances_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (!refreshing_ && character_) {
            canvases_.active_canvas().artwork().set_active_appearance(*character_, name.toStdString());
            refresh();
        }
    });
    connect(frames_, &QListWidget::currentRowChanged, this, [this] { if (!refreshing_) refresh_details(); });
    connect(slots_, &QTreeWidget::currentItemChanged, this, [this] {
        if (refreshing_) return;
        QScopedValueRollback<bool> guard(refreshing_, true);
        auto state = selected(states_);
        states_->clear();
        if (character_) {
            auto slot = selected(slots_);
            const auto& definitions = project_.core().artwork(*character_).slot_definitions();
            if (auto found = definitions.find(slot); found != definitions.end()) for (const auto& name : found->second.states) {
                auto* row = item(states_, name, QString::fromStdString(name));
                if (name != "default") row->setFlags(row->flags() | Qt::ItemIsEditable);
            }
        }
        restore(states_, state);
    });
    connect(slots_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* row, int column) {
        if (refreshing_ || !character_ || column != 0) return;
        auto old = row->data(0, slot_role).toString().toStdString();
        auto name = row->text(0).trimmed();
        if (old == name.toStdString()) return;
        if (name.isEmpty() || !edit([&](auto& a) { a.rename_slot(old, name.toStdString()); })) { refresh(); return; }
        restore(slots_, name.toStdString());
    });
    connect(states_, &QListWidget::itemChanged, this, [this](QListWidgetItem* row) {
        if (refreshing_ || !character_) return;
        auto slot = selected(slots_);
        auto old = row->data(Qt::UserRole).toString().toStdString();
        auto name = row->text().trimmed();
        if (slot.empty() || old == name.toStdString()) return;
        if (name.isEmpty() || !edit([&](auto& a) { a.rename_state(slot, old, name.toStdString()); })) { refresh(); return; }
        restore(states_, name.toStdString());
    });
    connect(appearance_structure_, &QTreeWidget::currentItemChanged, this, [this] {
        if (!refreshing_) {
            if (character_) {
                auto [slot, state] = selected_appearance_item(appearance_structure_);
                QScopedValueRollback<bool> guard(refreshing_, true);
                canvases_.active_canvas().artwork().set_selected_slot(*character_, slot);
            }
            refresh_details();
        }
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this] { update_transform_editing(); });
    connect(this, &QDockWidget::visibilityChanged, this, [this] { update_transform_editing(); });
    connect(&canvases_, &canvas::manager::active_canvas_changed, this, [this] { refresh(); });
    connect(&canvases_, &canvas::manager::selection_changed, this, [this] { refresh(); });
    connect(&project_, &mdl::project::project_changed, this, [this] { refresh(); });
    connect(&project_, &mdl::project::new_project_opened, this, [this] { refresh(); });
    refresh();
}
QString ui::pane::artwork_browser::ask_name(const QString& title, const QString& current) {
    bool ok = false; auto name = QInputDialog::getText(this, title, "Name", QLineEdit::Normal, current, &ok);
    return ok ? name.trimmed() : QString{};
}
bool ui::pane::artwork_browser::edit(const std::function<void(sm::artwork&)>& fn) {
    if (!character_) return false;
    try { project_.edit_artwork(*character_, fn); return true; }
    catch (const std::exception& e) { QMessageBox::warning(this, "Artwork", e.what()); return false; }
}
void ui::pane::artwork_browser::connect_canvas() {
    auto* layer = &canvases_.active_canvas().artwork();
    if (connected_layer_ == layer) return;
    auto* previous = qobject_cast<canvas::artwork_layer*>(connected_layer_.data());
    disconnect(layer_selection_); disconnect(layer_appearance_); disconnect(layer_preview_); disconnect(layer_transform_);
    if (previous) previous->set_transform_editing(false);
    connected_layer_ = layer;
    layer_selection_ = connect(layer, &canvas::artwork_layer::selection_changed, this, [this] { refresh(); });
    layer_appearance_ = connect(layer, &canvas::artwork_layer::appearance_changed, this, [this] { refresh(); });
    layer_preview_ = connect(layer, &canvas::artwork_layer::preview_changed, this, [this] { refresh(); });
    layer_transform_ = connect(layer, &canvas::artwork_layer::transform_changed, this, [this] { refresh_details(); });
    update_transform_editing();
}
void ui::pane::artwork_browser::update_transform_editing() {
    if (!connected_layer_) return;
    auto* layer = qobject_cast<canvas::artwork_layer*>(connected_layer_.data());
    if (!layer) return;
    layer->set_transform_editing(character_.has_value() && transform_toggle_ && transform_toggle_->isChecked() &&
        transform_panel_ && transform_panel_->isVisibleTo(this) && isVisible());
}
void ui::pane::artwork_browser::reorder_slot(const std::string& slot, int index) {
    if (refreshing_ || !character_) return;
    auto name = active_appearance().toStdString();
    auto app = project_.core().artwork(*character_).appearances().find(name);
    if (app == project_.core().artwork(*character_).appearances().end()) return;
    const auto& painter_slots = app->second.appearance_slots;
    auto found = std::find_if(painter_slots.begin(), painter_slots.end(), [&](const auto& s) { return s.slot == slot; });
    if (found == painter_slots.end()) return;
    int from = int(found - painter_slots.begin());
    index = std::clamp(index, 0, int(painter_slots.size()) - 1);
    if (from == index) return;
    edit([&](auto& art) {
        auto changed = art.appearances().at(name);
        auto moved = std::move(changed.appearance_slots[from]);
        changed.appearance_slots.erase(changed.appearance_slots.begin() + from);
        changed.appearance_slots.insert(changed.appearance_slots.begin() + index, std::move(moved));
        art.set_appearance(name, std::move(changed));
    });
}
void ui::pane::artwork_browser::move_selected_slot(int direction) {
    auto [slot, state] = selected_appearance_item(appearance_structure_);
    auto* row = appearance_structure_->currentItem();
    if (!row) return;
    if (row->parent()) row = row->parent();
    int index = appearance_structure_->indexOfTopLevelItem(row);
    reorder_slot(slot, direction == 2 ? appearance_structure_->topLevelItemCount() : direction == -2 ? 0 : index + direction);
}
void ui::pane::artwork_browser::refresh() {
    if (refreshing_) return;
    refreshing_ = true;
    connect_canvas();
    auto context = artwork_character(canvases_.active_canvas().selected_objects());
    auto& layer = canvases_.active_canvas().artwork();
    if (const auto& selected_sprite = layer.selected_slot();
        selected_sprite && (!context || selected_sprite->character != *context))
        layer.clear_selected_slot();
    if (context != character_) thumbnails_.clear();
    character_ = context;
    auto frame = selected(frames_), slot = selected(slots_), state = selected(states_);
    auto [appearance_slot_name, appearance_state] = selected_appearance_item(appearance_structure_);
    const auto& sprite = layer.selected_slot();
    if (sprite && character_ == sprite->character) {
        if (appearance_slot_name != sprite->slot) appearance_state.clear();
        appearance_slot_name = sprite->slot;
    }
    appearances_->clear(); frames_->clear(); slots_->clear(); states_->clear(); appearance_structure_->clear();
    body_->setEnabled(character_.has_value());
    character_label_->setText("Select a character or one of its members");
    if (character_) {
        character_label_->setText("Character: " + QString::fromStdString(project_.core().character(*character_)->get().name()));
        const auto& art = project_.core().artwork(*character_);
        for (const auto& [name, _] : art.appearances()) appearances_->addItem(QString::fromStdString(name));
        auto active_name = QString::fromStdString(canvases_.active_canvas().artwork().active_appearance(*character_));
        if (appearances_->findText(active_name) >= 0) appearances_->setCurrentText(active_name);
        canvases_.active_canvas().artwork().set_active_appearance(*character_, active_appearance().toStdString());

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
        QStringList bone_names, bone_ids;
        for (auto skeleton : project_.core().character(*character_)->get().rig().skeletons()) for (auto bone : skeleton->bones()) {
            bone_names.push_back(QString::fromStdString(skeleton->name() + " / " + bone->name()));
            bone_ids.push_back(QString::fromStdString(bone->id().to_string()));
        }
        for (const auto& [name, definition] : art.slot_definitions()) {
            auto bone_id = QString::fromStdString(definition.bone.to_string());
            auto bone_index = bone_ids.indexOf(bone_id);
            auto bone_name = bone_index >= 0 ? bone_names[bone_index] : QStringLiteral("Unresolved");
            auto anchor = definition.anchor == sm::bone_anchor::root ? QStringLiteral("Root") : QStringLiteral("Tip");
            auto* row = new QTreeWidgetItem(slots_, QStringList{QString::fromStdString(name), bone_name, anchor});
            row->setData(0, slot_role, QString::fromStdString(name));
            row->setData(1, bone_id_role, bone_id);
            row->setData(1, bone_names_role, bone_names);
            row->setData(1, bone_ids_role, bone_ids);
            row->setData(2, anchor_role, int(definition.anchor));
            row->setFlags(row->flags() | Qt::ItemIsEditable);
            if (bone_index < 0) row->setToolTip(1, QStringLiteral("The bound bone is not present in this character."));
        }
        restore(frames_, frame); restore(slots_, slot);
        slot = selected(slots_);
        if (!slot.empty()) for (const auto& name : art.slot_definitions().at(slot).states) {
            auto* row = item(states_, name, QString::fromStdString(name));
            if (name != "default") row->setFlags(row->flags() | Qt::ItemIsEditable);
        }
        restore(states_, state);

        const sm::appearance* active = nullptr;
        if (auto found = art.appearances().find(active_appearance().toStdString()); found != art.appearances().end()) active = &found->second;
        std::vector<std::string> ordered_names;
        if (active) for (const auto& implementation : active->appearance_slots) ordered_names.push_back(implementation.slot);
        for (const auto& [name, definition] : art.slot_definitions())
            if (!active || !appearance_slot(*active, name)) ordered_names.push_back(name);
        const auto included_icon = appearance_membership_icon(true);
        const auto excluded_icon = appearance_membership_icon(false);
        QStringList frame_names;
        for (const auto& [name, _] : art.frames()) frame_names.push_back(QString::fromStdString(name));
        for (const auto& name : ordered_names) {
            const auto& definition = art.slot_definitions().at(name);
            auto* implementation = active ? appearance_slot(*active, name) : nullptr;
            auto label = QString::fromStdString(name);
            auto preview = canvases_.active_canvas().artwork().preview_state(*character_, name);
            if (!project_.core().slot_resolved(*character_, name)) label += " — unresolved";
            auto* top = new QTreeWidgetItem(appearance_structure_, QStringList{label, QString{}});
            top->setIcon(0, implementation ? included_icon : excluded_icon);
            top->setToolTip(0, implementation ? "Click the check mark to remove this slot from the appearance."
                : "Click the prohibition mark to include this slot in the appearance.");
            top->setData(0, slot_role, QString::fromStdString(name));
            top->setData(0, state_role, QString{});
            top->setData(0, included_role, implementation != nullptr);
            top->setFlags((top->flags() & ~Qt::ItemIsDropEnabled & ~Qt::ItemIsDragEnabled) |
                (implementation ? Qt::ItemIsDragEnabled : Qt::NoItemFlags));
            for (const auto& semantic_state : definition.states) {
                auto [kind, mapped_frame] = mapping_value(implementation, semantic_state);
                auto* child = new QTreeWidgetItem(top, QStringList{
                    QString::fromStdString(semantic_state), mapping_text(implementation, semantic_state)
                });
                child->setData(0, slot_role, QString::fromStdString(name));
                child->setData(0, state_role, QString::fromStdString(semantic_state));
                child->setData(0, included_role, implementation != nullptr);
                child->setData(0, preview_visible_role, definition.states.size() > 1);
                child->setData(0, preview_checked_role, semantic_state == preview);
                child->setData(1, mapping_kind_role, int(kind));
                child->setData(1, mapping_frame_role, mapped_frame);
                child->setData(1, frame_names_role, frame_names);
                child->setFlags((child->flags() | Qt::ItemIsEditable) & ~Qt::ItemIsDragEnabled & ~Qt::ItemIsDropEnabled);
            }
            top->setExpanded(true);
        }
        restore_appearance_item(appearance_structure_, appearance_slot_name, appearance_state);
        appearance_structure_->resizeColumnToContents(0);
    }
    refreshing_ = false;
    update_transform_editing();
    refresh_details();
}
void ui::pane::artwork_browser::refresh_details() {
    refreshing_ = true;
    auto frame = selected(frames_);
    origin_x_->setEnabled(!frame.empty()); origin_y_->setEnabled(!frame.empty());
    for (auto* spin : transform_) spin->setEnabled(false);
    transform_reset_->setEnabled(false);
    for (auto* button : order_buttons_) button->setEnabled(false);
    if (character_) {
        const auto& art = project_.core().artwork(*character_);
        if (!frame.empty()) { auto p = art.frames().at(frame).registration_origin; origin_x_->setValue(p.x); origin_y_->setValue(p.y); }
        auto app = art.appearances().find(active_appearance().toStdString());
        auto [slot, state] = selected_appearance_item(appearance_structure_);
        if (app != art.appearances().end() && !slot.empty()) {
            auto* implementation = appearance_slot(app->second, slot);
            if (implementation) {
                auto t = implementation->transform;
                const auto& layer = canvases_.active_canvas().artwork();
                const auto& selected_sprite = layer.selected_slot();
                if (selected_sprite && selected_sprite->character == *character_ && selected_sprite->appearance == app->first &&
                    selected_sprite->slot == slot)
                    if (auto preview = layer.selected_transform()) t = *preview;
                const std::array<double, 5> values{t.translation.x, t.translation.y, t.rotation * 180 / std::numbers::pi, t.scale.x, t.scale.y};
                for (int i = 0; i < 5; ++i) { transform_[i]->setEnabled(true); transform_[i]->setValue(values[i]); }
                transform_reset_->setEnabled(true);
                const auto& order = app->second.appearance_slots;
                bool front = order.back().slot == slot, back = order.front().slot == slot;
                order_buttons_[0]->setEnabled(!back); order_buttons_[1]->setEnabled(!back);
                order_buttons_[2]->setEnabled(!front); order_buttons_[3]->setEnabled(!front);
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
void ui::pane::artwork_browser::new_slot_dialog() {
    QDialog dialog(this); dialog.setWindowTitle("New slot"); QFormLayout layout(&dialog);
    QLineEdit name; layout.addRow("Name", &name);
    QComboBox bones, anchor; std::vector<sm::object_id> ids;
    for (auto skeleton : project_.core().character(*character_)->get().rig().skeletons()) for (auto bone : skeleton->bones()) {
        ids.push_back(bone->id()); bones.addItem(QString::fromStdString(skeleton->name() + "/" + bone->name()));
    }
    if (ids.empty()) { QMessageBox::information(this, "Artwork", "Add a bone to this character before creating a slot."); return; }
    anchor.addItems({"Root", "Tip"});
    for (const auto& object : canvases_.active_canvas().selected_objects()) if (auto bone = std::get_if<sm::const_bone_ref>(&object))
        for (std::size_t i = 0; i < ids.size(); ++i) if (ids[i] == bone->get().id()) bones.setCurrentIndex(int(i));
    layout.addRow("Bone", &bones); layout.addRow("Anchor", &anchor);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    edit([&](auto& art) {
        auto bone = ids.at(bones.currentIndex()); auto endpoint = anchor.currentIndex() == 0 ? sm::bone_anchor::root : sm::bone_anchor::tip;
        art.add_slot(name.text().trimmed().toStdString(), {bone, endpoint});
    });
}

void ui::pane::artwork_browser::begin_bone_pick(const std::string& slot) {
    if (refreshing_ || !character_) return;
    const auto& definitions = project_.core().artwork(*character_).slot_definitions();
    auto definition = definitions.find(slot);
    if (definition == definitions.end()) return;

    const auto character = *character_;
    const auto anchor = definition->second.anchor;
    restore(slots_, slot);
    canvases_.active_canvas().begin_bone_pick(character, QString::fromStdString(slot),
        [this, character, slot, anchor](sm::object_id bone) {
            if (refreshing_ || character_ != character) return;
            if (edit([&](auto& art) { art.bind_slot(slot, bone, anchor); })) restore(slots_, slot);
        });
}
