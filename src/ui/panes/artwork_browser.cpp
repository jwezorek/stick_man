#include "artwork_browser.hpp"
#include "artwork_browser_detail.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/artwork_layer.hpp"
#include "../../core/sm_bone.hpp"
#include <type_traits>
#include <algorithm>
#include <iterator>
#include <numbers>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>

namespace {
	using namespace ui::pane::artwork_browser_detail;

	class frame_list : public QListWidget {
	public:
		std::function<std::optional<sm::object_id>()> character;
		using QListWidget::QListWidget;

	protected:
		QStringList mimeTypes() const override {
			return { ui::canvas::frame_mime_type };
		}

		QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
			auto* mime = new QMimeData;
			auto id = character();
			if (id && !items.empty()) {
				QJsonObject data{ { "character", QString::fromStdString(id->to_string()) },
					{ "frame", items.front()->data(Qt::UserRole).toString() } };
				mime->setData(ui::canvas::frame_mime_type,
				    QJsonDocument(data).toJson(QJsonDocument::Compact));
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
			if (!currentItem() || currentItem()->parent() ||
			    !currentItem()->data(0, Qt::UserRole + 2).toBool()) {
				return;
			}
			QDrag drag(this);
			auto* mime = new QMimeData;
			mime->setData("application/x-stickman-appearance-slot",
			    currentItem()->data(0, Qt::UserRole).toString().toUtf8());
			drag.setMimeData(mime);
			drag.exec(Qt::MoveAction);
		}

		void dragEnterEvent(QDragEnterEvent* event) override {
			if (event->source() == this &&
			    event->mimeData()->hasFormat("application/x-stickman-appearance-slot")) {
				event->acceptProposedAction();
			} else {
				event->ignore();
			}
		}

		void dragMoveEvent(QDragMoveEvent* event) override {
			auto* target = itemAt(event->position().toPoint());
			if (event->source() != this || (target && target->parent())) {
				event->ignore();
				return;
			}
			event->acceptProposedAction();
		}

		void dropEvent(QDropEvent* event) override {
			auto* source = currentItem();
			auto* target = itemAt(event->position().toPoint());
			if (event->source() != this || !source || source->parent() ||
			    (target && target->parent()) || !source->data(0, Qt::UserRole + 2).toBool()) {
				event->ignore();
				return;
			}
			int index = target ? indexOfTopLevelItem(target) : topLevelItemCount();
			if (target && event->position().y() > visualItemRect(target).center().y()) {
				++index;
			}
			if (indexOfTopLevelItem(source) < index) {
				--index;
			}
			auto slot =
			    QString::fromUtf8(event->mimeData()->data("application/x-stickman-appearance-slot"))
			        .toStdString();
			event->setDropAction(Qt::MoveAction);
			event->accept();
			reorder(slot, index);
		}
	};

	class slot_item_delegate : public QStyledItemDelegate {
	public:
		std::function<void(const std::string&, sm::object_id, sm::bone_anchor)> binding_changed;
		std::function<void(const std::string&)> bone_pick_requested;
		using QStyledItemDelegate::QStyledItemDelegate;

		QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option,
		    const QModelIndex& index) const override {
			if (index.column() == 0) {
				return QStyledItemDelegate::createEditor(parent, option, index);
			}
			if (index.column() != 1 && index.column() != 2) {
				return nullptr;
			}

			auto* combo = new QComboBox(parent);
			combo->setFrame(false);
			if (index.column() == 1) {
				const auto names = index.data(bone_names_role).toStringList();
				const auto ids = index.data(bone_ids_role).toStringList();
				for (int i = 0; i < std::min(names.size(), ids.size()); ++i) {
					combo->addItem(names[i], ids[i]);
				}
				combo->insertSeparator(combo->count());
				combo->addItem(QStringLiteral("Pick on canvas..."));
				combo->setItemData(combo->count() - 1, true, bone_pick_command_role);
				const auto current = index.data(bone_id_role).toString();
				for (int i = 0; i < combo->count(); ++i) {
					if (combo->itemData(i).toString() == current) {
						combo->setCurrentIndex(i);
						break;
					}
				}
				combo->setProperty("slot", index.siblingAtColumn(0).data(slot_role));
				combo->setProperty("anchor", index.siblingAtColumn(2).data(anchor_role));
			} else {
				combo->addItem(QStringLiteral("Root"), int(sm::bone_anchor::root));
				combo->addItem(QStringLiteral("Tip"), int(sm::bone_anchor::tip));
				const auto current = index.data(anchor_role).toInt();
				for (int i = 0; i < combo->count(); ++i) {
					if (combo->itemData(i).toInt() == current) {
						combo->setCurrentIndex(i);
						break;
					}
				}
				combo->setProperty("slot", index.siblingAtColumn(0).data(slot_role));
				combo->setProperty("bone", index.siblingAtColumn(1).data(bone_id_role));
			}

			auto* self = const_cast<slot_item_delegate*>(this);
			connect(combo, &QComboBox::activated, self,
			    [self, combo, column = index.column()](int choice) {
				    auto slot = combo->property("slot").toString().toStdString();
				    if (column == 1 && combo->itemData(choice, bone_pick_command_role).toBool()) {
					    emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
					    if (self->bone_pick_requested) {
						    self->bone_pick_requested(slot);
					    }
					    return;
				    }
				    auto bone_text = column == 1 ? combo->itemData(choice).toString()
				                                 : combo->property("bone").toString();
				    auto bone = sm::object_id::from_string(bone_text.toStdString());
				    if (!bone) {
					    return;
				    }
				    auto anchor = column == 1 ? sm::bone_anchor(combo->property("anchor").toInt())
				                              : sm::bone_anchor(combo->itemData(choice).toInt());
				    emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
				    if (self->binding_changed) {
					    self->binding_changed(slot, *bone, anchor);
				    }
			    });
			QTimer::singleShot(0, combo, &QComboBox::showPopup);
			return combo;
		}

		void setEditorData(QWidget* editor, const QModelIndex& index) const override {
			if (index.column() == 0) {
				QStyledItemDelegate::setEditorData(editor, index);
			}
		}

		void setModelData(
		    QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override {
			if (index.column() == 0) {
				QStyledItemDelegate::setModelData(editor, model, index);
			}
		}

		void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
		    const QModelIndex&) const override {
			editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
		}

		QSize sizeHint(
		    const QStyleOptionViewItem& option, const QModelIndex& index) const override {
			auto result = QStyledItemDelegate::sizeHint(option, index);
			if (index.column() == 1 || index.column() == 2) {
				result.setHeight(std::max(result.height(), 24));
			}
			return result;
		}

		void paint(QPainter* painter, const QStyleOptionViewItem& option,
		    const QModelIndex& index) const override {
			if (index.column() == 0) {
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
			combo.state = QStyle::State_Active | QStyle::State_Enabled;
			style->drawComplexControl(QStyle::CC_ComboBox, &combo, painter, option.widget);
			style->drawControl(QStyle::CE_ComboBoxLabel, &combo, painter, option.widget);
		}
	};

	class appearance_item_delegate : public QStyledItemDelegate {
		static QRect preview_radio_rect(const QStyle* style, const QStyleOptionViewItem& option) {
			const int width =
			    style->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth, nullptr, option.widget);
			const int height =
			    style->pixelMetric(QStyle::PM_ExclusiveIndicatorHeight, nullptr, option.widget);
			return { option.rect.left() + 2, option.rect.center().y() - height / 2, width, height };
		}

	public:
		std::function<void(const std::string&, bool)> membership_changed;
		std::function<void(
		    const std::string&, const std::string&, mapping_choice, const std::string&)>
		    mapping_changed;
		std::function<void(const std::string&, const std::string&)> preview_changed;
		using QStyledItemDelegate::QStyledItemDelegate;

		QWidget* createEditor(
		    QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override {
			auto metadata = index.siblingAtColumn(0);
			auto state = metadata.data(state_role).toString();
			if (index.column() != 1 || state.isEmpty() || !metadata.data(included_role).toBool()) {
				return nullptr;
			}

			auto* combo = new QComboBox(parent);
			combo->setFrame(false);
			if (state != QStringLiteral("default")) {
				combo->addItem(
				    QStringLiteral("Use default (unmapped)"), int(mapping_choice::inherited));
			}
			combo->addItem(QStringLiteral("Hidden (none)"), int(mapping_choice::hidden));
			for (const auto& frame : index.data(frame_names_role).toStringList()) {
				combo->addItem(frame, int(mapping_choice::frame));
				combo->setItemData(combo->count() - 1, frame, Qt::UserRole + 1);
			}

			auto kind = mapping_choice(index.data(mapping_kind_role).toInt());
			auto frame = index.data(mapping_frame_role).toString();
			for (int i = 0; i < combo->count(); ++i) {
				if (mapping_choice(combo->itemData(i).toInt()) != kind) {
					continue;
				}
				if (kind == mapping_choice::frame &&
				    combo->itemData(i, Qt::UserRole + 1).toString() != frame) {
					continue;
				}
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
				auto frame = kind == mapping_choice::frame
				    ? combo->itemData(index, Qt::UserRole + 1).toString().toStdString()
				    : std::string{};
				emit self->closeEditor(combo, QAbstractItemDelegate::NoHint);
				if (self->mapping_changed) {
					self->mapping_changed(slot, state, kind, frame);
				}
			});
			QTimer::singleShot(0, combo, &QComboBox::showPopup);
			return combo;
		}

		void setEditorData(QWidget*, const QModelIndex&) const override {}

		void setModelData(QWidget*, QAbstractItemModel*, const QModelIndex&) const override {}

		void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
		    const QModelIndex&) const override {
			editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
		}

		QSize sizeHint(
		    const QStyleOptionViewItem& option, const QModelIndex& index) const override {
			auto result = QStyledItemDelegate::sizeHint(option, index);
			if (index.column() == 1 &&
			    !index.siblingAtColumn(0).data(state_role).toString().isEmpty()) {
				result.setHeight(std::max(result.height(), 24));
			}
			return result;
		}

		void paint(QPainter* painter, const QStyleOptionViewItem& option,
		    const QModelIndex& index) const override {
			auto metadata = index.siblingAtColumn(0);
			const auto state = metadata.data(state_role).toString();
			if (index.column() == 0 && !state.isEmpty() &&
			    metadata.data(preview_visible_role).toBool()) {
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
				    (metadata.data(preview_checked_role).toBool() ? QStyle::State_On
				                                                  : QStyle::State_Off);
				if (option.state & QStyle::State_MouseOver) {
					radio.state |= QStyle::State_MouseOver;
				}
				style->drawPrimitive(
				    QStyle::PE_IndicatorRadioButton, &radio, painter, option.widget);

				auto text_rect = option.rect;
				text_rect.setLeft(radio.rect.right() + 5);
				const auto role = option.state & QStyle::State_Selected ? QPalette::HighlightedText
				                                                        : QPalette::Text;
				style->drawItemText(painter, text_rect, Qt::AlignVCenter | Qt::AlignLeft,
				    option.palette, option.state & QStyle::State_Enabled, text, role);
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
			if (metadata.data(included_role).toBool()) {
				combo.state |= QStyle::State_Enabled;
			}
			style->drawComplexControl(QStyle::CC_ComboBox, &combo, painter, option.widget);
			style->drawControl(QStyle::CE_ComboBoxLabel, &combo, painter, option.widget);
		}

		bool editorEvent(QEvent* event, QAbstractItemModel* model,
		    const QStyleOptionViewItem& option, const QModelIndex& index) override {
			auto slot = index.data(slot_role).toString();
			if (index.column() == 0 && !slot.isEmpty() &&
			    event->type() == QEvent::MouseButtonRelease) {
				auto* mouse = static_cast<QMouseEvent*>(event);
				if (mouse->button() == Qt::LeftButton) {
					const auto state = index.data(state_role).toString();
					auto* style = option.widget ? option.widget->style() : QApplication::style();
					if (!state.isEmpty() && index.data(preview_visible_role).toBool() &&
					    preview_radio_rect(style, option).contains(mouse->position().toPoint())) {
						if (preview_changed) {
							preview_changed(slot.toStdString(), state.toStdString());
						}
						return true;
					}
					if (state.isEmpty()) {
						QStyleOptionViewItem item_option(option);
						initStyleOption(&item_option, index);
						auto icon_rect = style->subElementRect(
						    QStyle::SE_ItemViewItemDecoration, &item_option, option.widget);
						if (icon_rect.contains(mouse->position().toPoint())) {
							if (membership_changed) {
								membership_changed(
								    slot.toStdString(), index.data(included_role).toBool());
							}
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
		auto id = std::visit(
		    [](auto ref) -> std::optional<sm::object_id> {
			    using T = std::remove_cvref_t<decltype(ref.get())>;
			    if constexpr (std::is_same_v<T, sm::character>) {
				    return ref->id();
			    } else {
				    const sm::skeleton* skeleton;
				    if constexpr (std::is_same_v<T, sm::skeleton>) {
					    skeleton = &ref.get();
				    } else {
					    skeleton = &ref->owner();
				    }
				    auto parent = skeleton->parent_character();
				    return parent ? std::optional(parent->get().id()) : std::nullopt;
			    }
		    },
		    object);
		if (!id || (result && result != id)) {
			return std::nullopt;
		}
		result = id;
	}
	return result;
}

ui::pane::artwork_browser::artwork_browser(
    mdl::project& project, canvas::manager& canvases, QWidget* parent) :
    QDockWidget("Artwork Browser", parent),
    project_(project),
    canvases_(canvases) {
	setObjectName("artwork_browser");
	body_ = new QWidget(this);
	setWidget(body_);
	auto* layout = new QVBoxLayout(body_);
	auto* target_row = new QFormLayout;
	target_ = new QComboBox(body_);
	target_->setObjectName("artwork_target");
	target_row->addRow("Target", target_);
	layout->addLayout(target_row);
	auto buttons = [this](QVBoxLayout* target,
	                   std::initializer_list<std::pair<QString, std::function<void()>>> actions) {
		std::vector<QPushButton*> result;
		auto* row = new QHBoxLayout;
		for (const auto& [label, fn] : actions) {
			auto* button = new QPushButton(label, body_);
			button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
			row->addWidget(button);
			result.push_back(button);
			connect(button, &QPushButton::clicked, this, [this, fn] {
				if (!character_) {
					return;
				}
				try {
					fn();
				} catch (const std::exception& e) {
					QMessageBox::warning(this, "Artwork", e.what());
				}
			});
		}
		target->addLayout(row);
		return result;
	};

	tabs_ = new QTabWidget(body_);
	auto* tabs = tabs_;
	layout->addWidget(tabs);

	// Character-wide artwork structure. Nothing on this page is scoped to an appearance.
	auto* structure_tab = new QWidget(tabs);
	auto* structure_layout = new QVBoxLayout(structure_tab);
	tabs->addTab(structure_tab, "Structure");
	structure_layout->addWidget(new QLabel("Slots", structure_tab));
	slots_ = new QTreeWidget(structure_tab);
	slots_->setObjectName("artwork_slots");
	slots_->setHeaderLabels({ "Slot", "Bone", "Anchor" });
	slots_->setRootIsDecorated(false);
	slots_->setEditTriggers(QAbstractItemView::EditKeyPressed);
	slots_->header()->setSectionResizeMode(QHeaderView::Interactive);
	slots_->setColumnWidth(2, 70);
	auto* slot_delegate = new slot_item_delegate(slots_);
	slot_delegate->binding_changed = [this](const std::string& slot, sm::object_id bone,
	                                     sm::bone_anchor endpoint) {
		if (refreshing_ || !character_) {
			return;
		}
		// Binding edits only affect this Structure row and the rendered artwork.
		// Suppress our generic artwork_changed rebuild and update the row in place.
		QScopedValueRollback<bool> guard(refreshing_, true);
		if (edit([&](auto& art) { art.bind_slot(slot, bone, endpoint); })) {
			update_slot_binding_row(slot, bone, endpoint);
		}
	};
	slot_delegate->bone_pick_requested = [this](const std::string& slot) {
		begin_bone_pick(slot);
	};
	slots_->setItemDelegate(slot_delegate);
	// The Bone and Anchor cells are painted to look like combo boxes, but their
	// real editor is delegate-created. Start that editor on the initial press so
	// the first click opens the popup instead of merely selecting the cell.
	connect(slots_, &QTreeWidget::itemPressed, this, [this](QTreeWidgetItem* item, int column) {
		if (column == 1 || column == 2) {
			slots_->editItem(item, column);
		}
	});
	connect(
	    slots_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int column) {
		    if (column == 0) {
			    slots_->editItem(item, column);
		    }
	    });
	structure_layout->addWidget(slots_);
	buttons(structure_layout,
	    { { "New slot…", [this] { new_slot_dialog(); } }, { "Delete", [this] {
		                                                       auto name = selected(slots_);
		                                                       if (!name.empty()) {
			                                                       edit([&](auto& a) {
				                                                       a.delete_slot(name);
			                                                       });
		                                                       }
	                                                       } } });
	structure_layout->addWidget(new QLabel("States for selected slot", structure_tab));
	states_ = new QListWidget(structure_tab);
	states_->setObjectName("artwork_states");
	states_->setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::DoubleClicked |
	    QAbstractItemView::EditKeyPressed);
	structure_layout->addWidget(states_);
	buttons(structure_layout,
	    { { "New state",
	          [this] {
		          auto slot = selected(slots_);
		          if (slot.empty()) {
			          return;
		          }
		          auto name = ask_name("New state");
		          if (!name.isEmpty()) {
			          edit([&](auto& a) { a.add_state(slot, name.toStdString()); });
		          }
	          } },
	        { "Delete", [this] {
		         auto slot = selected(slots_), state = selected(states_);
		         if (!state.empty()) {
			         edit([&](auto& a) { a.delete_state(slot, state); });
		         }
	         } } });

	// One concrete implementation of the shared structure.
	auto* appearance_tab = new QWidget(tabs);
	auto* appearance_layout = new QVBoxLayout(appearance_tab);
	tabs->addTab(appearance_tab, "Appearances");
	auto* appearance_selector = new QFormLayout;
	appearances_ = new QComboBox(appearance_tab);
	appearances_->setObjectName("artwork_appearance");
	appearance_selector->addRow("Appearance", appearances_);
	appearance_layout->addLayout(appearance_selector);
	buttons(appearance_layout,
	    { { "New appearance",
	          [this] {
		          auto name = ask_name("New appearance");
		          if (name.isEmpty()) {
			          return;
		          }
		          edit([&](auto& a) { a.add_appearance(name.toStdString()); });
		          appearances_->setCurrentText(name);
	          } },
	        { "Rename",
	            [this] {
		            auto old = active_appearance();
		            if (old.isEmpty()) {
			            return;
		            }
		            auto name = ask_name("Rename appearance", old);
		            if (name.isEmpty()) {
			            return;
		            }
		            edit([&](auto& a) {
			            a.rename_appearance(old.toStdString(), name.toStdString());
		            });
		            appearances_->setCurrentText(name);
	            } },
	        { "Delete", [this] {
		         auto name = active_appearance().toStdString();
		         if (!name.empty()) {
			         edit([&](auto& a) { a.delete_appearance(name); });
		         }
	         } } });
	auto* order_panel = new QFrame(appearance_tab);
	order_panel->setFrameShape(QFrame::StyledPanel);
	auto* order_panel_layout = new QVBoxLayout(order_panel);
	order_panel_layout->setContentsMargins(0, 0, 0, 0);
	order_panel_layout->setSpacing(0);

	auto* ordered_tree = new appearance_tree(order_panel);
	appearance_structure_ = ordered_tree;
	ordered_tree->reorder = [this](const std::string& slot, int index) {
		reorder_slot(slot, index);
	};
	appearance_structure_->setDragDropMode(QAbstractItemView::InternalMove);
	appearance_structure_->setDefaultDropAction(Qt::MoveAction);
	appearance_structure_->setDropIndicatorShown(false);
	appearance_structure_->setObjectName("artwork_appearance_structure");
	appearance_structure_->setHeaderLabels({ "Slot / state", "Image" });
	appearance_structure_->setRootIsDecorated(true);
	appearance_structure_->setIconSize({ 12, 12 });
	appearance_structure_->setFrameShape(QFrame::NoFrame);
	appearance_structure_->setEditTriggers(QAbstractItemView::CurrentChanged |
	    QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
	auto* appearance_delegate = new appearance_item_delegate(appearance_structure_);
	appearance_delegate->membership_changed = [this](const std::string& slot, bool included) {
		if (refreshing_ || !character_) {
			return;
		}
		auto name = active_appearance().toStdString();
		if (name.empty()) {
			return;
		}
		edit([&](auto& art) {
			auto app = art.appearances().at(name);
			if (included) {
				std::erase_if(app.appearance_slots,
				    [&](const auto& candidate) { return candidate.slot == slot; });
			} else if (!appearance_slot(app, slot)) {
				app.appearance_slots.push_back({ slot });
			}
			art.set_appearance(name, std::move(app));
		});
	};
	appearance_delegate->mapping_changed = [this](const std::string& slot, const std::string& state,
	                                           mapping_choice choice, const std::string& frame) {
		if (refreshing_ || !character_) {
			return;
		}
		auto name = active_appearance().toStdString();
		if (name.empty()) {
			return;
		}
		edit([&](auto& art) {
			auto app = art.appearances().at(name);
			for (auto& implementation : app.appearance_slots) {
				if (implementation.slot == slot) {
					switch (choice) {
					case mapping_choice::inherited:
						implementation.states.erase(state);
						break;
					case mapping_choice::hidden:
						implementation.states[state] = sm::frame_target{};
						break;
					case mapping_choice::frame:
						implementation.states[state] = sm::frame_target{ frame };
						break;
					}
				}
			}
			art.set_appearance(name, std::move(app));
		});
	};
	appearance_delegate->preview_changed = [this](
	                                           const std::string& slot, const std::string& state) {
		if (refreshing_ || !character_) {
			return;
		}
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
	auto order_button = [this, order_toolbar, order_toolbar_layout](
	                        const QString& text, const QString& tooltip, int direction) {
		auto* button = new QToolButton(order_toolbar);
		button->setText(text);
		button->setToolTip(tooltip);
		button->setAccessibleName(tooltip);
		button->setAutoRaise(true);
		button->setFixedSize(24, 22);
		connect(button, &QToolButton::clicked, this, [this, direction] {
			if (!character_) {
				return;
			}
			try {
				move_selected_slot(direction);
			} catch (const std::exception& e) {
				QMessageBox::warning(this, "Artwork", e.what());
			}
		});
		order_toolbar_layout->addWidget(button);
		order_buttons_.push_back(button);
		return button;
	};
	order_button(QStringLiteral("⇈"), QStringLiteral("Send to Back"), -2)
	    ->setObjectName("artwork_send_to_back");
	order_button(QStringLiteral("↑"), QStringLiteral("Send Backward"), -1)
	    ->setObjectName("artwork_send_backward");
	order_button(QStringLiteral("↓"), QStringLiteral("Bring Forward"), 1)
	    ->setObjectName("artwork_bring_forward");
	order_button(QStringLiteral("⇊"), QStringLiteral("Bring to Front"), 2)
	    ->setObjectName("artwork_bring_to_front");
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
	const QStringList transform_labels{ "Translation X", "Translation Y (up)", "Rotation (degrees)",
		"Scale X", "Scale Y" };
	const QStringList transform_names{ "artwork_translation_x", "artwork_translation_y",
		"artwork_rotation", "artwork_scale_x", "artwork_scale_y" };
	for (int i = 0; i < 5; ++i) {
		transform_[i] = coordinate(transform_panel_);
		transform_[i]->setKeyboardTracking(false);
		transform_[i]->setObjectName(transform_names[i]);
		transform_form->addRow(transform_labels[i], transform_[i]);
		connect(transform_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
			if (refreshing_ || !character_) {
				return;
			}
			auto [slot, state] = selected_appearance_item(appearance_structure_);
			auto name = active_appearance().toStdString();
			const auto& art = project_.core().artwork(*character_);
			auto app = art.appearances().find(name);
			if (app == art.appearances().end()) {
				return;
			}
			auto* current = appearance_slot(app->second, slot);
			if (!current) {
				return;
			}
			const auto& t = current->transform;
			const std::array<double, 5> values{ t.translation.x, t.translation.y,
				t.rotation * 180 / std::numbers::pi, t.scale.x, t.scale.y };
			// Display rounding on another field must never rewrite the transform.
			if (QString::number(value, 'f', transform_[i]->decimals()) ==
			    QString::number(values[i], 'f', transform_[i]->decimals())) {
				return;
			}
			edit([&](auto& a) {
				auto changed = a.appearances().at(name);
				for (auto& s : changed.appearance_slots) {
					if (s.slot == slot) {
						switch (i) {
						case 0:
							s.transform.translation.x = value;
							break;
						case 1:
							s.transform.translation.y = value;
							break;
						case 2:
							s.transform.rotation = value * std::numbers::pi / 180;
							break;
						case 3:
							s.transform.scale.x = value;
							break;
						case 4:
							s.transform.scale.y = value;
							break;
						}
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
		if (refreshing_ || !character_) {
			return;
		}
		auto [slot, state] = selected_appearance_item(appearance_structure_);
		auto name = active_appearance().toStdString();
		const auto& art = project_.core().artwork(*character_);
		auto app = art.appearances().find(name);
		if (app == art.appearances().end()) {
			return;
		}
		auto* current = appearance_slot(app->second, slot);
		if (!current) {
			return;
		}
		const sm::sprite_transform identity;
		if (current->transform.translation == identity.translation &&
		    current->transform.rotation == identity.rotation &&
		    current->transform.scale == identity.scale) {
			return;
		}
		edit([&](auto& a) {
			auto changed = a.appearances().at(name);
			for (auto& s : changed.appearance_slots) {
				if (s.slot == slot) {
					s.transform = identity;
				}
			}
			a.set_appearance(name, std::move(changed));
		});
	});
	transform_panel_->setVisible(false);
	appearance_layout->addWidget(transform_panel_);
	connect(transform_toggle_, &QToolButton::toggled, this, [this](bool enabled) {
		transform_panel_->setVisible(enabled);
		transform_toggle_->setText(
		    enabled ? QStringLiteral("Transform ▾") : QStringLiteral("Transform ▸"));
		update_transform_editing();
		refresh_details();
	});

	// Character-local image resources shared by all appearances.
	auto* image_tab = new QWidget(tabs);
	auto* image_layout = new QVBoxLayout(image_tab);
	tabs->addTab(image_tab, "Images");
	auto* image_help =
	    new QLabel("Images are character resources shared by all appearances.", image_tab);
	image_help->setWordWrap(true);
	image_layout->addWidget(image_help);
	auto* draggable_frames = new frame_list(image_tab);
	draggable_frames->character = [this] {
		return character_;
	};
	frames_ = draggable_frames;
	frames_->setDragEnabled(true);
	frames_->setDragDropMode(QAbstractItemView::DragOnly);
	frames_->setSupportedDragActions(Qt::CopyAction);
	frames_->setObjectName("artwork_frames");
	frames_->setIconSize({ 64, 64 });
	image_layout->addWidget(frames_);
	buttons(image_layout,
	    { { "Import…", [this] { import_frames(); } },
	        { "Rename",
	            [this] {
		            auto old = selected(frames_);
		            if (old.empty()) {
			            return;
		            }
		            auto name = ask_name("Rename image", QString::fromStdString(old));
		            if (name.isEmpty()) {
			            return;
		            }
		            edit([&](auto& a) { a.rename_frame(old, name.toStdString()); });
		            restore(frames_, name.toStdString());
	            } },
	        { "Delete", [this] {
		         auto name = selected(frames_);
		         if (!name.empty()) {
			         edit([&](auto& a) { a.delete_frame(name); });
		         }
	         } } });
	auto* origin = new QFormLayout;
	origin_x_ = coordinate(image_tab);
	origin_y_ = coordinate(image_tab);
	origin_x_->setObjectName("artwork_origin_x");
	origin_y_->setObjectName("artwork_origin_y");
	origin->addRow("Origin X", origin_x_);
	origin->addRow("Origin Y (up)", origin_y_);
	image_layout->addLayout(origin);
	auto save_origin = [this] {
		if (refreshing_ || !character_) {
			return;
		}
		auto name = selected(frames_);
		if (name.empty()) {
			return;
		}
		sm::point value{ origin_x_->value(), origin_y_->value() };
		if (project_.core().artwork(*character_).frames().at(name).registration_origin == value) {
			return;
		}
		edit([&](auto& a) { a.set_registration_origin(name, value); });
	};
	connect(origin_x_, &QDoubleSpinBox::editingFinished, this, save_origin);
	connect(origin_y_, &QDoubleSpinBox::editingFinished, this, save_origin);

	// Project-level reference images. They intentionally live beside, not inside,
	// character artwork and remain available when the project has no characters.
	auto* background_tab = new QWidget(tabs);
	auto* background_layout = new QVBoxLayout(background_tab);
	tabs->addTab(background_tab, "Backgrounds");
	backgrounds_ = new QListWidget(background_tab);
	backgrounds_->setObjectName("artwork_backgrounds");
	backgrounds_->setIconSize({64, 64});
	background_layout->addWidget(backgrounds_);
	auto* background_buttons = new QHBoxLayout;
	auto background_button = [this, background_buttons](const QString& label, auto fn) {
		auto* button = new QPushButton(label, body_);
		button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
		background_buttons->addWidget(button);
		connect(button, &QPushButton::clicked, this, [this, fn] {
			try { fn(); } catch (const std::exception& e) { QMessageBox::warning(this, "Artwork", e.what()); }
		});
		return button;
	};
	background_button("Import…", [this] { import_backgrounds(); })->setObjectName("background_import");
	background_button("Rename", [this] {
		auto id = selected_background_id();
		if (!id) return;
		auto* current = project_.core().background(*id);
		if (!current) return;
		auto name = ask_name("Rename background", QString::fromStdString(current->name));
		if (name.isEmpty()) return;
		project_.edit_backgrounds([&](auto& backgrounds) {
			for (auto& background : backgrounds) if (background.id == *id) background.name = name.toStdString();
		});
	})->setObjectName("background_rename");
	background_button("Delete", [this] {
		auto id = selected_background_id();
		if (!id) return;
		project_.edit_backgrounds([&](auto& backgrounds) {
			std::erase_if(backgrounds, [&](const auto& background) { return background.id == *id; });
		});
	})->setObjectName("background_delete");
	background_layout->addLayout(background_buttons);

	auto* background_order = new QHBoxLayout;
	background_order->addStretch();
	auto add_background_order_button = [this, background_order](const QString& text, const QString& tip, int direction) {
		auto* button = new QToolButton(body_);
		button->setText(text);
		button->setToolTip(tip);
		button->setAccessibleName(tip);
		button->setAutoRaise(true);
		button->setFixedSize(24, 22);
		connect(button, &QToolButton::clicked, this, [this, direction] { move_selected_background(direction); });
		background_order->addWidget(button);
		background_order_buttons_.push_back(button);
		return button;
	};
	add_background_order_button("⇈", "Send to Back", -2)->setObjectName("background_send_to_back");
	add_background_order_button("↑", "Send Backward", -1)->setObjectName("background_send_backward");
	add_background_order_button("↓", "Bring Forward", 1)->setObjectName("background_bring_forward");
	add_background_order_button("⇊", "Bring to Front", 2)->setObjectName("background_bring_to_front");
	background_layout->addLayout(background_order);

	background_transform_toggle_ = new QToolButton(background_tab);
	background_transform_toggle_->setObjectName("background_transform_toggle");
	background_transform_toggle_->setText("Transform ▸");
	background_transform_toggle_->setCheckable(true);
	background_transform_toggle_->setAutoRaise(true);
	background_transform_toggle_->setToolButtonStyle(Qt::ToolButtonTextOnly);
	background_layout->addWidget(background_transform_toggle_, 0, Qt::AlignLeft);
	background_transform_panel_ = new QFrame(background_tab);
	background_transform_panel_->setObjectName("background_transform_panel");
	background_transform_panel_->setFrameShape(QFrame::StyledPanel);
	auto* background_transform_layout = new QVBoxLayout(background_transform_panel_);
	auto* background_transform_form = new QFormLayout;
	const QStringList background_transform_labels{ "Translation X", "Translation Y (up)", "Rotation (degrees)", "Scale X", "Scale Y" };
	const QStringList background_transform_names{ "background_translation_x", "background_translation_y", "background_rotation", "background_scale_x", "background_scale_y" };
	for (int i = 0; i < 5; ++i) {
		background_transform_[i] = coordinate(background_transform_panel_);
		background_transform_[i]->setKeyboardTracking(false);
		background_transform_[i]->setObjectName(background_transform_names[i]);
		background_transform_form->addRow(background_transform_labels[i], background_transform_[i]);
		connect(background_transform_[i], &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
			if (refreshing_ || !backgrounds_target_) return;
			auto id = selected_background_id();
			if (!id) return;
			auto* current = project_.core().background(*id);
			if (!current) return;
			const auto& t = current->transform;
			const std::array<double, 5> values{t.translation.x, t.translation.y, t.rotation * 180 / std::numbers::pi, t.scale.x, t.scale.y};
			if (QString::number(value, 'f', background_transform_[i]->decimals()) ==
			    QString::number(values[i], 'f', background_transform_[i]->decimals())) return;
			project_.edit_backgrounds([&](auto& backgrounds) {
				for (auto& background : backgrounds) if (background.id == *id) {
					switch (i) {
					case 0: background.transform.translation.x = value; break;
					case 1: background.transform.translation.y = value; break;
					case 2: background.transform.rotation = value * std::numbers::pi / 180; break;
					case 3: background.transform.scale.x = value; break;
					case 4: background.transform.scale.y = value; break;
					}
				}
			});
		});
	}
	background_transform_layout->addLayout(background_transform_form);
	background_transform_reset_ = new QPushButton("Reset transform", background_transform_panel_);
	background_transform_reset_->setObjectName("background_reset_transform");
	background_transform_layout->addWidget(background_transform_reset_, 0, Qt::AlignLeft);
	connect(background_transform_reset_, &QPushButton::clicked, this, [this] {
		if (refreshing_) return;
		auto id = selected_background_id();
		if (!id) return;
		project_.edit_backgrounds([&](auto& backgrounds) {
			for (auto& background : backgrounds) if (background.id == *id) background.transform = {};
		});
	});
	background_transform_panel_->setVisible(false);
	background_layout->addWidget(background_transform_panel_);
	connect(background_transform_toggle_, &QToolButton::toggled, this, [this](bool enabled) {
		background_transform_panel_->setVisible(enabled);
		background_transform_toggle_->setText(enabled ? "Transform ▾" : "Transform ▸");
		update_transform_editing();
		refresh_details();
	});
	connect(backgrounds_, &QListWidget::currentRowChanged, this, [this] {
		if (refreshing_) return;
		auto id = selected_background_id();
		auto& layer = canvases_.active_canvas().artwork();
		if (id) layer.set_selected_background(*id); else layer.clear_selected_background();
		refresh_details();
	});

	connect(target_, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (refreshing_ || index < 0) return;
		auto value = target_->itemData(index).toString();
		if (value.isEmpty()) {
			character_.reset();
			backgrounds_target_ = true;
		} else if (auto id = sm::object_id::from_string(value.toStdString())) {
			character_ = *id;
			backgrounds_target_ = false;
		}
		canvases_.active_canvas().artwork().clear_selected_slot();
		refresh();
	});

	connect(appearances_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
		if (!refreshing_ && character_) {
			canvases_.active_canvas().artwork().set_active_appearance(
			    *character_, name.toStdString());
			refresh();
		}
	});
	connect(frames_, &QListWidget::currentRowChanged, this, [this] {
		if (!refreshing_) {
			refresh_details();
		}
	});
	connect(slots_, &QTreeWidget::currentItemChanged, this, [this] {
		if (refreshing_) {
			return;
		}
		QScopedValueRollback<bool> guard(refreshing_, true);
		auto state = selected(states_);
		states_->clear();
		if (character_) {
			auto slot = selected(slots_);
			const auto& definitions = project_.core().artwork(*character_).slot_definitions();
			if (auto found = definitions.find(slot); found != definitions.end()) {
				for (const auto& name : found->second.states) {
					auto* row = item(states_, name, QString::fromStdString(name));
					if (name != "default") {
						row->setFlags(row->flags() | Qt::ItemIsEditable);
					}
				}
			}
		}
		restore(states_, state);
	});
	connect(slots_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* row, int column) {
		if (refreshing_ || !character_ || column != 0) {
			return;
		}
		auto old = row->data(0, slot_role).toString().toStdString();
		auto name = row->text(0).trimmed();
		if (old == name.toStdString()) {
			return;
		}
		if (name.isEmpty() || !edit([&](auto& a) { a.rename_slot(old, name.toStdString()); })) {
			refresh();
			return;
		}
		restore(slots_, name.toStdString());
	});
	connect(states_, &QListWidget::itemChanged, this, [this](QListWidgetItem* row) {
		if (refreshing_ || !character_) {
			return;
		}
		auto slot = selected(slots_);
		auto old = row->data(Qt::UserRole).toString().toStdString();
		auto name = row->text().trimmed();
		if (slot.empty() || old == name.toStdString()) {
			return;
		}
		if (name.isEmpty() ||
		    !edit([&](auto& a) { a.rename_state(slot, old, name.toStdString()); })) {
			refresh();
			return;
		}
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
	connect(&canvases_, &canvas::manager::selection_changed, this, [this] {
		sync_target_to_canvas();
		refresh();
	});
	connect(&project_, &mdl::project::project_changed, this, [this] { refresh(); });
	connect(&project_, &mdl::project::artwork_changed, this,
	    [this](mdl::project&, sm::object_id) { refresh(); });
	connect(&project_, &mdl::project::backgrounds_changed, this, [this] { refresh(); });
	connect(&project_, &mdl::project::new_project_opened, this, [this] {
		sync_target_to_canvas();
		refresh();
	});
	sync_target_to_canvas();
	refresh();
}
