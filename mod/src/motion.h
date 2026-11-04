#pragma once
#include "flight.h"
#include <cstdint>
#include <deque>
namespace fpv {
inline Quat BlendRotation(Quat a,Quat b,float t) {
    float dot=a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;
    if(dot<0){b={-b.w,-b.x,-b.y,-b.z};dot=-dot;}
    if(dot>0.9995f)return Quat{a.w+(b.w-a.w)*t,a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t}.Normalized();
    float angle=std::acos(std::clamp(dot,0.0f,1.0f)),den=std::sin(angle),x=std::sin((1-t)*angle)/den,y=std::sin(t*angle)/den;
    return Quat{a.w*x+b.w*y,a.x*x+b.x*y,a.y*x+b.y*y,a.z*x+b.z*y}.Normalized();
}
// Arrival-clock interpolation: no remote clock assumptions. Resets on teleports,
// pauses and respawns. Prediction is bounded so packet loss cannot run away.
struct MotionHistory {
    struct Sample {std::uint64_t time{};Vec3 position{},velocity{};Quat rotation{};};
    std::deque<Sample> samples;
    void Push(Sample sample) {
        if(!samples.empty()){
            auto& last=samples.back();auto gap=sample.time-last.time;
            if(sample.time<last.time||gap>1000||Length(sample.position-last.position)>std::max(8.0f,Length(last.velocity)*gap*0.002f+3))samples.clear();
            else if(sample.time==last.time){samples.back()=sample;return;}
        }
        samples.push_back(sample);while(samples.size()>32)samples.pop_front();
    }
    Sample At(std::uint64_t now,int delay) const {
        if(samples.empty())return {};
        auto target=now>static_cast<unsigned>(delay)?now-delay:0;
        if(target<=samples.front().time)return samples.front();
        for(size_t i=1;i<samples.size();i++)if(target<=samples[i].time){
            auto& a=samples[i-1];auto& b=samples[i];float span=static_cast<float>(b.time-a.time),t=(target-a.time)/span;
            Sample out=b;out.position=a.position+(b.position-a.position)*t;out.velocity=a.velocity+(b.velocity-a.velocity)*t;out.rotation=BlendRotation(a.rotation,b.rotation,t);return out;
        }
        auto out=samples.back();float ahead=std::min(static_cast<float>(target-out.time),75.0f)*0.001f;out.position=out.position+out.velocity*ahead;return out;
    }
};
}
