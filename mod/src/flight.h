#pragma once
#include <algorithm>
#include <cmath>
#include <array>

namespace fpv {
constexpr float Pi = 3.14159265358979323846f;
struct Vec3 {
    float x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator*(float s) const { return {x*s,y*s,z*s}; }
};
inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float Length(Vec3 a) { return std::sqrt(Dot(a,a)); }
inline Vec3 Unit(Vec3 a) {float n=Length(a);return n>0.00001f?a*(1/n):Vec3{0,1,0};}
struct Quat {
    float w{1}, x{}, y{}, z{};
    Quat operator*(Quat b) const {
        return {w*b.w-x*b.x-y*b.y-z*b.z,w*b.x+x*b.w+y*b.z-z*b.y,
                w*b.y-x*b.z+y*b.w+z*b.x,w*b.z+x*b.y-y*b.x+z*b.w};
    }
    Quat Normalized() const {
        float n=std::sqrt(w*w+x*x+y*y+z*z);
        return n>0.00001f?Quat{w/n,x/n,y/n,z/n}:Quat{};
    }
    Vec3 Rotate(Vec3 v) const {
        Vec3 q{x,y,z};
        return v+Cross(q,v)*(2*w)+Cross(q,Cross(q,v))*2;
    }
    static Quat AxisAngle(Vec3 axis, float angle) {
        float s=std::sin(angle*0.5f);
        return {std::cos(angle*0.5f),axis.x*s,axis.y*s,axis.z*s};
    }
};
struct Input { float throttle{}, yaw{}, pitch{}, roll{}; bool connected{}, toggle{}; };
struct AxisRates {float rate{0.8f},super{0.7f},expo{};};
struct FlightSettings {
    float rcRate{0.8f},superRate{0.7f},expo{0.0f};
    float massGrams{650},propInches{5},propPitch{4.3f};
    int cells{6},blades{3},propType{0}; // 0 freestyle, 1 race, 2 ducted/cine
    float thrustPerMotorGrams{1200},motorResponseMs{28},throttleCurve{1.15f};
    float dragArea{0.012f},angularResponseMs{20},cameraTilt{25},radius{0.16f};
    float restitution{0.15f},groundFriction{0.65f};
    bool collisions{true};
    int rateModel{}; // Betaflight, Raceflight, KISS, Actual, Quick
    std::array<AxisRates,3> rates{}; // roll, pitch, yaw
    bool individualRates{},quickRcExpo{},mode3d{},vehicleCollisions{true};
    float throttleMid{0.5f},throttleExpo{},throttleLimit{1},neutral3d{0.05f};
    // Static thrust is editable motor/prop test-stand thrust, rather than a KV-only guess.
    // Prop changes scale this reference with a bounded motor-load approximation.
    float referenceProp{5},referencePitch{4.3f},referenceCells{6},referenceBlades{3};
    float MaxThrustNewtons() const {
        float diameter=std::clamp(propInches/referenceProp,0.35f,2.0f);
        float voltage=static_cast<float>(cells)/referenceCells;
        float pitch=std::clamp(propPitch/referencePitch,0.4f,2.0f);
        float solidity=std::clamp(static_cast<float>(blades)/referenceBlades,0.5f,2.0f);
        float factor=std::pow(diameter,2.2f)*std::pow(voltage,1.65f)*std::pow(pitch,0.22f)*std::pow(solidity,0.18f);
        float efficiency=propType==1?1.04f:(propType==2?0.96f:1.0f);
        return thrustPerMotorGrams*0.001f*9.81f*4*std::clamp(factor*efficiency,0.2f,2.5f);
    }
    float ThrustToWeight() const {return MaxThrustNewtons()/(std::max(massGrams,20.0f)*0.001f*9.81f);}
    float HoverThrottle() const {return std::pow(1/std::max(ThrustToWeight(),1.0f),1/throttleCurve);}
};
struct DronePreset {const char* name;FlightSettings settings;};
inline const std::array<DronePreset,6>& Presets() {
    static const auto presets=[] {
        std::array<DronePreset,6> p{};
        p[0]={"5-inch freestyle / 6S",{}};
        auto& race=p[1];race.name="5-inch race quad / 6S";race.settings.massGrams=500;race.settings.thrustPerMotorGrams=1450;race.settings.propPitch=4.9f;race.settings.referencePitch=4.9f;race.settings.propType=1;race.settings.motorResponseMs=18;race.settings.dragArea=0.008f;race.settings.cameraTilt=40;race.settings.superRate=0.75f;
        auto& three=p[2];three.name="3-inch freestyle / 4S";three.settings.massGrams=220;three.settings.propInches=three.settings.referenceProp=3;three.settings.propPitch=three.settings.referencePitch=2.5f;three.settings.cells=4;three.settings.referenceCells=4;three.settings.thrustPerMotorGrams=360;three.settings.motorResponseMs=22;three.settings.dragArea=0.007f;three.settings.radius=0.11f;
        auto& cine=p[3];cine.name="3-inch cinewhoop / 6S";cine.settings.massGrams=480;cine.settings.propInches=cine.settings.referenceProp=3;cine.settings.propPitch=cine.settings.referencePitch=2;cine.settings.propType=2;cine.settings.thrustPerMotorGrams=420;cine.settings.motorResponseMs=45;cine.settings.dragArea=0.025f;cine.settings.cameraTilt=15;cine.settings.radius=0.14f;cine.settings.superRate=0.55f;
        auto& pavo=p[4];pavo.name="Pavo20-inspired / 2-inch / 3S";pavo.settings.massGrams=145;pavo.settings.propInches=pavo.settings.referenceProp=2;pavo.settings.propPitch=pavo.settings.referencePitch=1.5f;pavo.settings.blades=2;pavo.settings.referenceBlades=2;pavo.settings.cells=3;pavo.settings.referenceCells=3;pavo.settings.propType=2;pavo.settings.thrustPerMotorGrams=125;pavo.settings.motorResponseMs=40;pavo.settings.dragArea=0.009f;pavo.settings.radius=0.08f;pavo.settings.cameraTilt=15;pavo.settings.superRate=0.5f;
        auto& dji=p[5];dji.name="DJI FPV-inspired / 5.3-inch / 6S";dji.settings.massGrams=795;dji.settings.propInches=dji.settings.referenceProp=5.3f;dji.settings.propPitch=dji.settings.referencePitch=2.8f;dji.settings.blades=3;dji.settings.thrustPerMotorGrams=1000;dji.settings.motorResponseMs=45;dji.settings.dragArea=0.024f;dji.settings.radius=0.18f;dji.settings.cameraTilt=20;dji.settings.superRate=0.5f;
        return p;
    }();return presets;
}
struct Contact {Vec3 point{},normal{0,1,0},velocity{};bool vehicle{};};
struct CollisionWorld {
    void* context{};
    bool (*sweep)(void*,Vec3,Vec3,float,Contact&){};
};
inline float Rate(float stick, const FlightSettings& s) {
    stick=std::clamp(stick,-1.0f,1.0f);
    float magnitude=std::abs(stick);
    float curved=stick*(1-s.expo)+stick*magnitude*magnitude*magnitude*s.expo;
    return 200*s.rcRate*curved/std::max(0.01f,1-magnitude*s.superRate);
}
inline float AxisRate(float stick,const FlightSettings& s,int axis) {
    if(!s.individualRates)return Rate(stick,s);
    const auto& r=s.rates[std::clamp(axis,0,2)];float x=std::clamp(stick,-1.0f,1.0f),a=std::abs(x),v{};
    float cubic=x*x*x,expo=std::clamp(r.expo,0.0f,1.0f);
    switch(s.rateModel) {
    case 1:v=10*r.rate*x*(1+expo*(x*x-1))*(1+a*r.super*0.01f);break;
    case 2:v=200*r.rate*(cubic*expo+x*(1-expo))/std::max(0.01f,1-a*r.super);break;
    case 3:v=x*r.rate+std::max(0.0f,r.super-r.rate)*a*(x*x*x*x*x*expo+x*(1-expo));break;
    case 4:{float center=std::max(1.0f,r.rate),maximum=std::max(center,r.super),factor=1-center/maximum;
        float curve=s.quickRcExpo?cubic*expo+x*(1-expo):a*a*a*expo+a*(1-expo);
        v=center*(s.quickRcExpo?curve:x)/std::max(0.01f,1-(s.quickRcExpo?a:curve)*factor);break;}
    default:{float rc=r.rate+(r.rate>2?14.54f*(r.rate-2):0);v=200*rc*x*(1-expo+expo*a*a*a)/std::max(0.01f,1-a*r.super);break;}
    }return std::clamp(v,-1998.0f,1998.0f);
}
inline float ThrottleCommand(float raw,const FlightSettings& s) {
    float x=std::clamp(raw,0.0f,1.0f),sign=1;
    if(s.mode3d){x=x*2-1;sign=x<0?-1.0f:1.0f;x=std::max(0.0f,(std::abs(x)-s.neutral3d)/(1-s.neutral3d));}
    float mid=std::clamp(s.throttleMid,0.01f,0.99f),d=x-mid,span=d>0?1-mid:mid;
    x=mid+d*(1-s.throttleExpo+s.throttleExpo*(d*d)/(span*span));
    return sign*std::pow(std::clamp(x,0.0f,1.0f)*s.throttleLimit,s.throttleCurve);
}
class Flight {
public:
    Vec3 position{}, velocity{};
    Quat rotation{};
    Vec3 angularVelocity{};
    float motorOutput{};
    bool grounded{};
    bool crashed{};float impactSpeed{};
    void Spawn(Vec3 bike, float yawDegrees) {
        rotation=Quat::AxisAngle({0,1,0},yawDegrees*Pi/180.0f);
        position=bike+rotation.Rotate({2,1.5f,0});
        velocity={};
        angularVelocity={};motorOutput=0;grounded=false;crashed=false;impactSpeed=0;
    }
    void Step(const Input& input, const FlightSettings& settings, float dt,const CollisionWorld* world=nullptr) {
        dt=std::clamp(dt,0.0f,0.01f);
        Vec3 requested{AxisRate(input.pitch,settings,1),AxisRate(input.yaw,settings,2),-AxisRate(input.roll,settings,0)};
        float response=1-std::exp(-dt/std::max(0.001f,settings.angularResponseMs*0.001f));
        if(!crashed)angularVelocity=angularVelocity+(requested-angularVelocity)*response;
        else angularVelocity=angularVelocity*std::exp(-dt*(grounded?12.0f:0.35f));
        float radians=Pi/180.0f*dt;
        Quat delta=Quat::AxisAngle({0,1,0},angularVelocity.y*radians)
            *Quat::AxisAngle({1,0,0},angularVelocity.x*radians)
            *Quat::AxisAngle({0,0,1},angularVelocity.z*radians);
        rotation=(rotation*delta).Normalized();
        float target=crashed?0:ThrottleCommand(input.throttle,settings);
        float spool=std::max(0.005f,settings.motorResponseMs*0.001f)*(target<motorOutput?0.65f:1);
        motorOutput+=(target-motorOutput)*(1-std::exp(-dt/spool));
        Vec3 force=rotation.Rotate({0,motorOutput*settings.MaxThrustNewtons(),0});
        float mass=std::max(20.0f,settings.massGrams)*0.001f;
        float dragCoefficient=0.6125f*settings.dragArea*(settings.propType==2?1.2f:1);
        Vec3 acceleration=force*(1/mass)+Vec3{0,-9.81f,0}-velocity*(dragCoefficient*Length(velocity)/mass);
        velocity=velocity+acceleration*dt;
        Vec3 next=position+velocity*dt;grounded=false;
        Contact contact;
        if(settings.collisions&&world&&world->sweep&&world->sweep(world->context,position,next,settings.radius,contact)) {
            Vec3 normal=Unit(contact.normal);
            position=contact.point+normal*(settings.radius+0.002f);
            Vec3 relative=velocity-contact.velocity;float into=Dot(relative,normal);
            if(contact.vehicle&&into<0){crashed=true;motorOutput=0;impactSpeed=std::max(impactSpeed,-into);
                angularVelocity=Cross(normal,relative)*180+Vec3{170,-90,220};
                angularVelocity=angularVelocity*(std::min(1.0f,1400.0f/std::max(1.0f,Length(angularVelocity))));}
            if(into<0)relative=relative-normal*((1+(contact.vehicle?0.3f:settings.restitution))*into);
            velocity=relative+contact.velocity;
            Vec3 tangent=relative-normal*Dot(relative,normal);
            velocity=velocity-tangent*std::clamp(settings.groundFriction*dt*35,0.0f,1.0f);
            if(Length(velocity)<0.2f)velocity={};
            grounded=!contact.vehicle&&normal.y>0.4f;
        } else position=next;
    }
};
}
