#include "artwork_browser.hpp"
#include "artwork_browser_detail.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/artwork_layer.hpp"
#include <algorithm>
#include <iterator>
#include <numbers>
#include <ranges>
#include <unordered_set>

using namespace ui::pane::artwork_browser_detail;

void ui::pane::artwork_browser::connect_canvas() {
	auto* layer = &canvases_.active_canvas().artwork();
	if (connected_layer_ == layer) {
		return;
	}
	auto* previous = qobject_cast<canvas::artwork_layer*>(connected_layer_.data());
	disconnect(layer_selection_);
	disconnect(layer_appearance_);
	disconnect(layer_preview_);
	disconnect(layer_transform_);
	if (previous) {
		previous->set_transform_editing(false);
	}
	connected_layer_ = layer;
	layer_selection_ =
	    connect(layer, &canvas::artwork_layer::selection_changed, this, [this] { refresh(); });
	layer_appearance_ =
	    connect(layer, &canvas::artwork_layer::appearance_changed, this, [this] { refresh(); });
	layer_preview_ =
	    connect(layer, &canvas::artwork_layer::preview_changed, this, [this] { refresh(); });
	layer_transform_ = connect(
	    layer, &canvas::artwork_layer::transform_changed, this, [this] { refresh_details(); });
	update_transform_editing();
}

void ui::pane::artwork_browser::update_transform_editing() {
	if (!connected_layer_) {
		return;
	}
	auto* layer = qobject_cast<canvas::artwork_layer*>(connected_layer_.data());
	if (!layer) {
		return;
	}
	if (backgrounds_target_) {
		layer->set_background_transform_editing(background_transform_toggle_ &&
		    background_transform_toggle_->isChecked() && background_transform_panel_ &&
		    background_transform_panel_->isVisibleTo(this) && isVisible());
	} else {
		layer->set_transform_editing(character_.has_value() && transform_toggle_ &&
		    transform_toggle_->isChecked() && transform_panel_ &&
		    transform_panel_->isVisibleTo(this) && isVisible());
	}
}

void ui::pane::artwork_browser::refresh() {
	if (refreshing_) {
		return;
	}
	refreshing_ = true;
	connect_canvas();
	auto& layer = canvases_.active_canvas().artwork();
	if (character_ && !project_.core().character(*character_)) {
		character_.reset();
		backgrounds_target_ = true;
	}
	if (backgrounds_target_) character_.reset();

	auto frame = selected(frames_), slot = selected(slots_), state = selected(states_);
	auto selected_background = selected_background_id();
	if (layer.selected_background()) selected_background = layer.selected_background();
	auto [appearance_slot_name, appearance_state] = selected_appearance_item(appearance_structure_);
	const auto& sprite = layer.selected_slot();
	if (sprite && character_ == sprite->character) {
		if (appearance_slot_name != sprite->slot) appearance_state.clear();
		appearance_slot_name = sprite->slot;
	}

	// Rebuild the target selector from project state while preserving the active target.
	target_->clear();
	target_->addItem("Backgrounds", QString{});
	std::vector<std::pair<QString, sm::object_id>> targets;
	for (auto candidate : project_.core().characters())
		targets.emplace_back(QString::fromStdString(candidate->name()), candidate->id());
	std::ranges::sort(targets, [](const auto& a, const auto& b) {
		if (a.first != b.first) return a.first.localeAwareCompare(b.first) < 0;
		return a.second < b.second;
	});
	for (const auto& [name, id] : targets)
		target_->addItem(name, QString::fromStdString(id.to_string()));
	if (character_) {
		auto id = QString::fromStdString(character_->to_string());
		for (int i = 0; i < target_->count(); ++i)
			if (target_->itemData(i).toString() == id) target_->setCurrentIndex(i);
	} else {
		target_->setCurrentIndex(0);
	}

	appearances_->clear();
	frames_->clear();
	slots_->clear();
	states_->clear();
	appearance_structure_->clear();
	backgrounds_->clear();
	for (int i = 0; i < tabs_->count(); ++i)
		tabs_->setTabVisible(i, backgrounds_target_ ? i == 3 : i != 3);
	if (backgrounds_target_) {
		if (tabs_->currentIndex() != 3) tabs_->setCurrentIndex(3);
		layer.clear_selected_slot();
	} else {
		if (tabs_->currentIndex() == 3) tabs_->setCurrentIndex(0);
		layer.clear_selected_background();
		if (const auto& selected_sprite = layer.selected_slot();
		    selected_sprite && (!character_ || selected_sprite->character != *character_))
			layer.clear_selected_slot();
	}

	if (character_) {
		const auto& art = project_.core().artwork(*character_);
		for (const auto& [name, _] : art.appearances()) {
			appearances_->addItem(QString::fromStdString(name));
		}
		auto active_name = QString::fromStdString(
		    canvases_.active_canvas().artwork().active_appearance(*character_));
		if (appearances_->findText(active_name) >= 0) {
			appearances_->setCurrentText(active_name);
		}
		canvases_.active_canvas().artwork().set_active_appearance(
		    *character_, active_appearance().toStdString());

		std::erase_if(
		    thumbnails_, [&](const auto& entry) { return !art.frames().contains(entry.first); });
		for (const auto& [name, f] : art.frames()) {
			auto cached = thumbnails_.find(name);
			if (cached == thumbnails_.end() ||
			    cached->second.first.row(0).data() != f.image.row(0).data() ||
			    cached->second.first.width() != f.image.width() ||
			    cached->second.first.height() != f.image.height()) {
				// Read directly from immutable Core rows. Only the small thumbnail is copied;
				// retaining the resource in the cache keeps its backing identity alive.
				auto stride = f.image.height() > 1 ? f.image.row(1).data() - f.image.row(0).data()
				                                   : f.image.width() * 4;
				QImage view(f.image.row(0).data(), f.image.width(), f.image.height(),
				    qsizetype(stride), QImage::Format_RGBA8888);
				auto icon = QIcon(QPixmap::fromImage(
				    view.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
				thumbnails_.insert_or_assign(name, std::make_pair(f.image, std::move(icon)));
			}
			item(frames_, name,
			    QString::fromStdString(name) +
			        QString(" (%1 × %2)").arg(f.image.width()).arg(f.image.height()));
			frames_->item(frames_->count() - 1)->setIcon(thumbnails_.at(name).second);
		}
		QStringList bone_names, bone_ids;
		const auto& rig = project_.core().character(*character_)->get().rig();
		const bool qualify_bone_names = rig.size() > 1;
		for (auto skeleton : rig.skeletons()) {
			for (auto bone : skeleton->bones()) {
				auto bone_name = bone->name();
				if (qualify_bone_names) {
					bone_name = skeleton->name() + " / " + bone_name;
				}
				bone_names.push_back(QString::fromStdString(bone_name));
				bone_ids.push_back(QString::fromStdString(bone->id().to_string()));
			}
		}
		for (const auto& [name, definition] : art.slot_definitions()) {
			auto bone_id = QString::fromStdString(definition.bone.to_string());
			auto bone_index = bone_ids.indexOf(bone_id);
			auto bone_name =
			    bone_index >= 0 ? bone_names[bone_index] : QStringLiteral("Unresolved");
			auto anchor = definition.anchor == sm::bone_anchor::root ? QStringLiteral("Root")
			                                                         : QStringLiteral("Tip");
			auto* row = new QTreeWidgetItem(
			    slots_, QStringList{ QString::fromStdString(name), bone_name, anchor });
			row->setData(0, slot_role, QString::fromStdString(name));
			row->setData(1, bone_id_role, bone_id);
			row->setData(1, bone_names_role, bone_names);
			row->setData(1, bone_ids_role, bone_ids);
			row->setData(2, anchor_role, int(definition.anchor));
			row->setFlags(row->flags() | Qt::ItemIsEditable);
			if (bone_index < 0) {
				row->setToolTip(
				    1, QStringLiteral("The bound bone is not present in this character."));
			}
		}
		restore(frames_, frame);
		restore(slots_, slot);
		slot = selected(slots_);
		if (!slot.empty()) {
			for (const auto& name : art.slot_definitions().at(slot).states) {
				auto* row = item(states_, name, QString::fromStdString(name));
				if (name != "default") {
					row->setFlags(row->flags() | Qt::ItemIsEditable);
				}
			}
		}
		restore(states_, state);

		const sm::appearance* active = nullptr;
		if (auto found = art.appearances().find(active_appearance().toStdString());
		    found != art.appearances().end()) {
			active = &found->second;
		}
		std::vector<std::string> ordered_names;
		if (active) {
			for (const auto& implementation : active->appearance_slots) {
				ordered_names.push_back(implementation.slot);
			}
		}
		for (const auto& [name, definition] : art.slot_definitions()) {
			if (!active || !appearance_slot(*active, name)) {
				ordered_names.push_back(name);
			}
		}
		const auto included_icon = appearance_membership_icon(true);
		const auto excluded_icon = appearance_membership_icon(false);
		QStringList frame_names;
		for (const auto& [name, _] : art.frames()) {
			frame_names.push_back(QString::fromStdString(name));
		}
		for (const auto& name : ordered_names) {
			const auto& definition = art.slot_definitions().at(name);
			auto* implementation = active ? appearance_slot(*active, name) : nullptr;
			auto label = QString::fromStdString(name);
			auto preview = canvases_.active_canvas().artwork().preview_state(*character_, name);
			if (!project_.core().slot_resolved(*character_, name)) {
				label += " — unresolved";
			}
			auto* top = new QTreeWidgetItem(appearance_structure_, QStringList{ label, QString{} });
			top->setIcon(0, implementation ? included_icon : excluded_icon);
			top->setToolTip(0,
			    implementation
			        ? "Click the check mark to remove this slot from the appearance."
			        : "Click the prohibition mark to include this slot in the appearance.");
			top->setData(0, slot_role, QString::fromStdString(name));
			top->setData(0, state_role, QString{});
			top->setData(0, included_role, implementation != nullptr);
			top->setFlags((top->flags() & ~Qt::ItemIsDropEnabled & ~Qt::ItemIsDragEnabled) |
			    (implementation ? Qt::ItemIsDragEnabled : Qt::NoItemFlags));
			for (const auto& semantic_state : definition.states) {
				auto [kind, mapped_frame] = mapping_value(implementation, semantic_state);
				auto* child = new QTreeWidgetItem(top,
				    QStringList{ QString::fromStdString(semantic_state),
				        mapping_text(implementation, semantic_state) });
				child->setData(0, slot_role, QString::fromStdString(name));
				child->setData(0, state_role, QString::fromStdString(semantic_state));
				child->setData(0, included_role, implementation != nullptr);
				child->setData(0, preview_visible_role, definition.states.size() > 1);
				child->setData(0, preview_checked_role, semantic_state == preview);
				child->setData(1, mapping_kind_role, int(kind));
				child->setData(1, mapping_frame_role, mapped_frame);
				child->setData(1, frame_names_role, frame_names);
				child->setFlags((child->flags() | Qt::ItemIsEditable) & ~Qt::ItemIsDragEnabled &
				    ~Qt::ItemIsDropEnabled);
			}
			top->setExpanded(true);
		}
		restore_appearance_item(appearance_structure_, appearance_slot_name, appearance_state);
		appearance_structure_->resizeColumnToContents(0);
	} else if (backgrounds_target_) {
		std::unordered_set<std::string> live_ids;
		for (const auto& background : project_.core().backgrounds()) {
			const auto id = background.id.to_string();
			live_ids.insert(id);
			auto cached = background_thumbnails_.find(id);
			if (cached == background_thumbnails_.end() ||
			    cached->second.first.row(0).data() != background.image.row(0).data() ||
			    cached->second.first.width() != background.image.width() ||
			    cached->second.first.height() != background.image.height()) {
				auto stride = background.image.height() > 1
				    ? background.image.row(1).data() - background.image.row(0).data()
				    : background.image.width() * 4;
				QImage view(background.image.row(0).data(), background.image.width(),
				    background.image.height(), qsizetype(stride), QImage::Format_RGBA8888);
				auto icon = QIcon(QPixmap::fromImage(
				    view.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
				background_thumbnails_.insert_or_assign(
				    id, std::make_pair(background.image, std::move(icon)));
			}
			auto* row = item(backgrounds_, id,
			    QString::fromStdString(background.name) +
			        QString(" (%1 × %2)").arg(background.image.width()).arg(background.image.height()));
			row->setIcon(background_thumbnails_.at(id).second);
		}
		std::erase_if(background_thumbnails_,
		    [&](const auto& entry) { return !live_ids.contains(entry.first); });

		if (selected_background && project_.core().background(*selected_background)) {
			restore(backgrounds_, selected_background->to_string());
		} else {
			restore(backgrounds_, {});
		}
		if (auto id = selected_background_id()) {
			if (layer.selected_background() != id) {
				layer.set_selected_background(*id);
			}
		} else if (layer.selected_background()) {
			layer.clear_selected_background();
		}
	}
	refreshing_ = false;
	update_transform_editing();
	refresh_details();
}

void ui::pane::artwork_browser::refresh_details() {
	refreshing_ = true;
	auto frame = selected(frames_);
	origin_x_->setEnabled(character_.has_value() && !frame.empty());
	origin_y_->setEnabled(character_.has_value() && !frame.empty());
	for (auto* spin : transform_) {
		spin->setEnabled(false);
	}
	transform_reset_->setEnabled(false);
	for (auto* button : order_buttons_) {
		button->setEnabled(false);
	}
	for (auto* spin : background_transform_) {
		spin->setEnabled(false);
	}
	background_transform_reset_->setEnabled(false);
	for (auto* button : background_order_buttons_) {
		button->setEnabled(false);
	}
	if (backgrounds_target_) {
		auto id = selected_background_id();
		if (id) {
			if (const auto* background = project_.core().background(*id)) {
				auto transform = background->transform;
				const auto& layer = canvases_.active_canvas().artwork();
				if (layer.selected_background() == id) {
					if (auto preview = layer.selected_background_transform()) {
						transform = *preview;
					}
				}
				const std::array<double, 5> values{ transform.translation.x,
					transform.translation.y, transform.rotation * 180 / std::numbers::pi,
					transform.scale.x, transform.scale.y };
				for (int i = 0; i < 5; ++i) {
					background_transform_[i]->setEnabled(true);
					background_transform_[i]->setValue(values[i]);
				}
				background_transform_reset_->setEnabled(true);
				const auto& backgrounds = project_.core().backgrounds();
				auto found = std::ranges::find(backgrounds, *id, &sm::background_image::id);
				if (found != backgrounds.end()) {
					const bool back = found == backgrounds.begin();
					const bool front = std::next(found) == backgrounds.end();
					background_order_buttons_[0]->setEnabled(!back);
					background_order_buttons_[1]->setEnabled(!back);
					background_order_buttons_[2]->setEnabled(!front);
					background_order_buttons_[3]->setEnabled(!front);
				}
			}
		}
		refreshing_ = false;
		return;
	}
	if (character_) {
		const auto& art = project_.core().artwork(*character_);
		if (!frame.empty()) {
			auto p = art.frames().at(frame).registration_origin;
			origin_x_->setValue(p.x);
			origin_y_->setValue(p.y);
		}
		auto app = art.appearances().find(active_appearance().toStdString());
		auto [slot, state] = selected_appearance_item(appearance_structure_);
		if (app != art.appearances().end() && !slot.empty()) {
			auto* implementation = appearance_slot(app->second, slot);
			if (implementation) {
				auto t = implementation->transform;
				const auto& layer = canvases_.active_canvas().artwork();
				const auto& selected_sprite = layer.selected_slot();
				if (selected_sprite && selected_sprite->character == *character_ &&
				    selected_sprite->appearance == app->first && selected_sprite->slot == slot) {
					if (auto preview = layer.selected_transform()) {
						t = *preview;
					}
				}
				const std::array<double, 5> values{ t.translation.x, t.translation.y,
					t.rotation * 180 / std::numbers::pi, t.scale.x, t.scale.y };
				for (int i = 0; i < 5; ++i) {
					transform_[i]->setEnabled(true);
					transform_[i]->setValue(values[i]);
				}
				transform_reset_->setEnabled(true);
				const auto& order = app->second.appearance_slots;
				bool front = order.back().slot == slot, back = order.front().slot == slot;
				order_buttons_[0]->setEnabled(!back);
				order_buttons_[1]->setEnabled(!back);
				order_buttons_[2]->setEnabled(!front);
				order_buttons_[3]->setEnabled(!front);
			}
		}
	}
	refreshing_ = false;
}

