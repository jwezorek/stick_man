#pragma once

#include "artwork_browser.hpp"
#include <QPainter>
#include <QPainterPath>
#include <string>
#include <utility>

namespace ui::pane::artwork_browser_detail {

inline std::string selected(QListWidget* list) {
	return list->currentItem()
		? list->currentItem()->data(Qt::UserRole).toString().toStdString()
		: "";
}

inline std::string selected(QTreeWidget* tree) {
	return tree->currentItem()
		? tree->currentItem()->data(0, Qt::UserRole).toString().toStdString()
		: "";
}

inline void restore(QListWidget* list, const std::string& name) {
	for (int i = 0; i < list->count(); ++i) {
		if (list->item(i)->data(Qt::UserRole).toString().toStdString() == name) {
			list->setCurrentRow(i);
			return;
		}
	}
	if (list->count())
		list->setCurrentRow(0);
}

inline void restore(QTreeWidget* tree, const std::string& name) {
	for (int i = 0; i < tree->topLevelItemCount(); ++i) {
		if (tree->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString() == name) {
			tree->setCurrentItem(tree->topLevelItem(i));
			return;
		}
	}
	if (tree->topLevelItemCount())
		tree->setCurrentItem(tree->topLevelItem(0));
}

inline QListWidgetItem* item(QListWidget* list, const std::string& name, const QString& label) {
	auto* row = new QListWidgetItem(label, list);
	row->setData(Qt::UserRole, QString::fromStdString(name));
	return row;
}

inline QDoubleSpinBox* coordinate(QWidget* parent) {
	auto* spin = new QDoubleSpinBox(parent);
	spin->setRange(-1e9, 1e9);
	spin->setDecimals(4);
	return spin;
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

inline QIcon appearance_membership_icon(bool included) {
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

inline std::pair<std::string, std::string> selected_appearance_item(QTreeWidget* tree) {
	auto* current = tree->currentItem();
	if (!current)
		return {};
	return { current->data(0, Qt::UserRole).toString().toStdString(),
		current->data(0, Qt::UserRole + 1).toString().toStdString() };
}

inline void restore_appearance_item(
		QTreeWidget* tree, const std::string& slot, const std::string& state) {
	if (slot.empty()) {
		tree->setCurrentItem(nullptr);
		return;
	}
	QTreeWidgetItem* fallback = nullptr;
	for (int i = 0; i < tree->topLevelItemCount(); ++i) {
		auto* top = tree->topLevelItem(i);
		if (!fallback)
			fallback = top->childCount() ? top->child(0) : top;
		if (top->data(0, Qt::UserRole).toString().toStdString() != slot)
			continue;
		if (state.empty()) {
			tree->setCurrentItem(top);
			return;
		}
		for (int j = 0; j < top->childCount(); ++j) {
			auto* child = top->child(j);
			if (child->data(0, Qt::UserRole + 1).toString().toStdString() == state) {
				tree->setCurrentItem(child);
				return;
			}
		}
		tree->setCurrentItem(top);
		return;
	}
	if (fallback)
		tree->setCurrentItem(fallback);
}

inline const sm::appearance_slot* appearance_slot(
		const sm::appearance& appearance, const std::string& slot) {
	for (const auto& candidate : appearance.appearance_slots)
		if (candidate.slot == slot)
			return &candidate;
	return nullptr;
}

inline QString mapping_text(const sm::appearance_slot* implementation, const std::string& state) {
	if (!implementation)
		return QStringLiteral("—");
	auto mapping = implementation->states.find(state);
	if (mapping == implementation->states.end())
		return QStringLiteral("Use default");
	if (!mapping->second)
		return QStringLiteral("Hidden");
	return QString::fromStdString(*mapping->second);
}

inline std::pair<mapping_choice, QString> mapping_value(
		const sm::appearance_slot* implementation, const std::string& state) {
	if (!implementation)
		return { mapping_choice::hidden, {} };
	auto mapping = implementation->states.find(state);
	if (mapping == implementation->states.end())
		return { mapping_choice::inherited, {} };
	if (!mapping->second)
		return { mapping_choice::hidden, {} };
	return { mapping_choice::frame, QString::fromStdString(*mapping->second) };
}

}
