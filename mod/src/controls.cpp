#include "controller.h"
#include <filesystem>
#include <sstream>
#include <iomanip>

namespace {
HINSTANCE instance; HWND mainWindow,backend,guidEdit,live,raw,axis[4],invert[4],buttonEdit,keyEdit,deadzoneEdit;
std::filesystem::path config; fpv::Controller controller; HFONT font;
std::wstring Read(const wchar_t* key,const wchar_t* fallback) {
    wchar_t text[256]{};GetPrivateProfileStringW(L"Controls",key,fallback,text,256,config.c_str());return text;
}
std::wstring Text(HWND control) {wchar_t buffer[256]{};GetWindowTextW(control,buffer,256);return buffer;}
HWND Control(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND c=CreateWindowExW(type==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;
}
fpv::DeviceSettings Settings() {
    fpv::DeviceSettings s;
    s.useDirectInput=SendMessageW(backend,CB_GETCURSEL,0,0)==1;
    s.guid=Text(guidEdit);if(s.guid.empty())s.guid=L"auto";
    int* axes[]={&s.rollAxis,&s.pitchAxis,&s.yawAxis,&s.throttleAxis};
    bool* inverted[]={&s.invertRoll,&s.invertPitch,&s.invertYaw,&s.invertThrottle};
    for(int i=0;i<4;i++){*axes[i]=static_cast<int>(SendMessageW(axis[i],CB_GETCURSEL,0,0));*inverted[i]=SendMessageW(invert[i],BM_GETCHECK,0,0)==BST_CHECKED;}
    s.toggleButton=std::clamp(_wtoi(Text(buttonEdit).c_str()),-1,127);
    s.deadzone=std::clamp(static_cast<float>(_wtof(Text(deadzoneEdit).c_str())),0.0f,0.5f);
    s.xinputIndex=_wtoi(Read(L"XInputIndex",L"0").c_str());
    return s;
}
void Apply(bool save) {
    auto s=Settings();
    if(save) {
        auto write=[&](const wchar_t* key,const std::wstring& value){WritePrivateProfileStringW(L"Controls",key,value.c_str(),config.c_str());};
        write(L"Backend",s.useDirectInput?L"directinput":L"auto");write(L"DeviceGuid",s.guid);
        const wchar_t* keys[]={L"RollAxis",L"PitchAxis",L"YawAxis",L"ThrottleAxis",L"InvertRoll",L"InvertPitch",L"InvertYaw",L"InvertThrottle"};
        int values[]={s.rollAxis,s.pitchAxis,s.yawAxis,s.throttleAxis,s.invertRoll,s.invertPitch,s.invertYaw,s.invertThrottle};
        for(int i=0;i<8;i++)write(keys[i],std::to_wstring(values[i]));
        write(L"ToggleButton",std::to_wstring(s.toggleButton));write(L"Deadzone",std::to_wstring(s.deadzone));
        write(L"ToggleKey",std::to_wstring(std::clamp(_wtoi(Text(keyEdit).c_str()),1,254)));
    }
    controller.Open(mainWindow,s);
}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_CREATE) {
        mainWindow=window;font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        Control(L"STATIC",L"MXB FPV controller setup",0,20,15,650,25);
        Control(L"STATIC",L"Connect your gamepad or USB FPV radio, then check the live values below.",0,20,43,660,22);
        Control(L"STATIC",L"Input backend",0,20,78,120,22);
        backend=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,145,75,490,100);
        SendMessageW(backend,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Auto (XInput gamepad first, then DirectInput)"));
        SendMessageW(backend,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"DirectInput (USB FPV radio / joystick)"));
        SendMessageW(backend,CB_SETCURSEL,Read(L"Backend",L"auto")==L"directinput"?1:0,0);
        Control(L"STATIC",L"Device GUID",0,20,114,120,22);
        guidEdit=Control(L"EDIT",Read(L"DeviceGuid",L"auto").c_str(),ES_AUTOHSCROLL,145,110,350,26);
        Control(L"BUTTON",L"Use detected",BS_PUSHBUTTON,505,110,130,26,12);
        const wchar_t* labels[]={L"Roll",L"Pitch",L"Yaw",L"Throttle"};
        const wchar_t* axisKeys[]={L"RollAxis",L"PitchAxis",L"YawAxis",L"ThrottleAxis"};
        const wchar_t* invKeys[]={L"InvertRoll",L"InvertPitch",L"InvertYaw",L"InvertThrottle"};
        for(int i=0;i<4;i++) {
            int y=155+i*34;Control(L"STATIC",labels[i],0,20,y+3,115,22);
            axis[i]=Control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,145,y,250,250);
            for(int j=0;j<8;j++){auto label=L"Axis "+std::to_wstring(j);SendMessageW(axis[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
            SendMessageW(axis[i],CB_SETCURSEL,std::clamp(_wtoi(Read(axisKeys[i],std::to_wstring(i).c_str()).c_str()),0,7),0);
            invert[i]=Control(L"BUTTON",L"Invert",BS_AUTOCHECKBOX,415,y,120,26);
            SendMessageW(invert[i],BM_SETCHECK,_wtoi(Read(invKeys[i],L"0").c_str())?BST_CHECKED:BST_UNCHECKED,0);
        }
        Control(L"STATIC",L"Toggle button (-1 off)",0,20,300,160,24);
        buttonEdit=Control(L"EDIT",Read(L"ToggleButton",L"-1").c_str(),ES_AUTOHSCROLL,185,296,65,26);
        Control(L"STATIC",L"Toggle key (119 = F8)",0,280,300,170,24);
        keyEdit=Control(L"EDIT",Read(L"ToggleKey",L"119").c_str(),ES_AUTOHSCROLL,450,296,65,26);
        Control(L"STATIC",L"Deadzone (0 to 0.5)",0,20,336,160,24);
        deadzoneEdit=Control(L"EDIT",Read(L"Deadzone",L"0.05").c_str(),ES_AUTOHSCROLL,185,332,65,26);
        Control(L"BUTTON",L"Preview mapping",BS_PUSHBUTTON,280,332,170,28,10);
        Control(L"BUTTON",L"Save",BS_DEFPUSHBUTTON,465,332,170,28,11);
        live=Control(L"STATIC",L"Connecting...",0,20,378,650,65);
        raw=Control(L"STATIC",L"",0,20,448,650,45);
        Control(L"STATIC",L"DirectInput: X, Y, Z, Rx, Ry, Rz, slider 1, slider 2.\nXInput: right X/Y, left X/Y, left/right trigger. Throttle should read 0 at low stick.\nRestart MX Bikes after saving. F8 toggles FPV; Escape returns to the bike/menu.",0,20,502,650,60);
        Apply(false);SetTimer(window,1,80,nullptr);return 0;
    }
    if(message==WM_TIMER) {
        auto input=controller.Poll();std::wostringstream text;text<<std::fixed<<std::setprecision(2);
        text<<controller.Name()<<L"\nRoll: "<<input.roll<<L"     Pitch: "<<input.pitch<<L"     Yaw: "<<input.yaw<<L"     Throttle: "<<input.throttle;
        text<<L"\nToggle: "<<(input.toggle?L"pressed":L"released");SetWindowTextW(live,text.str().c_str());
        std::wostringstream values;values<<L"Raw axes: "<<std::fixed<<std::setprecision(2);int i=0;
        for(auto value:controller.Axes())values<<i++<<L"="<<value<<L"  ";SetWindowTextW(raw,values.str().c_str());return 0;
    }
    if(message==WM_COMMAND) {
        if(LOWORD(wParam)==10)Apply(false);
        if(LOWORD(wParam)==11){Apply(true);MessageBoxW(window,L"Controls saved. Restart MX Bikes to use this mapping.",L"MXB FPV",MB_OK);}
        if(LOWORD(wParam)==12 && !controller.Guid().empty())SetWindowTextW(guidEdit,controller.Guid().c_str());
        return 0;
    }
    if(message==WM_DESTROY){controller.Close();PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,PWSTR,int show) {
    instance=app;SetProcessDPIAware();wchar_t filename[MAX_PATH]{};GetModuleFileNameW(nullptr,filename,MAX_PATH);
    auto folder=std::filesystem::path(filename).parent_path();
    config=(folder.filename()==L"mxb_fpv"?folder.parent_path():folder)/L"mxb_fpv.ini";
    WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=app;wc.lpszClassName=L"MXBFPVControls";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"MXB FPV - Controller setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,710,620,nullptr,nullptr,app,nullptr);
    ShowWindow(window,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}return 0;
}
