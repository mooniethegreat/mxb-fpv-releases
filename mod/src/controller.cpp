#include "controller.h"
#include <objbase.h>

namespace fpv {
float Controller::Axis(float value,bool invert,bool applyDeadzone) const {
    value=std::clamp(value,-1.0f,1.0f);
    if(invert) value=-value;
    if(!applyDeadzone) return value;
    float magnitude=std::abs(value);
    if(magnitude<=settings_.deadzone) return 0;
    return std::copysign((magnitude-settings_.deadzone)/(1-settings_.deadzone),value);
}
BOOL CALLBACK Controller::AxisRange(const DIDEVICEOBJECTINSTANCEW* object,void* context) {
    auto self=static_cast<Controller*>(context);
    DIPROPRANGE range{};
    range.diph={sizeof(range),sizeof(DIPROPHEADER),object->dwType,DIPH_BYID};
    range.lMin=-32767; range.lMax=32767;
    self->device_->SetProperty(DIPROP_RANGE,&range.diph);
    return DIENUM_CONTINUE;
}
BOOL CALLBACK Controller::Enumerate(const DIDEVICEINSTANCEW* info,void* context) {
    auto self=static_cast<Controller*>(context);
    wchar_t guid[64]{};
    StringFromGUID2(info->guidInstance,guid,64);
    if(self->settings_.guid!=L"auto" && _wcsicmp(guid,self->settings_.guid.c_str())!=0) return DIENUM_CONTINUE;
    IDirectInputDevice8W* candidate{};
    if(FAILED(self->directInput_->CreateDevice(info->guidInstance,&candidate,nullptr))) return DIENUM_CONTINUE;
    if(FAILED(candidate->SetDataFormat(&c_dfDIJoystick2)) ||
       FAILED(candidate->SetCooperativeLevel(self->window_,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND))) {
        candidate->Release(); return DIENUM_CONTINUE;
    }
    self->device_=candidate;
    self->device_->EnumObjects(AxisRange,self,DIDFT_AXIS);
    self->device_->Acquire();
    self->selectedGuid_=guid;
    self->name_=info->tszProductName;
    return DIENUM_STOP;
}
void Controller::Open(HWND window,DeviceSettings settings) {
    Close(); window_=window; settings_=settings;
    for(auto dll:{L"xinput1_4.dll",L"xinput1_3.dll",L"xinput9_1_0.dll"}) {
        xinput_=LoadLibraryExW(dll,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(xinput_) break;
    }
    if(xinput_) getState_=reinterpret_cast<GetState>(GetProcAddress(xinput_,"XInputGetState"));
    DirectInput8Create(GetModuleHandleW(nullptr),DIRECTINPUT_VERSION,IID_IDirectInput8W,
        reinterpret_cast<void**>(&directInput_),nullptr);
    Scan();
}
void Controller::Scan() {
    lastScan_=GetTickCount64();
    if(!device_ && directInput_) directInput_->EnumDevices(DI8DEVCLASS_GAMECTRL,Enumerate,this,DIEDFL_ATTACHEDONLY);
}
Input Controller::Poll() {
    pressedButton_=-1;
    std::array<float,8> axes{};
    bool found=false;
    bool toggle=false;
    if(!settings_.useDirectInput && getState_) {
        XINPUT_STATE state{};
        if(getState_(settings_.xinputIndex,&state)==ERROR_SUCCESS) {
            axes={state.Gamepad.sThumbRX/32767.0f,state.Gamepad.sThumbRY/32767.0f,
                state.Gamepad.sThumbLX/32767.0f,state.Gamepad.sThumbLY/32767.0f,
                state.Gamepad.bLeftTrigger/127.5f-1,state.Gamepad.bRightTrigger/127.5f-1,0,0};
            name_=L"XInput gamepad "+std::to_wstring(settings_.xinputIndex+1);
            if(settings_.toggleButton>=0 && settings_.toggleButton<16)
                toggle=(state.Gamepad.wButtons & (1u<<settings_.toggleButton))!=0;
            found=true;
            for(int i=0;i<16;i++)if(state.Gamepad.wButtons&(1u<<i)){pressedButton_=i;break;}
        }
    }
    if(!found) {
        if(!device_ && GetTickCount64()-lastScan_>1500) Scan();
        if(device_) {
            if(FAILED(device_->Poll())) device_->Acquire();
            DIJOYSTATE2 state{};
            if(SUCCEEDED(device_->GetDeviceState(sizeof(state),&state))) {
                axes={state.lX/32767.0f,state.lY/32767.0f,state.lZ/32767.0f,
                    state.lRx/32767.0f,state.lRy/32767.0f,state.lRz/32767.0f,
                    state.rglSlider[0]/32767.0f,state.rglSlider[1]/32767.0f};
                found=true;
                for(int i=0;i<128;i++)if(state.rgbButtons[i]&0x80){pressedButton_=i;break;}
                if(settings_.toggleButton>=0 && settings_.toggleButton<128)
                    toggle=(state.rgbButtons[settings_.toggleButton]&0x80)!=0;
            } else {
                device_->Unacquire(); device_->Release(); device_=nullptr;
            }
        }
    }
    axes_=axes;
    if(!found) { name_=L"No controller"; return {}; }
    auto mapped=[&](int index,bool invert,bool deadzone) {
        return Axis(axes[std::clamp(index,0,7)],invert,deadzone);
    };
    return {(mapped(settings_.throttleAxis,settings_.invertThrottle,false)+1)*0.5f,
        mapped(settings_.yawAxis,settings_.invertYaw,true),
        mapped(settings_.pitchAxis,settings_.invertPitch,true),
        mapped(settings_.rollAxis,settings_.invertRoll,true),true,toggle};
}
void Controller::Close() {
    if(device_) {device_->Unacquire();device_->Release();device_=nullptr;}
    if(directInput_) {directInput_->Release();directInput_=nullptr;}
    if(xinput_) {FreeLibrary(xinput_);xinput_=nullptr;}
    getState_=nullptr;
}
}
