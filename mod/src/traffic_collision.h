#pragma once
#include "terrain.h"
#include <vector>
namespace fpv {
class TrafficCollision {
    struct Capsule {Vec3 a,b,velocity;float radius;};
    std::vector<Capsule> capsules_;
    std::array<Vec3,50> previous_{};std::array<int,50> keys_{};
    float dt_{0.01f};CollisionWorld terrain_{};
    static bool Sweep(void*,Vec3,Vec3,float,Contact&);
public:
    void Capture(std::uintptr_t base,float dt,bool enabled,CollisionWorld terrain);
    CollisionWorld World(){return {this,Sweep};}
};
}
