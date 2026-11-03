#pragma once
#include "flight.h"
#include <cstdint>
namespace fpv {
class NativeView {
public:
    bool Initialize(std::uintptr_t base);
    bool InPits() const;
    bool InLiveView() const;
    bool InRidingView() const;
    bool Enter();
    void Leave();
    void Forget() { owned_=false; }
    void HideReplayControls() const;
    int Camera() const;
    int Connection() const;
    float GateButtonY() const;
    bool RaceInfoTab() const;
private:
    bool Page(int id) const;
    std::uintptr_t base_{};
    bool ready_{},owned_{};
};
}
