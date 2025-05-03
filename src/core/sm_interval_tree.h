#pragma once

#include <cassert>
#include <compare>
#include <concepts>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>

namespace sm {

    template <std::totally_ordered K>
    struct interval {
        K start;
        K end;

        interval(K s, K e) : start(s), end(e) {
            if (!(s < e)) {
                throw std::invalid_argument("interval start must be less than end");
            }
        }

        bool overlaps(const interval& other) const {
            return start < other.end && end > other.start;
        }

        auto operator<=>(const interval&) const = default;
    };

    template <typename K, typename V>
    class interval_tree {
    public:
        using interval_type = interval<K>;
        using entry_type = std::pair<interval_type, V>;

        interval_tree() = default;

        interval_tree(const interval_tree& other) {
            root_ = clone_node(other.root_);
        }

        interval_tree& operator=(const interval_tree& other) {
            if (this != &other) {
                root_ = clone_node(other.root_);
            }
            return *this;
        }

        void insert(K start, K end, const V& val) {
            insert({ start, end }, val);
        }

        void insert(interval_type iv, const V& val) {
            bool added = false;
            root_ = insert_impl(std::move(root_), std::move(iv), val, added);
        }

        std::vector<entry_type> query_overlapping(const interval_type& target) const {
            std::vector<entry_type> result;
            query_impl(root_.get(), target, result);
            return result;
        }

        bool remove(const interval_type& iv, const V& val) {
            bool removed = false;
            root_ = remove_impl(std::move(root_), iv, val, removed);
            return removed;
        }

        std::vector<entry_type> entries() const {
            std::vector<entry_type> result;
            collect_impl(root_.get(), result);
            return result;
        }

    private:
        struct node;
        using node_ptr = std::unique_ptr<node>;

        struct node {
            interval_type iv;
            V val;
            node_ptr left, right;
            int balance = 0;
            K max;
            std::vector<std::pair<K, V>> extra;

            node(interval_type i, V v)
                : iv(std::move(i)), val(std::move(v)), max(iv.end) {
            }

            void update_max() {
                max = iv.end;
                if (left) max = std::max(max, left->max);
                if (right) max = std::max(max, right->max);
            }
        };

        node_ptr root_;

        node_ptr clone_node(const node_ptr& other) const {
            if (!other) return nullptr;

            auto new_node = std::make_unique<node>(other->iv, other->val);
            new_node->balance = other->balance;
            new_node->max = other->max;
            new_node->extra = other->extra;
            new_node->left = clone_node(other->left);
            new_node->right = clone_node(other->right);
            return new_node;
        }

        void collect_impl(const node* n, std::vector<entry_type>& out) const {
            if (!n) return;

            collect_impl(n->left.get(), out);

            out.emplace_back(n->iv, n->val);
            for (const auto& [end, val] : n->extra) {
                interval_type iv{ n->iv.start, end };
                out.emplace_back(iv, val);
            }

            collect_impl(n->right.get(), out);
        }

        node_ptr insert_impl(node_ptr root, interval_type iv, const V& val, bool& added) {
            if (!root) {
                added = true;
                return std::make_unique<node>(iv, val);
            }

            if (iv.start < root->iv.start) {
                root->left = insert_impl(std::move(root->left), iv, val, added);
                if (added) root->balance--;
            }
            else if (iv.start > root->iv.start) {
                root->right = insert_impl(std::move(root->right), iv, val, added);
                if (added) root->balance++;
            }
            else {
                // same start time - track by max end
                if (iv.end > root->iv.end) {
                    root->extra.insert(root->extra.begin(), { root->iv.end, root->val });
                    root->iv = iv;
                    root->val = val;
                }
                else {
                    auto comp = [](const std::pair<K, V>& a, const K& b) {
                        return a.first > b;
                        };
                    auto it = std::lower_bound(root->extra.begin(), root->extra.end(), iv.end, comp);
                    root->extra.insert(it, { iv.end, val });
                }
                added = false;
                return root;
            }

            root->update_max();
            if (root->balance == -2) {
                if (root->left->balance > 0) {
                    root->left = rotate_left(std::move(root->left));
                }
                return rotate_right(std::move(root));
            }
            else if (root->balance == 2) {
                if (root->right->balance < 0) {
                    root->right = rotate_right(std::move(root->right));
                }
                return rotate_left(std::move(root));
            }

            return root;
        }

        node_ptr remove_impl(node_ptr root, const interval_type& iv, const V& val, bool& removed) {
            if (!root) return nullptr;

            if (iv.start < root->iv.start) {
                root->left = remove_impl(std::move(root->left), iv, val, removed);
                if (removed) root->balance++;
            }
            else if (iv.start > root->iv.start) {
                root->right = remove_impl(std::move(root->right), iv, val, removed);
                if (removed) root->balance--;
            }
            else {
                // start matches
                if (iv.end == root->iv.end && root->val == val) {
                    if (!root->extra.empty()) {
                        auto [next_end, next_val] = root->extra.front();
                        root->extra.erase(root->extra.begin());
                        root->iv.end = next_end;
                        root->val = next_val;
                    }
                    else {
                        // Remove this node
                        removed = true;
                        if (!root->left) return std::move(root->right);
                        if (!root->right) return std::move(root->left);

                        // Two children: find successor
                        auto* min_node = root->right.get();
                        while (min_node->left) min_node = min_node->left.get();

                        root->iv = min_node->iv;
                        root->val = min_node->val;
                        root->extra = min_node->extra;

                        bool dummy = false;
                        root->right = remove_impl(std::move(root->right), min_node->iv, min_node->val, dummy);
                        if (removed) root->balance--;
                    }
                }
                else {
                    // Try removing from extra
                    auto it = std::find_if(root->extra.begin(), root->extra.end(),
                        [&](const std::pair<K, V>& p) { return p.first == iv.end && p.second == val; });
                    if (it != root->extra.end()) {
                        root->extra.erase(it);
                        removed = true;
                    }
                }
            }

            root->update_max();

            if (removed) {
                if (root->balance == -2) {
                    if (root->left->balance > 0)
                        root->left = rotate_left(std::move(root->left));
                    return rotate_right(std::move(root));
                }
                else if (root->balance == 2) {
                    if (root->right->balance < 0)
                        root->right = rotate_right(std::move(root->right));
                    return rotate_left(std::move(root));
                }
            }

            return root;
        }

        node_ptr rotate_left(node_ptr a) {
            auto b = std::move(a->right);
            a->right = std::move(b->left);
            b->left = std::move(a);

            b->left->update_max();
            b->update_max();

            b->left->balance = 0;
            b->balance = 0;
            return b;
        }

        node_ptr rotate_right(node_ptr a) {
            auto b = std::move(a->left);
            a->left = std::move(b->right);
            b->right = std::move(a);

            b->right->update_max();
            b->update_max();

            b->right->balance = 0;
            b->balance = 0;
            return b;
        }

        void query_impl(const node* n, const interval_type& target, std::vector<entry_type>& out) const {
            if (!n) return;

            if (target.end <= n->iv.start) {
                query_impl(n->left.get(), target, out);
            }
            else if (target.start >= n->max) {
                query_impl(n->right.get(), target, out);
            }
            else {
                query_impl(n->left.get(), target, out);

                if (n->iv.overlaps(target)) {
                    out.emplace_back(n->iv, n->val);
                    for (const auto& [end, val] : n->extra) {
                        interval_type i{ n->iv.start, end };
                        if (i.overlaps(target)) {
                            out.emplace_back(i, val);
                        }
                        else {
                            break;
                        }
                    }
                }

                query_impl(n->right.get(), target, out);
            }
        }
    };

} 