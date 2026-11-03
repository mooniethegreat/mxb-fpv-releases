#include "controller.h"
#include "terrain.h"
#include "peer_sync.h"
#include "rider_visuals.h"
#include "drone_audio.h"
#include "traffic_collision.h"
#include "updater.h"
#include "version.h"
#include "../vendor/minhook/include/MinHook.h"
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstring>
#include <cstddef>
#include <gl/GL.h>
#include <shellapi.h>
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl3.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);

namespace {
using namespace fpv;
HMODULE module{};
std::filesystem::path configPath, logPath;
Controller controller;
DeviceSettings deviceSettings;
Input liveInput;
Input flightInput;
std::atomic<int> liveButton{-1};
Flight flight;
Terrain terrain;
PeerSync peerSync;
SyncSettings syncSettings;
RiderVisuals riderVisuals;
VisualSettings visualSettings;
DroneAudio droneAudio;
AudioSettings audioSettings;
TrafficCollision traffic;
Updater updater;
bool automaticUpdates{true},previewUpdates{true};
bool showControls{true},showDroneHud{true},replaceBikeHud{true};
FlightSettings flightSettings;
int presetIndex{};
bool settingsScreen{};
float previousTelemetryTime{};
Vec3 bikePosition;
float bikeYaw{};
std::atomic<bool> active{},onTrack{};
std::atomic<bool> toggleRequested{};
std::atomic<bool> menuOpen{},menuRequested{};
bool overlayReady{};
ImGuiContext* overlayContext{};
using SwapFn=BOOL(WINAPI*)(HDC);
using CursorFn=BOOL(WINAPI*)(int,int);
SwapFn originalSwap{};
CursorFn originalCursor{};
HWND gameWindow{};
WNDPROC originalWindowProc{};
std::atomic<ULONGLONG> telemetryTick{};
thread_local bool readingFpv{};
bool supported{},previousToggle{},previousButton{},hooksReady{},flyingWithController{};
int toggleKey=VK_F8;
float accumulator{};
std::mutex flightMutex;
std::mutex controllerMutex;
std::uintptr_t gameBase{};
using CameraSetter=int(*)(int,const float*);
using FovSetter=int(*)(int,float);
using CameraBuilder=std::int64_t(*)(void*,void*,int,float,void*);
using RiderRenderer=std::int64_t(*)(void*,int*,void*,int);
using DeviceState=HRESULT(WINAPI*)(IDirectInputDevice8W*,DWORD,void*);
using NativeMixer=std::int64_t(*)(int);
using BikeStep=std::int64_t(*)(void*,float);
using NativeDash=std::int64_t(*)(const void*);
using NativeText=void(*)(int,float,float,float,std::uint32_t,const char*);
using NativeNetEvent=std::int64_t(*)(int,std::uintptr_t,std::uintptr_t);
NativeNetEvent originalNetEvent{};
NativeMixer originalMixer{};BikeStep originalBikeStep{};NativeDash originalDash{};NativeText originalText{};
thread_local bool droneDashboard{};
float gearX{},gearY{},gearSize{};ULONGLONG gearTick{};
using OtherHudDraw=void(*)(int,int*,void**,int*,void**);
OtherHudDraw originalMaxHudDraw{};
CameraSetter originalCamera{};
FovSetter originalFov{};
CameraBuilder originalBuilder{};
RiderRenderer originalRider{};
DeviceState originalDeviceState{};
struct XHook { HMODULE library{}; using Fn=DWORD(WINAPI*)(DWORD,XINPUT_STATE*); Fn original{}; };
XHook xhooks[3];
float cameraFov=100;
float lastCameraFov=100;
std::array<float,16> lastCameraMatrix{};
Input PollController() {
    std::lock_guard<std::mutex> lock(controllerMutex);
    readingFpv=true;auto input=controller.Poll();liveButton=controller.Button();readingFpv=false;return input;
}

LRESULT CALLBACK WindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_KEYDOWN && wParam==VK_F9 && !(lParam&(1LL<<30))) {menuRequested=true;return 0;}
    if((menuOpen||settingsScreen) && overlayReady) {
        ImGui::SetCurrentContext(overlayContext);
        ImGui_ImplWin32_WndProcHandler(window,message,wParam,lParam);
        if(menuOpen&&message==WM_KEYDOWN && wParam==VK_ESCAPE) {menuRequested=true;return 0;}
        bool mouse=message>=WM_MOUSEFIRST&&message<=WM_MOUSELAST;
        bool keyboard=(message>=WM_KEYFIRST&&message<=WM_KEYLAST)||message==WM_CHAR;
        if((mouse&&(menuOpen||ImGui::GetIO().WantCaptureMouse))||(keyboard&&menuOpen))return 0;
    }
    if(message==WM_KEYDOWN && wParam==static_cast<WPARAM>(toggleKey) && !(lParam&(1LL<<30))) toggleRequested=true;
    return CallWindowProcW(originalWindowProc,window,message,wParam,lParam);
}

void Log(const std::string& message) {
    std::ofstream file(logPath,std::ios::app);
    file<<GetTickCount64()<<" "<<message<<"\n";
}
std::wstring Setting(const wchar_t* section,const wchar_t* key,const wchar_t* fallback) {
    wchar_t buffer[512]{};
    GetPrivateProfileStringW(section,key,fallback,buffer,512,configPath.c_str());
    return buffer;
}
float Number(const wchar_t* section,const wchar_t* key,float fallback,float minimum,float maximum) {
    auto value=Setting(section,key,std::to_wstring(fallback).c_str());
    wchar_t* end{}; float number=wcstof(value.c_str(),&end);
    return end==value.c_str() || !std::isfinite(number)?fallback:std::clamp(number,minimum,maximum);
}
bool MainCamera(int id) {
    // Static traces of the riding and alternate riding view for the pinned build.
    return onTrack && id>0 && id<=12 &&
        (id==*reinterpret_cast<int*>(gameBase+0x4cfb6c) || id==*reinterpret_cast<int*>(gameBase+0x4cfda8));
}
int SetCamera(int id,const float* matrix) {
    if(!active || !MainCamera(id)) {
        if(MainCamera(id)){std::memcpy(lastCameraMatrix.data(),matrix,64);riderVisuals.Camera(matrix,lastCameraFov);}
        return originalCamera(id,matrix);
    }
    static bool reported=false;
    if(!reported){Log("Live camera override applied.");reported=true;}
    float replacement[16]{};
    {
        std::lock_guard<std::mutex> lock(flightMutex);
        Quat q=(flight.rotation*Quat::AxisAngle({1,0,0},-flightSettings.cameraTilt*Pi/180)).Normalized();
        Vec3 right=q.Rotate({1,0,0}), up=q.Rotate({0,1,0}), forward=q.Rotate({0,0,1});
        float values[16]={right.x,up.x,forward.x,flight.position.x,
            right.y,up.y,forward.y,flight.position.y,right.z,up.z,forward.z,flight.position.z,0,0,0,1};
        std::memcpy(replacement,values,sizeof(values));
    }
    std::memcpy(lastCameraMatrix.data(),replacement,64);riderVisuals.Camera(replacement,cameraFov);return originalCamera(id,replacement);
}
int SetFov(int id,float fov) {float value=active&&MainCamera(id)?cameraFov:fov;if(MainCamera(id)){lastCameraFov=value;riderVisuals.Camera(lastCameraMatrix.data(),value);}return originalFov(id,value);}
bool NativeFpvMessage(std::uintptr_t address,int* sender,const void** data,int* length) {
    __try {
        if(!address||*reinterpret_cast<const int*>(address+4)!=PeerSync::NativeMessageType)return false;
        *sender=*reinterpret_cast<const unsigned char*>(address);*length=*reinterpret_cast<const int*>(address+8);
        *data=reinterpret_cast<const void*>(address+12);return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::int64_t NetworkEvent(int kind,std::uintptr_t argument,std::uintptr_t extra) {
    if(kind==10){int sender{},length{};const void* data{};if(NativeFpvMessage(argument,&sender,&data,&length)){
        if(length>=0&&length<=1100)peerSync.ReceiveNative(sender,data,length);return 0;
    }}
    // Network shutdown and world reinitialization invalidate the prior roster/nonce.
    if(kind==0x12||kind==0xd)peerSync.ResetNative();
    auto result=originalNetEvent(kind,argument,extra);
    // This callback runs inside MX Bikes' own serialized client network update.
    if(kind==0x13)peerSync.PumpNative();return result;
}
std::int64_t MixAudio(int milliseconds) {
    float saved[12]{};bool overrideListener=active&&onTrack;
    auto listener=reinterpret_cast<float*>(gameBase+0x399608);
    if(overrideListener){
        std::lock_guard<std::mutex> lock(flightMutex);std::memcpy(saved,listener,sizeof(saved));
        auto q=(flight.rotation*Quat::AxisAngle({1,0,0},-flightSettings.cameraTilt*Pi/180)).Normalized();
        auto f=q.Rotate({0,0,1}),u=q.Rotate({0,1,0});
        float replacement[]={flight.position.x,flight.position.y,flight.position.z,flight.velocity.x,flight.velocity.y,flight.velocity.z,f.x,f.y,f.z,u.x,u.y,u.z};
        std::memcpy(listener,replacement,sizeof(replacement));
    }
    // Native distance-priority/culling and the AL listener both consume this block.
    auto result=originalMixer(milliseconds);droneAudio.Process();
    if(overrideListener)std::memcpy(listener,saved,sizeof(saved));return result;
}
void* OwnPhysics() {
    int index=*reinterpret_cast<const int*>(gameBase+0xe62710)-1;if(index<0||index>=50)return nullptr;
    auto record=gameBase+0xf4ee20+index*0x5b24;if(!*reinterpret_cast<const int*>(record))return nullptr;
    int handle=*reinterpret_cast<const int*>(record+4);if(handle<=0)return nullptr;
    return reinterpret_cast<void*(*)(int)>(gameBase+0x1bf1a0)(handle);
}
std::int64_t ParkBike(void* physics,float dt) {
    if(!active||!onTrack||physics!=OwnPhysics())return originalBikeStep(physics,dt);
    // These are the native normalized control channels, before each physics step.
    // Full brakes, clutch pulled, no throttle/steering/gear requests. No body teleport.
    auto p=static_cast<unsigned char*>(physics);unsigned char saved[0x48]{};std::memcpy(saved,p+0x270,sizeof(saved));
    *reinterpret_cast<double*>(p+0x270)=0;*reinterpret_cast<double*>(p+0x278)=0;
    *reinterpret_cast<double*>(p+0x280)=1;*reinterpret_cast<double*>(p+0x288)=1;*reinterpret_cast<double*>(p+0x290)=1;
    *reinterpret_cast<int*>(p+0x298)=0;*reinterpret_cast<int*>(p+0x29c)=0;
    auto result=originalBikeStep(physics,dt);std::memcpy(p+0x270,saved,sizeof(saved));return result;
}
std::int64_t DroneDashboard(const void* data) {
    if(!active)return originalDash(data);
    if(replaceBikeHud)return 0;
    if(!showDroneHud)return originalDash(data);
    std::array<unsigned char,0x600> view{};std::memcpy(view.data(),data,view.size());
    {std::lock_guard<std::mutex> lock(flightMutex);*reinterpret_cast<float*>(view.data()+0x50)=Length(flight.velocity);}
    droneDashboard=true;auto result=originalDash(view.data());droneDashboard=false;return result;
}
void MaxHudDraw(int state,int* quads,void** quadData,int* strings,void** stringData) {
    originalMaxHudDraw(state,quads,quadData,strings,stringData);
    if(active&&replaceBikeHud&&state==0){*quads=0;*strings=0;}
}
void DashboardText(int font,float x,float y,float size,std::uint32_t color,const char* text) {
    if(droneDashboard&&font==*reinterpret_cast<int*>(gameBase+0x109dfa4)){
        gearX=x;gearY=y;gearSize=size;gearTick=GetTickCount64();return;
    }originalText(font,x,y,size,color,text);
}
std::int64_t RenderRider(void* graphics,int* instance,void* physics,int flags) {
    auto result=originalRider(graphics,instance,physics,flags);riderVisuals.Rider(graphics,instance,physics);return result;
}
std::int64_t BuildCamera(void* vehicle,void* look,int flags,float lookAxis,void* output) {
    if(!active)return originalBuilder(vehicle,look,flags,lookAxis,output);
    // Use the game's complete external rider representation while the FPV camera
    // is detached. Native first-person mode deliberately hides head/helmet meshes.
    // Restore the selected riding camera immediately after this one build call.
    int* mode=reinterpret_cast<int*>(gameBase+0x3df798);
    int saved=*mode;*mode=2;
    auto result=originalBuilder(vehicle,look,flags,lookAxis,output);
    *mode=saved;return result;
}
template<int Index> DWORD WINAPI ReadXInput(DWORD index,XINPUT_STATE* state) {
    auto result=xhooks[Index].original(index,state);
    if(result==ERROR_SUCCESS && (active||menuOpen) && !readingFpv) std::memset(&state->Gamepad,0,sizeof(state->Gamepad));
    return result;
}
HRESULT WINAPI ReadDevice(IDirectInputDevice8W* device,DWORD size,void* state) {
    HRESULT result=originalDeviceState(device,size,state);
    if(FAILED(result)||(!active&&!menuOpen)||readingFpv) return result;
    if(size==256) {
        // Keyboard input for the bike is neutral while flying. Escape still opens the menu.
        auto escape=static_cast<unsigned char*>(state)[DIK_ESCAPE];
        std::memset(state,0,size);
        static_cast<unsigned char*>(state)[DIK_ESCAPE]=escape;
    } else if(size==sizeof(DIJOYSTATE) || size==sizeof(DIJOYSTATE2)) {
        std::memset(state,0,size);
        auto joystick=static_cast<DIJOYSTATE*>(state);
        for(DWORD axis=0;axis<8;axis++) {
            DIPROPRANGE range{};
            range.diph={sizeof(range),sizeof(DIPROPHEADER),axis*sizeof(LONG),DIPH_BYOFFSET};
            if(SUCCEEDED(device->GetProperty(DIPROP_RANGE,&range.diph)))
                reinterpret_cast<LONG*>(state)[axis]=range.lMin+(range.lMax-range.lMin)/2;
        }
        for(auto& pov:joystick->rgdwPOV) pov=0xffffffff;
    } else if(menuOpen && (size==sizeof(DIMOUSESTATE)||size==sizeof(DIMOUSESTATE2))) {
        std::memset(state,0,size);
    }
    return result;
}
bool Hook(void* target,void* replacement,void** original) {
    return MH_CreateHook(target,replacement,original)==MH_OK;
}
BOOL WINAPI RenderFrame(HDC dc);
BOOL WINAPI SetGameCursor(int x,int y) {return menuOpen?TRUE:originalCursor(x,y);}
bool InstallHooks() {
    if(MH_Initialize()!=MH_OK) return false;
    const unsigned char expected[]={0x48,0x83,0xec,0x28,0x83,0xf9,0x01,0x7d,0x0a,0xb8,0x01,0,0,0};
    void* camera=reinterpret_cast<void*>(gameBase+0x255d60);
    if(std::memcmp(camera,expected,sizeof(expected))!=0) {Log("Camera signature mismatch. FPV unavailable.");return false;}
    if(!Hook(camera,reinterpret_cast<void*>(SetCamera),reinterpret_cast<void**>(&originalCamera)) ||
       !Hook(reinterpret_cast<void*>(gameBase+0x255d30),reinterpret_cast<void*>(SetFov),reinterpret_cast<void**>(&originalFov))) return false;
    const unsigned char builderBytes[]={0x48,0x8b,0xc4,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
    auto builder=reinterpret_cast<void*>(gameBase+0xb7e0);
    if(std::memcmp(builder,builderBytes,sizeof(builderBytes))!=0 || !Hook(builder,reinterpret_cast<void*>(BuildCamera),reinterpret_cast<void**>(&originalBuilder)))return false;
    const unsigned char riderBytes[]={0x48,0x8b,0xc4,0x44,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x53,0x55,0x56};
    auto rider=reinterpret_cast<void*>(gameBase+0x523e0);
    if(std::memcmp(rider,riderBytes,sizeof(riderBytes))!=0||!Hook(rider,reinterpret_cast<void*>(RenderRider),reinterpret_cast<void**>(&originalRider)))return false;
    const unsigned char mixerBytes[]={0x48,0x83,0xec,0x68,0x48,0x83,0x3d,0x3c,0xf9,0x10,0};
    const unsigned char bikeBytes[]={0x4c,0x8b,0xdc,0x57,0x48,0x81,0xec,0xf0,0,0,0,0x83,0x79,0x38,0};
    const unsigned char dashBytes[]={0x40,0x53,0x48,0x83,0xec,0x70};
    const unsigned char textBytes[]={0x48,0x83,0xec,0x48,0x48,0x8b,0x44,0x24,0x78};
    struct PrivateHook {std::uintptr_t rva;const unsigned char* bytes;size_t count;void* fn;void** original;};
    PrivateHook additional[]={
        {0x289c90,mixerBytes,sizeof(mixerBytes),reinterpret_cast<void*>(MixAudio),reinterpret_cast<void**>(&originalMixer)},
        {0x1ada40,bikeBytes,sizeof(bikeBytes),reinterpret_cast<void*>(ParkBike),reinterpret_cast<void**>(&originalBikeStep)},
        {0x11fa0,dashBytes,sizeof(dashBytes),reinterpret_cast<void*>(DroneDashboard),reinterpret_cast<void**>(&originalDash)},
        {0x209e30,textBytes,sizeof(textBytes),reinterpret_cast<void*>(DashboardText),reinterpret_cast<void**>(&originalText)}};
    for(const auto& h:additional){auto address=reinterpret_cast<void*>(gameBase+h.rva);if(std::memcmp(address,h.bytes,h.count)||!Hook(address,h.fn,h.original)){Log("Audio/parking/dashboard hook signature mismatch.");return false;}}
    const unsigned char netEventBytes[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x41,0x8b,0xf8};
    const unsigned char nativeSendBytes[]={0x40,0x53,0x55,0x56,0x57,0x48,0x81,0xec,0xd8,0x05,0,0};
    auto eventAddress=reinterpret_cast<void*>(gameBase+0x120890);
    bool nativeSync=std::memcmp(eventAddress,netEventBytes,sizeof(netEventBytes))==0&&
        std::memcmp(reinterpret_cast<void*>(gameBase+0x2a3920),nativeSendBytes,sizeof(nativeSendBytes))==0&&
        Hook(eventAddress,reinterpret_cast<void*>(NetworkEvent),reinterpret_cast<void**>(&originalNetEvent));
    peerSync.BindNative(gameBase,nativeSync);Log(nativeSync?"Automatic game-server FPV channel installed.":"Native FPV channel unavailable; manual UDP remains available.");
    // MaxHUD reports its own bike input mapping. Replace it only during FPV;
    // its configuration and normal riding callbacks remain untouched.
    if(auto maxhud=GetModuleHandleW(L"MaxHUD_MXB64.dlo")){
        if(auto draw=GetProcAddress(maxhud,"Draw")){
            if(!Hook(reinterpret_cast<void*>(draw),reinterpret_cast<void*>(MaxHudDraw),reinterpret_cast<void**>(&originalMaxHudDraw)))Log("MaxHUD draw override unavailable.");
        }
    }
    auto swap=GetProcAddress(GetModuleHandleW(L"gdi32.dll"),"SwapBuffers");
    if(!swap||!Hook(reinterpret_cast<void*>(swap),reinterpret_cast<void*>(RenderFrame),reinterpret_cast<void**>(&originalSwap))) return false;
    auto cursor=GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetCursorPos");
    if(cursor)Hook(reinterpret_cast<void*>(cursor),reinterpret_cast<void*>(SetGameCursor),reinterpret_cast<void**>(&originalCursor));
    const wchar_t* libraries[]={L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"};
    void* replacements[]={reinterpret_cast<void*>(ReadXInput<0>),reinterpret_cast<void*>(ReadXInput<1>),reinterpret_cast<void*>(ReadXInput<2>)};
    for(int i=0;i<3;i++) {
        xhooks[i].library=LoadLibraryExW(libraries[i],nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(xhooks[i].library) {
            auto address=GetProcAddress(xhooks[i].library,"XInputGetState");
            if(address && MH_CreateHook(reinterpret_cast<void*>(address),replacements[i],reinterpret_cast<void**>(&xhooks[i].original))!=MH_OK)
                Log("An XInput suppression hook was unavailable.");
        }
    }
    IDirectInput8W* input{};
    if(SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,reinterpret_cast<void**>(&input),nullptr))) {
        IDirectInputDevice8W* keyboard{};
        if(SUCCEEDED(input->CreateDevice(GUID_SysKeyboard,&keyboard,nullptr))) {
            auto address=(*reinterpret_cast<void***>(keyboard))[9];
            Hook(address,reinterpret_cast<void*>(ReadDevice),reinterpret_cast<void**>(&originalDeviceState));
            keyboard->Release();
        }
        input->Release();
    }
    return MH_EnableHook(MH_ALL_HOOKS)==MH_OK;
}
bool VerifyBuild() {
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr,executable,MAX_PATH);
    std::ifstream input(executable,std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
    unsigned char digest[32]{};
    if(bytes.empty()) return false;
    BCRYPT_ALG_HANDLE algorithm{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    NTSTATUS status=BCryptHash(algorithm,nullptr,0,bytes.data(),static_cast<ULONG>(bytes.size()),digest,32);
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(status<0) return false;
    char hex[65]{};for(int i=0;i<32;i++) sprintf_s(hex+2*i,3,"%02x",digest[i]);
    return std::strcmp(hex,SupportedGameHash)==0;
}
HWND FindWindowForGame() {
    HWND result{};
    EnumWindows([](HWND window,LPARAM context)->BOOL {
        DWORD process{};GetWindowThreadProcessId(window,&process);
        if(process==GetCurrentProcessId() && IsWindowVisible(window) && GetWindow(window,GW_OWNER)==nullptr) {
            *reinterpret_cast<HWND*>(context)=window;return FALSE;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
void Update() {
    HWND foreground=GetForegroundWindow();DWORD foregroundProcess{};
    GetWindowThreadProcessId(foreground,&foregroundProcess);
    if(foregroundProcess!=GetCurrentProcessId()) {active=false;return;}
    Input input=PollController();
    liveInput=input;
    if(!settingsScreen)menuOpen=false;
    if(menuRequested.exchange(false)&&settingsScreen) {menuOpen=!menuOpen;active=false;toggleRequested=false;Log(menuOpen?"In-game FPV settings opened.":"In-game FPV settings closed.");}
    if(active && flyingWithController && !input.connected) {active=false;Log("Controller disconnected. Returned to bike.");}
    SHORT keyState=GetAsyncKeyState(toggleKey);
    bool key=(keyState&0x8000)!=0;
    bool requested=toggleRequested.exchange(false);
    bool pressed=(originalWindowProc?requested:((key&&!previousToggle)||(keyState&1)))||(input.toggle&&!previousButton);
    previousToggle=key;previousButton=input.toggle;
    static bool previousReset=false;bool reset=(GetAsyncKeyState('R')&0x8000)!=0;
    if(active&&reset&&!previousReset){std::lock_guard<std::mutex> lock(flightMutex);flight.Spawn(bikePosition,bikeYaw);accumulator=0;}previousReset=reset;
    if(active && (!onTrack || GetTickCount64()-telemetryTick>1000 || (GetAsyncKeyState(VK_ESCAPE)&0x8000))) {active=false;Log("FPV exited.");}
    if(pressed && !menuOpen && onTrack && supported && hooksReady && GetTickCount64()-telemetryTick<1000) {
        active=!active;
        if(active) {std::lock_guard<std::mutex> lock(flightMutex);flight.Spawn(bikePosition,bikeYaw);accumulator=0;flyingWithController=input.connected;}
        Log(active?"FPV entered beside bike.":"Returned to bike.");
    }
}
std::string Utf8(const std::wstring& value) {
    if(value.empty())return {};
    int length=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    std::string result(length,0);WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),length,nullptr,nullptr);return result;
}
void SaveSettings() {
    auto write=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value){return WritePrivateProfileStringW(section,key,value.c_str(),configPath.c_str())!=0;};
    bool ok=write(L"Controls",L"Backend",deviceSettings.useDirectInput?L"directinput":L"auto");
    ok=write(L"Controls",L"DeviceGuid",deviceSettings.guid)&&ok;
    const wchar_t* keys[]={L"RollAxis",L"PitchAxis",L"YawAxis",L"ThrottleAxis",L"InvertRoll",L"InvertPitch",L"InvertYaw",L"InvertThrottle",L"ToggleButton",L"ToggleKey",L"XInputIndex"};
    int values[]={deviceSettings.rollAxis,deviceSettings.pitchAxis,deviceSettings.yawAxis,deviceSettings.throttleAxis,deviceSettings.invertRoll,deviceSettings.invertPitch,deviceSettings.invertYaw,deviceSettings.invertThrottle,deviceSettings.toggleButton,toggleKey,deviceSettings.xinputIndex};
    for(int i=0;i<11;i++)ok=write(L"Controls",keys[i],std::to_wstring(values[i]))&&ok;
    ok=write(L"Controls",L"Deadzone",std::to_wstring(deviceSettings.deadzone))&&ok;
    ok=write(L"Flight",L"Fov",std::to_wstring(cameraFov))&&ok;
    ok=write(L"Flight",L"RcRate",std::to_wstring(flightSettings.rcRate))&&ok;
    ok=write(L"Flight",L"SuperRate",std::to_wstring(flightSettings.superRate))&&ok;
    ok=write(L"Flight",L"Expo",std::to_wstring(flightSettings.expo))&&ok;
    ok=write(L"Flight",L"Preset",std::to_wstring(presetIndex))&&ok;
    ok=write(L"Rates",L"Model",std::to_wstring(flightSettings.rateModel))&&ok;
    ok=write(L"Rates",L"Individual",std::to_wstring(flightSettings.individualRates))&&ok;
    ok=write(L"Rates",L"QuickRcExpo",std::to_wstring(flightSettings.quickRcExpo))&&ok;
    const wchar_t* axes[]={L"Roll",L"Pitch",L"Yaw"};
    for(int i=0;i<3;i++){const auto& r=flightSettings.rates[i];auto prefix=std::wstring(axes[i]);ok=write(L"Rates",(prefix+L"Rate").c_str(),std::to_wstring(r.rate))&&ok;ok=write(L"Rates",(prefix+L"Super").c_str(),std::to_wstring(r.super))&&ok;ok=write(L"Rates",(prefix+L"Expo").c_str(),std::to_wstring(r.expo))&&ok;}
    ok=write(L"Flight",L"Mode3D",std::to_wstring(flightSettings.mode3d))&&ok;
    ok=write(L"Flight",L"ThrottleMid",std::to_wstring(flightSettings.throttleMid))&&ok;
    ok=write(L"Flight",L"ThrottleExpo",std::to_wstring(flightSettings.throttleExpo))&&ok;
    ok=write(L"Flight",L"ThrottleLimit",std::to_wstring(flightSettings.throttleLimit))&&ok;
    ok=write(L"Flight",L"Neutral3D",std::to_wstring(flightSettings.neutral3d))&&ok;
    ok=write(L"Flight",L"VehicleCollisions",std::to_wstring(flightSettings.vehicleCollisions))&&ok;
    ok=write(L"Audio",L"Enabled",std::to_wstring(audioSettings.enabled))&&ok;
    ok=write(L"Audio",L"Remote",std::to_wstring(audioSettings.remote))&&ok;
    ok=write(L"Audio",L"Volume",std::to_wstring(audioSettings.volume))&&ok;
    ok=write(L"Audio",L"RemoteVolume",std::to_wstring(audioSettings.remoteVolume))&&ok;
    ok=write(L"Audio",L"Range",std::to_wstring(audioSettings.range))&&ok;
    ok=write(L"OSD",L"Controls",std::to_wstring(showControls))&&ok;
    ok=write(L"OSD",L"Dashboard",std::to_wstring(showDroneHud))&&ok;
    ok=write(L"OSD",L"ReplaceBikeHud",std::to_wstring(replaceBikeHud))&&ok;
    const wchar_t* physicalKeys[]={L"MassGrams",L"PropInches",L"PropPitch",L"ThrustPerMotorGrams",L"MotorResponseMs",L"ThrottleCurve",L"DragArea",L"AngularResponseMs",L"CameraTilt",L"CollisionRadius",L"Restitution",L"GroundFriction",L"ReferenceProp",L"ReferencePitch",L"ReferenceCells",L"ReferenceBlades"};
    float physicalValues[]={flightSettings.massGrams,flightSettings.propInches,flightSettings.propPitch,flightSettings.thrustPerMotorGrams,flightSettings.motorResponseMs,flightSettings.throttleCurve,flightSettings.dragArea,flightSettings.angularResponseMs,flightSettings.cameraTilt,flightSettings.radius,flightSettings.restitution,flightSettings.groundFriction,flightSettings.referenceProp,flightSettings.referencePitch,flightSettings.referenceCells,flightSettings.referenceBlades};
    for(int i=0;i<16;i++)ok=write(L"Flight",physicalKeys[i],std::to_wstring(physicalValues[i]))&&ok;
    ok=write(L"Flight",L"Cells",std::to_wstring(flightSettings.cells))&&ok;
    ok=write(L"Flight",L"Blades",std::to_wstring(flightSettings.blades))&&ok;
    ok=write(L"Flight",L"PropType",std::to_wstring(flightSettings.propType))&&ok;
    ok=write(L"Flight",L"Collisions",flightSettings.collisions?L"1":L"0")&&ok;
    ok=write(L"Visuals",L"Goggles",visualSettings.goggles?L"1":L"0")&&ok;
    ok=write(L"Visuals",L"Controller",visualSettings.controller?L"1":L"0")&&ok;
    ok=write(L"Visuals",L"ControllerPose",visualSettings.pose?L"1":L"0")&&ok;
    ok=write(L"Visuals",L"RemoteDrones",visualSettings.drones?L"1":L"0")&&ok;
    ok=write(L"Visuals",L"ControllerDown",std::to_wstring(visualSettings.controllerDown))&&ok;
    ok=write(L"Visuals",L"ControllerForward",std::to_wstring(visualSettings.controllerForward))&&ok;
    ok=write(L"Visuals",L"GogglesForward",std::to_wstring(visualSettings.gogglesForward))&&ok;
    ok=write(L"Multiplayer",L"Transport",std::to_wstring(syncSettings.transport))&&ok;
    ok=write(L"Multiplayer",syncSettings.transport==0?L"Automatic":L"Enabled",syncSettings.enabled?L"1":L"0")&&ok;
    ok=write(L"Multiplayer",L"HostRelay",syncSettings.host?L"1":L"0")&&ok;
    ok=write(L"Multiplayer",L"Port",std::to_wstring(syncSettings.port))&&ok;
    ok=write(L"Multiplayer",L"Address",std::wstring(syncSettings.address.begin(),syncSettings.address.end()))&&ok;
    ok=write(L"Multiplayer",L"Room",std::wstring(syncSettings.room.begin(),syncSettings.room.end()))&&ok;
    ok=write(L"Updates",L"Automatic",automaticUpdates?L"1":L"0")&&ok;
    ok=write(L"Updates",L"TesterReleases",previewUpdates?L"1":L"0")&&ok;
    Log(ok?"In-game controls and flight settings saved.":"Could not save all FPV settings.");
}
void ReloadController() {
    std::lock_guard<std::mutex> lock(controllerMutex);
    readingFpv=true;controller.Open(gameWindow,deviceSettings);readingFpv=false;
    previousButton=false;active=false;
}
void ChannelBar(const char* id,float value,bool throttle,ImVec2 size) {
    ImGui::PushID(id);
    auto position=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("bar",size);
    auto draw=ImGui::GetWindowDrawList();
    float fraction=std::clamp(throttle?value:(value+1)*0.5f,0.0f,1.0f);
    float center=position.x+size.x*0.5f,endpoint=position.x+size.x*fraction;
    draw->AddRectFilled(position,{position.x+size.x,position.y+size.y},IM_COL32(34,41,50,255),4);
    float start=throttle?position.x:center;
    draw->AddRectFilled({std::min(start,endpoint),position.y+2},{std::max(start,endpoint),position.y+size.y-2},IM_COL32(31,196,166,255),3);
    if(!throttle)draw->AddLine({center,position.y},{center,position.y+size.y},IM_COL32(190,205,220,130),1);
    char text[32];sprintf_s(text,throttle?"%.0f %%":"%+.2f",throttle?value*100:value);
    auto textSize=ImGui::CalcTextSize(text);
    draw->AddText({position.x+(size.x-textSize.x)*0.5f,position.y+(size.y-textSize.y)*0.5f},IM_COL32(240,246,250,255),text);
    ImGui::PopID();
}
#include "settings_ui.inl"
#include "osd.inl"
BOOL WINAPI RenderFrame(HDC dc) {
    if(WindowFromDC(dc)!=gameWindow||!wglGetCurrentContext())return originalSwap(dc);
    static bool lateMaxHudAttempted=false;
    if(!originalMaxHudDraw&&!lateMaxHudAttempted){if(auto library=GetModuleHandleW(L"MaxHUD_MXB64.dlo")){lateMaxHudAttempted=true;if(auto draw=GetProcAddress(library,"Draw")){if(Hook(reinterpret_cast<void*>(draw),reinterpret_cast<void*>(MaxHudDraw),reinterpret_cast<void**>(&originalMaxHudDraw)))MH_EnableHook(reinterpret_cast<void*>(draw));}}}
    auto previous=ImGui::GetCurrentContext();
    if(!overlayReady) {
        IMGUI_CHECKVERSION();overlayContext=ImGui::CreateContext();ImGui::SetCurrentContext(overlayContext);
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
        io.FontGlobalScale=1.0f;
        if(std::filesystem::exists(L"C:/Windows/Fonts/tahoma.ttf"))io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/tahoma.ttf",17);
        ImGui::StyleColorsLight();auto& style=ImGui::GetStyle();style.WindowRounding=0;style.FrameRounding=0;style.ChildRounding=0;style.TabRounding=0;style.WindowPadding={14,12};style.ItemSpacing={9,8};style.FramePadding={8,5};
        style.Colors[ImGuiCol_WindowBg]={0.84f,0.86f,0.88f,0.99f};style.Colors[ImGuiCol_TitleBg]=style.Colors[ImGuiCol_TitleBgActive]={0.62f,0.69f,0.76f,1};
        style.Colors[ImGuiCol_Text]={0.07f,0.1f,0.14f,1};style.Colors[ImGuiCol_CheckMark]={0.1f,0.3f,0.5f,1};style.Colors[ImGuiCol_SliderGrab]={0.18f,0.36f,0.54f,1};
        style.Colors[ImGuiCol_Button]={0.73f,0.77f,0.81f,1};style.Colors[ImGuiCol_ButtonHovered]={0.58f,0.68f,0.78f,1};style.Colors[ImGuiCol_Header]={0.57f,0.67f,0.77f,1};
        const char* version=reinterpret_cast<const char*>(glGetString(GL_VERSION));
        bool platform=ImGui_ImplWin32_InitForOpenGL(gameWindow);
        bool renderer=platform&&ImGui_ImplOpenGL3_Init(version&&version[0]>='3'?"#version 130":"#version 120");
        if(!renderer){Log("OpenGL settings overlay initialization failed.");ImGui::SetCurrentContext(previous);return originalSwap(dc);}
        overlayReady=true;Log(std::string("In-game overlay initialized. OpenGL ")+(version?version:"unknown"));
    }
    ImGui::SetCurrentContext(overlayContext);
    settingsScreen=GameSettingsPage(gameBase);
    Update();
    PeerState own;own.race=riderVisuals.OwnRace();own.flying=active;own.preset=presetIndex;
    {std::lock_guard<std::mutex> lock(flightMutex);own.position=flight.position;own.rotation=flight.rotation;own.velocity=flight.velocity;own.motor=flight.crashed?0:std::max(0.02f,std::abs(flight.motorOutput));own.activity=std::max({std::abs(flightInput.roll),std::abs(flightInput.pitch),std::abs(flightInput.yaw)});own.propInches=flightSettings.propInches;}
    peerSync.Tick(own);auto peers=peerSync.Peers();riderVisuals.States(own.race,own.flying,peers);droneAudio.Submit(own,peers,audioSettings,onTrack);
    RECT viewport{};GetClientRect(gameWindow,&viewport);riderVisuals.Draw(viewport.right,viewport.bottom);
    ImGui_ImplOpenGL3_NewFrame();ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().MouseDrawCursor=menuOpen||settingsScreen;
    ImGui::NewFrame();Overlay();FlightOsd();ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(previous);
    return originalSwap(dc);
}
}

#define EXPORT extern "C" __declspec(dllexport)
EXPORT char* GetModID(){return const_cast<char*>("mxbikes");}
EXPORT const char* GetFpvPackageId(){return "MXBFPV_PLUGIN_IDENTIFIER_V1_2026";}
EXPORT int GetModDataVersion(){return 8;}
EXPORT int GetInterfaceVersion(){return 9;}
EXPORT int Startup(char*) {
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(module,path,MAX_PATH);
    configPath=std::filesystem::path(path).parent_path()/L"mxb_fpv.ini";
    logPath=std::filesystem::path(path).parent_path()/L"mxb_fpv.log";
    gameBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    supported=VerifyBuild();
    if(!supported) {Log("Unsupported executable. FPV hooks disabled.");return 0;}
    automaticUpdates=Number(L"Updates",L"Automatic",1,0,1)!=0;
    previewUpdates=Number(L"Updates",L"TesterReleases",1,0,1)!=0;
    updater.Start(automaticUpdates,previewUpdates);
    toggleKey=static_cast<int>(Number(L"Controls",L"ToggleKey",VK_F8,1,254));
    cameraFov=Number(L"Flight",L"Fov",100,60,120);
    presetIndex=static_cast<int>(Number(L"Flight",L"Preset",0,0,static_cast<float>(Presets().size()-1)));
    flightSettings=Presets()[presetIndex].settings;
    flightSettings.rcRate=Number(L"Flight",L"RcRate",flightSettings.rcRate,0.1f,2);
    flightSettings.superRate=Number(L"Flight",L"SuperRate",flightSettings.superRate,0,0.95f);
    flightSettings.expo=Number(L"Flight",L"Expo",flightSettings.expo,0,1);
    struct FloatSetting {const wchar_t* key;float* value;float minimum,maximum;};
    FloatSetting physical[]={
        {L"MassGrams",&flightSettings.massGrams,50,1800},{L"PropInches",&flightSettings.propInches,1.5f,7},{L"PropPitch",&flightSettings.propPitch,1,6},
        {L"ThrustPerMotorGrams",&flightSettings.thrustPerMotorGrams,60,2200},{L"MotorResponseMs",&flightSettings.motorResponseMs,10,100},
        {L"ThrottleCurve",&flightSettings.throttleCurve,0.7f,2},{L"DragArea",&flightSettings.dragArea,0.001f,0.1f},
        {L"AngularResponseMs",&flightSettings.angularResponseMs,5,100},{L"CameraTilt",&flightSettings.cameraTilt,0,60},{L"CollisionRadius",&flightSettings.radius,0.05f,0.3f},
        {L"Restitution",&flightSettings.restitution,0,0.8f},{L"GroundFriction",&flightSettings.groundFriction,0,1},
        {L"ReferenceProp",&flightSettings.referenceProp,1,10},{L"ReferencePitch",&flightSettings.referencePitch,0.5f,8},
        {L"ReferenceCells",&flightSettings.referenceCells,1,12},{L"ReferenceBlades",&flightSettings.referenceBlades,1,8}};
    for(auto& field:physical)*field.value=Number(L"Flight",field.key,*field.value,field.minimum,field.maximum);
    flightSettings.cells=static_cast<int>(Number(L"Flight",L"Cells",static_cast<float>(flightSettings.cells),2,8));
    flightSettings.blades=static_cast<int>(Number(L"Flight",L"Blades",static_cast<float>(flightSettings.blades),2,5));
    flightSettings.propType=static_cast<int>(Number(L"Flight",L"PropType",static_cast<float>(flightSettings.propType),0,2));
    flightSettings.collisions=Number(L"Flight",L"Collisions",1,0,1)!=0;
    flightSettings.rateModel=static_cast<int>(Number(L"Rates",L"Model",0,0,4));
    flightSettings.individualRates=Number(L"Rates",L"Individual",1,0,1)!=0;
    flightSettings.quickRcExpo=Number(L"Rates",L"QuickRcExpo",0,0,1)!=0;
    const wchar_t* rateAxes[]={L"Roll",L"Pitch",L"Yaw"};for(int i=0;i<3;i++){auto prefix=std::wstring(rateAxes[i]);auto& r=flightSettings.rates[i];bool absolute=flightSettings.rateModel>=3;bool race=flightSettings.rateModel==1;
        r.rate=Number(L"Rates",(prefix+L"Rate").c_str(),absolute?200.0f:race?80.0f:flightSettings.rcRate,0.01f,absolute?1000.0f:race?200.0f:3.0f);
        r.super=Number(L"Rates",(prefix+L"Super").c_str(),absolute?670:race?70:flightSettings.superRate,0,absolute?1998:race?100:0.99f);
        r.expo=Number(L"Rates",(prefix+L"Expo").c_str(),flightSettings.expo,0,1);}
    flightSettings.mode3d=Number(L"Flight",L"Mode3D",0,0,1)!=0;
    flightSettings.throttleMid=Number(L"Flight",L"ThrottleMid",0.5f,0.05f,0.95f);
    flightSettings.throttleExpo=Number(L"Flight",L"ThrottleExpo",0,0,1);
    flightSettings.throttleLimit=Number(L"Flight",L"ThrottleLimit",1,0.1f,1);
    flightSettings.neutral3d=Number(L"Flight",L"Neutral3D",0.05f,0.01f,0.25f);
    flightSettings.vehicleCollisions=Number(L"Flight",L"VehicleCollisions",1,0,1)!=0;
    audioSettings.enabled=Number(L"Audio",L"Enabled",1,0,1)!=0;audioSettings.remote=Number(L"Audio",L"Remote",1,0,1)!=0;
    audioSettings.volume=Number(L"Audio",L"Volume",0.7f,0,2);audioSettings.remoteVolume=Number(L"Audio",L"RemoteVolume",1,0,2);audioSettings.range=Number(L"Audio",L"Range",180,30,300);
    showControls=Number(L"OSD",L"Controls",1,0,1)!=0;showDroneHud=Number(L"OSD",L"Dashboard",1,0,1)!=0;replaceBikeHud=Number(L"OSD",L"ReplaceBikeHud",1,0,1)!=0;
    droneAudio.Load(reinterpret_cast<std::uintptr_t>(module));
    auto& settings=deviceSettings;
    settings.guid=Setting(L"Controls",L"DeviceGuid",L"auto");
    settings.useDirectInput=Setting(L"Controls",L"Backend",L"auto")==L"directinput";
    settings.xinputIndex=static_cast<int>(Number(L"Controls",L"XInputIndex",0,0,3));
    settings.toggleButton=static_cast<int>(Number(L"Controls",L"ToggleButton",-1,-1,127));
    settings.rollAxis=static_cast<int>(Number(L"Controls",L"RollAxis",0,0,7));
    settings.pitchAxis=static_cast<int>(Number(L"Controls",L"PitchAxis",1,0,7));
    settings.yawAxis=static_cast<int>(Number(L"Controls",L"YawAxis",3,0,7));
    settings.throttleAxis=static_cast<int>(Number(L"Controls",L"ThrottleAxis",2,0,7));
    settings.invertRoll=Number(L"Controls",L"InvertRoll",0,0,1)!=0;
    settings.invertPitch=Number(L"Controls",L"InvertPitch",0,0,1)!=0;
    settings.invertYaw=Number(L"Controls",L"InvertYaw",0,0,1)!=0;
    settings.invertThrottle=Number(L"Controls",L"InvertThrottle",0,0,1)!=0;
    settings.deadzone=Number(L"Controls",L"Deadzone",0.05f,0,0.5f);
    gameWindow=FindWindowForGame();
    controller.Open(gameWindow,settings);
    if(gameWindow) originalWindowProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(WindowProc)));
    Log(terrain.Initialize(gameBase)?"Native terrain sampler signature verified.":"Terrain collision unavailable: sampler signature mismatch.");
    visualSettings.goggles=Number(L"Visuals",L"Goggles",1,0,1)!=0;
    visualSettings.controller=Number(L"Visuals",L"Controller",1,0,1)!=0;
    visualSettings.pose=Number(L"Visuals",L"ControllerPose",1,0,1)!=0;
    visualSettings.drones=Number(L"Visuals",L"RemoteDrones",1,0,1)!=0;
    visualSettings.controllerDown=Number(L"Visuals",L"ControllerDown",0.48f,0.2f,0.8f);
    visualSettings.controllerForward=Number(L"Visuals",L"ControllerForward",0.3f,0.1f,0.6f);
    visualSettings.gogglesForward=Number(L"Visuals",L"GogglesForward",0.1f,0.02f,0.25f);
    riderVisuals.Configure(gameBase,visualSettings);
    syncSettings.transport=static_cast<int>(Number(L"Multiplayer",L"Transport",0,0,1));
    syncSettings.enabled=syncSettings.transport==0?Number(L"Multiplayer",L"Automatic",1,0,1)!=0:Number(L"Multiplayer",L"Enabled",0,0,1)!=0;
    syncSettings.host=Number(L"Multiplayer",L"HostRelay",0,0,1)!=0;
    syncSettings.port=static_cast<int>(Number(L"Multiplayer",L"Port",48127,1024,65535));
    syncSettings.address=Utf8(Setting(L"Multiplayer",L"Address",L"127.0.0.1"));
    syncSettings.room=Utf8(Setting(L"Multiplayer",L"Room",L"mx-fpv"));
    peerSync.Configure(syncSettings);
    hooksReady=InstallHooks();
    Log(hooksReady?"MXB FPV 0.6 initialized. F8 toggles FPV; Settings > FPV configures the drone.":"Hook installation failed. FPV unavailable.");
    return 0;
}
EXPORT void Shutdown(){
    active=false;menuOpen=false;peerSync.Close();
    updater.Close();
    if(originalWindowProc&&IsWindow(gameWindow)&&reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow,GWLP_WNDPROC))==WindowProc)SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalWindowProc));
    if(supported){MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();}
    droneAudio.Close();
    if(overlayReady){ImGui::SetCurrentContext(overlayContext);if(wglGetCurrentContext())ImGui_ImplOpenGL3_Shutdown();ImGui_ImplWin32_Shutdown();ImGui::DestroyContext(overlayContext);overlayContext=nullptr;overlayReady=false;}
    controller.Close();for(auto& hook:xhooks)if(hook.library){FreeLibrary(hook.library);hook.library=nullptr;}
}
EXPORT void RunStart(){onTrack=true;Log("Riding simulation started.");}
EXPORT void RunStop(){onTrack=false;active=false;Log("Riding simulation paused.");}
EXPORT void RunDeinit(){onTrack=false;active=false;}
EXPORT void RunTelemetry(void* data,int size,float time,float) {
    // Official interface v8. World position follows six 32-bit fields.
    if(!data || size<120) return;
    const float* fields=static_cast<const float*>(data);
    if(!std::isfinite(fields[6])||!std::isfinite(fields[7])||!std::isfinite(fields[8])||!std::isfinite(fields[24])){active=false;return;}
    std::lock_guard<std::mutex> lock(flightMutex);
    bikePosition={fields[6],fields[7],fields[8]};
    bikeYaw=fields[24];
    static bool reported=false;if(!reported){Log("Live bike telemetry received.");reported=true;}
    telemetryTick=GetTickCount64();
    float dt=time-previousTelemetryTime;previousTelemetryTime=time;
    if(!active)return;
    if(dt<=0||dt>0.1f)dt=0.01f;
    Input input=PollController();
    if(!input.connected) {
        input.throttle=flightSettings.mode3d?0.5f+flightSettings.HoverThrottle()*0.5f:flightSettings.HoverThrottle();
        if(GetAsyncKeyState(VK_SPACE)&0x8000)input.throttle=0.75f;
        if(GetAsyncKeyState(VK_CONTROL)&0x8000)input.throttle=0;
        input.pitch=((GetAsyncKeyState('W')&0x8000)?1.0f:0)-((GetAsyncKeyState('S')&0x8000)?1.0f:0);
        input.roll=((GetAsyncKeyState('D')&0x8000)?1.0f:0)-((GetAsyncKeyState('A')&0x8000)?1.0f:0);
        input.yaw=((GetAsyncKeyState('E')&0x8000)?1.0f:0)-((GetAsyncKeyState('Q')&0x8000)?1.0f:0);
    }
    flightInput=input;
    if(flightSettings.collisions) {
        float height{};Vec3 normal;
        if(!terrain.Sample(flight.position.x,flight.position.z,height,normal)){active=false;Log("FPV returned: no valid terrain under drone. Ground collision remains enabled.");return;}
    }
    auto ground=flightSettings.collisions?terrain.World():CollisionWorld{};
    traffic.Capture(gameBase,dt,flightSettings.vehicleCollisions,ground);auto world=traffic.World();
    accumulator+=std::min(dt,0.05f);
    while(accumulator>=0.004f){flight.Step(input,flightSettings,0.004f,&world);accumulator-=0.004f;}
}
EXPORT int DrawInit(int* sprites,char** spriteNames,int* fonts,char** fontNames) {
    *sprites=0;*spriteNames=nullptr;*fonts=0;*fontNames=nullptr;return 0;
}
EXPORT void Draw(int state,int* numQuads,void**,int* numStrings,void**) {
    *numQuads=0;*numStrings=0;
    if(state!=0) {active=false;return;}
    static bool reported=false;if(!reported){Log("Live Draw callback received.");reported=true;}
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);}return TRUE;}
