#include "controller.h"
#include "native_view.h"
#include <cstring>
namespace fpv {
bool NativeView::Initialize(std::uintptr_t base) {
    base_=base;
    constexpr unsigned char push[]={0x40,0x53,0x48,0x83,0xec,0x30,0x83,0x3d,0x6b,0x36,0x3e,0,1,0x8b,0xda,0x7d};
    constexpr unsigned char pop[]={0x40,0x57,0x48,0x83,0xec,0x30,0x8b,0x3d,0x5c,0x33,0x3e,0,0x83,0xff,1,0x7d};
    constexpr unsigned char replay[]={0x48,0x89,0x5c,0x24,0x18,0x56,0x57,0x41,0x55,0x48,0x81,0xec,0x70,1,0,0};
    ready_=!std::memcmp(reinterpret_cast<void*>(base+0xec330),push,sizeof(push))&&
        !std::memcmp(reinterpret_cast<void*>(base+0xec640),pop,sizeof(pop))&&
        !std::memcmp(reinterpret_cast<void*>(base+0xc30f0),replay,sizeof(replay));return ready_;
}
bool NativeView::Page(int id) const {
    __try {
        if(!ready_)return false;
        int layer=*reinterpret_cast<const int*>(base_+0x4cf9a8);if(layer<1||layer>16)return false;
        auto stack=base_+0xe55ca4+static_cast<std::uintptr_t>(layer-1)*0x254;
        int depth=*reinterpret_cast<const int*>(stack);if(depth<1||depth>4)return false;
        for(int i=0;i<depth;i++)if(*reinterpret_cast<const int*>(stack+4+i*4)==id)return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){}return false;
}
bool NativeView::InPits() const {return Page(4)&&Page(17)&&!Page(21)&&!Page(24)&&!Page(25);}
bool NativeView::InLiveView() const {
    __try {return Page(21)&&*reinterpret_cast<const int*>(base_+0x4c9194)==3;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool NativeView::InRidingView() const {return Page(24)||Page(25);}
bool NativeView::Enter() {
    if(!InPits())return false;
    // This is the stock pit screen's live race-view action, not To Track or Join.
    using Push=std::int64_t(*)(const char*,int);
    auto result=reinterpret_cast<Push>(base_+0xec330)("chat/replay",3);
    owned_=result==0&&InLiveView();if(owned_)HideReplayControls();return owned_;
}
void NativeView::Leave() {
    bool restore=owned_&&InLiveView();owned_=false;
    if(restore)reinterpret_cast<std::int64_t(*)()>(base_+0xec640)();
}
void NativeView::HideReplayControls() const {
    if(!InLiveView())return;
    using Bus=std::int64_t(*)(unsigned,...);auto bus=*reinterpret_cast<Bus*>(base_+0x566c48);
    if(!bus)return;
    // Native live state and camera remain active; only replay tool windows close.
    for(auto rva:{0x4ca29cu,0x4ca274u,0x4ca27cu,0x4c91d0u,0x4ca2acu}) {
        int window=*reinterpret_cast<const int*>(base_+rva);if(window>0)bus(0x107,window);
    }
}
int NativeView::Camera() const {
    __try {return InLiveView()?*reinterpret_cast<const int*>(base_+0x4c9190):0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
int NativeView::Connection() const {
    __try {if(*reinterpret_cast<const int*>(base_+0x598904)!=7)return 0;int id=*reinterpret_cast<const int*>(base_+0x598908);return id>=0&&id<=255?id:0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
float NativeView::GateButtonY() const {return *reinterpret_cast<const int*>(base_+0x50ff28)?0.5f:0.533333f;}
bool NativeView::RaceInfoTab() const {return *reinterpret_cast<const int*>(base_+0x4c9160)==0;}
}
