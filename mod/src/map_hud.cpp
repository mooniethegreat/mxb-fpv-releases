#include "controller.h"
#include "map_hud.h"
#include "imgui.h"
#include <cstring>
#include <limits>
#include <cwchar>
#include <cstdlib>
namespace fpv {
namespace {
bool Finite(Vec3 p) {return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<1000000&&std::abs(p.y)<1000000&&std::abs(p.z)<1000000;}
Vec3 SegmentPoint(const TrackSegment& s,float distance,int angleSign,int northSign,int turnSign) {
    float angle=s.angle*Pi/180*angleSign;Vec3 f{std::sin(angle),0,std::cos(angle)*northSign},r{std::cos(angle),0,-std::sin(angle)*northSign};
    Vec3 start{s.start[0],s.height,s.start[1]};if(!s.type||std::abs(s.radius)<0.01f)return start+f*distance;
    float radius=s.radius*turnSign,turn=distance/radius;return start+r*(radius*(1-std::cos(turn)))+f*(radius*std::sin(turn));
}
ImVec2 Screen(float x,float y,ImVec2 display) {
    float w=display.x,h=display.y;if(w/h>4.0f/3)w=h*4.0f/3;else h=w*3.0f/4;
    return {(display.x-w)*0.5f+x*w,(display.y-h)*0.5f+y*h};
}
void DroneIcon(ImDrawList* draw,ImVec2 p,float s,ImU32 color) {
    for(int x:{-1,1})for(int y:{-1,1}){ImVec2 end{p.x+x*5*s,p.y+y*5*s};draw->AddLine(p,end,color,1.7f*s);draw->AddCircle(end,2.8f*s,color,10,1.4f*s);}
    draw->AddRectFilled({p.x-2*s,p.y-3*s},{p.x+2*s,p.y+3*s},color);
}
int Number(int race) {return race>=900000?race-900000:race+1;}
}
void MapHud::Reset(bool track) {std::lock_guard<std::mutex> lock(mutex_);riders_.clear();projection_={};externalRect_={};externalTick_=0;if(track)line_.clear();}
void MapHud::Configure(std::filesystem::path path) {std::lock_guard<std::mutex> lock(mutex_);maxHudIni_=std::move(path);layoutTick_=0;}
void MapHud::Centerline(int count,const TrackSegment* segments) {
    if(count<1||count>4096||!segments)return;
    std::vector<TrackSegment> source(segments,segments+count);
    for(const auto& s:source)if(!Finite({s.start[0],s.height,s.start[1]})||!std::isfinite(s.length)||s.length<0||s.length>100000||!std::isfinite(s.radius)||!std::isfinite(s.angle))return;
    // Match segment endpoints to the following SDK start. This establishes the
    // track's coordinate convention without mirroring north/south by assumption.
    int angleSign=1,northSign=1,turnSign=1;double best=std::numeric_limits<double>::max();
    for(int a:{-1,1})for(int n:{-1,1})for(int t:{-1,1}){
        double error=0;for(int i=0;i<count-1;i++){auto p=SegmentPoint(source[i],source[i].length,a,n,t);auto& next=source[i+1];double dx=p.x-next.start[0],dz=p.z-next.start[1];error+=dx*dx+dz*dz;}
        if(error<best){best=error;angleSign=a;northSign=n;turnSign=t;}
    }
    std::vector<Vec3> line;line.reserve(8192);
    for(const auto& s:source){int steps=std::clamp(static_cast<int>(s.length/5)+1,1,128);for(int i=0;i<steps&&line.size()<8191;i++)line.push_back(SegmentPoint(s,s.length*i/steps,angleSign,northSign,turnSign));}
    line.push_back(SegmentPoint(source.back(),source.back().length,angleSign,northSign,turnSign));
    std::lock_guard<std::mutex> lock(mutex_);line_=std::move(line);projection_={};
}
void MapHud::Positions(int count,const void* data,int stride) {
    if(count<0||count>1000||stride<static_cast<int>(sizeof(RacePoint))||stride>4096||(!data&&count))return;
    std::lock_guard<std::mutex> lock(mutex_);auto now=GetTickCount64();
    for(int i=0;i<count;i++){RacePoint p;std::memcpy(&p,static_cast<const unsigned char*>(data)+static_cast<size_t>(i)*stride,sizeof(p));
        if(p.race<0||p.race>=900000||!Finite({p.x,p.y,p.z}))continue;
        auto found=std::find_if(riders_.begin(),riders_.end(),[&](const Rider& r){return r.point.race==p.race;});
        if(found!=riders_.end())*found={p,now};else if(riders_.size()<100)riders_.push_back({p,now});
    }
    riders_.erase(std::remove_if(riders_.begin(),riders_.end(),[&](const Rider& r){return now-r.seen>5000;}),riders_.end());
}
void MapHud::Remove(int race) {std::lock_guard<std::mutex> lock(mutex_);riders_.erase(std::remove_if(riders_.begin(),riders_.end(),[&](const Rider& r){return r.point.race==race;}),riders_.end());}
bool MapHud::SpawnPoint(Vec3& p) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!line_.empty()){p=line_.front();return true;}
    if(!riders_.empty()){auto& r=riders_.front().point;p={r.x,r.y,r.z};return true;}return false;
}
void MapHud::ReadLayout(int state) {
    auto now=GetTickCount64();if(now-layoutTick_<5000&&layoutState_==state)return;layoutTick_=now;layoutState_=state;layout_={};
    std::error_code ignored;if(maxHudIni_.empty()||!std::filesystem::exists(maxHudIni_,ignored))return;
    const wchar_t* sections[]={L"HUDMap",L"HUDJoypad",L"HUDInputs",L"HUDSpeed",L"HUDLean"};int index=std::clamp(state,0,2);
    auto value=[&](const wchar_t* section,const wchar_t* key,float fallback){wchar_t text[128]{};GetPrivateProfileStringW(section,key,L"",text,128,maxHudIni_.c_str());float v[3]{fallback,fallback,fallback};swscanf_s(text,L"{%f,%f,%f}",v,v+1,v+2);return std::isfinite(v[index])?v[index]:fallback;};
    for(int i=0;i<5;i++){
        if(value(sections[i],L"_enabled",0)==0)continue;
        float x=value(sections[i],L"_pos_x",-10),y=value(sections[i],L"_pos_y",-10),size=value(sections[i],L"size",1);
        if(x< -1||x>2||y< -1||y>2||size<0.1f||size>5)continue;
        float width=i==0?0.25f:i==1?0.16f:0.19f,height=i==0?0.25f:i==1?0.12f:0.19f;
        layout_[i]={x,y,x+width*size,y+height*size,true};
    }
}
void MapHud::External(int state,int* count,void** data,int* stringCount,void** stringData,bool hideBike) {
    if(!count||!data||!stringCount||!stringData||*count<0||*count>32768||*stringCount<0||*stringCount>32768||(*count&&!*data)||(*stringCount&&!*stringData))return;
    std::lock_guard<std::mutex> lock(mutex_);ReadLayout(state);auto original=static_cast<const HudQuad*>(*data);auto originalStrings=static_cast<const HudString*>(*stringData);
    auto rects=layout_;
    // Background quads reveal actual configured widget bounds, including scale.
    for(auto& rect:rects)if(rect.valid){float best=0.025f;
        for(int i=0;i<*count;i++){auto& q=original[i];if(q.sprite||(q.color&0xffffff)!=0||(q.color>>24)<80)continue;
            float x=q.position[0][0],y=q.position[0][1],right=x,bottom=y;for(const auto& p:q.position){x=std::min(x,p[0]);y=std::min(y,p[1]);right=std::max(right,p[0]);bottom=std::max(bottom,p[1]);}
            float error=std::abs(x-rect.x)+std::abs(y-rect.y);if(error<best&&right-x>0.015f&&bottom-y>0.008f){best=error;rect={x,y,right,bottom,true};}
        }
    }
    auto inside=[](const Rect& r,float x,float y){return r.valid&&x>=r.x-0.002f&&x<=r.right+0.002f&&y>=r.y-0.002f&&y<=r.bottom+0.002f;};
    auto now=GetTickCount64();externalRect_=rects[0];externalTick_=now;projection_.valid=false;
    struct Pair {float x,y,u,v;};std::vector<Pair> pairs;
    // Match labels on the existing map to official SDK rider positions. Fit in
    // 4:3 HUD units (x * 4/3, y), which preserves world distances and rotation.
    if(rects[0].valid)for(int numbering:{0,1}){
        pairs.clear();for(int i=0;i<*stringCount;i++){const auto& s=originalStrings[i];if(!inside(rects[0],s.position[0],s.position[1])||!std::memchr(s.text,0,100))continue;
            const char* text=s.text;while(*text==' '||*text=='#')text++;char* end{};long number=strtol(text,&end,10);if(end==text||(*end&&*end!=' ')||number<0||number>1000)continue;
            auto rider=std::find_if(riders_.begin(),riders_.end(),[&](const Rider& r){return r.point.race==number-numbering&&now-r.seen<1500;});if(rider==riders_.end())continue;
            pairs.push_back({rider->point.x,rider->point.z,s.position[0]*4/3,s.position[1]});
        }
        if(pairs.size()<3)continue;
        double x=0,y=0,u=0,v=0;for(auto p:pairs){x+=p.x;y+=p.y;u+=p.u;v+=p.v;}x/=pairs.size();y/=pairs.size();u/=pairs.size();v/=pairs.size();
        double xx=0,yy=0,xy=0;for(auto p:pairs){double dx=p.x-x,dy=p.y-y;xx+=dx*dx;yy+=dy*dy;xy+=dx*dy;}
        if(xx*yy-xy*xy<0.001*std::max(1.0,xx*yy))continue; // collinear labels cannot establish handedness
        for(int flip:{-1,1}){double den=0,real=0,imag=0;for(auto p:pairs){double dx=p.x-x,dy=(p.y-y)*flip,du=p.u-u,dv=p.v-v;den+=dx*dx+dy*dy;real+=dx*du+dy*dv;imag+=dx*dv-dy*du;}
            if(den<4)continue;double a=real/den,b=imag/den,error=0;for(auto p:pairs){double du=u+a*(p.x-x)-b*(p.y-y)*flip-p.u,dv=v+b*(p.x-x)+a*(p.y-y)*flip-p.v;error+=du*du+dv*dv;}
            if(error/pairs.size()>0.000025||a*a+b*b<1e-10)continue;
            projection_={static_cast<float>(a),static_cast<float>(b),static_cast<float>(u-a*x+b*y*flip),static_cast<float>(v-b*x-a*y*flip),now,true};
            // The world Z handedness is retained explicitly in the projection.
            projectionFlip_=flip;break;
        }if(projection_.valid)break;
    }
    if(!hideBike)return;
    auto hide=[&](float x,float y){if(inside(rects[0],x,y))return false;for(size_t i=1;i<rects.size();i++)if(inside(rects[i],x,y))return true;return false;};
    quads_.clear();strings_.clear();quads_.reserve(*count);strings_.reserve(*stringCount);
    for(int i=0;i<*count;i++){float x=0,y=0;for(const auto& p:original[i].position){x+=p[0]*0.25f;y+=p[1]*0.25f;}if(!hide(x,y))quads_.push_back(original[i]);}
    for(int i=0;i<*stringCount;i++)if(!hide(originalStrings[i].position[0],originalStrings[i].position[1]))strings_.push_back(originalStrings[i]);
    *count=static_cast<int>(quads_.size());*data=quads_.data();*stringCount=static_cast<int>(strings_.size());*stringData=strings_.data();
}
void MapHud::Draw(const MapSettings& settings,bool visible,const PeerState& own,const std::vector<PeerState>& peers) {
    if(!settings.enabled||!visible)return;
    std::lock_guard<std::mutex> lock(mutex_);auto display=ImGui::GetIO().DisplaySize;auto draw=ImGui::GetBackgroundDrawList();auto now=GetTickCount64();
    float scale=std::clamp(std::min(display.y/1080.0f,display.x/1920.0f),0.5f,1.6f),size=settings.size*scale;
    ImVec2 top{display.x-size-24*scale,24*scale},bottom{top.x+size,top.y+size};
    bool external=settings.external&&externalRect_.valid&&now-externalTick_<200;
    if(external){top=Screen(externalRect_.x,externalRect_.y,display);bottom=Screen(externalRect_.right,externalRect_.bottom,display);}
    bool projected=external&&projection_.valid&&now-projection_.seen<200;
    float minX=0,maxX=0,minZ=0,maxZ=0;
    if(!projected){
        bool first=true;auto bounds=[&](Vec3 p){if(first){minX=maxX=p.x;minZ=maxZ=p.z;first=false;}else{minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);minZ=std::min(minZ,p.z);maxZ=std::max(maxZ,p.z);}};
        for(auto p:line_)bounds(p);if(line_.empty()){for(const auto& r:riders_)bounds({r.point.x,r.point.y,r.point.z});if(own.flying)bounds(own.position);}
        if(first)return;
        if(maxX-minX<20){minX-=10;maxX+=10;}if(maxZ-minZ<20){minZ-=10;maxZ+=10;}
        draw->AddRectFilled(top,bottom,IM_COL32(10,16,23,245));draw->AddRect(top,bottom,IM_COL32(137,151,168,190));
        draw->AddText(ImGui::GetFont(),13*scale,{top.x+9*scale,top.y+7*scale},IM_COL32(224,230,236,255),"TRACK / RIDERS + DRONES");
    }
    auto point=[&](Vec3 p){
        if(projected){float z=p.z*projectionFlip_;return Screen((projection_.a*p.x-projection_.b*z+projection_.tx)*0.75f,projection_.b*p.x+projection_.a*z+projection_.ty,display);}
        float padding=24*scale,width=bottom.x-top.x-padding*2,height=bottom.y-top.y-padding*2;
        float factor=std::min(width/(maxX-minX),height/(maxZ-minZ));return ImVec2{(top.x+bottom.x)*0.5f+(p.x-(minX+maxX)*0.5f)*factor,(top.y+bottom.y)*0.5f-(p.z-(minZ+maxZ)*0.5f)*factor};
    };
    draw->PushClipRect(top,bottom,true);
    if(!projected){for(size_t i=1;i<line_.size();i++)draw->AddLine(point(line_[i-1]),point(line_[i]),IM_COL32(196,205,214,230),3*scale);
        for(const auto& rider:riders_)if(now-rider.seen<2500){auto p=point({rider.point.x,rider.point.y,rider.point.z});ImU32 color=rider.point.race==own.race?IM_COL32(255,210,70,255):IM_COL32(85,180,245,255);draw->AddCircleFilled(p,3.5f*scale,color);char label[20]{};sprintf_s(label,"%d",Number(rider.point.race));draw->AddText(ImGui::GetFont(),12*scale,{p.x+5*scale,p.y-8*scale},color,label);}
    }
    auto drone=[&](const PeerState& p,bool me){if(!p.flying||!Finite(p.position))return;auto xy=point(p.position);xy.x=std::clamp(xy.x,top.x+10*scale,bottom.x-10*scale);xy.y=std::clamp(xy.y,top.y+24*scale,bottom.y-10*scale);ImU32 color=me?IM_COL32(110,240,135,255):IM_COL32(255,170,65,255);DroneIcon(draw,xy,scale,color);char label[24]{};sprintf_s(label,me?"YOU / %d":"FPV %d",Number(p.race));draw->AddText(ImGui::GetFont(),12*scale,{xy.x+9*scale,xy.y+4*scale},color,label);};
    for(const auto& p:peers)drone(p,false);drone(own,true);draw->PopClipRect();
}
}
