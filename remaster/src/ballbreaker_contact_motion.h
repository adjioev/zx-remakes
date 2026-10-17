#pragma once

#include "ballbreaker_hd.h"
#include "spectrum.h"
#include <array>
#include <limits>
#include <algorithm>
#include <deque>
#include <vector>

// Near the bat, present BOTH objects from one confirmed timeline. Independent
// filters can turn an actual miss into visible overlap, even at equal mean lag.
class BBContactMotion {
public:
    struct Point { double t; BBObject ball, bat; bool anchor = false, contact = false; };
    void clear() { points_.clear(); pending_.clear(); presented_.clear(); smoothed_at_=-1; paced_=false; seeded_=false; last_bat_x_=std::nanf(""); last_bat_change_=0; }
    void observe(const Spectrum& s, bool hit = false, bool paced = false) {
        if (s.peek(0x807a) || s.peek(0x809a) || s.peek(0x8083)>1) return;
        // Capture the actual clock throughout the blend approach, before its
        // phase must agree with exact impact anchors. The paired smoother below
        // removes redundant holds rather than replaying these turbo bursts.
        if (paced && !hit && s.peek(0x8065) > 40) return;
        pending_.push_back({paced ? s.video_ticks() : s.cpu_ticks(), float(s.peek(0x8064)),
            float(s.peek(hit ? 0x8062 : 0x8065)),
            paced ? bb_even_paddle_left(s.peek(0x8061),s.peek(0x8063)) : float(s.peek(0x8061)-s.peek(0x8063)/2), float(s.peek(0x8063)), hit});
    }
    void observe_falling_paddle(const Spectrum& s) {
        if(s.peek(0x809a) || s.peek(0x8083)>1)return;
        pending_.push_back({s.video_ticks(),0,0,bb_even_paddle_left(s.peek(0x8061),s.peek(0x8063)),float(s.peek(0x8063)),false,false,true});
    }
    void observe_contact(const Spectrum& s, float x, float z, float left, float width, bool anchor = true, std::optional<uint64_t> tick = {}) {
        pending_.push_back({tick.value_or(s.video_ticks()),x,z,left,width,anchor,true});
    }
    void sample(const Spectrum& s, const BBObject& ball, const BBObject& bat, double t, bool paced = false, const BBObject* seed_ball = nullptr, const BBObject* seed_bat = nullptr, const std::function<BBObject(double)>& baseline_bat = {}, const std::vector<double>& baseline_times = {}) {
        if (s.peek(0x809a) || s.peek(0x8083)>1) { clear(); return; }
        const double hz=s.model()==128 ? 3546894.0 : 3500000.0;
        auto append=[&](Point p) {
            if(paced) {
                auto at=std::lower_bound(points_.begin(),points_.end(),p.t,[](const Point& a,double t){return a.t<t;});
                if(at!=points_.end() && std::abs(at->t-p.t)<1e-10) {
                    if(p.anchor || !at->anchor)*at=p;
                } else points_.insert(at,p);
            } else if(!points_.empty() && p.t<=points_.back().t) points_.back()=p;
            else points_.push_back(p);
        };
        for(const auto& v:pending_) {
            BBObject b=ball, p=bat;
            if(!v.paddle_only) {
                b.x=v.x-5; b.z=v.z+5; b.sx=148+v.x-v.z; b.sy=104-v.x/2;
            }
            p.x=v.left; p.bat_width=v.width; p.sx=160+p.x-p.z; p.sy=100-p.x/2;
            // Contact samples share the event clock used by audio and flash.
            // Even parks the CPU after a pass and can run it at 16x before
            // that. Peripheral time includes the idle tail and excludes those
            // extra CPU ticks. Original retains its native CPU-time mapping.
            const auto now = paced ? s.video_ticks() : s.cpu_ticks();
            double when=v.hit && !v.geometric ? t : t-double(now-v.tick)/hz;
            if(v.hit && !v.geometric) {
                while(!points_.empty() && points_.back().t>t-.04 &&
                      points_.back().ball.z<b.z) points_.pop_back();
            }
            append({when,b,p,v.hit,v.hit});
            if(v.hit && !v.geometric) break; // native Original reserves the frame boundary
            // Modern contacts have their actual time; later steps remain visible.
        }
        pending_.clear();
        // Preserve the contact anchor if it shares this frame boundary.
        if(points_.empty() || points_.back().t<t-1e-8) {
            BBObject b=ball;
            if(!s.peek(0x807a)) {
                float x=s.peek(0x8064), z=s.peek(0x8065);
                b.x=x-5; b.z=z+5; b.sx=148+x-z; b.sy=104-x/2;
            }
            append({t,b,bat});
        }
        while(points_.size()>2 && points_[1].t<t-.3) points_.pop_front();
        paced_=paced;
        if(paced) smooth(t,seed_ball,seed_bat,baseline_bat,baseline_times);
    }
    bool at(double t, BBObject& ball, BBObject& bat) const {
        // A new serve must not replace an already smooth paddle with a clamped
        // future sample. Wait until the full presentation buffer is confirmed.
        if(paced_ && !seeded_ && (points_.empty() || points_.back().t-points_.front().t<.04-1e-8))return false;
        return position(paced_ && !presented_.empty()?presented_:points_,t,ball,bat);
    }
    bool at_confirmed(double t, BBObject& ball, BBObject& bat) const {
        return position(points_,t,ball,bat);
    }
    uint64_t safe_spans() const { return safe_spans_; }
    uint64_t fallback_spans() const { return fallback_spans_; }
private:
    static bool position(const std::deque<Point>& points_, double t, BBObject& ball, BBObject& bat) {
        if(points_.empty()) return false;
        auto right=std::lower_bound(points_.begin(),points_.end(),t,
            [](const Point& p,double v){return p.t<v;});
        if(right==points_.begin()) { ball=right->ball; bat=right->bat; return true; }
        if(right==points_.end()) { ball=points_.back().ball; bat=points_.back().bat; return true; }
        const auto& a=*(right-1); const auto& b=*right;
        float k=float((t-a.t)/(b.t-a.t));
        auto lerp=[&](BBObject x,const BBObject& y) {
            x.x+=(y.x-x.x)*k; x.y+=(y.y-x.y)*k; x.z+=(y.z-x.z)*k;
            x.sx+=(y.sx-x.sx)*k; x.sy+=(y.sy-x.sy)*k;
            return x;
        };
        ball=lerp(a.ball,b.ball); bat=lerp(a.bat,b.bat); return true;
    }
    static bool safe(const Point& a,const Point& b) {
        if(std::abs(a.bat.bat_width-b.bat.bat_width)>.001f || std::abs(a.bat.z-b.bat.z)>.001f)return false;
        // Both native paddle sprites (0A/0B) decode to dz=7,height=12.
        // RAM supplies their visible width, so the shared default shape is exact.
        auto body=bb_visible_paddle_body(a.bat.x,a.bat.bat_width,a.bat.z,a.bat.bat_inset);
        // Work in the moving paddle's reference frame. Projection carries the
        // actual ball height, including a lost ball falling below the platform.
        auto centre=[](const BBObject& ball,const BBObject& bat) {
            double x=ball.x+5;
            return std::array<double,3>{x-bat.x,108-x/2-ball.sy,160+x-(ball.sx+12)};
        };
        const auto from=centre(a.ball,a.bat),to=centre(b.ball,b.bat);
        const std::array<double,3> low={body.x0-a.bat.x,0,body.z0};
        const std::array<double,3> high={body.x1-a.bat.x,body.height,body.z1};
        std::vector<double> cuts={0,1};
        for(int axis=0;axis<3;++axis) {
            double speed=to[axis]-from[axis];if(std::abs(speed)<1e-10)continue;
            for(double plane:{low[axis],high[axis]}) {
                double time=(plane-from[axis])/speed;
                if(time>0 && time<1)cuts.push_back(time);
            }
        }
        std::sort(cuts.begin(),cuts.end());
        double minimum=std::numeric_limits<double>::infinity();
        for(size_t i=1;i<cuts.size();++i) {
            double begin=cuts[i-1],end=cuts[i],middle=(begin+end)/2;
            double aa=0,bb=0,cc=0;
            for(int axis=0;axis<3;++axis) {
                double speed=to[axis]-from[axis],at=from[axis]+speed*middle;
                if(at>=low[axis] && at<=high[axis])continue;
                double offset=from[axis]-(at<low[axis]?low[axis]:high[axis]);
                aa+=speed*speed;bb+=offset*speed;cc+=offset*offset;
            }
            auto squared=[&](double time){return aa*time*time+2*bb*time+cc;};
            minimum=std::min({minimum,squared(begin),squared(end)});
            if(aa>1e-12)minimum=std::min(minimum,squared(std::clamp(-bb/aa,begin,end)));
        }
        return minimum>=(BB_BALL_RADIUS-.001)*(BB_BALL_RADIUS-.001);
    }
    void smooth(double t, const BBObject* seed_ball=nullptr, const BBObject* seed_bat=nullptr, const std::function<BBObject(double)>& baseline_bat={}, const std::vector<double>& baseline_times={}) {
        const double frontier=t-.04;
        std::deque<Point> future;
        BBObject ball{},bat{};
        if(presented_.empty() || smoothed_at_<frontier-.00001) {
            if(seed_ball && seed_bat) {ball=*seed_ball;bat=*seed_bat;seeded_=true;}
            else position(points_,frontier,ball,bat);
            future.push_back({frontier,ball,bat});
        } else {
            position(presented_,frontier,ball,bat);future.push_back({frontier,ball,bat});
            for(const auto& p:presented_)if(p.t>frontier+1e-8 && p.t<=smoothed_at_+1e-8)future.push_back(p);
        }
        auto same=[](const Point& a,const Point& b) {
            return std::abs(a.ball.x-b.ball.x)<.00001f && std::abs(a.ball.z-b.ball.z)<.00001f &&
                   std::abs(a.ball.sy-b.ball.sy)<.00001f && a.ball.sprite==b.ball.sprite &&
                   std::abs(a.bat.x-b.bat.x)<.00001f && a.bat.bat_width==b.bat.bat_width;
        };
        for(const auto& p:points_)if(p.t>std::max(frontier,smoothed_at_)+1e-8) {
            if(p.anchor || !same(future.back(),p))future.push_back(p);
        }
        if(future.back().t<t-1e-8) {
            position(points_,t,ball,bat);Point tail{t,ball,bat};
            if(!same(future.back(),tail))future.push_back(tail);
        }
        // A moving-paddle TOI can revise the previous unshown frame. Insert its
        // exact anchor into that future, never into the frozen display history.
        for(const auto& p:points_)if(p.contact && p.t>frontier+1e-8 && p.t<=smoothed_at_+1e-8) {
            auto at=std::lower_bound(future.begin(),future.end(),p.t,[](const Point& a,double t){return a.t<t;});
            if(at!=future.end() && std::abs(at->t-p.t)<1e-10)*at=p;
            else future.insert(at,p);
        }
        // Preserve the native paddle velocity knots in the common trajectory.
        for(double time:baseline_times)if(time>frontier+1e-8 && time<t-1e-8) {
            auto at=std::lower_bound(future.begin(),future.end(),time,
                [](const Point& a,double t){return a.t<t;});
            if(at!=future.end() && std::abs(at->t-time)<1e-10)at->anchor=true;
            else {
                BBObject b{},p{};position(future,time,b,p);
                future.insert(at,{time,b,p,true});
            }
        }
        const auto fallback=future;
        if(baseline_bat)for(size_t i=1;i<future.size();++i)if(!future[i].contact) {
            Point candidate=future[i];const BBObject p=baseline_bat(candidate.t);
            candidate.bat.x=p.x;candidate.bat.y=p.y;candidate.bat.z=p.z;
            candidate.bat.sx=p.sx;candidate.bat.sy=p.sy;
            // The normal paddle track is already smooth and shares the actual
            // movement clock. Use it unless that pose would penetrate the ball;
            // true impact snapshots and the displayed frontier remain fixed.
            if(safe(candidate,candidate))future[i]=candidate;
        }
        // Preserve real reversals and the beginning of a stationary paddle tail.
        for(size_t i=1;i+1<future.size();++i) {
            size_t paddle_left=i,paddle_right=i;
            while(paddle_left>0 && std::abs(future[paddle_left-1].bat.x-future[i].bat.x)<.0001f)--paddle_left;
            while(paddle_right+1<future.size() && std::abs(future[paddle_right+1].bat.x-future[i].bat.x)<.0001f)++paddle_right;
            if(i==paddle_left && paddle_left>0 && paddle_right+1<future.size()) {
                float before=future[i].bat.x-future[paddle_left-1].bat.x;
                float after=future[paddle_right+1].bat.x-future[i].bat.x;
                if(before*after<0)future[i].anchor=true;
            }
            size_t left=i,right=i;
            while(left>0 && std::hypot(future[left-1].ball.x-future[i].ball.x,future[left-1].ball.z-future[i].ball.z)<.0001f)--left;
            while(right+1<future.size() && std::hypot(future[right+1].ball.x-future[i].ball.x,future[right+1].ball.z-future[i].ball.z)<.0001f)++right;
            if(left>0 && right+1<future.size()) {
                float ax=future[i].ball.x-future[left-1].ball.x,az=future[i].ball.z-future[left-1].ball.z;
                float bx=future[right+1].ball.x-future[i].ball.x,bz=future[right+1].ball.z-future[i].ball.z;
                if(std::abs(ax*bz-az*bx)>.001f || ax*bx+az*bz<0)future[i].anchor=true;
            }
        }
        BBObject current_ball{},current_bat{};
        position(points_,t,current_ball,current_bat);
        if(!std::isfinite(last_bat_x_) || std::abs(current_bat.x-last_bat_x_)>.001f)last_bat_change_=t;
        last_bat_x_=current_bat.x;
        if(t-last_bat_change_>=.04-1e-8) {
            double stop=last_bat_change_+.02;
            if(stop>frontier+1e-8 && stop<t-1e-8) {
                BBObject b{},p{};position(points_,stop,b,p);
                auto where=std::lower_bound(future.begin(),future.end(),stop,[](const Point& a,double time){return a.t<time;});
                if(where!=future.end() && std::abs(where->t-stop)<1e-8)*where={stop,b,p,true};
                else future.insert(where,{stop,b,p,true});
            }
        }
        std::deque<Point> simplified;
        simplified.push_back(future.front());
        bool unsafe=false;
        for(size_t first=0;first+1<future.size();) {
            size_t limit=first+1;
            while(limit+1<future.size() && !future[limit].anchor)++limit;
            size_t chosen=limit;
            while(chosen>first+1 && !safe(future[first],future[chosen]))--chosen;
            if(chosen==limit && safe(future[first],future[chosen]))++safe_spans_;else ++fallback_spans_;
            if(!safe(future[first],future[chosen])) {unsafe=true;break;}
            simplified.push_back(future[chosen]);first=chosen;
        }
        if(unsafe)simplified=fallback;
        while(!presented_.empty() && presented_.back().t>=frontier-1e-8)presented_.pop_back();
        presented_.insert(presented_.end(),simplified.begin(),simplified.end());
        while(presented_.size()>2 && presented_[1].t<t-.3)presented_.pop_front();
        smoothed_at_=t;
    }
    struct Pending { uint64_t tick; float x,z,left,width; bool hit; bool geometric = false, paddle_only = false; };
    std::vector<Pending> pending_;
    std::deque<Point> points_, presented_;
    double smoothed_at_=-1;
    bool paced_=false,seeded_=false;
    float last_bat_x_=std::nanf("");
    double last_bat_change_=0;
    uint64_t safe_spans_=0,fallback_spans_=0;
};
