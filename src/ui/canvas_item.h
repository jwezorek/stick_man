#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene>
#include <any>

namespace sm {
	class node;
	class bone;
	class ik_sandbox;
}

namespace ui {

	class canvas;

	class abstract_canvas_item {
	protected:
		QGraphicsItem* selection_frame_;

		virtual QGraphicsItem* create_selection_frame() const = 0;
		virtual void sync_item_to_model() = 0;
		virtual void sync_sel_frame_to_model() = 0;

	public:
		abstract_canvas_item();
		void sync_to_model();
		canvas* canvas() const;
		bool is_selected() const;
		void set_selected(bool selected);
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
	};

	class node_item :
		public has_stick_man_model<node_item, sm::node&>, public QGraphicsEllipseItem {
	private:
		QGraphicsEllipseItem* pin_;
		void sync_item_to_model() override;
		void sync_sel_frame_to_model() override;
		QGraphicsItem* create_selection_frame() const override;
	public:
		using model_type = sm::node;

		node_item(sm::node& node, double scale);
		void set_pinned(bool pinned);
	};

	class bone_item :
		public has_stick_man_model<bone_item, sm::bone&>, public QGraphicsPolygonItem {
	private:
		void sync_item_to_model() override;
		void sync_sel_frame_to_model() override;
		QGraphicsItem* create_selection_frame() const override;
	public:
		using model_type = sm::bone;

		bone_item(sm::bone& bone, double scale);
		node_item& parent_node_item() const;
		node_item& child_node_item() const;
	};

	template<typename T, typename U>
	T& item_from_model(U& model_obj) {
		return std::any_cast<std::reference_wrapper<T>>(model_obj.get_user_data()).get();
	}
}