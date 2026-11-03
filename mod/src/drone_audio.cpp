#include "controller.h"
#include "drone_audio.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstring>
namespace fpv {
namespace {
using ALuint=unsigned;using ALenum=int;using ALsizei=int;
constexpr ALenum Position=0x1004,Velocity=0x1006,Pitch=0x1003,Gain=0x100a,Looping=0x1007,Buffer=0x1009,Relative=0x202,RefDistance=0x1020,Rolloff=0x1021,MaxDistance=0x1023,SourceState=0x1010,Playing=0x1012;
struct Sample {std::vector<unsigned char> pcm;int frequency{};};
Sample ResourceSample(HMODULE module,int id) {
    auto r=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);if(!r)return {};
    auto n=SizeofResource(module,r);auto p=static_cast<const unsigned char*>(LockResource(LoadResource(module,r)));
    if(!p||n<44||memcmp(p,"RIFF",4)||memcmp(p+8,"WAVE",4))return {};
    Sample s;bool valid=false;for(size_t i=12;i+8<=n;){std::uint32_t length{};memcpy(&length,p+i+4,4);if(length>n-i-8)return {};
        if(!memcmp(p+i,"fmt ",4)&&length>=16){std::uint16_t format{},channels{},bits{};memcpy(&format,p+i+8,2);memcpy(&channels,p+i+10,2);memcpy(&s.frequency,p+i+12,4);memcpy(&bits,p+i+22,2);valid=format==1&&channels==1&&bits==16&&s.frequency>=8000&&s.frequency<=192000;}
        if(!memcmp(p+i,"data",4))s.pcm.assign(p+i+8,p+i+8+length);i+=8+length+(length&1);
    }return valid&&!s.pcm.empty()?s:Sample{};
}
}
struct DroneAudio::Impl {
    using Gen=void(*)(ALsizei,ALuint*);using Del=void(*)(ALsizei,const ALuint*);
    Gen genBuffers{},genSources{};Del delBuffers{},delSources{};
    void(*bufferData)(ALuint,ALenum,const void*,ALsizei,ALsizei){};
    void(*sourcei)(ALuint,ALenum,int){};void(*sourcef)(ALuint,ALenum,float){};void(*source3f)(ALuint,ALenum,float,float,float){};
    void(*play)(ALuint){};void(*stop)(ALuint){};void(*getSourcei)(ALuint,ALenum,int*){};
    void*(*currentContext)(){};int(*getError)(){};void* context{};HMODULE library{};
    std::array<Sample,6> samples;std::array<ALuint,6> buffers{};
    struct Voice {std::array<ALuint,6> sources{};float motor{},gain{};std::uint64_t tick{};};std::unordered_map<std::uint64_t,Voice> voices;
    PeerState own;std::vector<PeerState> peers;AudioSettings settings;bool onTrack{};
    std::mutex mutex;std::atomic<int> status{};std::uint64_t lastTick{},lastSubmit{};
    bool Connect(){
        if(!library){library=GetModuleHandleW(L"soft_oal.dll");if(!library)library=GetModuleHandleW(L"openal32.dll");if(!library)return false;
#define AL_LOAD(field,name) field=reinterpret_cast<decltype(field)>(GetProcAddress(library,name));if(!field)return false
        AL_LOAD(genBuffers,"alGenBuffers");AL_LOAD(genSources,"alGenSources");AL_LOAD(delBuffers,"alDeleteBuffers");AL_LOAD(delSources,"alDeleteSources");
        AL_LOAD(bufferData,"alBufferData");AL_LOAD(sourcei,"alSourcei");AL_LOAD(sourcef,"alSourcef");AL_LOAD(source3f,"alSource3f");AL_LOAD(play,"alSourcePlay");AL_LOAD(stop,"alSourceStop");AL_LOAD(getSourcei,"alGetSourcei");AL_LOAD(currentContext,"alcGetCurrentContext");AL_LOAD(getError,"alGetError");
#undef AL_LOAD
        }
        if(!currentContext)return false;void* now=currentContext();if(!now)return false;
        if(context&&context!=now){voices.clear();buffers={};context=nullptr;}
        if(!context){
            for(const auto& s:samples)if(s.pcm.empty()){status=2;return false;}
            // Preserve AL's shared error state unless an operation of ours has failed.
            genBuffers(6,buffers.data());
            for(int i=0;i<6;i++)if(buffers[i])bufferData(buffers[i],0x1101,samples[i].pcm.data(),static_cast<int>(samples[i].pcm.size()),samples[i].frequency);
            if(std::any_of(buffers.begin(),buffers.end(),[](ALuint b){return !b;})){status=3;return false;}
            context=now;status=1;
        }return true;
    }
    void Drop(Voice& v){for(auto source:v.sources)if(source){stop(source);delSources(1,&source);}}
    bool Create(Voice& v){
        genSources(6,v.sources.data());
        if(std::any_of(v.sources.begin(),v.sources.end(),[](ALuint s){return !s;})){Drop(v);v.sources={};status=3;return false;}
        // FSB subsounds 5,0,2,1 are the tonal motor layers; 3 and 4 add air/load texture.
        constexpr int layers[]={5,0,2,1,3,4};
        for(int i=0;i<6;i++){auto s=v.sources[i];sourcei(s,Buffer,static_cast<int>(buffers[layers[i]]));sourcei(s,Looping,1);sourcei(s,Relative,0);sourcef(s,Gain,0);sourcef(s,RefDistance,3);sourcef(s,Rolloff,1.5f);sourcef(s,MaxDistance,180);play(s);}return true;
    }
    void Update(std::uint64_t id,const PeerState& p,float volume,bool ownVoice,float dt,std::uint64_t now){
        auto found=voices.find(id);if(found==voices.end()){Voice voice;if(!Create(voice))return;found=voices.emplace(id,voice).first;}
        auto& v=found->second;v.tick=now;float target=std::abs(p.motor);v.motor+=(target-v.motor)*(1-std::exp(-8*dt));
        v.gain+=(volume-v.gain)*(1-std::exp(-18*dt));float layer=std::clamp(v.motor*3,0.0f,3.0f);
        float prop=std::sqrt(5/std::clamp(p.propInches,1.5f,7.0f)),speed=Length(p.velocity);
        for(int i=0;i<6;i++){auto s=v.sources[i];float weight=i<4?std::sqrt(std::max(0.0f,1-std::abs(layer-i))):
                (i==4?std::clamp(p.activity*0.35f,0.0f,0.35f):std::clamp(speed/70,0.0f,0.6f));
            float amplitude=(0.2f+std::sqrt(v.motor)*0.8f)*weight*v.gain*(ownVoice?1.5f:2.2f)*std::clamp(v.motor/0.02f,0.0f,1.0f);
            sourcef(s,Gain,amplitude);sourcef(s,Pitch,i<4?std::clamp((0.68f+v.motor*0.95f)*prop,0.4f,2.7f):0.85f+speed*0.005f);
            sourcef(s,MaxDistance,settings.range);source3f(s,Position,p.position.x,p.position.y,-p.position.z);
            source3f(s,Velocity,p.velocity.x,p.velocity.y,-p.velocity.z);
            int state{};getSourcei(s,SourceState,&state);if(state!=Playing)play(s);
        }
    }
};
DroneAudio::DroneAudio():impl_(new Impl){}
DroneAudio::~DroneAudio(){Close();delete impl_;}
void DroneAudio::Load(std::uintptr_t module){for(int i=0;i<6;i++)impl_->samples[i]=ResourceSample(reinterpret_cast<HMODULE>(module),201+i);}
void DroneAudio::Submit(const PeerState& own,const std::vector<PeerState>& peers,const AudioSettings& settings,bool onTrack){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->own=own;impl_->peers=peers;impl_->settings=settings;impl_->onTrack=onTrack;impl_->lastSubmit=GetTickCount64();}
void DroneAudio::Process(){
    std::lock_guard<std::mutex> lock(impl_->mutex);auto& a=*impl_;if(!a.Connect())return;auto now=GetTickCount64();float dt=a.lastTick?std::clamp((now-a.lastTick)*0.001f,0.001f,0.05f):0.016f;a.lastTick=now;
    if(a.onTrack&&a.settings.enabled&&now-a.lastSubmit<1000){
        if(a.own.flying)a.Update(0,a.own,a.settings.volume,true,dt,now);
        if(a.settings.remote){auto peers=a.peers;Vec3 listener=a.own.flying?a.own.position:Vec3{};
            // The native mixer's world listener is already current, even when riding a bike.
            auto getListener=reinterpret_cast<void(*)(int,float*)>(GetProcAddress(a.library,"alGetListenerfv"));if(getListener){float p[3]{};getListener(Position,p);listener={p[0],p[1],-p[2]};}
            std::sort(peers.begin(),peers.end(),[&](const PeerState& x,const PeerState& y){return Length(x.position-listener)<Length(y.position-listener);});
            int count=0;for(const auto& p:peers)if(p.flying&&now-p.received<1500&&Length(p.position-listener)<a.settings.range&&count++<8)a.Update(p.id,p,a.settings.volume*a.settings.remoteVolume,false,dt,now);
        }
    }
    for(auto i=a.voices.begin();i!=a.voices.end();){if(i->second.tick!=now){a.Drop(i->second);i=a.voices.erase(i);}else ++i;}
}
void DroneAudio::Close(){std::lock_guard<std::mutex> lock(impl_->mutex);auto& a=*impl_;if(a.context&&a.currentContext&&a.currentContext()==a.context){for(auto& voice:a.voices)a.Drop(voice.second);a.delBuffers(6,a.buffers.data());}a.voices.clear();a.buffers={};a.context=nullptr;}
const char* DroneAudio::Status()const{switch(impl_->status.load()){case 1:return "Kino motor layers loaded";case 2:return "Audio sample resources missing";case 3:return "Audio source allocation failed";default:return "Waiting for game audio context";}}
}
