#include "controller.h"
#include "terrain.h"
#include <cstring>

namespace fpv {
// Sampler signature/track-slot layout and UI-page layout were independently matched
// against this project's runtime .text capture. Frostn1/FrostMod documented the same
// beta21e interfaces (MIT; included in third-party notices).
namespace {
using SampleFn=int(*)(const void*,float*,float*,float,float);
constexpr std::uintptr_t SamplerRva=0x1f1720,SlotsRva=0x568488;
bool HeaderAndQuery(std::uintptr_t base,int slot,float x,float z,float* y,Vec3* normal) {
    __try {
        auto track=reinterpret_cast<const unsigned char*>(base+SlotsRva+slot*0x6160);
        int cols{},rows{};const void* grid{};float sx{},sz{},ox{},oz{};
        std::memcpy(&cols,track+0x740,4);std::memcpy(&rows,track+0x744,4);std::memcpy(&grid,track+0x750,8);
        std::memcpy(&sx,track+0x758,4);std::memcpy(&sz,track+0x75c,4);std::memcpy(&ox,track+0x764,4);std::memcpy(&oz,track+0x76c,4);
        if(!grid||cols<2||rows<2||cols>65536||rows>65536||static_cast<int64_t>(cols)*rows>1073741824)return false;
        if(!std::isfinite(sx)||!std::isfinite(sz)||!std::isfinite(ox)||!std::isfinite(oz)||sx<1||sz<1||sx>20000||sz>20000)return false;
        if(x<ox||z<oz||x>ox+sx||z>oz+sz)return false;
        float n[3]{};
        if(!reinterpret_cast<SampleFn>(base+SamplerRva)(track,y,n,x,z))return false;
        if(!std::isfinite(*y)||!std::isfinite(n[0])||!std::isfinite(n[1])||!std::isfinite(n[2]))return false;
        *normal=Unit({n[0],n[1],n[2]});if(normal->y<0)*normal=*normal*-1;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool IsSettingsPage(std::uintptr_t base) {
    __try {
        int layer=*reinterpret_cast<const int*>(base+0x4cf9a8);
        if(layer<1||layer>16)return false;
        auto stack=base+0xe55ca4+static_cast<std::uintptr_t>(layer-1)*0x254;
        int count=*reinterpret_cast<const int*>(stack);if(count<0||count>4)return false;
        for(int i=0;i<count;i++) {
            int page=*reinterpret_cast<const int*>(stack+4+i*4);if(page<1||page>34)return false;
            const char* name=*reinterpret_cast<const char* const*>(base+0x3954f0+static_cast<std::uintptr_t>(page-1)*0x28);
            if(!name)continue;
            // Bounded comparison, never a scan of a foreign pointer.
            char text[17]{};for(int j=0;j<16&&name[j];j++)text[j]=name[j];
            if(std::strcmp(text,"settings")==0||std::strcmp(text,"options")==0)return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
bool Terrain::Initialize(std::uintptr_t base) {
    base_=base;
    const unsigned char signature[]={0x4c,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x53,0x48,0x81,0xec,0x40,0x01,0,0,0x4c,0x8b,0x89,0x50,0x07,0,0,0x48,0x8b,0xd9,0x4d,0x85,0xc9};
    ready_=std::memcmp(reinterpret_cast<void*>(base+SamplerRva),signature,sizeof(signature))==0;return ready_;
}
bool Terrain::Sample(float x,float z,float& height,Vec3& normal) const {
    if(!ready_||!std::isfinite(x)||!std::isfinite(z))return false;
    for(int slot=0;slot<3;slot++)if(HeaderAndQuery(base_,slot,x,z,&height,&normal))return true;
    return false;
}
bool Terrain::Sweep(void* context,Vec3 from,Vec3 to,float radius,Contact& contact) {
    auto terrain=static_cast<Terrain*>(context);
    int steps=std::clamp(static_cast<int>(std::ceil(Length(to-from)/std::max(0.025f,radius*0.5f))),1,64);
    for(int i=0;i<=steps;i++) {
        Vec3 point=from+(to-from)*(static_cast<float>(i)/steps);float height{};Vec3 normal;
        if(!terrain->Sample(point.x,point.z,height,normal))continue;
        if((point.y-height)*std::max(normal.y,0.05f)<=radius) {
            contact.point={point.x,height,point.z};contact.normal=normal;return true;
        }
    }
    return false;
}
bool GameSettingsPage(std::uintptr_t base) {return IsSettingsPage(base);}
}
