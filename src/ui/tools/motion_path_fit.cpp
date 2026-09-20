#include "motion_path_fit.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
using sm::point;
constexpr double eps=1e-9;
double dot(point a,point b){return a.x*b.x+a.y*b.y;}
double len(point p){return std::sqrt(dot(p,p));}
point normalized(point p){double l=len(p);return l>eps?(1.0/l)*p:point{1,0};}
point bezier(const sm::cubic_bezier_path& c,double u){
    double t=1-u;return t*t*t*c.start+3*t*t*u*c.control1+3*t*u*u*c.control2+u*u*u*c.end;
}
point bezier_d1(const sm::cubic_bezier_path& c,double u){
    double t=1-u;return 3*t*t*(c.control1-c.start)+6*t*u*(c.control2-c.control1)+3*u*u*(c.end-c.control2);
}
point bezier_d2(const sm::cubic_bezier_path& c,double u){
    return 6*(1-u)*(c.control2-2.0*c.control1+c.start)+6*u*(c.end-2.0*c.control2+c.control1);
}
std::vector<point> cleaned(std::span<const point> input){
    std::vector<point> out;if(input.empty())return out;out.push_back(input.front());
    constexpr double min_sample_distance=.25;
    for(std::size_t i=1;i<input.size();++i) if(sm::distance(input[i],out.back())>=min_sample_distance) out.push_back(input[i]);
    if(out.size()==1 && input.size()>1) out.push_back(input.back());
    else if(input.size()>1 && out.back()!=input.back()) out.push_back(input.back());
    point origin=out.front();for(auto& p:out)p-=origin;return out;
}
std::vector<double> chord_params(std::span<const point> pts){
    std::vector<double> u(pts.size(),0);for(std::size_t i=1;i<pts.size();++i)u[i]=u[i-1]+sm::distance(pts[i-1],pts[i]);
    if(u.back()>eps)for(auto& v:u)v/=u.back();else for(std::size_t i=0;i<u.size();++i)u[i]=double(i)/std::max<std::size_t>(1,u.size()-1);
    return u;
}
sm::cubic_bezier_path generate(std::span<const point> pts,std::span<const double> u,point tan1,point tan2){
    const point p0=pts.front(),p3=pts.back();double c00=0,c01=0,c11=0,x0=0,x1=0;
    for(std::size_t i=0;i<pts.size();++i){
        double t=u[i],v=1-t,b0=v*v*v,b1=3*v*v*t,b2=3*v*t*t,b3=t*t*t;
        point a1=b1*tan1,a2=b2*tan2;
        point tmp=pts[i]-((b0+b1)*p0+(b2+b3)*p3);
        c00+=dot(a1,a1);c01+=dot(a1,a2);c11+=dot(a2,a2);x0+=dot(a1,tmp);x1+=dot(a2,tmp);
    }
    double det=c00*c11-c01*c01,alpha1=0,alpha2=0;
    if(std::abs(det)>eps){alpha1=(x0*c11-x1*c01)/det;alpha2=(c00*x1-c01*x0)/det;}
    double seg=sm::distance(p0,p3),fallback=seg/3.0;
    if(alpha1<eps||alpha2<eps||!std::isfinite(alpha1)||!std::isfinite(alpha2))alpha1=alpha2=fallback;
    return {p0,p0+alpha1*tan1,p3+alpha2*tan2,p3};
}
double newton(const sm::cubic_bezier_path& c,point p,double u){
    point q=bezier(c,u),q1=bezier_d1(c,u),q2=bezier_d2(c,u),d=q-p;
    double den=dot(q1,q1)+dot(d,q2);if(std::abs(den)<eps)return u;
    return std::clamp(u-dot(d,q1)/den,0.0,1.0);
}
std::pair<double,std::size_t> max_error(std::span<const point> pts,const sm::cubic_bezier_path& c,std::span<const double> u){
    double max=-1;std::size_t at=pts.size()/2;
    for(std::size_t i=1;i+1<pts.size();++i){
        // Use a local closest-point refinement before measuring Euclidean error.
        // The chord parameter is only the initial guess; fitting tolerance is
        // therefore geometric rather than an artifact of sample timing.
        const double closest=newton(c,pts[i],u[i]);
        point d=bezier(c,closest)-pts[i];double e=dot(d,d);if(e>max){max=e;at=i;}
    }
    return {std::max(0.0,max),at};
}
sm::cubic_bezier_path fit_one(std::span<const point> pts,point tan1,point tan2){
    if(pts.size()<2)return {};
    if(pts.size()==2){double d=sm::distance(pts[0],pts[1])/3.0;return {pts[0],pts[0]+d*tan1,pts[1]+d*tan2,pts[1]};}
    auto u=chord_params(pts);auto c=generate(pts,u,tan1,tan2);
    for(int iter=0;iter<3;++iter){for(std::size_t i=1;i+1<u.size();++i)u[i]=newton(c,pts[i],u[i]);c=generate(pts,u,tan1,tan2);}
    return c;
}
void fit_recursive(std::span<const point> pts,point tan1,point tan2,double tolerance,std::vector<sm::cubic_bezier_path>& out,int depth=0){
    auto u=chord_params(pts);auto c=fit_one(pts,tan1,tan2);auto [err,split]=max_error(pts,c,u);
    if(err<=tolerance*tolerance||pts.size()<=4||depth>=20){out.push_back(c);return;}
    // Endpoint noise should not create tiny one-sample spline segments.
    split=std::clamp<std::size_t>(split,2,pts.size()-3);
    point center=normalized(pts[split-1]-pts[split+1]);
    fit_recursive(pts.first(split+1),tan1,center,tolerance,out,depth+1);
    fit_recursive(pts.subspan(split),-center,tan2,tolerance,out,depth+1);
}
sm::cubic_bezier_path straight_cubic(point end){return {{0,0},(1.0/3.0)*end,(2.0/3.0)*end,end};}
}

sm::motion_path ui::tool::fit_motion_path(std::span<const sm::point> samples,sm::motion_path_kind kind,double tolerance){
    auto pts=cleaned(samples);if(pts.size()<2)pts={{0,0},{0,0}};
    if(kind==sm::motion_path_kind::straight)return sm::motion_path(sm::line_path{{0,0},pts.back()});
    point tan1=normalized(pts[1]-pts[0]),tan2=normalized(pts[pts.size()-2]-pts.back());
    if(kind==sm::motion_path_kind::curve)return sm::motion_path(fit_one(pts,tan1,tan2));
    std::vector<sm::cubic_bezier_path> segments;fit_recursive(pts,tan1,tan2,std::max(.01,tolerance),segments);
    return sm::motion_path(sm::spline_path{std::move(segments)});
}
sm::motion_path ui::tool::convert_motion_path(const sm::motion_path& path,sm::motion_path_kind kind){
    if(path.kind()==kind)return path;auto end=path.final_displacement();
    if(kind==sm::motion_path_kind::straight)return sm::motion_path(sm::line_path{{0,0},end});
    if(kind==sm::motion_path_kind::spline){
        sm::cubic_bezier_path c;
        if(auto* existing=std::get_if<sm::cubic_bezier_path>(&path.geometry()))c=*existing;else c=straight_cubic(end);
        return sm::motion_path(sm::spline_path{{c}});
    }
    if(auto* line=std::get_if<sm::line_path>(&path.geometry()))return sm::motion_path(straight_cubic(line->end));
    std::vector<point> samples; samples.reserve(65);
    for(int i=0;i<=64;++i)samples.push_back(path.evaluate_by_arc_length(double(i)/64.0));
    return fit_motion_path(samples,sm::motion_path_kind::curve,2.0);
}
