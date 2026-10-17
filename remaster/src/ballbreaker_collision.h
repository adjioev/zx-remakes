#pragma once
#include "ballbreaker_paddle.h"
#include <algorithm>
#include <cmath>
#include <limits>

struct BBPaddleSweep {
    bool hit = false;
    float fraction = 1, x = 0, z = 0, nx = 0, nz = 1;
};
// The sphere centre Y=4 is inside the paddle's vertical extent. Its horizontal
// cross-section is therefore a radius-6 circle against the exact rounded box,
// not the square corners of an expanded AABB. paddle_dx is a translating body.
inline BBPaddleSweep bb_sweep_paddle(float x, float z, float end_x, float end_z,
                                    BBPaddleBody body, float paddle_dx = 0) {
    BBPaddleSweep best;
    const float dy = std::max({0.f, -4.f, 4.f-body.height});
    if (dy >= BB_BALL_RADIUS) return best;
    const float radius = std::sqrt(BB_BALL_RADIUS*BB_BALL_RADIUS-dy*dy);
    const float vx = end_x-x-paddle_dx, vz = end_z-z;
    auto accept = [&](float t,float nx,float nz) {
        if(t < -1e-5f || t > 1+1e-5f || (best.hit && t>=best.fraction))return;
        if(vx*nx+vz*nz >= -1e-5f)return;
        best={true,std::clamp(t,0.f,1.f),x+(end_x-x)*t,z+(end_z-z)*t,nx,nz};
    };
    // Recover existing overlap only when approaching, never bounce a separating
    // tangent again. Position resolution is applied by the caller.
    float cx=std::clamp(x,body.x0,body.x1),cz=std::clamp(z,body.z0,body.z1);
    float ox=x-cx,oz=z-cz,dist=std::hypot(ox,oz);
    if(dist>1e-5f && dist<radius-1e-5f)accept(0,ox/dist,oz/dist);
    if(dist<=1e-5f) {
        float distances[]={x-body.x0,body.x1-x,z-body.z0,body.z1-z};
        int side=int(std::min_element(distances,distances+4)-distances);
        const float nx[]={-1,1,0,0},nz[]={0,0,-1,1};accept(0,nx[side],nz[side]);
    }
    if(std::abs(vx)>1e-6f)for(int side=0;side<2;++side) {
        float bx=side?body.x1+radius:body.x0-radius,t=(bx-x)/vx;
        float at=z+vz*t;if(at>=body.z0 && at<=body.z1)accept(t,side?1.f:-1.f,0);
    }
    if(std::abs(vz)>1e-6f)for(int side=0;side<2;++side) {
        float bz=side?body.z1+radius:body.z0-radius,t=(bz-z)/vz;
        float at=x+vx*t;if(at>=body.x0 && at<=body.x1)accept(t,0,side?1.f:-1.f);
    }
    float a=vx*vx+vz*vz;
    if(a>1e-8f)for(int sx=0;sx<2;++sx)for(int sz=0;sz<2;++sz) {
        float qx=sx?body.x1:body.x0,qz=sz?body.z1:body.z0;
        float dx=x-qx,dz=z-qz,b=2*(dx*vx+dz*vz),c=dx*dx+dz*dz-radius*radius;
        float disc=b*b-4*a*c;if(disc<0)continue;
        float t=(-b-std::sqrt(disc))/(2*a),px=x+vx*t-qx,pz=z+vz*t-qz;
        if((sx?px>=-1e-5f:px<=1e-5f) && (sz?pz>=-1e-5f:pz<=1e-5f))accept(t,px/radius,pz/radius);
    }
    return best;
}
