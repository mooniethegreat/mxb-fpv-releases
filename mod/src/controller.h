#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <Xinput.h>
#include <array>
#include <string>
#include "flight.h"

namespace fpv {
struct DeviceSettings {
    std::wstring guid{L"auto"};
    int xinputIndex{0};
    int toggleButton{-1};
    int rollAxis{0}, pitchAxis{1}, yawAxis{3}, throttleAxis{2};
    bool invertRoll{}, invertPitch{}, invertYaw{}, invertThrottle{};
    float deadzone{0.05f};
    bool useDirectInput{};
};
class Controller {
    IDirectInput8W* directInput_{};
    IDirectInputDevice8W* device_{};
    HMODULE xinput_{};
    using GetState = DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
    GetState getState_{};
    DeviceSettings settings_{};
    HWND window_{};
    ULONGLONG lastScan_{};
    std::wstring name_{L"No controller"};
    std::wstring selectedGuid_{};
    std::array<float,8> axes_{};
    int pressedButton_{-1};
    static BOOL CALLBACK Enumerate(const DIDEVICEINSTANCEW*,void*);
    static BOOL CALLBACK AxisRange(const DIDEVICEOBJECTINSTANCEW*,void*);
    float Axis(float value, bool invert, bool deadzone) const;
    void Scan();
public:
    void Open(HWND window, DeviceSettings settings);
    Input Poll();
    std::wstring Name() const { return name_; }
    std::wstring Guid() const { return selectedGuid_; }
    std::array<float,8> Axes() const { return axes_; }
    int Button() const {return pressedButton_;}
    void Close();
    ~Controller() { Close(); }
};
}
