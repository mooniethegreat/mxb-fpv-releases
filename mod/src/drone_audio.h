#pragma once
#include "peer_sync.h"
#include <cstdint>
namespace fpv {
struct AudioSettings {bool enabled{true},remote{true};float volume{0.7f},remoteVolume{1},range{180};};
// Runs AL operations on the native mixer callback, in the game's existing context.
class DroneAudio {
    struct Impl;Impl* impl_;
public:
    DroneAudio();~DroneAudio();
    void Load(std::uintptr_t module);
    void Submit(const PeerState&,const std::vector<PeerState>&,const AudioSettings&,bool onTrack);
    void Process();void Close();
    const char* Status() const;
};
}
