#pragma once

#include <QWidget>
#include <QtWidgets>
#include <QGraphicsScene> 
#include <QMetaType>
#include <any>
#include <ranges>
#include <variant>
#include "../../core/sm_types.h"
#include "../../model/project.h"

/*------------------------------------------------------------------------------------------------*/

namespace ui {

	class canvas;

	class canvas_item {
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
		canvas_item();
		void sync_to_model();
		canvas* canvas() const;
		bool is_selected() const;
		void set_selected(bool selected);
        mdl::skel_piece to_skeleton_piece();
        virtual mdl::const_skel_piece to_skeleton_piece() const = 0;
		virtual ~canvas_item();
	};

	template<typename T, typename U>
	class has_stick_man_model : public canvas_item {
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
	
    // items are a view of canvas_item pointers of some type.
    // returns a view of sm::node, sm::bone, or sm::skeleton pointers. 
	auto to_model_ptrs(auto&& items) {
		return items | std::ranges::views::transform(
			[](auto* item) {
				return &item->model();
			}
		);
	}

    // items are a view of canvas_item pointers of some type.
    // returns a view of sm::node_ref, sm::bone_ref, or sm::skel_ref references. 
    auto to_model_refs(auto&& items) {
        return items | std::ranges::views::transform(
            [](auto* item) {
                return std::ref(item->model());
            }
        );
    }

    constexpr auto k_sel_frame_distance = 4.0;
    constexpr auto k_sel_color = QColorConstants::Svg::turquoise;
    constexpr auto k_sel_thickness = 3.0;
    constexpr auto k_node_radius = 8.0;
}