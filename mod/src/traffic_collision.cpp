#include "controller.h"
#include "traffic_collision.h"
#include <cstring>
namespace fpv {
namespace {
bool ReadBike(std::uintptr_t base,int index,Vec3* position,Vec3* forward,Vec3* up,Vec3* head,int* key) {
    __try {
        auto record=base+0xf4ee20+index*0x5b24;if(!*reinterpret_cast<int*>(record))return false;
        // The same render-physics record passed to the native riding camera.
        auto p=reinterpret_cast<const float*>(record+0x5054+4);
        *position={p[3],p[7],p[11]};*forward={p[2],p[6],p[10]};*up={p[1],p[5],p[9]};
        auto h=reinterpret_cast<const float*>(record+0x5054+0x544);*head={h[3],h[7],h[11]};
        *key=*reinterpret_cast<const int*>(record+0xc)+1;
        return std::isfinite(position->x)&&std::isfinite(position->y)&&std::isfinite(position->z)&&
            Length(*forward)>0.8f&&Length(*forward)<1.2f&&Length(*up)>0.8f&&Length(*up)<1.2f;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Vec3 Closest(Vec3 a,Vec3 b,Vec3 p){Vec3 ab=b-a;return a+ab*std::clamp(Dot(p-a,ab)/std::max(0.0001f,Dot(ab,ab)),0.0f,1.0f);}
}
void TrafficCollision::Capture(std::uintptr_t base,float dt,bool enabled,CollisionWorld terrain) {
    terrain_=terrain;dt_=std::clamp(dt,0.001f,0.05f);capsules_.clear();if(!enabled)return;
    for(int i=0;i<50;i++){
        Vec3 p,f,u,h;int key{};if(!ReadBike(base,i,&p,&f,&u,&h,&key)){keys_[i]=0;continue;}
        Vec3 v=keys_[i]==key?(p-previous_[i])*(1/dt_):Vec3{};if(Length(v)>150)v={};keys_[i]=key;previous_[i]=p;
        f=Unit(f);u=Unit(u);
        // Conservative frame/wheel volume plus torso/head capsule, including own bike.
        capsules_.push_back({p-f*0.85f+u*0.30f,p+f*0.85f+u*0.30f,v,0.31f});
        Vec3 front=p+f*0.73f-u*0.18f,rear=p-f*0.73f-u*0.18f;
        capsules_.push_back({front,front,v,0.36f});capsules_.push_back({rear,rear,v,0.36f});
        if(!std::isfinite(h.x)||!std::isfinite(h.y)||!std::isfinite(h.z)||Length(h-p)>2.8f||Length(h-p)<0.4f)h=p+u*1.5f;
        capsules_.push_back({p+u*0.65f,h,v,0.27f});
    }
}
bool TrafficCollision::Sweep(void* context,Vec3 from,Vec3 to,float radius,Contact& contact) {
    auto self=static_cast<TrafficCollision*>(context);bool hit=false;float best=2;
    Contact ground;if(self->terrain_.sweep&&self->terrain_.sweep(self->terrain_.context,from,to,radius,ground)){
        contact=ground;hit=true;best=std::clamp(Dot(ground.point+ground.normal*radius-from,to-from)/std::max(0.000001f,Dot(to-from,to-from)),0.0f,1.0f);
    }
    for(const auto& c:self->capsules_){
        // Relative sweep handles a moving bike crossing a slow or hovering drone.
        Vec3 relativeEnd=to-c.velocity*0.004f;float reach=radius+c.radius;
        if(Length(Closest(c.a,c.b,from)-from)>Length(relativeEnd-from)+reach)continue;
        int steps=std::clamp(static_cast<int>(std::ceil(Length(relativeEnd-from)/std::max(0.025f,reach*0.3f))),1,128);
        for(int i=0;i<=steps;i++){float t=static_cast<float>(i)/steps;if(t>best)break;
            Vec3 p=from+(relativeEnd-from)*t,nearest=Closest(c.a,c.b,p);Vec3 delta=p-nearest;
            if(Dot(delta,delta)>reach*reach)continue;
            Vec3 normal=Length(delta)>0.001f?Unit(delta):Unit(from-relativeEnd+Vec3{0,0.01f,0});
            contact={nearest+c.velocity*(t*0.004f)+normal*c.radius,normal,c.velocity,true};best=t;hit=true;break;
        }
    }return hit;
}
}
