#include "controller.h"
#include "rider_visuals.h"
#include <gl/GL.h>
#include <unordered_map>
#include <mutex>
#include <cstring>

namespace fpv {
namespace {
using Matrix=std::array<float,16>;
using Bus=std::int64_t(*)(unsigned,...);
struct Frame {Vec3 right{1,0,0},up{0,1,0},forward{0,0,1},position{};Vec3 Point(Vec3 p)const{return position+right*p.x+up*p.y+forward*p.z;}};
Frame FromMatrix(const Matrix& m){return {{m[0],m[4],m[8]},{m[1],m[5],m[9]},{m[2],m[6],m[10]},{m[3],m[7],m[11]}};}
bool MatrixValid(const Matrix& m){for(float v:m)if(!std::isfinite(v)||std::abs(v)>1000000)return false;auto f=FromMatrix(m);return Length(f.right)>0.8f&&Length(f.right)<1.2f&&Length(f.up)>0.8f&&Length(f.up)<1.2f&&Length(f.forward)>0.8f&&Length(f.forward)<1.2f;}
struct Color {float r,g,b;};
struct Triangle {Vec3 a,b,c;Color color;};
void Box(std::vector<Triangle>& out,const Frame& f,Vec3 c,Vec3 size,Color color) {
    Vec3 p[8];for(int i=0;i<8;i++)p[i]=f.Point(c+Vec3{(i&1?1.0f:-1.0f)*size.x*0.5f,(i&2?1.0f:-1.0f)*size.y*0.5f,(i&4?1.0f:-1.0f)*size.z*0.5f});
    constexpr int faces[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}};
    for(int i=0;i<6;i++){float shade=0.65f+i*0.06f;Color col{color.r*shade,color.g*shade,color.b*shade};const auto& a=faces[i];out.push_back({p[a[0]],p[a[1]],p[a[2]],col});out.push_back({p[a[0]],p[a[2]],p[a[3]],col});}
}
void Cylinder(std::vector<Triangle>& out,const Frame& f,Vec3 c,float radius,float height,Color col,int sides=12) {
    for(int i=0;i<sides;i++){float a=2*Pi*i/sides,b=2*Pi*(i+1)/sides;Vec3 p=f.Point(c+Vec3{std::cos(a)*radius,-height*0.5f,std::sin(a)*radius}),q=f.Point(c+Vec3{std::cos(b)*radius,-height*0.5f,std::sin(b)*radius}),r=p+f.up*height,s=q+f.up*height;
        out.push_back({p,q,r,col});out.push_back({q,s,r,col});out.push_back({f.Point(c+Vec3{0,height*0.5f,0}),r,s,col});}
}
void Goggles(std::vector<Triangle>& mesh,const Frame& f,float forward) {
    Color shell{0.22f,0.24f,0.27f},dark{0.035f,0.04f,0.045f};
    Box(mesh,f,{0,0,forward},{0.205f,0.105f,0.105f},shell);
    Box(mesh,f,{-0.052f,0,forward+0.055f},{0.082f,0.07f,0.015f},dark);Box(mesh,f,{0.052f,0,forward+0.055f},{0.082f,0.07f,0.015f},dark);
    Box(mesh,f,{-0.105f,0,-0.015f},{0.018f,0.035f,0.22f},dark);Box(mesh,f,{0.105f,0,-0.015f},{0.018f,0.035f,0.22f},dark);Box(mesh,f,{0,0,-0.125f},{0.21f,0.035f,0.018f},dark);
    for(int side:{-1,1})for(int front:{-1,1})Cylinder(mesh,f,{side*0.079f,0.085f,forward+front*0.035f},0.004f,0.08f,dark,6);
}
void RemoteController(std::vector<Triangle>& mesh,const Frame& f) {
    Color shell{0.57f,0.59f,0.62f},dark{0.045f,0.05f,0.055f};
    Box(mesh,f,{0,0,0},{0.19f,0.045f,0.115f},shell);Box(mesh,f,{-0.075f,-0.012f,-0.045f},{0.055f,0.055f,0.075f},shell);Box(mesh,f,{0.075f,-0.012f,-0.045f},{0.055f,0.055f,0.075f},shell);
    for(int side:{-1,1}){Cylinder(mesh,f,{side*0.05f,0.032f,0.005f},0.013f,0.02f,dark);Cylinder(mesh,f,{side*0.05f,0.047f,0.005f},0.005f,0.025f,dark,8);}
    Box(mesh,f,{0,0.005f,0.09f},{0.10f,0.012f,0.02f},dark);Box(mesh,f,{0,0.025f,-0.025f},{0.025f,0.008f,0.015f},{0.08f,0.7f,0.22f});
}
void Quad(std::vector<Triangle>& mesh,const PeerState& p) {
    Frame f{p.rotation.Rotate({1,0,0}),p.rotation.Rotate({0,1,0}),p.rotation.Rotate({0,0,1}),p.position};float size=std::clamp(p.propInches,1.5f,7.0f)*0.0254f;
    Box(mesh,f,{0,0,0},{0.045f,0.035f,size*1.5f},{0.08f,0.085f,0.09f});Box(mesh,f,{0,0.04f,0},{0.04f,0.04f,0.07f},{0.28f,0.30f,0.32f});
    for(int x:{-1,1})for(int z:{-1,1}){Vec3 pos{x*size*0.65f,0,z*size*0.65f};Box(mesh,f,pos*0.5f,{size*0.75f,0.012f,0.018f},{0.1f,0.1f,0.11f});Cylinder(mesh,f,pos,0.014f,0.025f,{0.16f,0.17f,0.18f});Cylinder(mesh,f,pos+Vec3{0,0.017f,0},size*0.5f,0.002f,z>0?Color{0.2f,0.75f,0.68f}:Color{0.18f,0.2f,0.22f},24);}
}
bool Snapshot(std::uintptr_t base,void* graphics,int* instance,void* physics,int* race,int* model,Matrix* head,int* left,int* right,int* leftRef,int* rightRef) {
    __try {
        auto pointer=reinterpret_cast<std::uintptr_t>(graphics);auto first=base+0xf4ee20+0x274;
        if(pointer<first||(pointer-first)%0x5b24||(pointer-first)/0x5b24>=50||graphics!=instance)return false;
        auto record=pointer-0x274;if(!*reinterpret_cast<int*>(record))return false;*race=*reinterpret_cast<int*>(record+0xc);*model=instance[0x75];
        if(!instance[0x74]||*model<=0)return false;
        std::memcpy(head->data(),static_cast<unsigned char*>(physics)+0x544,sizeof(Matrix));
        *left=instance[0xdd];*right=instance[0xde];*leftRef=instance[0xdf];*rightRef=instance[0xe7];return MatrixValid(*head);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
int OwnKey(std::uintptr_t base) {__try {int i=*reinterpret_cast<int*>(base+0xe62710)-1;if(i<0||i>=50)return -1;return *reinterpret_cast<int*>(base+0xf4ee20+i*0x5b24+0xc);}__except(EXCEPTION_EXECUTE_HANDLER){return -1;}}
void ApplyHands(Bus bus,int model,int left,int right,int leftRef,int rightRef,Frame f) {
    // Existing native wrist IK chains; position/axes follow the controller frame.
    Vec3 a=f.Point({-0.077f,0.025f,-0.005f}),b=f.Point({0.077f,0.025f,-0.005f});
    __try {if(left>0&&leftRef>0)bus(0x8a,model,left,leftRef,&a,&f.forward,&f.up,0.0);if(right>0&&rightRef>0)bus(0x8a,model,right,rightRef,&b,&f.forward,&f.up,0.0);}__except(EXCEPTION_EXECUTE_HANDLER){}
}
}
struct RiderVisuals::Impl {
    std::uintptr_t base{};VisualSettings settings;std::mutex mutex;
    int own{-1};bool active{};std::vector<PeerState> peers;
    struct Pilot {Matrix head{};Frame control;std::uint64_t first{},seen{};Vec3 start{};};std::unordered_map<int,Pilot> pilots;
    Matrix camera{};float fov{100};bool cameraReady{};
};
RiderVisuals::RiderVisuals():impl_(new Impl){}
RiderVisuals::~RiderVisuals(){delete impl_;}
void RiderVisuals::Configure(std::uintptr_t base,const VisualSettings& settings){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->base=base;impl_->settings=settings;}
int RiderVisuals::OwnRace()const{return OwnKey(impl_->base);}
void RiderVisuals::States(int own,bool active,const std::vector<PeerState>& peers){std::lock_guard<std::mutex> lock(impl_->mutex);impl_->own=own;impl_->active=active;impl_->peers=peers;}
void RiderVisuals::Rider(void* graphics,int* instance,void* physics) {
    int race{},model{},left{},right{},leftRef{},rightRef{};Matrix head;
    if(!Snapshot(impl_->base,graphics,instance,physics,&race,&model,&head,&left,&right,&leftRef,&rightRef))return;
    std::lock_guard<std::mutex> lock(impl_->mutex);bool enabled=race==impl_->own&&impl_->active;
    for(const auto& peer:impl_->peers)if(peer.race==race&&peer.flying)enabled=true;
    if(!enabled){impl_->pilots.erase(race);return;}
    auto now=GetTickCount64();auto found=impl_->pilots.find(race);bool fresh=found==impl_->pilots.end();auto& pilot=impl_->pilots[race];
    pilot.head=head;pilot.seen=now;Frame frame=FromMatrix(head);
    Vec3 desired=frame.position-frame.up*impl_->settings.controllerDown+frame.forward*impl_->settings.controllerForward;
    if(fresh){pilot.first=now;pilot.start=desired-frame.up*0.20f+frame.forward*0.15f;}
    float blend=std::clamp((now-pilot.first)/650.0f,0.0f,1.0f);blend=blend*blend*(3-2*blend);
    frame.position=pilot.start+(desired-pilot.start)*blend;pilot.control=frame;
    if(impl_->settings.pose){auto bus=*reinterpret_cast<Bus*>(impl_->base+0x566c48);if(bus)ApplyHands(bus,model,left,right,leftRef,rightRef,frame);}
}
void RiderVisuals::Camera(const float* matrix,float fov){std::lock_guard<std::mutex> lock(impl_->mutex);std::memcpy(impl_->camera.data(),matrix,sizeof(Matrix));impl_->fov=fov;impl_->cameraReady=MatrixValid(impl_->camera);}
void RiderVisuals::Draw(int width,int height) {
    std::vector<Triangle> mesh;Frame camera;float fov{};
    {std::lock_guard<std::mutex> lock(impl_->mutex);if(!impl_->cameraReady)return;camera=FromMatrix(impl_->camera);fov=impl_->fov;
        auto now=GetTickCount64();for(auto i=impl_->pilots.begin();i!=impl_->pilots.end();){if(now-i->second.seen>250){i=impl_->pilots.erase(i);continue;}if(impl_->settings.goggles)Goggles(mesh,FromMatrix(i->second.head),impl_->settings.gogglesForward);if(impl_->settings.controller)RemoteController(mesh,i->second.control);++i;}
        if(impl_->settings.drones)for(const auto& p:impl_->peers)if(p.flying)Quad(mesh,p);
    }
    if(mesh.empty()||width<1||height<1)return;
    using UseProgram=void(APIENTRY*)(GLuint);auto useProgram=reinterpret_cast<UseProgram>(wglGetProcAddress("glUseProgram"));GLint oldProgram{},mode{};glGetIntegerv(0x8b8d,&oldProgram);glGetIntegerv(GL_MATRIX_MODE,&mode);
    glPushAttrib(GL_ALL_ATTRIB_BITS);if(useProgram)useProgram(0);glDisable(GL_TEXTURE_2D);glDisable(GL_LIGHTING);glDisable(GL_FOG);glDisable(GL_CULL_FACE);glDisable(GL_ALPHA_TEST);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glViewport(0,0,width,height);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);
    glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
    float tangent=std::tan(std::clamp(fov,30.0f,150.0f)*Pi/360),aspect=static_cast<float>(width)/height;
    auto vertex=[&](Vec3 p){Vec3 d=p-camera.position;float z=Dot(d,camera.forward);glVertex4f(Dot(d,camera.right)/(tangent*aspect),Dot(d,camera.up)/tangent,1.00001f*z-0.100001f,z);};
    glBegin(GL_TRIANGLES);for(const auto& triangle:mesh){if(Dot(triangle.a-camera.position,camera.forward)<0.05f||Dot(triangle.b-camera.position,camera.forward)<0.05f||Dot(triangle.c-camera.position,camera.forward)<0.05f)continue;glColor3f(triangle.color.r,triangle.color.g,triangle.color.b);vertex(triangle.a);vertex(triangle.b);vertex(triangle.c);}glEnd();
    glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(mode);if(useProgram)useProgram(static_cast<GLuint>(oldProgram));glPopAttrib();
}
}
