#pragma once
#include "peer_sync.h"
#include <array>
#include <filesystem>
#include <mutex>
#include <vector>
namespace fpv {
// Official MX Bikes data v8 layouts. No private race-memory writes.
struct TrackSegment {int type{};float length{},radius{},angle{},start[2]{},height{};};
struct RacePoint {int race{};float x{},y{},z{},yaw{},trackPosition{};int crashed{};};
struct HudQuad {float position[4][2];int sprite;unsigned long color;};
struct HudString {char text[100];float position[2];int font;float size;int justify;unsigned long color;};
static_assert(sizeof(TrackSegment)==28&&sizeof(RacePoint)==28&&sizeof(HudQuad)==40&&sizeof(HudString)==124,"SDK layout mismatch");
struct MapSettings {bool enabled{true},external{true};float size{280};int key{'M'};};
class MapHud {
public:
    void Reset(bool track);
    void Centerline(int count,const TrackSegment* segments);
    void Positions(int count,const void* data,int stride);
    void Remove(int race);
    bool SpawnPoint(Vec3& position) const;
    void Configure(std::filesystem::path maxHudIni);
    void External(int state,int* quads,void** quadData,int* strings,void** stringData,bool hideBike);
    void Draw(const MapSettings&,bool visible,const PeerState& own,const std::vector<PeerState>& peers);
private:
    struct Rider {RacePoint point;std::uint64_t seen{};};
    struct Rect {float x{},y{},right{},bottom{};bool valid{};};
    struct Projection {float a{},b{},tx{},ty{};std::uint64_t seen{};bool valid{};};
    void ReadLayout(int state);
    mutable std::mutex mutex_;
    std::vector<Vec3> line_;
    std::vector<Rider> riders_;
    std::array<Rect,5> layout_{};
    std::filesystem::path maxHudIni_;
    std::uint64_t layoutTick_{},externalTick_{};
    int layoutState_{-1};
    Rect externalRect_;
    Projection projection_;
    int projectionFlip_{1};
    std::vector<HudQuad> quads_;
    std::vector<HudString> strings_;
};
}
