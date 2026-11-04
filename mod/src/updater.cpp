#include "updater.h"
#include "version.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace fpv {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr size_t MaxInstaller = 128 * 1024 * 1024;
const std::wstring Repo = L"https://github.com/mooniethegreat/mxb-fpv-releases";
struct HttpHandle {
    HINTERNET value{};
    explicit HttpHandle(HINTERNET h):value(h) { if(!h)throw std::runtime_error("Cannot initialize HTTPS."); }
    ~HttpHandle() { WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&)=delete;
    operator HINTERNET() const { return value; }
};
struct Apartment {
    Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~Apartment() { winrt::uninit_apartment(); }
};
std::filesystem::path UpdateFolder() {
    PWSTR folder{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&folder)))throw std::runtime_error("Cannot locate the update download folder.");
    std::filesystem::path result(folder);CoTaskMemFree(folder);
    result/=L"MXB-FPV";std::filesystem::create_directories(result);return result;
}
unsigned long long Now() {
    return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
std::array<int,3> VersionParts(const std::string& text) {
    std::array<int,3> parts{};int part=0,digits=0;
    if(text.empty()||text.size()>20)throw std::runtime_error("Invalid release version.");
    for(char c:text){
        if(c=='.'){if(!digits||part==2)throw std::runtime_error("Invalid release version.");++part;digits=0;}
        else if(c>='0'&&c<='9'&&digits<5){parts[part]=parts[part]*10+c-'0';++digits;}
        else throw std::runtime_error("Invalid release version.");
    }
    if(!digits||part<1)throw std::runtime_error("Invalid release version.");return parts;
}
std::string Hash(const std::vector<unsigned char>& bytes) {
    BCRYPT_ALG_HANDLE algorithm{};unsigned char digest[32]{};
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 is unavailable.");
    auto result=BCryptHash(algorithm,nullptr,0,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),digest,32);BCryptCloseAlgorithmProvider(algorithm,0);
    if(result<0)throw std::runtime_error("Cannot verify download.");
    char hex[65]{};for(int i=0;i<32;i++)sprintf_s(hex+2*i,3,"%02x",digest[i]);return hex;
}
std::vector<unsigned char> Fetch(const std::wstring& host,const std::wstring& path,size_t limit,const std::atomic<bool>& stop) {
    HttpHandle session(WinHttpOpen((L"MXB-FPV/"+std::wstring(winrt::to_hstring(Version))).c_str(),WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session,4000,4000,4000,4000);
    HttpHandle connection(WinHttpConnect(session,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0));
    HttpHandle request(WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE|WINHTTP_FLAG_REFRESH));
    // Keep ordinary certificate validation and prohibit HTTPS -> HTTP redirects.
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
    DWORD autologon=WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    WinHttpSetOption(request,WINHTTP_OPTION_AUTOLOGON_POLICY,&autologon,sizeof(autologon));
    if(stop||!WinHttpSendRequest(request,L"Cache-Control: no-cache\r\n",static_cast<DWORD>(-1L),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request,nullptr))throw std::runtime_error("Cannot reach GitHub. Check your connection and try again.");
    DWORD code{},size=sizeof(code);
    if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX)||code!=200)throw std::runtime_error("GitHub returned HTTP "+std::to_string(code)+". Try again later.");
    std::vector<unsigned char> data;std::array<unsigned char,65536> chunk{};auto start=GetTickCount64();
    for(;;){
        if(stop||GetTickCount64()-start>60000)throw std::runtime_error("Update request cancelled or timed out.");
        DWORD count{};if(!WinHttpReadData(request,chunk.data(),static_cast<DWORD>(chunk.size()),&count))throw std::runtime_error("Download interrupted. Try again.");
        if(!count)break;if(data.size()+count>limit)throw std::runtime_error("Update exceeds the expected size limit.");
        data.insert(data.end(),chunk.begin(),chunk.begin()+count);
    }
    return data;
}
std::string ReadCache(const std::filesystem::path& path) {
    std::error_code error;auto size=std::filesystem::file_size(path,error);if(error||size>65536)return {};
    std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
}
void AtomicWrite(const std::filesystem::path& path,const std::vector<unsigned char>& bytes) {
    auto temp=path;temp+=L".part";
    try {
        std::ofstream output(temp,std::ios::binary|std::ios::trunc);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));output.close();
        if(!output||!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot save the verified update. Check folder permissions.");
    }catch(...){std::error_code ignored;std::filesystem::remove(temp,ignored);throw;}
}
UpdateStatus Parse(const std::string& text,bool preview) {
    auto root=JsonObject::Parse(winrt::to_hstring(text));
    if(root.GetNamedNumber(L"schema")!=1)throw std::runtime_error("This update manifest needs a newer checker.");
    UpdateStatus best;best.checked=true;bool found=false;
    for(auto channel:{L"stable",L"preview"}){
        if(!preview&&std::wstring(channel)==L"preview")continue;
        if(!root.HasKey(channel)||root.GetNamedValue(channel).ValueType()==JsonValueType::Null)continue;
        auto release=root.GetNamedObject(channel);UpdateStatus candidate;candidate.checked=true;
        candidate.version=winrt::to_string(release.GetNamedString(L"version"));auto parts=VersionParts(candidate.version);
        if(found&&parts<=VersionParts(best.version))continue;
        candidate.notes=winrt::to_string(release.GetNamedString(L"notes",L""));if(candidate.notes.size()>8192)throw std::runtime_error("Release notes are too long.");
        auto installer=release.GetNamedObject(L"installer");candidate.sha256=winrt::to_string(installer.GetNamedString(L"sha256"));
        std::transform(candidate.sha256.begin(),candidate.sha256.end(),candidate.sha256.begin(),[](char c){return c>='A'&&c<='F'?static_cast<char>(c+32):c;});
        if(candidate.sha256.size()!=64||candidate.sha256.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid installer checksum.");
        double bytes=installer.GetNamedNumber(L"bytes");if(!std::isfinite(bytes)||bytes<1||bytes>MaxInstaller||std::floor(bytes)!=bytes)throw std::runtime_error("Invalid installer size.");
        candidate.bytes=static_cast<unsigned long long>(bytes);
        for(auto hash:release.GetNamedArray(L"game_sha256"))if(winrt::to_string(hash.GetString())==SupportedGameHash)candidate.compatible=true;
        candidate.available=parts>VersionParts(Version);
        candidate.releaseUrl=Repo+L"/releases/tag/v"+winrt::to_hstring(candidate.version).c_str();
        candidate.message=candidate.available?"Running v"+std::string(Version)+". Update to v"+candidate.version+" is available.":
            (parts==VersionParts(Version)?"Up to date. You are running v"+std::string(Version)+".":"Running v"+std::string(Version)+", newer than the selected channel. No update needed.");
        if(!candidate.compatible)candidate.message="This release supports a different MX Bikes build. Download is disabled.";
        best=candidate;found=true;
    }
    if(!found){best.checked=false;best.message="No release is published in the selected update channel yet.";}
    return best;
}
}
UpdateStatus Updater::Status() const { std::lock_guard<std::mutex> lock(mutex_);return state_; }
void Updater::Start(bool automatic,bool preview) { if(automatic)Launch(false,preview,true); }
void Updater::Check(bool preview) { Launch(false,preview,false); }
void Updater::Download() { Launch(true,false,false); }
void Updater::Close() { stopping_=true;if(worker_.joinable())worker_.join(); }
void Updater::Launch(bool download,bool preview,bool automatic) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(stopping_||state_.busy||(download&&(!state_.available||!state_.compatible||!state_.downloaded.empty())))return;
        state_.busy=true;state_.message=download?"Downloading and verifying installer...":"Checking GitHub for updates...";
    }
    if(worker_.joinable())worker_.join();
    try{worker_=std::thread([this,download,preview,automatic]{Work(download,preview,automatic);});}
    catch(...){std::lock_guard<std::mutex> lock(mutex_);state_.busy=false;state_.message="Cannot start the update worker.";}
}
void Updater::Work(bool download,bool preview,bool automatic) {
    try {
        Apartment apartment;auto folder=UpdateFolder();auto state=Status();
        if(download){
            auto version=winrt::to_hstring(state.version);std::wstring filename=L"MXB-FPV-"+std::wstring(version)+L"-Setup.exe";
            auto bytes=Fetch(L"github.com",L"/mooniethegreat/mxb-fpv-releases/releases/download/v"+std::wstring(version)+L"/"+filename,static_cast<size_t>(state.bytes),stopping_);
            if(bytes.size()!=state.bytes||Hash(bytes)!=state.sha256)throw std::runtime_error("Installer verification failed. Nothing was installed; check again before retrying.");
            if(stopping_)throw std::runtime_error("Update cancelled.");
            std::filesystem::create_directories(folder/L"Downloads");state.downloaded=folder/L"Downloads"/filename;AtomicWrite(state.downloaded,bytes);
            state.message="v"+state.version+" is downloaded, not installed. Still running v"+Version+". Close MX Bikes, run the saved installer, then restart.";
        }else{
            auto cache=folder/L"update.json",checkState=folder/L"updates.ini";bool fresh=false;std::string text;
            wchar_t previous[32]{},checkedVersion[32]{};GetPrivateProfileStringW(L"Updates",L"LastCheck",L"0",previous,32,checkState.c_str());
            GetPrivateProfileStringW(L"Updates",L"RunningVersion",L"",checkedVersion,32,checkState.c_str());
            unsigned long long last=_wcstoui64(previous,nullptr,10),now=Now();
            if(automatic&&winrt::to_string(checkedVersion)==Version&&now>=last&&now-last<6*60*60){text=ReadCache(cache);if(!text.empty())try{state=Parse(text,preview);fresh=true;}catch(...){}}
            if(!fresh){
                auto bytes=Fetch(L"raw.githubusercontent.com",L"/mooniethegreat/mxb-fpv-releases/main/update.json",65536,stopping_);
                text.assign(bytes.begin(),bytes.end());state=Parse(text,preview);
                AtomicWrite(cache,bytes);WritePrivateProfileStringW(L"Updates",L"LastCheck",std::to_wstring(now).c_str(),checkState.c_str());
                WritePrivateProfileStringW(L"Updates",L"RunningVersion",winrt::to_hstring(Version).c_str(),checkState.c_str());
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);state.busy=false;state_=state;
    }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex_);state_.busy=false;if(!download){state_.available=false;state_.checked=false;}state_.message=e.what();}
    catch(...){std::lock_guard<std::mutex> lock(mutex_);state_.busy=false;if(!download){state_.available=false;state_.checked=false;}state_.message="Cannot read the GitHub update information. Try again later.";}
}
}
