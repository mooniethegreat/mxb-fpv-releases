#include "../src/flight.h"
#include <cstdio>
#include <cstdlib>
void Check(bool pass,const char* name) {if(!pass) {std::fprintf(stderr,"FAIL: %s\n",name);std::exit(1);}}
int main() {
    fpv::Flight f; fpv::FlightSettings s;
    f.Spawn({10,20,30},0);
    Check(std::abs(f.position.x-12)<0.001f && std::abs(f.position.y-21.5f)<0.001f,"spawn beside rider");
    auto before=f.position;
    fpv::Input hover{s.HoverThrottle(),0,0,0,true,false};f.motorOutput=std::pow(hover.throttle,s.throttleCurve);
    for(int i=0;i<250;i++) f.Step(hover,s,0.004f);
    Check(std::abs(f.position.y-before.y)<0.001f,"level hover at calculated throttle");
    for(int i=0;i<250;i++) f.Step({},s,0.004f);
    Check(f.position.y<before.y-3,"gravity at zero throttle");
    Check(std::abs(fpv::Rate(0,s))<0.001f && std::abs(fpv::Rate(1,s)+fpv::Rate(-1,s))<0.001f,"symmetric rates");
    f.Spawn({},90);
    Check(std::abs(f.position.x)<0.001f && std::abs(f.position.z+2)<0.001f,"rotated spawn");
    fpv::Input spin{0.5f,0.5f,0.2f,-0.7f,true,false};
    for(int i=0;i<15000;i++) f.Step(spin,s,0.004f);
    Check(std::isfinite(f.position.x)&&std::abs(f.rotation.w*f.rotation.w+f.rotation.x*f.rotation.x+f.rotation.y*f.rotation.y+f.rotation.z*f.rotation.z-1)<0.001f,"finite normalized flight after 60 seconds");
    fpv::Flight punch;punch.Spawn({},0);float start=punch.position.y;
    for(int i=0;i<250;i++)punch.Step({1,0,0,0,true,false},s,0.004f);
    Check(punch.position.y-start>15,"6S freestyle thrust punch");
    auto presets=fpv::Presets();Check(presets[1].settings.ThrustToWeight()>presets[3].settings.ThrustToWeight()*2,"race/cine power differentiation");
    Check(presets[4].settings.propInches==2&&presets[4].settings.cells==3&&presets[5].settings.massGrams==795,"requested airframe profiles");
    fpv::CollisionWorld ground{nullptr,[](void*,fpv::Vec3 from,fpv::Vec3 to,float radius,fpv::Contact& hit){
        if(std::min(from.y,to.y)>radius)return false;hit.point={to.x,0,to.z};hit.normal={0,1,0};return true;
    }};
    fpv::Flight landing;landing.Spawn({},0);landing.position.y=8;landing.velocity={4,-20,0};
    for(int i=0;i<2500;i++){landing.Step({},s,0.004f,&ground);Check(landing.position.y>=s.radius-0.001f,"no ground penetration");}
    Check(landing.position.y<s.radius+0.02f&&fpv::Length(landing.velocity)<0.3f,"landing and ground friction");
    fpv::Flight impact;impact.position={0,0.5f,0};impact.velocity={0,-150,0};impact.Step({},s,0.004f,&ground);
    Check(impact.position.y>=s.radius&&impact.velocity.y>=0,"fast ground impact reflects velocity");
    auto prop=s;prop.propInches=6;Check(prop.MaxThrustNewtons()>s.MaxThrustNewtons(),"prop size affects thrust");
    std::puts("PASS: spawn, hover, gravity, rates, sustained flight, thrust punch, profiles, prop effects, collision, bounce, friction");
}
