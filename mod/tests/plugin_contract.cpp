#include <windows.h>
#include <cstdio>
#include <cstring>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 1;
    auto module=LoadLibraryW(argv[1]);if(!module){std::printf("Load failed: %lu\n",GetLastError());return 1;}
    using Version=int(*)();using Id=const char*(*)();using Start=int(*)(char*);using Stop=void(*)();
    auto id=reinterpret_cast<Id>(GetProcAddress(module,"GetModID"));
    auto data=reinterpret_cast<Version>(GetProcAddress(module,"GetModDataVersion"));
    auto api=reinterpret_cast<Version>(GetProcAddress(module,"GetInterfaceVersion"));
    auto start=reinterpret_cast<Start>(GetProcAddress(module,"Startup"));
    auto stop=reinterpret_cast<Stop>(GetProcAddress(module,"Shutdown"));
    auto marker=reinterpret_cast<Id>(GetProcAddress(module,"GetFpvPackageId"));
    if(!id||!data||!api||!start||!stop||!marker||strcmp(id(),"mxbikes")||data()!=8||api()!=9)return 2;
    // This offline host must be rejected before any game hooks are created.
    if(start(nullptr)!=0)return 3;stop();FreeLibrary(module);
    std::puts("PASS: native SDK contract, unsupported-host startup, clean unload");return 0;
}
