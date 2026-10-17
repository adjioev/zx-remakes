#pragma once

// BB1 Original-speed presentation. Never writes to the authoritative Spectrum.
#include "ballbreaker_hd.h"
#include "spectrum.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

class BBOriginalPreview {
public:
    struct Point { double t; BBObject ball; bool anchor = false; };
    static constexpr double HORIZON = 0.16;
    void clear() {
        history_.clear(); path_.clear(); pending_.clear(); offset_x_ = offset_z_ = 0;
        contact_t_ = consumed_contact_ = -10;
    }

    // BB1 only: called immediately after the original adds its signed RAM increments.
    void microstep(const Spectrum& s) {
        if (s.peek(0x807a) || s.peek(0x809a) || s.peek(0x8083)>1) return;
        pending_.push_back({s.cpu_ticks(), s.peek(0x8064), s.peek(0x8065)});
    }

    // Called by the real collision hook only. Forecasts cannot publish contacts.
    void contact(double t, float centre_x, float face_z) {
        contact_t_ = t;
        contact_x_ = centre_x - 5;
        contact_z_ = face_z + 5;
    }

    static bool same_direction(const BBObject& a, const BBObject& b, const BBObject& c) {
        float x=b.x-a.x, z=b.z-a.z, nx=c.x-b.x, nz=c.z-b.z;
        return std::abs(x*nz-z*nx)<0.001f && x*nx+z*nz>0;
    }
    static void retime(std::vector<Point>& points) {
        const auto source=points;
        size_t first=0;
        auto fit=[&](size_t last) {
            std::vector<double> distance(last-first+1,0);
            for(size_t i=first+1;i<=last;++i)
                distance[i-first]=distance[i-first-1]+std::hypot(source[i].ball.x-source[i-1].ball.x,source[i].ball.z-source[i-1].ball.z);
            // Spread each original straight run continuously between its timed
            // endpoints. CPU bursts are spatial samples, not display keyframes.
            if(distance.back()>0)
                for(size_t i=first+1;i<last;++i)
                    points[i].t=source[first].t+(source[last].t-source[first].t)*distance[i-first]/distance.back();
        };
        for(size_t i=1;i+1<source.size();++i) {
            if(source[i].anchor || !same_direction(source[i-1].ball,source[i].ball,source[i+1].ball)) {
                fit(i);first=i;
            }
        }
        if(!source.empty()) fit(source.size()-1);
        // Unexpected degenerate data must never create a backwards presentation clock.
        for(size_t i=1;i<points.size();++i)
            if(points[i].t<=points[i-1].t) { points=source;break; }
    }

    static BBObject interpolate(const std::vector<Point>& points,double t) {
        auto right=std::lower_bound(points.begin(),points.end(),t,[](const Point& p,double time){return p.t<time;});
        if(right==points.begin()) return right->ball;
        if(right==points.end()) return points.back().ball;
        const auto& a=*(right-1); const auto& b=*right;
        float k=float(std::clamp((t-a.t)/(b.t-a.t),0.0,1.0));
        BBObject out=a.ball;
        out.x+=(b.ball.x-a.ball.x)*k;out.y+=(b.ball.y-a.ball.y)*k;out.z+=(b.ball.z-a.ball.z)*k;
        out.sx+=(b.ball.sx-a.ball.sx)*k;out.sy+=(b.ball.sy-a.ball.sy)*k;
        return out;
    }

    bool at(double t,BBObject& ball) const {
        if(path_.empty()) return false;
        ball=interpolate(path_,t);
        float fade=float(std::clamp(1-(t-updated_)/0.04,0.0,1.0));
        float dx=offset_x_*fade,dz=offset_z_*fade;
        // Corrections stay on the current straight section and inside its endpoints.
        auto hi=std::upper_bound(path_.begin(),path_.end(),t,[](double time,const Point& p){return time<p.t;});
        size_t i=hi==path_.begin()?0:size_t(hi-path_.begin()-1), first=i,last=std::min(i+1,path_.size()-1);
        while(first>0 && first+1<path_.size() && !path_[first].anchor && same_direction(path_[first-1].ball,path_[first].ball,path_[first+1].ball)) --first;
        while(last+1<path_.size() && !path_[last].anchor && same_direction(path_[last-1].ball,path_[last].ball,path_[last+1].ball)) ++last;
        const auto& a=path_[first].ball;const auto& b=path_[last].ball;
        float vx=b.x-a.x,vz=b.z-a.z,len=vx*vx+vz*vz;
        float along=len>0?(dx*vx+dz*vz)/len:0;
        float x=std::clamp(ball.x+vx*along,std::min(a.x,b.x),std::max(a.x,b.x));
        float z=std::clamp(ball.z+vz*along,std::min(a.z,b.z),std::max(a.z,b.z));
        dx=x-ball.x;dz=z-ball.z;
        ball.x=x;ball.z=z;ball.sx+=dx-dz;ball.sy-=dx/2;
        return true;
    }

    void sample(const Spectrum& live,const BBObject& raw,double t,uint16_t paddle_hit,uint16_t countdown,uint16_t level) {
        const float height=118-(raw.x+5)/2-(raw.sy+10);
        if(raw.sprite!=0x0c || height<0 || height>8 || live.peek(countdown)>1 || live.peek(0x807a) || live.peek(0x809a)) { clear();return; }
        BBObject old{};bool had=at(t,old);
        if(!history_.empty() && (std::abs(raw.x-history_.back().ball.x)>24 || std::abs(raw.z-history_.back().ball.z)>24 || t-history_.back().t>0.3)) clear(),had=false;
        bool hit=contact_t_>consumed_contact_ && contact_t_<=t;
        auto append=[](auto& list,Point p) {
            if(!list.empty() && p.t<=list.back().t) {list.back()=p;return;}
            if(list.empty() || p.anchor || p.ball.x!=list.back().ball.x || p.ball.z!=list.back().ball.z) list.push_back(p);
        };
        const auto tick = live.cpu_ticks();
        const double hz = live.model()==128 ? 3546894.0 : 3500000.0;
        auto physical=[&](int x,int z) {
            BBObject b=raw;
            b.x=float(x)-5; b.z=float(z)+5;
            // 886C/88CF projection, retaining the half pixel discarded by SRL.
            // Reusing the sprite's rounded Y would add a wobble on odd X values.
            b.sx=148.0f+x-z; b.sy=104.0f-x/2.0f;
            return b;
        };
        for(const auto& step:pending_)
            append(history_,{t-double(tick-step.tick)/hz,physical(step.x,step.z),false});
        pending_.clear();
        if(hit) {
            BBObject contact=raw;float dx=contact_x_-raw.x,dz=contact_z_-raw.z;
            contact.x+=dx;contact.z+=dz;contact.sx+=dx-dz;contact.sy-=dx/2;
            append(history_,{t,contact,true});consumed_contact_=contact_t_;had=false;
        } else if(history_.empty())
            append(history_,{t,physical(live.peek(0x8064),live.peek(0x8065)),false});
        while(history_.size()>3 && history_[1].t<t-0.6) history_.pop_front();
        path_.assign(history_.begin(),history_.end());
        auto forecast=Spectrum::from_state(live.save_state());
        if(!forecast) {path_.clear();return;}
        // No game adapter on the clone: no external events, counters, or progression.
        const uint64_t forecast_tick=forecast->cpu_ticks();
        bool stopped=false;
        const int dx=int8_t(live.peek(0x8067)), dz=int8_t(live.peek(0x8066));
        forecast->on_exec(paddle_hit,[&](const zxc_regs_t&) {
            if(stopped) return;
            // The native collision test accepts a centre already past the face.
            // End this conditional approach on the face instead of publishing
            // those penetrating microsteps. No outgoing step or event is shown.
            const float face=float(forecast->peek(0x8062))+5;
            // New input can turn a predicted miss into a catch. If the picture
            // already passed the face, do not pull it back on speculation; only
            // the live contact may correct that now-invalid approach.
            if(had && old.z<face) { stopped=true; return; }
            while(!path_.empty() && !path_.back().anchor && path_.back().ball.z<=face)
                path_.pop_back();
            append(path_,{t+double(forecast->cpu_ticks()-forecast_tick)/hz,
                          physical(forecast->peek(0x8064),forecast->peek(0x8062)),false});
            stopped=true;
        });
        forecast->on_exec(0x886b,[&](const zxc_regs_t&) {
            if(stopped) return;
            if(forecast->peek(0x807a) || forecast->peek(0x809a) ||
               int8_t(forecast->peek(0x8067))!=dx || int8_t(forecast->peek(0x8066))!=dz) {
                stopped=true; return;
            }
            append(path_,{t+double(forecast->cpu_ticks()-forecast_tick)/hz,
                          physical(forecast->peek(0x8064),forecast->peek(0x8065)),false});
        });
        int original_level=live.peek(level);
        for(int frame=1;frame<=int(HORIZON*Spectrum::FRAME_HZ);++frame) {
            forecast->run_frame();
            if(stopped || forecast->peek(level)!=original_level || forecast->peek(countdown)>1) break;
        }
        retime(path_);
        updated_=t;offset_x_=offset_z_=0;
        if(had && !path_.empty()) {
            BBObject fresh=interpolate(path_,t);
            offset_x_=old.x-fresh.x;offset_z_=old.z-fresh.z;
            // Large discontinuities are transitions, not errors to drag through walls.
            if(std::hypot(offset_x_,offset_z_)>4) offset_x_=offset_z_=0;
        }
    }
private:
    struct Microstep { uint64_t tick; int x,z; };
    std::vector<Microstep> pending_;
    std::deque<Point> history_;
    std::vector<Point> path_;
    double updated_=0,contact_t_=-10,consumed_contact_=-10;
    float contact_x_=0,contact_z_=0,offset_x_=0,offset_z_=0;
};
