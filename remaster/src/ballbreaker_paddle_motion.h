#pragma once

#include <algorithm>
#include <cmath>
#include <deque>

// BB1 presentation only. Average a short, confirmed RAM trajectory; never
// extrapolate beyond it. One simulation frame of buffering makes interpolation
// available throughout the next display frame. The 40 ms averaging window adds
// another 20 ms of nominal visual lag, with a 60 ms stop-settling bound.
class BBPaddleMotion {
public:
    static constexpr double BUFFER = 0.02, WINDOW = 0.04;
    void clear() { samples_.clear(); }
    bool valid() const { return !samples_.empty(); }
    float at(double t) const {
        if(samples_.empty()) return 0;
        double end=t-BUFFER, start=end-WINDOW, cursor=start, area=0;
        float value=position(start);
        // Integrate the piecewise-linear confirmed trajectory exactly, including
        // stationary tails. Its average cannot overshoot a monotone movement.
        for(const auto& s:samples_) {
            if(s.t<=start || s.t>=end) continue;
            area+=(value+s.x)*0.5*(s.t-cursor);
            cursor=s.t;value=s.x;
        }
        area+=(value+position(end))*0.5*(end-cursor);
        return float(area/WINDOW);
    }
    void sample(float raw, float width, double t, bool contact) {
        if(!valid() || width!=width_ || std::abs(raw-samples_.back().x)>24 ||
           t-samples_.back().t>0.1 || contact) clear();
        width_=width;
        if(!samples_.empty() && t<=samples_.back().t) samples_.back()={t,raw};
        else samples_.push_back({t,raw});
        while(samples_.size()>2 && samples_[1].t<t-BUFFER-WINDOW) samples_.pop_front();
    }
private:
    struct Sample { double t; float x; };
    std::deque<Sample> samples_;
    float width_=0;
    float position(double t) const {
        const Sample* left=&samples_.front();
        if(t<=left->t)return left->x;
        for(const auto& right:samples_) {
            if(right.t>=t) {
                double k=(t-left->t)/(right.t-left->t);
                return left->x+float(k)*(right.x-left->x);
            }
            left=&right;
        }
        return samples_.back().x;
    }
};
