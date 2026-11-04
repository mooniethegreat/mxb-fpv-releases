#pragma once
#include "flight.h"
#include <array>
#include <atomic>
#include <cstdint>
namespace fpv {
class RiderView {
public:
    bool Initialize(std::uintptr_t base);
    void Active(bool flying,bool widen);
    void Close();
    // A private render copy: never changes simulation, network snapshots or bike controls.
    bool Smooth(std::uintptr_t base,void* graphics,void* instance,const void* state,int milliseconds,void* copy);
private:
    struct Rider {int handle{};std::uint64_t time{};Vec3 raw{},position{},velocity{};};
    std::array<Rider,50> riders_{};
    std::uintptr_t site_{};
    std::atomic<double>* limit_{};
    std::array<unsigned char,9> original_{},patch_{};
};
}
