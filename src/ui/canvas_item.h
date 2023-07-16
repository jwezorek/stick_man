#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene>
#include <any>
#include <ranges>

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

	class joint_constraint_item : public abstract_canvas_item,
		public QGraphicsEllipseItem {
	private:
		void sync_item_to_model() override;
		void sync_sel_frame_to_model() override;
		QGraphicsItem* create_selection_frame() const override;

		const sm::node* node_;
		double min_angle_;
		double max_angle_;
	public:
		joint_constraint_item(
			const sm::node* node = nullptr,
			double min_angle = 0,
			double max_angle = 0,
			double scale = 0.0
		);
		void set(
			const sm::node* node_,
			double min_angle,
			double max_angle,
			double scale
		);
	};

	template<typename T, typename U>
	T& item_from_model(U& model_obj) {
		return std::any_cast<std::reference_wrapper<T>>(model_obj.get_user_data()).get();
	}

	template<typename T>
	auto to_models_of_item_type(const auto& sel) {
		namespace r = std::ranges;
		namespace rv = std::ranges::views;
		using out_type = typename T::model_type;
		return sel | rv::transform(
			[](auto ptr)->out_type* {
				auto bi = dynamic_cast<T*>(ptr);
				if (!bi) {
					return nullptr;
				}
				return &(bi->model());
			}
		) | rv::filter(
			[](auto ptr) {
				return ptr;
			}
		) | r::to<std::vector<out_type*>>();
	}
}