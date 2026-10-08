#include "core/sm_animation_path.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool b, const char* msg) {if (!b) throw std::runtime_error(msg);}
void close(sm::point a, sm::point b, double epsilon=1e-6) {
    check(sm::distance(a,b)<epsilon,"unexpected path position");
}
void geometry() {
    using namespace sm;
    const point a{0,0},b{12,0};
    animation_path path;
    path.reset_shape(path_shape::line,a,b);
    close(path.evaluate(0,a,b),a);
    close(path.evaluate(.5,a,b),{6,0});
    close(path.evaluate(1,a,b),b);
    path.reset_shape(path_shape::cubic,a,b);
    close(path.at_parameter(.5,a,b),{6,0});
    path.knots[0].handle_out={0,20};
    path.knots[1].handle_in={-10,0};
    // Arc fractions and cubic polynomial parameters are generally different.
    check(distance(path.evaluate(.5,a,b),path.at_parameter(.5,a,b))>.05,
        "curve should be reparameterized by arc length");
    auto previous=path.evaluate(0,a,b);
    std::vector<double> step;
    for(int i=1;i<=20;++i) {
        auto p=path.evaluate(i/20.0,a,b);
        step.push_back(distance(p,previous)); previous=p;
    }
    for(auto d:step) check(d>0.25,"arc length table contains a near-zero step");
    close(path.evaluate(0,a,b),a);
    close(path.evaluate(1,a,b),b);
    const auto first=path.evaluate(.5,a,b);
    path.knots[0].handle_out={9,40}; // cache must invalidate automatically
    check(distance(first,path.evaluate(.5,a,b))>.1,"geometry cache stale");
    path.reset_shape(path_shape::spline,a,b);
    path.insert_knot(0,a,b);
    check(path.knots.size()==3,"spline insertion failed");
    close(path.evaluate(.5,a,b),{6,0});
    path.knots[1].handle_out={-8,20};
    path.knots[1].handle_in={4,3};
    // Equal arc fractions across the WHOLE spline, not each segment.
    auto mid=path.evaluate(.5,a,b);
    check(distance(mid,path.knots[1].position)>.05,
        "spline segments must not each consume half the transition");
    path.remove_knot(1);
    check(path.knots.size()==2,"spline removal failed");
    path.reset_shape(path_shape::cubic,a,a);
    path.knots[0].handle_out={0,0};
    path.knots[1].handle_in={0,0};
    close(path.evaluate(.4,a,a),a);
    path.knots[0].handle_out={10,0};
    path.knots[1].handle_in={10,0};
    close(path.evaluate(0,a,a),a);
    close(path.evaluate(1,a,a),a);
    check(std::isfinite(path.evaluate(.5,a,a).x),"degenerate non-zero loop failed");
}
void coordinates() {
    sm::animation_root_frame f{{50,20},std::acos(-1.0)/2};
    close(f.to_world({0,0}),{50,20});
    close(f.to_world({10,0}),{50,30});
    close(f.to_local({50,30}),{10,0});
    auto other=f;
    other.origin={100,200};
    close(other.to_local(other.to_world({4,7})),{4,7});
}
}
int main() {
    try {geometry();coordinates();std::cout<<"animation_path tests passed\n";}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
