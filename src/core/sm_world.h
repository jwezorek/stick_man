#pragma once

namespace sm {

    class world {
        friend class skeleton;
        friend class node;
        friend class bone;
    private:
        using skeleton_tbl = std::unordered_map<std::string, std::unique_ptr<skeleton>>;

        std::vector<std::unique_ptr<node>> nodes_;
        std::vector<std::unique_ptr<bone>> bones_;
        skeleton_tbl skeletons_;

        node_ref create_node(skeleton& parent, const std::string& name, double x, double y);
        node_ref create_node(skeleton& parent, double x, double y);
        expected_bone create_bone_in_skeleton(const std::string& bone_name, node& u, node& v);

    public:
        world();
        world(world&& other);
        world& operator=(world&& other);
        world(const world& other) = delete;
        world& operator=(const world& other) = delete;
        ~world() = default;

        void clear();
        bool empty() const;
        skeleton& create_skeleton(double x, double y);
        skeleton& create_skeleton(const point& pt);
        expected_skel create_skeleton(const std::string& name);
        expected_skel skeleton(const std::string& name);
        expected_const_skel skeleton(const std::string& name) const;

        result delete_skeleton(const std::string& skel_name);

        std::vector<std::string> skeleton_names() const;
        bool contains_skeleton(const std::string& name) const;
        result set_name(sm::skeleton& skel, const std::string& new_name);

        expected_bone create_bone(const std::string& name, node& u, node& v);
        result from_json_str(const std::string& js);
        result from_json(const nlohmann::json& js);
        std::string to_json_str() const;
        nlohmann::json to_json() const;
        void apply(matrix& mat);

        auto skeletons() { return detail::to_range_view<skel_ref>(skeletons_); }
        auto skeletons() const { return detail::to_range_view<const_skel_ref>(skeletons_); }
    };

}