#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene> 
#include <QMetaType>
#include <any>
#include <ranges>
#include <variant>
#include "../core/sm_types.h"
#include "project.h"

namespace ui {

	class canvas;

	class abstract_canvas_item {
		friend class canvas;
	protected:
		QGraphicsItem* selection_frame_;

		virtual QGraphicsItem* create_selection_frame() const = 0;
		virtual void sync_item_to_model() = 0;
		virtual void sync_sel_frame_to_model() = 0;
		virtual bool is_selection_frame_only() const = 0;
		virtual QGraphicsItem* item_body() = 0;
		QGraphicsItem* selection_frame();
		const QGraphicsItem* item_body() const;
	public:
		abstract_canvas_item();
		void sync_to_model();
		canvas* canvas() const;
		bool is_selected() const;
		void set_selected(bool selected);
        skel_piece to_skeleton_piece();
        virtual const_skel_piece to_skeleton_piece() const = 0;
		virtual ~abstract_canvas_item();
	};

	template<typename T, typename U>
	class has_stick_man_model : public abstract_canvas_item {
	protected:
		U& model_;
	public:
		has_stick_man_model(U& model) : model_(model) {
			auto* derived = static_cast<T*>(this);
			model.set_user_data(std::ref(*derived));
		}
		U& model() { return model_; }
        const U& model() const { return model_; }
		virtual ~has_stick_man_model() {
			//model_.set_user_data(std::any{});
		}
	};

	class has_treeview_item {
		QStandardItem* tree_view_item_;
	public:
		has_treeview_item();
		void set_treeview_item(QStandardItem* itm);
		QStandardItem* treeview_item() const;
	};

	class skeleton_item :
		public has_treeview_item,
		public has_stick_man_model<skeleton_item, sm::skeleton&>, 
		public QGraphicsRectItem {
	private:
		void sync_item_to_model() override;
		void sync_sel_frame_to_model() override;
		QGraphicsItem* create_selection_frame() const override;
		bool is_selection_frame_only() const override;
		QGraphicsItem* item_body() override;
        const_skel_piece to_skeleton_piece() const override;

	public:
		using model_type = sm::skeleton;
		skeleton_item(sm::skeleton& skel, double scale);
	};

	class node_item :
		public has_stick_man_model<node_item, sm::node&>, public QGraphicsEllipseItem {
	private:
		QGraphicsEllipseItem* pin_;
		void sync_item_to_model() override;
		void sync_sel_frame_to_model() override;
		QGraphicsItem* create_selection_frame() const override;
		bool is_selection_frame_only() const override;
	    QGraphicsItem* item_body() override;
        const_skel_piece to_skeleton_piece() const override;

		bool is_pinned_;
	public:
		using model_type = sm::node;

		node_item(sm::node& node, double scale);
		void set_pinned(bool pinned);
		bool is_pinned() const;
	};

	class rot_constraint_adornment;

	class bone_item :
		public has_treeview_item,
		public has_stick_man_model<bone_item, sm::bone&>, 
		public QGraphicsPolygonItem {
	private:
		rot_constraint_adornment* rot_constraint_;
		QStandardItem* treeview_item_;

		void sync_item_to_model() override;
		void sync_sel_frame_to_model() override;
		QGraphicsItem* create_selection_frame() const override;
		bool is_selection_frame_only() const override;
		QGraphicsItem* item_body() override;
		void sync_rotation_constraint_to_model();
        const_skel_piece to_skeleton_piece() const override;

	public:
		using model_type = sm::bone;

		bone_item(sm::bone& bone, double scale);
		node_item& parent_node_item() const;
		node_item& child_node_item() const;
	};

	Q_DECLARE_METATYPE(bone_item*);

	class rot_constraint_adornment : 
		public QGraphicsEllipseItem {

	public:
		rot_constraint_adornment();
		void set(
			const sm::bone& node,
			const sm::rot_constraint& constraint,
			double scale
		);
	};

	// T is a graphics item type (node_item, bone_item, or skeleton_item) and 
	// is provided explicitely by the caller
	// U is a model type (node, bone, or skeleton) and is deduced.
	template<typename T, typename U>
	T& item_from_model(U& model_obj) {
		return std::any_cast<std::reference_wrapper<T>>(model_obj.get_user_data());
	}

	bool has_canvas_item(const auto& model_obj) {
		return model_obj.get_user_data().has_value();
	}
	
	auto to_model_ptrs(auto&& items) {
		return items | std::ranges::views::transform(
			[](auto* item) {
				return &item->model();
			}
		);
	}
}