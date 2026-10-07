#include "artwork_browser.hpp"
#include "artwork_browser_detail.hpp"
#include "../canvas/canvas_manager.hpp"
#include "../canvas/artwork_layer.hpp"
#include <algorithm>
#include <numbers>
#include <stdexcept>

using namespace ui::pane::artwork_browser_detail;

QString ui::pane::artwork_browser::ask_name(const QString& title, const QString& current) {
	bool ok = false;
	auto name = QInputDialog::getText(this, title, "Name", QLineEdit::Normal, current, &ok);
	return ok ? name.trimmed() : QString{};
}

bool ui::pane::artwork_browser::edit(const std::function<void(sm::artwork&)>& fn) {
	if (!character_) {
		return false;
	}
	try {
		project_.edit_artwork(*character_, fn);
		return true;
	} catch (const std::exception& e) {
		QMessageBox::warning(this, "Artwork", e.what());
		return false;
	}
}

void ui::pane::artwork_browser::update_slot_binding_row(
    const std::string& slot, sm::object_id bone, sm::bone_anchor anchor) {
	for (int i = 0; i < slots_->topLevelItemCount(); ++i) {
		auto* row = slots_->topLevelItem(i);
		if (row->data(0, slot_role).toString().toStdString() != slot) {
			continue;
		}

		const auto bone_id = QString::fromStdString(bone.to_string());
		const auto bone_ids = row->data(1, bone_ids_role).toStringList();
		const auto bone_names = row->data(1, bone_names_role).toStringList();
		const auto bone_index = bone_ids.indexOf(bone_id);
		row->setText(1,
		    bone_index >= 0 && bone_index < bone_names.size() ? bone_names[bone_index]
		                                                      : QStringLiteral("Unresolved"));
		row->setData(1, bone_id_role, bone_id);
		row->setToolTip(1,
		    bone_index < 0 ? QStringLiteral("The bound bone is not present in this character.")
		                   : QString{});
		row->setText(
		    2, anchor == sm::bone_anchor::root ? QStringLiteral("Root") : QStringLiteral("Tip"));
		row->setData(2, anchor_role, int(anchor));
		return;
	}
}

void ui::pane::artwork_browser::reorder_slot(const std::string& slot, int index) {
	if (refreshing_ || !character_) {
		return;
	}
	auto name = active_appearance().toStdString();
	auto app = project_.core().artwork(*character_).appearances().find(name);
	if (app == project_.core().artwork(*character_).appearances().end()) {
		return;
	}
	const auto& painter_slots = app->second.appearance_slots;
	auto found = std::find_if(
	    painter_slots.begin(), painter_slots.end(), [&](const auto& s) { return s.slot == slot; });
	if (found == painter_slots.end()) {
		return;
	}
	int from = int(found - painter_slots.begin());
	index = std::clamp(index, 0, int(painter_slots.size()) - 1);
	if (from == index) {
		return;
	}
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
	if (!row) {
		return;
	}
	if (row->parent()) {
		row = row->parent();
	}
	int index = appearance_structure_->indexOfTopLevelItem(row);
	reorder_slot(slot,
	    direction == 2        ? appearance_structure_->topLevelItemCount()
	        : direction == -2 ? 0
	                          : index + direction);
}

void ui::pane::artwork_browser::import_frames() {
	auto files = QFileDialog::getOpenFileNames(this, "Import sprite images", {},
	    "Images (*.png *.jpg *.jpeg *.bmp *.tga *.gif *.psd *.pic *.pnm)");
	if (files.empty()) {
		return;
	}
	// All file access belongs to the editor; Core receives encoded memory buffers.
	std::vector<std::pair<std::string, sm::image_buffer>> imports;
	for (const auto& path : files) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			throw std::runtime_error("Could not read image file.");
		}
		auto bytes = file.readAll();
		if (file.error() != QFileDevice::NoError) {
			throw std::runtime_error("Could not read complete image file.");
		}
		const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.constData());
		imports.emplace_back(QFileInfo(path).completeBaseName().toStdString(),
		    sm::image_buffer(data, data + bytes.size()));
	}
	edit([&](auto& art) {
		for (const auto& [base, bytes] : imports) {
			auto name = base;
			for (int suffix = 2; art.frames().contains(name); ++suffix) {
				name = base + "_" + std::to_string(suffix);
			}
			art.insert_frame(name, bytes);
		}
	});
}

void ui::pane::artwork_browser::new_slot_dialog() {
	QDialog dialog(this);
	dialog.setWindowTitle("New slot");
	QFormLayout layout(&dialog);
	QLineEdit name;
	layout.addRow("Name", &name);
	QComboBox bones, anchor;
	std::vector<sm::object_id> ids;
	for (auto skeleton : project_.core().character(*character_)->get().rig().skeletons()) {
		for (auto bone : skeleton->bones()) {
			ids.push_back(bone->id());
			bones.addItem(QString::fromStdString(skeleton->name() + "/" + bone->name()));
		}
	}
	if (ids.empty()) {
		QMessageBox::information(
		    this, "Artwork", "Add a bone to this character before creating a slot.");
		return;
	}
	anchor.addItems({ "Root", "Tip" });
	for (const auto& object : canvases_.active_canvas().selected_objects()) {
		if (auto bone = std::get_if<sm::const_bone_ref>(&object)) {
			for (std::size_t i = 0; i < ids.size(); ++i) {
				if (ids[i] == bone->get().id()) {
					bones.setCurrentIndex(int(i));
				}
			}
		}
	}
	layout.addRow("Bone", &bones);
	layout.addRow("Anchor", &anchor);
	QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout.addRow(&buttons);
	connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted) {
		return;
	}
	edit([&](auto& art) {
		auto bone = ids.at(bones.currentIndex());
		auto endpoint = anchor.currentIndex() == 0 ? sm::bone_anchor::root : sm::bone_anchor::tip;
		art.add_slot(name.text().trimmed().toStdString(), { bone, endpoint });
	});
}

void ui::pane::artwork_browser::begin_bone_pick(const std::string& slot) {
	if (refreshing_ || !character_) {
		return;
	}
	const auto& definitions = project_.core().artwork(*character_).slot_definitions();
	auto definition = definitions.find(slot);
	if (definition == definitions.end()) {
		return;
	}

	const auto character = *character_;
	const auto anchor = definition->second.anchor;
	restore(slots_, slot);
	canvases_.active_canvas().begin_bone_pick(character, QString::fromStdString(slot),
	    [this, character, slot, anchor](sm::object_id bone) {
		    if (refreshing_ || character_ != character) {
			    return;
		    }
		    QScopedValueRollback<bool> guard(refreshing_, true);
		    if (edit([&](auto& art) { art.bind_slot(slot, bone, anchor); })) {
			    update_slot_binding_row(slot, bone, anchor);
		    }
	    });
}
