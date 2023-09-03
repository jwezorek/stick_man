#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <span>
#include <unordered_map>
#include <expected>
#include <tuple>
#include <any>
#include <ranges>
#include <variant>
#include "sm_types.h"
#include "sm_bone.h"

/*------------------------------------------------------------------------------------------------*/

namespace sm {

	class skeleton : public detail::enable_protected_make_unique<skeleton> {
		friend class world;

	private:

		std::string name_;
		node_ref root_;
		std::unordered_map<std::string, node*> nodes_;
		std::unordered_map<std::string, bone*> bones_;

	protected:

		skeleton(world& w, const std::string& name, double x, double y);
		void on_new_bone(sm::bone& bone);
		void set_name(const std::string& str);

	public:
		std::string name() const;
		node_ref root_node();
		const_node_ref root_node() const;

		result set_name(bone& bone, const std::string& new_name);
		result set_name(node& node, const std::string& new_name);

		result from_json(const std::string&);
		std::string to_json() const;
		auto nodes() { return detail::to_range_view<node_ref>(nodes_); }
		auto bones() { return detail::to_range_view<bone_ref>(bones_); }
		auto nodes() const { return detail::to_range_view<const_node_ref>(nodes_); }
		auto bones() const { return detail::to_range_view<const_bone_ref>(bones_); }
	};

    class world {
		friend class skeleton;
    private:
		
		std::vector<std::unique_ptr<node>> nodes_;
		std::vector<std::unique_ptr<bone>> bones_;
		std::unordered_map<std::string, std::unique_ptr<skeleton>> skeletons_;

		node_ref create_node(skeleton& parent, double x, double y);

    public:
		world();
		skeleton_ref create_skeleton(double x, double y);
		expected_skeleton skeleton(const std::string& name);
		std::vector<std::string> skeleton_names() const;

        expected_bone create_bone(const std::string& name, node& u, node& v);
		result from_json(const std::string&);
		std::string to_json() const;

		auto skeletons() { return detail::to_range_view<skeleton_ref>(skeletons_); }
		auto skeletons() const { return detail::to_range_view<const_skeleton_ref>(skeletons_); }
    };

	using node_visitor = std::function<bool(node&)>;
	using bone_visitor = std::function<bool(bone&)>;

    void dfs(node& j1, node_visitor visit_node = {}, bone_visitor visit_bone = {},
        bool just_downstream = false);

    void visit_nodes(node& j, node_visitor visit_node);
	void visit_bones(node& j, bone_visitor visit_node);

	struct fabrik_options {
		int max_iterations;
		double tolerance;
		bool forw_reaching_constraints;
		double max_ang_delta;

		fabrik_options();
	};

	result perform_fabrik(
		const std::vector<std::tuple<node_ref,point>>& effectors, 
		const std::vector<sm::node_ref>& pinned_nodes, 
		const fabrik_options& opts = {}
	);

}