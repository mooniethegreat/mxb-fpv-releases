// Included in the plugin namespace: the exact mapped Input drives flight and OSD.
void LcdDigit(ImDrawList* d,int digit,ImVec2 p,float height,ImU32 color) {
    constexpr unsigned masks[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};if(digit<0||digit>9)return;
    float w=height*0.48f,t=height*0.09f,half=height*0.5f;
    const float rects[7][4]={{t,0,w-t,t},{w-t,t,w,half-t/2},{w-t,half+t/2,w,height-t},{t,height-t,w-t,height},{0,half+t/2,t,height-t},{0,t,t,half-t/2},{t,half-t/2,w-t,half+t/2}};
    for(int i=0;i<7;i++){auto& r=rects[i];d->AddRectFilled({p.x+r[0],p.y+r[1]},{p.x+r[2],p.y+r[3]},masks[digit]&(1<<i)?color:IM_COL32(90,100,100,25));}
}
void DroneLcd(ImDrawList* draw,ImVec2 display) {
    float speed{};bool crashed{};{std::lock_guard<std::mutex> lock(flightMutex);speed=Length(flight.velocity);crashed=flight.crashed;}
    // This is the live native profile preference: 0 metric, 1 US, 2 UK.
    int units=*reinterpret_cast<const int*>(gameBase+0xe557d4);bool miles=units==1||units==2;
    int number=std::clamp(static_cast<int>(speed*(miles?2.2369363f:3.6f)),0,999);
    float scale=std::clamp(display.y/1080.0f,0.7f,1.6f),left=display.x-310*scale,top=display.y-128*scale;
    draw->AddRectFilled({left,top},{left+286*scale,top+107*scale},IM_COL32(4,7,10,215));ImU32 lcd=IM_COL32(231,236,225,255);
    const char* icon[]={"11100000111","10110001101","11101010111","00001110000","00001110000","11101010111","10110001101","11100000111"};
    float pixel=4*scale;for(int y=0;y<8;y++)for(int x=0;x<11;x++)if(icon[y][x]=='1')draw->AddRectFilled({left+(12+x*4)*scale,top+(25+y*4)*scale},{left+(12+x*4)*scale+pixel*0.8f,top+(25+y*4)*scale+pixel*0.8f},lcd);
    char digits[4]{};sprintf_s(digits,"%d",number);float x=left+75*scale;for(const char* p=digits;*p;p++){LcdDigit(draw,*p-'0',{x,top+14*scale},65*scale,lcd);x+=39*scale;}
    draw->AddText({left+211*scale,top+61*scale},lcd,miles?"mph":"km/h");draw->AddText({left+12*scale,top+82*scale},crashed?IM_COL32(255,110,70,255):lcd,crashed?"CRASHED":"FPV");
}
void FlightOsd() {
    if(!active||!onTrack||settingsScreen)return;
    auto display=ImGui::GetIO().DisplaySize;auto draw=ImGui::GetBackgroundDrawList();
    if(showDroneHud&&(replaceBikeHud||GetTickCount64()-gearTick>=200))DroneLcd(draw,display);
    if(showDroneHud&&!replaceBikeHud&&GetTickCount64()-gearTick<200){
        // Native text coordinates are transformed into the game's 4:3 safe region.
        float aspect=display.x/display.y,reference=4.0f/3;float x=gearX,y=gearY;
        if(aspect>reference)x=(1-reference/aspect)*0.5f+(reference/aspect)*x;
        else y=(1-aspect/reference)*0.5f+(aspect/reference)*y;
        float unit=std::clamp(gearSize*display.y/8,3.0f,15.0f);
        const char* mask[]={"11100000111","10110001101","11101010111","00001110000","00001110000","11101010111","10110001101","11100000111"};
        for(int row=0;row<8;row++)for(int col=0;col<11;col++)if(mask[row][col]=='1'){
            ImVec2 a{x*display.x+col*unit,y*display.y+row*unit};draw->AddRectFilled(a,{a.x+unit*0.82f,a.y+unit*0.82f},IM_COL32(235,239,225,255));
        }
    }
    if(!showControls)return;Input input;bool crashed{},mode3d{};float command{},impact{};
    {std::lock_guard<std::mutex> lock(flightMutex);input=flightInput;crashed=flight.crashed;mode3d=flightSettings.mode3d;command=ThrottleCommand(input.throttle,flightSettings);impact=flight.impactSpeed;}
    float s=std::clamp(display.y/1080.0f,0.7f,1.6f),left=24*s,top=display.y-170*s;
    draw->AddRectFilled({left-8*s,top-8*s},{left+338*s,top+151*s},IM_COL32(8,12,18,205));
    auto label=[&](float x,float y,const char* text){draw->AddText({left+x*s,top+y*s},IM_COL32(225,230,234,255),text);};
    ImU32 line=IM_COL32(160,170,182,200),green=crashed?IM_COL32(235,90,55,255):IM_COL32(110,205,115,255);
    // Roll / pitch use their mapped physical channels, not stock bike steering inputs.
    ImVec2 center{left+66*s,top+69*s};float radius=46*s;
    draw->AddRect({center.x-radius,center.y-radius},{center.x+radius,center.y+radius},line);
    draw->AddLine({center.x-radius,center.y},{center.x+radius,center.y},line);draw->AddLine({center.x,center.y-radius},{center.x,center.y+radius},line);
    draw->AddCircleFilled({center.x+input.roll*radius,center.y-input.pitch*radius},5*s,green);label(18,0,"ROLL / PITCH");
    label(137,0,"YAW");float yawX=left+140*s,yawY=top+66*s,w=96*s;
    draw->AddRect({yawX,yawY-7*s},{yawX+w,yawY+7*s},line);
    draw->AddLine({yawX+w/2,yawY-11*s},{yawX+w/2,yawY+11*s},line);
    draw->AddCircleFilled({yawX+w*(input.yaw+1)/2,yawY},5*s,green);
    label(264,0,mode3d?"THR 3D":"THR");float barX=left+282*s,barTop=top+24*s,barBottom=top+116*s,barWidth=20*s;
    draw->AddRect({barX,barTop},{barX+barWidth,barBottom},line);
    if(mode3d){float middle=(barTop+barBottom)*0.5f,end=middle-command*(barBottom-barTop)*0.5f;draw->AddRectFilled({barX+2*s,std::min(middle,end)},{barX+barWidth-2*s,std::max(middle,end)},green);draw->AddLine({barX-4*s,middle},{barX+barWidth+4*s,middle},line);}
    else draw->AddRectFilled({barX+2*s,barBottom-input.throttle*(barBottom-barTop)},{barX+barWidth-2*s,barBottom},green);
    char value[64]{};sprintf_s(value,mode3d?"%+.0f%%":"%.0f%%",mode3d?command*100:input.throttle*100);label(260,121,value);
    if(crashed){sprintf_s(value,"CRASHED  %.0f m/s  /  R: reset",impact);label(0,132,value);}
    else label(0,132,"FPV  /  F8: bike  /  R: reset");
}
