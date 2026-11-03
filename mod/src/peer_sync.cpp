#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "peer_sync.h"
#include <array>
#include <mutex>
#include <unordered_map>
#include <cstring>
#include <algorithm>
namespace fpv {
namespace {
constexpr std::uint32_t Magic=0x33565046;
#pragma pack(push,1)
struct Packet {std::uint32_t magic{Magic},version{2};std::uint64_t room{},sender{},sequence{};std::int32_t race{},flying{},preset{};float position[3]{},rotation[4]{},velocity[3]{},motor{},activity{},propInches{5};};
struct NativeFrame {std::uint32_t magic{0x4e565046},version{1},kind{};Packet packet;}; // hello=1, ack=2, state=3
#pragma pack(pop)
static_assert(sizeof(NativeFrame)==108,"Native drone protocol v1 layout changed");
std::uint64_t Hash(const std::string& text) {std::uint64_t v=1469598103934665603ULL;for(unsigned char c:text){v^=c;v*=1099511628211ULL;}return v;}
bool Valid(const Packet& p,std::uint64_t room) {
    if(p.magic!=Magic||p.version!=2||p.room!=room||!p.sender||p.race<0||p.race>1000000||p.flying<0||p.flying>1||p.preset<0||p.preset>=6)return false;
    for(float x:p.velocity)if(!std::isfinite(x)||std::abs(x)>1000)return false;
    if(!std::isfinite(p.motor)||p.motor<0||p.motor>1||!std::isfinite(p.activity)||p.activity<0||p.activity>1||!std::isfinite(p.propInches)||p.propInches<1.5f||p.propInches>7)return false;
    for(float x:p.position)if(!std::isfinite(x)||std::abs(x)>1000000)return false;
    float n=0;for(float x:p.rotation){if(!std::isfinite(x)||std::abs(x)>1.01f)return false;n+=x*x;}
    return n>0.8f&&n<1.2f;
}
std::uint64_t NewSender(const void* pointer){LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);return static_cast<std::uint64_t>(counter.QuadPart)^(GetTickCount64()<<20)^static_cast<std::uint64_t>(GetCurrentProcessId())^reinterpret_cast<std::uintptr_t>(pointer);}
Packet MakePacket(const PeerState& own,std::uint64_t room,std::uint64_t sender,std::uint64_t sequence){
    Packet p;p.room=room;p.sender=sender;p.sequence=sequence;p.race=std::max(own.race,0);p.flying=own.flying;p.preset=std::clamp(own.preset,0,5);
    p.position[0]=own.position.x;p.position[1]=own.position.y;p.position[2]=own.position.z;auto q=own.rotation.Normalized();p.rotation[0]=q.w;p.rotation[1]=q.x;p.rotation[2]=q.y;p.rotation[3]=q.z;
    p.velocity[0]=own.velocity.x;p.velocity[1]=own.velocity.y;p.velocity[2]=own.velocity.z;p.motor=std::clamp(own.motor,0.0f,1.0f);p.activity=std::clamp(own.activity,0.0f,1.0f);p.propInches=std::clamp(own.propInches,1.5f,7.0f);return p;
}
PeerState State(const Packet& p,std::uint64_t seen){return {p.race,p.flying!=0,{p.position[0],p.position[1],p.position[2]},{p.rotation[0],p.rotation[1],p.rotation[2],p.rotation[3]},p.preset,seen,p.sender,{p.velocity[0],p.velocity[1],p.velocity[2]},p.motor,p.activity,p.propInches};}
struct NativeRoster {int own{},count{};std::array<int,50> peers{};};
bool ReadNativeRoster(std::uintptr_t base,NativeRoster* out){
    __try {
        // Server-authenticated logical connection IDs; not the transport handshake ID.
        if(!base||*reinterpret_cast<const int*>(base+0x598904)!=7)return false;
        out->own=*reinterpret_cast<const int*>(base+0x598908);if(out->own<1||out->own>127)return false;
        for(int i=0;i<50;i++){
            auto row=base+0x599044+i*0x2e0;int state=*reinterpret_cast<const int*>(row),id=*reinterpret_cast<const int*>(row+4);
            if(state>1&&id>0&&id<=127&&id!=out->own)out->peers[out->count++]=id;
        }return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
struct PeerSync::Impl {
    SOCKET socket{INVALID_SOCKET};bool winsock{},host{};sockaddr_in relay{};std::uint64_t room{},id{},sequence{},lastSend{};
    struct Entry {Packet packet;sockaddr_in address;std::uint64_t seen;};
    std::unordered_map<std::uint64_t,Entry> entries;std::string status{"Cosmetic sync off"};mutable std::mutex mutex;
    struct NativePeer {std::uint64_t token{},sequence{},seen{},dataTime{},sent{},probeAt{};unsigned probes{};bool confirmed{},ackPending{},haveData{},lastFlying{};Packet packet;};
    std::unordered_map<int,NativePeer> nativePeers;
    std::uintptr_t base{};bool nativeAvailable{},enabled{},nativeOnline{};int transport{};
    PeerState own;std::uint64_t submitted{};
    void ResetNative(){nativePeers.clear();nativeOnline=false;id=NewSender(this);sequence=0;}
};
PeerSync::PeerSync():impl_(new Impl){}
PeerSync::~PeerSync(){Close();delete impl_;}
void PeerSync::Close(){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->enabled=false;if(impl_->socket!=INVALID_SOCKET)closesocket(impl_->socket);impl_->socket=INVALID_SOCKET;if(impl_->winsock)WSACleanup();impl_->winsock=false;impl_->entries.clear();impl_->ResetNative();impl_->status="Drone sharing off";}
bool PeerSync::Configure(const SyncSettings& s) {
    Close();std::lock_guard<std::mutex> lock(impl_->mutex);impl_->transport=std::clamp(s.transport,0,1);impl_->enabled=s.enabled;if(!s.enabled)return true;
    if(impl_->transport==0){impl_->room=Hash("MXB-FPV-native-v1");impl_->status="Automatic game-server sync";return true;}
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)){impl_->status="Windows UDP initialization failed";return false;}impl_->winsock=true;
    impl_->host=s.host;impl_->room=Hash(s.room);impl_->id=NewSender(this);
    impl_->socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(impl_->socket==INVALID_SOCKET){impl_->status="Cannot create UDP socket";return false;}
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_ANY);local.sin_port=htons(static_cast<u_short>(s.host?std::clamp(s.port,1024,65535):0));
    if(bind(impl_->socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))==SOCKET_ERROR){closesocket(impl_->socket);impl_->socket=INVALID_SOCKET;impl_->status="UDP port could not be bound";return false;}
    u_long nonblocking=1;ioctlsocket(impl_->socket,FIONBIO,&nonblocking);
    impl_->relay={};impl_->relay.sin_family=AF_INET;impl_->relay.sin_port=htons(static_cast<u_short>(std::clamp(s.port,1024,65535)));
    if(!s.host&&inet_pton(AF_INET,s.address.c_str(),&impl_->relay.sin_addr)!=1){impl_->status="Enter the relay host's IPv4 address";closesocket(impl_->socket);impl_->socket=INVALID_SOCKET;return false;}
    impl_->lastSend=0;impl_->sequence=0;impl_->status=s.host?"Hosting cosmetic sync relay":"Sending cosmetic sync to "+s.address;return true;
}
void PeerSync::Tick(const PeerState& own) {
    std::lock_guard<std::mutex> lock(impl_->mutex);impl_->own=own;impl_->submitted=GetTickCount64();if(!impl_->enabled||impl_->transport==0||impl_->socket==INVALID_SOCKET)return;
    auto now=GetTickCount64();
    for(int count=0;count<96;count++) {
        Packet p{};sockaddr_in source{};int length=sizeof(source);
        int received=recvfrom(impl_->socket,reinterpret_cast<char*>(&p),sizeof(p),0,reinterpret_cast<sockaddr*>(&source),&length);
        if(received==SOCKET_ERROR)break;
        if(received!=sizeof(p)||!Valid(p,impl_->room)||p.sender==impl_->id)continue;
        if(!impl_->host&&(source.sin_addr.s_addr!=impl_->relay.sin_addr.s_addr||source.sin_port!=impl_->relay.sin_port))continue;
        auto found=impl_->entries.find(p.sender);if(found!=impl_->entries.end()&&p.sequence<=found->second.packet.sequence)continue;
        if(found==impl_->entries.end()&&impl_->entries.size()>=64)continue;
        impl_->entries[p.sender]={p,source,now};
        if(impl_->host)for(const auto& pair:impl_->entries)if(pair.first!=p.sender)sendto(impl_->socket,reinterpret_cast<const char*>(&p),sizeof(p),0,reinterpret_cast<const sockaddr*>(&pair.second.address),sizeof(source));
    }
    for(auto i=impl_->entries.begin();i!=impl_->entries.end();)if(now-i->second.seen>4000)i=impl_->entries.erase(i);else++i;
    if(now-impl_->lastSend<50)return;impl_->lastSend=now;
    Packet packet=MakePacket(own,impl_->room,impl_->id,++impl_->sequence);
    if(impl_->host) {
        for(const auto& destination:impl_->entries) {
            sendto(impl_->socket,reinterpret_cast<const char*>(&packet),sizeof(packet),0,reinterpret_cast<const sockaddr*>(&destination.second.address),sizeof(sockaddr_in));
            // A fresh listener receives the other known pilots without waiting for movement.
            for(const auto& state:impl_->entries)if(state.first!=destination.first)sendto(impl_->socket,reinterpret_cast<const char*>(&state.second.packet),sizeof(Packet),0,reinterpret_cast<const sockaddr*>(&destination.second.address),sizeof(sockaddr_in));
        }
    } else sendto(impl_->socket,reinterpret_cast<const char*>(&packet),sizeof(packet),0,reinterpret_cast<const sockaddr*>(&impl_->relay),sizeof(impl_->relay));
}
void PeerSync::BindNative(std::uintptr_t base,bool available){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->base=base;impl_->nativeAvailable=available;}
void PeerSync::ResetNative(){std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->transport==0)impl_->ResetNative();}
void PeerSync::PumpNative(){
    std::lock_guard<std::mutex> lock(impl_->mutex);auto& a=*impl_;if(!a.enabled||a.transport!=0||!a.nativeAvailable)return;
    NativeRoster roster;if(!ReadNativeRoster(a.base,&roster)){if(a.nativeOnline)a.ResetNative();return;}a.nativeOnline=true;
    auto present=[&](int id){return std::find(roster.peers.begin(),roster.peers.begin()+roster.count,id)!=roster.peers.begin()+roster.count;};
    for(auto i=a.nativePeers.begin();i!=a.nativePeers.end();)if(!present(i->first))i=a.nativePeers.erase(i);else++i;
    for(int i=0;i<roster.count;i++)a.nativePeers.try_emplace(roster.peers[i]);
    auto now=GetTickCount64();auto own=a.own;if(now-a.submitted>1000)own.flying=false;
    using Send=std::int64_t(*)(int,const void*,int,int);auto send=reinterpret_cast<Send>(a.base+0x2a3920);
    auto sendFrame=[&](int destination,unsigned kind){NativeFrame frame;frame.kind=kind;frame.packet=MakePacket(own,a.room,a.id,++a.sequence);send(NativeMessageType,&frame,sizeof(frame),destination);};
    for(auto& entry:a.nativePeers){auto& p=entry.second;
        if(p.confirmed&&now-p.seen>10000){p.confirmed=false;p.haveData=false;p.probeAt=0;p.probes=0;}
        if(p.ackPending){sendFrame(entry.first,2);p.ackPending=false;}
        if(!p.confirmed&&now>=p.probeAt){sendFrame(entry.first,1);unsigned wait=p.probes==0?2000:p.probes==1?8000:p.probes==2?30000:60000;p.probeAt=now+wait;p.probes=std::min(p.probes+1,4u);}
        // Unknown/vanilla clients receive only bounded discovery, never flight streams.
        if(p.confirmed&&(!p.sent||p.lastFlying!=own.flying||now-p.sent>=(own.flying?50u:500u))){sendFrame(entry.first,3);p.sent=now;p.lastFlying=own.flying;}
    }
}
void PeerSync::ReceiveNative(int connection,const void* data,int bytes){
    if(bytes!=sizeof(NativeFrame)||!data)return;NativeFrame frame;std::memcpy(&frame,data,sizeof(frame));
    if(frame.magic!=0x4e565046||frame.version!=1||frame.kind<1||frame.kind>3)return;
    std::lock_guard<std::mutex> lock(impl_->mutex);auto& a=*impl_;if(!a.enabled||a.transport!=0||!a.nativeAvailable)return;
    NativeRoster roster;if(!ReadNativeRoster(a.base,&roster)||std::find(roster.peers.begin(),roster.peers.begin()+roster.count,connection)==roster.peers.begin()+roster.count)return;
    const auto& packet=frame.packet;if(!Valid(packet,a.room)||packet.sender==a.id)return;
    auto& peer=a.nativePeers[connection];auto now=GetTickCount64();
    if(frame.kind==3){if(!peer.confirmed||packet.sender!=peer.token||packet.sequence<=peer.sequence)return;peer.packet=packet;peer.haveData=true;peer.dataTime=now;}
    else {
        if(peer.token!=packet.sender){peer=Impl::NativePeer{};peer.token=packet.sender;}
        if(packet.sequence<=peer.sequence)return;peer.confirmed=true;if(frame.kind==1)peer.ackPending=true;
    }
    peer.sequence=packet.sequence;peer.seen=now;
}
std::vector<PeerState> PeerSync::Peers() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);std::vector<PeerState> result;
    auto now=GetTickCount64();if(impl_->transport==0){for(const auto& entry:impl_->nativePeers)if(entry.second.haveData&&now-entry.second.dataTime<4000)result.push_back(State(entry.second.packet,entry.second.dataTime));}
    else for(const auto& entry:impl_->entries)result.push_back(State(entry.second.packet,entry.second.seen));return result;
}
std::string PeerSync::Status() const {std::lock_guard<std::mutex> lock(impl_->mutex);const auto& a=*impl_;
    if(!a.enabled)return "Drone sharing off";
    if(a.transport==0){if(!a.nativeAvailable)return "Native sync unavailable on this build; manual UDP is available";if(!a.nativeOnline)return "Automatic sync ready - join a multiplayer session";
        size_t count=0;for(const auto& p:a.nativePeers)if(p.second.confirmed)count++;return "Game-server sync: "+std::to_string(count)+" FPV peer(s) / "+std::to_string(a.nativePeers.size())+" rider(s)";}
    return a.status+" | "+std::to_string(a.entries.size())+" peer(s)";}
}
