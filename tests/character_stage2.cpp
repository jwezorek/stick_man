#include "model/project.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>

namespace {
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
sm::object_id own(sm::project& p, std::initializer_list<sm::const_skel_ref> members) {
    auto c = p.create_character(std::span(members.begin(), members.size()));
    require(c.has_value(), "create character");
    return c->get().id();
}
void check(const sm::project& p) {
    require(p.has_unique_object_ids(), "unique IDs");
    require(p.has_consistent_membership(), "core invariant check");
    for (auto c : p.characters()) {
        require(!c->rig().empty(), "empty character");
        for (const auto& id : c->rig().skeleton_ids()) {
            auto s = p.topology().skeleton(id);
            require(s.has_value(), "dangling rig ID");
            require(s->get().parent_character() &&
                &s->get().parent_character()->get() == &c.get(), "rig parent mismatch");
        }
    }
    for (auto s : p.topology().skeletons()) {
        if (auto c = s->parent_character()) {
            require(c->get().rig().contains(s->id()), "parent rig mismatch");
            require(&p.character(c->get().id())->get() == &c->get(), "dangling parent");
        }
    }
}
void merges() {
    for (int mode = 0; mode < 4; ++mode) {
        mdl::project model;
        auto& p = model.core();
        auto& a = p.create_skeleton({0, 0});
        auto& b = p.create_skeleton({1, 0});
        const auto aid = a.id(), bid = b.id();
        const auto u = a.root_node().id(), v = b.root_node().id();
        std::optional<sm::object_id> cid;
        if (mode == 1) cid = own(p, {a});
        if (mode == 2) cid = own(p, {b});
        if (mode == 3) cid = own(p, {a, b});
        model.add_bone(u, v);
        const auto bone_id = (*p.topology().skeleton(aid)->get().bones().begin())->id();
        for (int cycle = 0; cycle < 4; ++cycle) {
            check(p);
            auto merged = p.topology().skeleton(aid);
            require(merged.has_value() && !p.topology().skeleton(bid), "merge topology");
            require(merged->get().is_loose() == !cid, "merge membership");
            require(p.topology().get<sm::bone>(bone_id).has_value(), "merge redo bone ID");
            if (cid) require(p.character(*cid)->get().rig().contains(aid), "merge rig");
            model.undo();
            check(p);
            require(p.topology().skeleton(aid)->get().is_loose() == (mode == 0 || mode == 2), "undo A membership");
            require(p.topology().skeleton(bid)->get().is_loose() == (mode == 0 || mode == 1), "undo B membership");
            model.redo();
        }
    }
}
void rejected_merges() {
    mdl::project model;
    auto& p = model.core();
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    own(p, {a}); own(p, {b});
    const auto before = p.topology().to_json_str();
    const auto u = a.root_node().id(), v = b.root_node().id();
    require(p.can_create_bone(a.root_node(), b.root_node()) == sm::result::different_characters, "merge preflight");
    auto result = p.create_bone("bad", a.root_node(), b.root_node());
    require(!result && result.error() == sm::result::different_characters, "core cross-character failure");
    require(model.add_bone(u, v) == sm::result::different_characters, "model cross-character failure");
    require(!model.can_undo(), "failed command in history");
    model.add_new_skeleton_root({2, 0}); model.undo();
    require(model.can_redo(), "redo fixture");
    require(model.add_bone(v, u) == sm::result::different_characters && model.can_redo(), "failure cleared redo");
    require(before == p.topology().to_json_str(), "failed merge changed topology");
    sm::topology scratch;
    require(!scratch.create_bone("bypass", a.root_node(), b.root_node()), "scratch accepted live endpoints");
    check(p);
}
std::vector<sm::skel_ref> components(sm::topology& scratch, const sm::skeleton& source) {
    std::vector<sm::skel_ref> result;
    for (auto node : source.nodes()) {
        auto s = scratch.create_skeleton("split");
        require(s.has_value(), "split skeleton");
        require(node->copy_to(scratch, s->get().id()).has_value(), "split node");
        require(s->get().is_loose(), "scratch membership");
        result.push_back(*s);
    }
    return result;
}
void split_and_delete() {
    mdl::project model;
    auto& p = model.core();
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    const auto original = a.id();
    require(p.create_bone("join", a.root_node(), b.root_node()).has_value(), "split fixture merge");
    const auto cid = own(p, {a});
    p.rename(cid, "Fred");
    const auto* character_address = &p.character(cid)->get();
    sm::topology scratch;
    auto replacements = components(scratch, a);
    require(model.replace_skeletons({original}, replacements) == sm::result::success, "split");
    const auto split_ids = p.character(cid)->get().rig().skeleton_ids();
    for (int cycle = 0; cycle < 4; ++cycle) {
        check(p);
        require(p.character(cid)->get().rig().size() == 2, "split lost membership");
        require(&p.character(cid)->get() == character_address, "transiently destroyed character");
        for (auto id : split_ids) require(p.topology().skeleton(id).has_value(), "split redo IDs");
        model.undo(); check(p);
        require(p.character(cid)->get().rig().contains(original), "split undo identity");
        model.redo();
    }
    auto plan = p.plan_replacement({split_ids[0]}, {});
    require(plan && plan->deleted_character_ids.empty(), "partial deletion preflight");
    require(model.replace_skeletons({split_ids[0]}, {}) == sm::result::success, "delete component");
    check(p);
    require(p.character(cid)->get().rig().size() == 1, "multi-component deletion");
    plan = p.plan_replacement({split_ids[1]}, {});
    require(plan && plan->deleted_character_ids == std::vector{cid}, "final deletion preflight");
    require(model.replace_skeletons({split_ids[1]}, {}) == sm::result::success, "delete final component");
    for (int cycle = 0; cycle < 4; ++cycle) {
        check(p); require(!p.character(cid), "final deletion left character");
        model.undo(); check(p);
        require(p.character(cid)->get().name() == "Fred", "restored character name");
        require(p.character(cid)->get().rig().contains(split_ids[1]), "restored rig");
        model.redo();
    }
    model.undo(); model.undo(); check(p);
    require(p.character(cid)->get().rig().size() == 2, "restore both deletions");
    require(p.delete_skeleton(split_ids[0]) == sm::result::success, "direct deletion");
    check(p); require(p.character(cid).has_value(), "direct partial deletion");
    require(p.delete_skeleton(split_ids[1]) == sm::result::success, "direct final deletion");
    check(p); require(!p.character(cid), "direct final character survives");
}
void mixed_replacements() {
    mdl::project model;
    auto& p = model.core();
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    auto& loose = p.create_skeleton({2, 0});
    auto& other = p.create_skeleton({3, 0});
    const auto cid = own(p, {a, b}), other_cid = own(p, {other});
    const std::vector ids{a.id(), b.id(), loose.id(), other.id()};
    sm::topology scratch;
    std::vector<sm::skel_ref> replacements;
    for (auto id : ids) {
        auto single = components(scratch, p.topology().skeleton(id)->get());
        replacements.insert(replacements.end(), single.begin(), single.end());
    }
    const auto loose_new = replacements[2]->id(), other_new = replacements[3]->id();
    std::reverse(replacements.begin(), replacements.end());
    require(model.replace_skeletons(ids, replacements) == sm::result::success, "mixed replace");
    for (int cycle = 0; cycle < 3; ++cycle) {
        check(p);
        require(p.topology().skeleton(loose_new)->get().is_loose(), "mixed replacement adopted loose component");
        require(p.character(other_cid)->get().rig().contains(other_new), "mixed wrong character");
        require(p.character(cid)->get().rig().size() == 2, "mixed same-character components");
        model.undo(); check(p); model.redo();
    }
    sm::topology bad;
    auto sa = p.topology().skeleton(replacements[2]->id())->get().copy_to(bad);
    auto sb = p.topology().skeleton(other_new)->get().copy_to(bad);
    require(sa && sb, "bad replacement fixture");
    require(bad.create_bone("bad", sa->get().root_node(), sb->get().root_node()).has_value(), "scratch merge fixture");
    const auto before = p.topology().to_json_str();
    auto change = p.replace_skeletons({sa->get().id(), other_new}, {sm::skel_ref(sa->get())});
    require(change.status == sm::result::different_characters, "cross-character replacement");
    require(before == p.topology().to_json_str(), "invalid replacement mutated project");
    check(p);
}
void adoption() {
    sm::project p, foreign;
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    auto& c = p.create_skeleton({2, 0});
    auto& f = foreign.create_skeleton({3, 0});
    const auto cid = own(p, {a});
    const auto before = p.topology().to_json_str();
    std::vector<sm::const_skel_ref> candidates{b, f};
    require(p.adopt_skeletons(cid, candidates) == sm::result::foreign_skeleton && b.is_loose(), "foreign adoption atomicity");
    candidates = {b, a};
    require(p.adopt_skeletons(cid, candidates) == sm::result::skeleton_already_owned && b.is_loose(), "owned adoption atomicity");
    candidates = {b, b};
    require(p.adopt_skeletons(cid, candidates) == sm::result::duplicate_skeleton && b.is_loose(), "duplicate adoption atomicity");
    candidates = {b, c};
    require(p.adopt_skeletons(cid, candidates) == sm::result::success, "adoption");
    require(p.character(cid)->get().rig().size() == 3 && before == p.topology().to_json_str(), "adoption changed topology");
    check(p);
    sm::topology copies;
    auto copy = b.copy_to(copies);
    auto duplicate = b.duplicate_to(copies);
    require(copy && duplicate && copy->get().is_loose() && duplicate->get().is_loose(), "topology copy inherited membership");
}
void adoption_history() {
    mdl::project model;
    auto& p = model.core();
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    auto& c = p.create_skeleton({2, 0});
    const auto cid = own(p, {a});
    std::vector<sm::const_skel_ref> candidates{b, c};
    require(model.adopt_skeletons(cid, candidates) == sm::result::success, "model adoption");
    for (int cycle = 0; cycle < 4; ++cycle) {
        check(p); require(p.character(cid)->get().rig().size() == 3, "adoption redo");
        model.undo(); check(p);
        require(b.is_loose() && c.is_loose() && p.character(cid)->get().rig().size() == 1, "adoption undo");
        model.redo();
    }
    require(model.adopt_skeletons(cid, candidates) == sm::result::skeleton_already_owned, "model invalid adoption");
    model.undo();
    require(b.is_loose() && c.is_loose(), "invalid adoption pushed history");
}
void explicit_replacement_state() {
    sm::project p;
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    const auto aid = a.id(), bid = b.id();
    const auto ca = own(p, {a}), cb = own(p, {b});
    auto state = p.snapshot_membership({aid});
    sm::topology scratch;
    auto acopy = a.copy_to(scratch), bcopy = b.copy_to(scratch);
    require(acopy && bcopy, "explicit replacement fixture");
    require(scratch.create_bone("join", acopy->get().root_node(), bcopy->get().root_node()).has_value(), "scratch combine");
    const auto before = p.topology().to_json_str();
    auto result = p.replace_skeletons({aid, bid}, {*acopy}, {}, &state);
    require(result.status == sm::result::different_characters, "explicit state bypassed cross-character policy");
    require(before == p.topology().to_json_str() && p.character(ca) && p.character(cb), "invalid explicit state changed project");
    check(p);
}
void remapped_split() {
    mdl::project model;
    auto& p = model.core();
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    require(p.create_bone("join", a.root_node(), b.root_node()).has_value(), "remap fixture");
    const auto original = a.id(), root = a.root_node().id(), cid = own(p, {a});
    sm::topology scratch;
    auto replacements = components(scratch, a);
    // Selection boundaries can copy one original node into multiple components.
    auto duplicate_boundary = scratch.create_skeleton("boundary");
    require(a.root_node().copy_to(scratch, duplicate_boundary->get().id()).has_value(), "boundary copy");
    replacements.push_back(*duplicate_boundary);
    std::unordered_set<sm::object_id> regenerate{original, root};
    for (auto s : replacements) regenerate.insert(s->id());
    require(model.replace_skeletons({original}, replacements, regenerate) == sm::result::success, "remapped split");
    const auto inserted = p.character(cid)->get().rig().skeleton_ids();
    for (int cycle = 0; cycle < 4; ++cycle) {
        check(p); require(inserted.size() == 3, "boundary component count");
        for (auto id : inserted) require(p.character(cid)->get().rig().contains(id), "remap redo identity");
        model.undo(); check(p);
        require(p.character(cid)->get().rig().contains(original), "remap undo identity");
        model.redo();
    }
}
void ambiguous_replacement() {
    sm::project p;
    auto& a = p.create_skeleton({0, 0});
    const auto original = a.id(), cid = own(p, {a});
    sm::topology scratch;
    auto& replacement = scratch.create_skeleton(sm::point{2, 0});
    const auto before = p.topology().to_json_str();
    auto change = p.replace_skeletons({original}, {replacement});
    require(change.status == sm::result::ambiguous_membership && before == p.topology().to_json_str(), "ambiguous ownership guessed");
    auto state = p.snapshot_membership({original});
    state.parents.clear(); state.parents[replacement.id()] = cid;
    change = p.replace_skeletons({original}, {replacement}, {}, &state);
    require(change.status == sm::result::success, "explicit provenance rejected");
    check(p); require(p.character(cid)->get().rig().contains(replacement.id()), "explicit provenance lost");
}
void rejected_redo() {
    mdl::project model;
    auto& p = model.core();
    auto& a = p.create_skeleton({0, 0});
    auto& b = p.create_skeleton({1, 0});
    const auto bid = b.id();
    own(p, {a});
    require(model.add_bone(a.root_node().id(), b.root_node().id()) == sm::result::success, "redo fixture");
    model.undo();
    // A core client can change semantics between undo and redo. The command must
    // still enforce core policy and must not record its rejected redo as success.
    own(p, {p.topology().skeleton(bid)->get()});
    require(model.redo() == sm::result::different_characters, "redo failure result");
    require(!model.can_undo() && model.can_redo(), "failed redo entered undo history");
    check(p);
}
void live_duplication() {
    mdl::project model;
    auto& p = model.core();
    auto& a = p.create_skeleton({0, 0});
    const auto aid = a.id(), cid = own(p, {a});
    require(model.replace_skeletons({}, {a}) == sm::result::success, "duplicate live topology");
    for (auto s : p.topology().skeletons()) {
        if (s->id() != aid) require(s->is_loose(), "duplicate adopted source character");
    }
    require(p.character(cid)->get().rig().size() == 1, "duplication changed source rig");
    check(p); model.undo(); check(p); model.redo(); check(p);
}
}
int main() {
    try { merges(); rejected_merges(); split_and_delete(); mixed_replacements(); adoption(); adoption_history(); explicit_replacement_state(); remapped_split(); ambiguous_replacement(); rejected_redo(); live_duplication(); std::cout << "PASS character stage 2\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
