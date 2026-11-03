#pragma once
#include "flight.h"
#include <string>
#include <vector>
#include <cstdint>
namespace fpv {
struct SyncSettings {bool enabled{true},host{};std::string address{"127.0.0.1"},room{"mx-fpv"};int port{48127},transport{};}; // 0 automatic native game server, 1 manual UDP
struct PeerState {int race{};bool flying{};Vec3 position{};Quat rotation{};int preset{};std::uint64_t received{};std::uint64_t id{};Vec3 velocity{};float motor{},activity{},propInches{5};};
class PeerSync {
    struct Impl;Impl* impl_{};
public:
    PeerSync();~PeerSync();
    bool Configure(const SyncSettings&);
    void BindNative(std::uintptr_t base,bool available);
    void PumpNative(); // Called only on MX Bikes' own networking update thread.
    void ResetNative();
    void ReceiveNative(int connection,const void* data,int bytes);
    static constexpr int NativeMessageType=0x46505650;
    void Tick(const PeerState& own);
    std::vector<PeerState> Peers() const;
    std::string Status() const;
    void Close();
};
}
