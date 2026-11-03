#pragma once
#include "flight.h"
#include <cstdint>
namespace fpv {
// Build-pinned, read-only adapter to MX Bikes' loaded heightfield. No track files are extracted.
class Terrain {
    std::uintptr_t base_{};
    bool ready_{};
    static bool Sweep(void*,Vec3,Vec3,float,Contact&);
public:
    bool Initialize(std::uintptr_t base);
    bool Sample(float x,float z,float& height,Vec3& normal) const;
    bool Ready() const {return ready_;}
    CollisionWorld World() {return {this,Sweep};}
};
bool GameSettingsPage(std::uintptr_t base);
}
