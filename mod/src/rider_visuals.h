#pragma once
#include "peer_sync.h"
#include <array>
#include <cstdint>
#include <vector>
namespace fpv {
struct VisualSettings {bool goggles{true},controller{true},pose{true},drones{true};float controllerDown{0.48f},controllerForward{0.3f},gogglesForward{0.10f};};
class RiderVisuals {
    struct Impl;Impl* impl_{};
public:
    RiderVisuals();~RiderVisuals();
    void Configure(std::uintptr_t base,const VisualSettings& settings);
    void States(int ownRace,bool ownActive,const std::vector<PeerState>& peers);
    void Rider(void* graphics,int* instance,void* physics);
    void Camera(const float* matrix,float fov);
    void Draw(int width,int height);
    int OwnRace() const;
};
}
