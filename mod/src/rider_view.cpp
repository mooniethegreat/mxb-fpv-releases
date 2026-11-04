#include "controller.h"
#include "rider_view.h"
#include <cstring>
#include <new>
#include <limits>
namespace fpv {
namespace {
bool Matrix(const float* m){
    for(int i=0;i<16;i++)if(!std::isfinite(m[i])||std::abs(m[i])>1000000)return false;
    return std::abs(m[12])+std::abs(m[13])+std::abs(m[14])<0.001f&&std::abs(m[15]-1)<0.001f&&
        std::abs(Length({m[0],m[4],m[8]})-1)<0.05f&&std::abs(Length({m[1],m[5],m[9]})-1)<0.05f;
}
bool Read(std::uintptr_t base,void* graphics,void* instance,const void* state,int* index,int* handle,void* copy){
    __try{
        auto p=reinterpret_cast<std::uintptr_t>(graphics),first=base+0xf4ee20+0x274;
        if(graphics!=instance||p<first||(p-first)%0x5b24)return false;
        *index=static_cast<int>((p-first)/0x5b24);if(*index>=50||*index==*reinterpret_cast<const int*>(base+0xe62710)-1)return false;
        auto record=p-0x274;if(!*reinterpret_cast<const int*>(record)||!*static_cast<const int*>(state))return false;
        *handle=*reinterpret_cast<const int*>(record+4);if(*handle<=0)return false;
        std::memcpy(copy,state,0x760);return Matrix(reinterpret_cast<const float*>(static_cast<const unsigned char*>(state)+4));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool RiderView::Initialize(std::uintptr_t base){
    // FrostMod's documented snapshot-gap site, independently checked in the pinned snapshot.
    constexpr unsigned char expected[]={0x66,0x44,0x0f,0x2f,0x05,0xbc,0x94,0x0a,0x00};
    site_=base+0x2aa8cb;if(std::memcmp(reinterpret_cast<void*>(site_),expected,9)){site_=0;return false;}
    SYSTEM_INFO info{};GetSystemInfo(&info);auto step=static_cast<std::uintptr_t>(info.dwAllocationGranularity),anchor=site_&~(step-1);
    void* page{};
    for(std::uintptr_t distance=step;distance<0x7ffe0000&&!page;distance+=step)for(int sign:{1,-1}){
        auto address=sign>0?anchor+distance:anchor-distance;MEMORY_BASIC_INFORMATION region{};
        if(VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))&&region.State==MEM_FREE&&region.RegionSize>=info.dwPageSize)
            page=VirtualAlloc(reinterpret_cast<void*>(address),info.dwPageSize,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(page)break;
    }
    if(!page){site_=0;return false;}
    static_assert(sizeof(std::atomic<double>)==8,"Native comparison requires an aligned eight-byte value");
    limit_=new(page)std::atomic<double>(0.3);auto delta=reinterpret_cast<std::intptr_t>(page)-static_cast<std::intptr_t>(site_+9);
    if(delta<INT32_MIN||delta>INT32_MAX){VirtualFree(page,0,MEM_RELEASE);limit_=nullptr;site_=0;return false;}
    std::memcpy(original_.data(),expected,9);patch_=original_;auto displacement=static_cast<std::int32_t>(delta);std::memcpy(patch_.data()+5,&displacement,4);
    DWORD protection{};if(!VirtualProtect(reinterpret_cast<void*>(site_),9,PAGE_EXECUTE_READWRITE,&protection)){VirtualFree(page,0,MEM_RELEASE);limit_=nullptr;site_=0;return false;}
    std::memcpy(reinterpret_cast<void*>(site_),patch_.data(),9);DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(site_),9,protection,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site_),9);return true;
}
void RiderView::Active(bool flying,bool widen){if(limit_)limit_->store(flying&&widen?2.0:0.3,std::memory_order_relaxed);if(!flying)riders_={};}
void RiderView::Close(){
    if(!limit_)return;limit_->store(0.3,std::memory_order_relaxed);
    if(site_&&!std::memcmp(reinterpret_cast<void*>(site_),patch_.data(),9)){
        DWORD protection{};if(!VirtualProtect(reinterpret_cast<void*>(site_),9,PAGE_EXECUTE_READWRITE,&protection))return;
        std::memcpy(reinterpret_cast<void*>(site_),original_.data(),9);DWORD ignored{};VirtualProtect(reinterpret_cast<void*>(site_),9,protection,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site_),9);
        VirtualFree(limit_,0,MEM_RELEASE);
    } // If another hook took ownership, do not free memory its trampoline may reference.
    limit_=nullptr;site_=0;
}
bool RiderView::Smooth(std::uintptr_t base,void* graphics,void* instance,const void* state,int milliseconds,void* copy){
    if(milliseconds<=0)return false;int index{},handle{};if(!Read(base,graphics,instance,state,&index,&handle,copy))return false;
    auto root=reinterpret_cast<float*>(static_cast<unsigned char*>(copy)+4);Vec3 raw{root[3],root[7],root[11]};auto now=GetTickCount64();auto& rider=riders_[index];
    float dt=rider.time?std::clamp((now-rider.time)*0.001f,0.0f,0.5f):0;
    if(rider.handle!=handle||!rider.time||now-rider.time>500||Length(raw-rider.raw)>12){rider={handle,now,raw,raw,{}};return true;}
    if(dt>0){
        float blend=1-std::exp(-dt/(std::clamp(milliseconds,1,120)*0.001f));rider.position=rider.position+(raw-rider.position)*blend;
        auto delta=rider.position-raw;float error=Length(delta);if(error>2.5f)rider.position=raw+delta*(2.5f/error);
        rider.raw=raw;rider.time=now;
    }
    Vec3 delta=rider.position-raw;
    // World transforms read by rider_gfx's native draw path. Relative joint,
    // suspension, wheel spin and animation values retain native behavior.
    for(int offset:{0x4,0xbc,0xfc,0x1c4,0x28c,0x2dc,0x330,0x370,0x438}){
        auto matrix=reinterpret_cast<float*>(static_cast<unsigned char*>(copy)+offset);if(!Matrix(matrix))continue;
        matrix[3]+=delta.x;matrix[7]+=delta.y;matrix[11]+=delta.z;
    }
    return true;
}
}
