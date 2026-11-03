#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <regex>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include "version.h"

namespace {
namespace fs=std::filesystem;
HINSTANCE instance;HWND pathEdit,statusLabel;HFONT font;
const char* expected=fpv::SupportedGameHash;
std::vector<unsigned char> Bytes(const fs::path& path) {std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::string Hash(const fs::path& path) {
    auto bytes=Bytes(path);if(bytes.empty())throw std::runtime_error("MX Bikes executable not found.");
    BCRYPT_ALG_HANDLE algorithm{};unsigned char digest[32]{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 is unavailable.");
    auto result=BCryptHash(algorithm,nullptr,0,bytes.data(),static_cast<ULONG>(bytes.size()),digest,32);BCryptCloseAlgorithmProvider(algorithm,0);
    if(result<0)throw std::runtime_error("Cannot verify executable.");
    char hex[65]{};for(int i=0;i<32;i++)sprintf_s(hex+2*i,3,"%02x",digest[i]);return hex;
}
std::vector<unsigned char> Resource(int id) {
    auto resource=FindResourceW(instance,MAKEINTRESOURCEW(id),RT_RCDATA);if(!resource)throw std::runtime_error("Installer payload is missing.");
    auto size=SizeofResource(instance,resource);auto data=static_cast<const unsigned char*>(LockResource(LoadResource(instance,resource)));
    if(!data||!size)throw std::runtime_error("Installer payload is empty.");return {data,data+size};
}
bool OwnPlugin(const fs::path& path) {
    auto bytes=Bytes(path);const std::string marker="MXBFPV_PLUGIN_IDENTIFIER_V1_2026";
    if(std::search(bytes.begin(),bytes.end(),marker.begin(),marker.end())!=bytes.end())return true;
    auto hash=Hash(path);
    return hash=="d26f6072020493685cc54183f1ba61ee7b88892e5e1e22ee3ce54dcc880a1125"||
        hash=="29a9cbfe17aed03dd84769a79cbc88808c43cee97e33aa068ea2b5d0df782dac"||
        hash=="6dffa35d290d449bbf2eff2898c5c4900ed6e374667e6b1f0e8740551964bea5"||
        hash=="b8d530c0d04684209a7c1f011ba5136c6cf6995da638d8557860ec6fd9c05f09";
}
void Write(int id,const fs::path& path) {
    auto data=Resource(id);std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()),data.size());file.close();
    if(!file)throw std::runtime_error("Cannot write installer files. Check the folder permissions.");
}
bool Running(const fs::path& game) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return true;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);bool running=false;
    if(Process32FirstW(snapshot,&entry))do {
        if(_wcsicmp(entry.szExeFile,L"mxbikes.exe"))continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!process){running=true;break;}
        wchar_t path[32768]{};DWORD size=32768;
        if(QueryFullProcessImageNameW(process,0,path,&size)&&_wcsicmp(fs::absolute(game/L"mxbikes.exe").c_str(),path)==0)running=true;
        CloseHandle(process);if(running)break;
    }while(Process32NextW(snapshot,&entry));CloseHandle(snapshot);return running;
}
void Install(const fs::path& folder) {
    if(Hash(folder/L"mxbikes.exe")!=expected)throw std::runtime_error("This preview supports only the installed MX Bikes build used for development. This executable differs; no files were installed.");
    if(Running(folder))throw std::runtime_error("Close MX Bikes before installing the FPV plugin.");
    auto plugins=folder/L"plugins",tools=plugins/L"mxb_fpv",target=plugins/L"mxb_fpv.dlo";
    if(fs::exists(target)&&!OwnPlugin(target))throw std::runtime_error("An unrecognized mxb_fpv.dlo already exists. Back it up and remove it before installing.");
    fs::create_directories(tools);
    // Keep configuration in place; stage the binary before replacing a recognized plugin.
    Write(104,tools/L"README.txt");Write(105,tools/L"MinHook-LICENSE.txt");Write(106,tools/L"ImGui-LICENSE.txt");Write(107,tools/L"FrostMod-LICENSE.txt");Write(108,tools/L"THIRD-PARTY-NOTICES.txt");
    Write(109,tools/L"Kino-AUDIO-PROVENANCE.txt");
    if(!fs::exists(plugins/L"mxb_fpv.ini"))Write(102,plugins/L"mxb_fpv.ini");
    auto staged=plugins/L"mxb_fpv.dlo.new";
    try {
        Write(101,staged);
        if(fs::exists(target)){
            fs::create_directories(tools/L"previous");
            fs::copy_file(target,tools/L"previous"/L"mxb_fpv.dlo",fs::copy_options::overwrite_existing);
        }
        if(!MoveFileExW(staged.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace the FPV plugin. Close the game and try again.");
    }catch(...){std::error_code ignored;fs::remove(staged,ignored);throw;}
}
void Restore(const fs::path& folder) {
    if(Hash(folder/L"mxbikes.exe")!=expected)throw std::runtime_error("The selected MX Bikes build is not supported by this installer.");
    if(Running(folder))throw std::runtime_error("Close MX Bikes before restoring the previous plugin.");
    auto plugins=folder/L"plugins",target=plugins/L"mxb_fpv.dlo",previous=plugins/L"mxb_fpv"/L"previous"/L"mxb_fpv.dlo";
    if(!fs::exists(previous)||!OwnPlugin(previous))throw std::runtime_error("No recognized previous FPV plugin is available.");
    if(fs::exists(target)&&!OwnPlugin(target))throw std::runtime_error("The current FPV plugin is not recognized. No files were changed.");
    auto staged=plugins/L"mxb_fpv.dlo.restore";
    try{
        fs::copy_file(previous,staged,fs::copy_options::overwrite_existing);
        if(!MoveFileExW(staged.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot restore the previous plugin. Check folder permissions.");
    }catch(...){std::error_code ignored;fs::remove(staged,ignored);throw;}
}
void Remove(const fs::path& folder) {
    if(!fs::exists(folder/L"mxbikes.exe"))throw std::runtime_error("Choose the MX Bikes installation folder.");
    if(Running(folder))throw std::runtime_error("Close MX Bikes before removing the plugin.");
    auto plugins=folder/L"plugins",target=plugins/L"mxb_fpv.dlo",tools=plugins/L"mxb_fpv";
    if(fs::exists(target)&&!OwnPlugin(target))throw std::runtime_error("The plugin was not recognized as MXB FPV. Remove it manually if appropriate.");
    fs::remove(target);
    for(auto file:{L"MXB-FPV-Controls.exe",L"README.txt",L"MinHook-LICENSE.txt",L"ImGui-LICENSE.txt",L"FrostMod-LICENSE.txt",L"THIRD-PARTY-NOTICES.txt"})fs::remove(tools/file);
    fs::remove(tools/L"Kino-AUDIO-PROVENANCE.txt");
    std::error_code ignored;fs::remove(tools,ignored);
    // Keep user mappings and diagnostics. Never remove another plugin.
}
fs::path Detect() {
    std::vector<fs::path> roots;
    wchar_t steam[32768]{};DWORD size=sizeof(steam);
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steam,&size)==ERROR_SUCCESS)roots.emplace_back(steam);
    roots.emplace_back(L"C:\\Program Files (x86)\\Steam");
    auto initial=roots;
    for(auto& root:initial) {
        auto bytes=Bytes(root/L"steamapps"/L"libraryfolders.vdf");std::string vdf(bytes.begin(),bytes.end());
        std::regex pattern("\"path\"\\s*\"([^\"]+)\"");
        for(std::sregex_iterator i(vdf.begin(),vdf.end(),pattern),end;i!=end;++i) {
            std::string text=(*i)[1].str();for(size_t p=0;(p=text.find("\\\\",p))!=std::string::npos;)text.replace(p,2,"\\");
            roots.emplace_back(fs::u8path(text));
        }
    }
    for(auto& root:roots){auto candidate=root/L"steamapps"/L"common"/L"MX Bikes";if(fs::exists(candidate/L"mxbikes.exe"))return candidate;}
    return {};
}
fs::path Selection() {wchar_t path[32768]{};GetWindowTextW(pathEdit,path,32768);return fs::path(path).lexically_normal();}
void Browse(HWND window) {
    IFileOpenDialog* dialog{};
    if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) {
        DWORD flags{};dialog->GetOptions(&flags);dialog->SetOptions(flags|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);
        dialog->SetTitle(L"Choose the MX Bikes folder containing mxbikes.exe");
        if(SUCCEEDED(dialog->Show(window))){IShellItem* item{};if(SUCCEEDED(dialog->GetResult(&item))){PWSTR path{};if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){SetWindowTextW(pathEdit,path);CoTaskMemFree(path);}item->Release();}}
        dialog->Release();
    }
}
HWND Control(HWND window,const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND control=CreateWindowExW(std::wstring(type)==L"EDIT"?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return control;
}
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_CREATE) {
        font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        Control(window,L"STATIC",L"MX Bikes FPV 0.6 - automatic multiplayer sync candidate",0,22,20,620,25);
        Control(window,L"STATIC",L"F8 while riding: enter FPV beside your bike / return to the bike.\nIn game: Settings > FPV Drone Settings for live stick mapping and drone presets.",0,22,55,620,48);
        Control(window,L"STATIC",L"MX Bikes installation folder",0,22,120,620,22);
        pathEdit=Control(window,L"EDIT",Detect().c_str(),ES_AUTOHSCROLL|WS_TABSTOP,22,145,485,28);
        Control(window,L"BUTTON",L"Browse...",BS_PUSHBUTTON|WS_TABSTOP,517,145,105,28,1);
        Control(window,L"BUTTON",L"Install",BS_DEFPUSHBUTTON|WS_TABSTOP,22,195,140,38,2);
        Control(window,L"BUTTON",L"Start MX Bikes",BS_PUSHBUTTON|WS_TABSTOP,177,195,145,38,3);
        Control(window,L"BUTTON",L"Uninstall",BS_PUSHBUTTON|WS_TABSTOP,337,195,140,38,4);
        Control(window,L"BUTTON",L"Roll back",BS_PUSHBUTTON|WS_TABSTOP,492,195,130,38,5);
        statusLabel=Control(window,L"STATIC",L"Close MX Bikes before installing. Existing controller settings are preserved.",0,22,256,600,55);
        Control(window,L"STATIC",L"Shared drones and sound through the game server - no separate relay setup.\nAll viewers need FPV 0.6. Includes collisions, rates, 3D mode and in-game settings.",0,22,326,610,45);return 0;
    }
    if(message==WM_COMMAND) {
        try {
            int id=LOWORD(wParam);
            if(id==1)Browse(window);
            if(id==2){Install(Selection());SetWindowTextW(statusLabel,L"Installed. Start MX Bikes, open Settings, then FPV Drone Settings.\nYour saved controller mapping was preserved. F8 toggles FPV while riding.");}
            if(id==3){auto executable=Selection()/L"mxbikes.exe";if(!fs::exists(executable))throw std::runtime_error("Choose the MX Bikes folder first.");ShellExecuteW(window,L"open",executable.c_str(),nullptr,Selection().c_str(),SW_SHOWNORMAL);}
            if(id==4){Remove(Selection());SetWindowTextW(statusLabel,L"Plugin removed. Controller settings and the diagnostic log were kept in plugins.");}
            if(id==5){Restore(Selection());SetWindowTextW(statusLabel,L"Previous FPV plugin restored. Your saved controller settings were kept.");}
        }catch(const std::exception& e){MessageBoxA(window,e.what(),"MXB FPV Setup",MB_OK|MB_ICONERROR);}
        return 0;
    }
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wParam,lParam);
}
}
int WINAPI wWinMain(HINSTANCE app,HINSTANCE,PWSTR,int show) {
    instance=app;SetProcessDPIAware();CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int argc{};auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==3) {
        int result=0;
        try {
            if(_wcsicmp(argv[1],L"/install")==0)Install(argv[2]);
            else if(_wcsicmp(argv[1],L"/uninstall")==0)Remove(argv[2]);
            else if(_wcsicmp(argv[1],L"/rollback")==0)Restore(argv[2]);
            else if(_wcsicmp(argv[1],L"/verify")==0){if(Hash(fs::path(argv[2])/L"mxbikes.exe")!=expected)result=2;}
            else result=3;
        }catch(const std::exception& e){result=1;std::ofstream error(fs::temp_directory_path()/L"mxb-fpv-setup-error.txt");error<<e.what();}
        LocalFree(argv);CoUninitialize();return result;
    }
    LocalFree(argv);
    WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=app;wc.lpszClassName=L"MXBFPVSetup";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&wc);
    HWND window=CreateWindowW(wc.lpszClassName,L"MX Bikes FPV - Setup",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,665,430,nullptr,nullptr,app,nullptr);
    ShowWindow(window,show);MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    CoUninitialize();return 0;
}
