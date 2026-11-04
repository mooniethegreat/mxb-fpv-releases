// Offline two-client regression. Runs the real PeerSync against private mock
// rosters/transport; never loads, attaches to or launches MX Bikes.
#include "../src/controller.h"
#include "../src/peer_sync.h"
#include "../src/motion.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace fpv;
namespace {
PeerSync* destinations[256]{};int source{};bool dropHellos{true};
void Require(bool value,const char* text){if(!value){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
std::int64_t Send(int type,const void* data,int bytes,int destination){
    unsigned kind{};if(bytes>=12)std::memcpy(&kind,static_cast<const unsigned char*>(data)+8,4);
    if(!(dropHellos&&kind<=2)&&destination>0&&destination<256&&destinations[destination])destinations[destination]->ReceiveNative(source,data,bytes);
    Require(type==PeerSync::NativeMessageType,"reserved native subtype");return 0;
}
struct Client {
    void* memory=VirtualAlloc(nullptr,0x5b0000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);PeerSync sync;int id;
    explicit Client(int own,int other):id(own){
        Require(memory!=nullptr,"private mock allocation");auto base=reinterpret_cast<std::uintptr_t>(memory);
        *reinterpret_cast<int*>(base+0x598904)=7;*reinterpret_cast<int*>(base+0x598908)=own;
        *reinterpret_cast<int*>(base+0x599044)=2;*reinterpret_cast<int*>(base+0x599048)=other;
        unsigned char stub[]={0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};auto target=reinterpret_cast<std::uintptr_t>(&Send);std::memcpy(stub+2,&target,8);
        DWORD old{};Require(VirtualProtect(reinterpret_cast<void*>(base+0x2a3000),0x1000,PAGE_EXECUTE_READWRITE,&old)!=0,"mock sender page");
        std::memcpy(reinterpret_cast<void*>(base+0x2a3920),stub,sizeof(stub));FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base+0x2a3920),sizeof(stub));
        SyncSettings settings;settings.smoothingMs=0;sync.Configure(settings);sync.BindNative(base,true);destinations[own]=&sync;
    }
    ~Client(){destinations[id]=nullptr;sync.Close();VirtualFree(memory,0,MEM_RELEASE);}
    void Pump(PeerState state){sync.Tick(state);source=id;sync.PumpNative();}
};
}
int main(){
    PeerState a;a.race=900001;a.flying=true;a.position={10,2,20};PeerState b=a;b.race=900200;b.position={11,2,20};
    Client first(1,200),second(200,1);
    // Simultaneous spectator entry, all hello/acks lost. State must still recover.
    first.Pump(a);second.Pump(b);first.Pump(a);second.Pump(b);
    Require(first.sync.Peers().size()==1&&second.sync.Peers().size()==1,"both pilots visible with lost handshake");
    Require(first.sync.Peers()[0].race==900200&&second.sync.Peers()[0].race==900001,"spectator identity and byte-sized connection");
    Require(first.sync.Peers()[0].flying&&Length(first.sync.Peers()[0].position-b.position)<0.01f,"remote flying pose");
    dropHellos=false;second.sync.ResetNative();second.Pump(b);first.Pump(a);second.Pump(b);first.Pump(a);
    Require(second.sync.Peers().size()==1,"world/view reset recovers current state");
    a.flying=false;first.Pump(a);Require(!second.sync.Peers()[0].flying,"return removes drone immediately");
    MotionHistory history;history.Push({1000,{0,0,0},{10,0,0},{1,0,0,0}});history.Push({1100,{1,0,0},{10,0,0},{-1,0,0,0}});
    auto mid=history.At(1150,100);Require(std::abs(mid.position.x-0.5f)<0.001f&&std::abs(mid.rotation.w)>0.99f,"position / quaternion interpolation");
    Require(std::abs(history.At(4000,100).position.x-1.75f)<0.001f,"bounded prediction on packet loss");
    history.Push({1200,{100,0,0},{},{}});Require(history.samples.size()==1,"teleport clears stale motion history");
    std::puts("Offline sharing regression passed: side-by-side spectators, dropped handshake, reset, stop, smoothing.");
}
