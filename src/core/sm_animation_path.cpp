#include "sm_animation_path.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
sm::point mix(sm::point a, sm::point b, double t) { return (1-t)*a+t*b; }
sm::point cubic(sm::point a, sm::point b, sm::point c, sm::point d, double t) {
    const auto ab = mix(a,b,t), bc = mix(b,c,t), cd = mix(c,d,t);
    return mix(mix(ab,bc,t),mix(bc,cd,t),t);
}
sm::point knot_position(const sm::animation_path& path, std::size_t i, sm::point start, sm::point end) {
    return i==0 ? start : (i+1==path.knots.size() ? end : path.knots[i].position);
}
constexpr std::size_t samples_per_segment = 96;
}

sm::point sm::animation_root_frame::to_world(point p) const {
    const auto c=std::cos(radians), s=std::sin(radians);
    return {origin.x+c*p.x-s*p.y, origin.y+s*p.x+c*p.y};
}
sm::point sm::animation_root_frame::to_local(point p) const {
    p=p-origin;
    const auto c=std::cos(radians), s=std::sin(radians);
    return {c*p.x+s*p.y, -s*p.x+c*p.y};
}

void sm::animation_path::invalidate() const {
    cache_signature_.clear(); cumulative_lengths_.clear();
}

sm::point sm::animation_path::at_parameter(double u, point start, point end) const {
    u=std::clamp(u,0.0,1.0);
    if (shape==path_shape::line || knots.size()<2) return mix(start,end,u);
    const auto segments=knots.size()-1;
    const double scaled=u*segments;
    const auto i=std::min(static_cast<std::size_t>(scaled),segments-1);
    const auto t= i+1==segments && u==1.0 ? 1.0 : scaled-i;
    const auto a=knot_position(*this,i,start,end);
    const auto b=knot_position(*this,i+1,start,end);
    return cubic(a,a+knots[i].handle_out,b+knots[i+1].handle_in,b,t);
}

sm::point sm::animation_path::evaluate(double fraction, point start, point end) const {
    if (!std::isfinite(fraction)) fraction=0;
    fraction=std::clamp(fraction,0.0,1.0);
    if (fraction==0) return start;
    if (fraction==1) return end;
    if (shape==path_shape::line || knots.size()<2) return mix(start,end,fraction);
    std::vector<point> signature{start,end};
    for(const auto& k:knots) {
        signature.push_back(k.position);
        signature.push_back(k.handle_in);
        signature.push_back(k.handle_out);
    }
    if (signature!=cache_signature_ || cumulative_lengths_.empty()) {
        cache_signature_=std::move(signature);
        const auto steps=(knots.size()-1)*samples_per_segment;
        cumulative_lengths_.clear(); cumulative_lengths_.reserve(steps+1);
        cumulative_lengths_.push_back(0);
        auto prev=start;
        for (std::size_t i=1;i<=steps;++i) {
            const auto next=at_parameter(double(i)/steps,start,end);
            cumulative_lengths_.push_back(cumulative_lengths_.back()+distance(prev,next));
            prev=next;
        }
    }
    const auto total=cumulative_lengths_.back();
    if (!(total>1e-12) || !std::isfinite(total)) return start;
    const auto desired=fraction*total;
    auto it=std::lower_bound(cumulative_lengths_.begin(),cumulative_lengths_.end(),desired);
    const auto idx=static_cast<std::size_t>(it-cumulative_lengths_.begin());
    if (idx==0) return start;
    const auto previous=cumulative_lengths_[idx-1];
    const auto span=*it-previous;
    const auto alpha=span>1e-15 ? (desired-previous)/span : 0.0;
    return at_parameter((idx-1+alpha)/(cumulative_lengths_.size()-1),start,end);
}

void sm::animation_path::reset_shape(path_shape new_shape, point start, point end) {
    shape=new_shape;
    knots.clear();
    if (shape!=path_shape::line) {
        const auto d=end-start;
        knots.resize(2);
        knots.front().handle_out=(1.0/3)*d;
        knots.back().handle_in=(-1.0/3)*d;
    }
    invalidate();
}
void sm::animation_path::insert_knot(std::size_t i, point start, point end) {
    if (shape==path_shape::line) reset_shape(path_shape::spline,start,end);
    if (knots.size()<2 || i>=knots.size()-1) throw std::out_of_range("path segment");
    shape=path_shape::spline;
    const auto a=knot_position(*this,i,start,end), b=knot_position(*this,i+1,start,end);
    const auto p0=a, p1=a+knots[i].handle_out, p2=b+knots[i+1].handle_in, p3=b;
    const auto a1=mix(p0,p1,.5), a2=mix(p1,p2,.5), a3=mix(p2,p3,.5);
    const auto b1=mix(a1,a2,.5), b2=mix(a2,a3,.5), mid=mix(b1,b2,.5);
    knots[i].handle_out=a1-a;
    knots[i+1].handle_in=a3-b;
    knots.insert(knots.begin()+i+1,path_knot{mid,b1-mid,b2-mid});
    invalidate();
}
void sm::animation_path::remove_knot(std::size_t i) {
    if (i==0 || i+1>=knots.size()) throw std::out_of_range("endpoint knots cannot be removed");
    knots.erase(knots.begin()+i);
    invalidate();
}
