void PitEntry() {
    if(!pitScreen||menuOpen||settingsScreen||spectatorMode)return;
    auto display=ImGui::GetIO().DisplaySize;float width=std::min(display.x,display.y*16/9),height=width*9/16;
    bool nextToGate=nativeView.RaceInfoTab();float x=nextToGate?0.275f:0.25f,y=nextToGate?nativeView.GateButtonY():0.966667f;
    ImVec2 position{(display.x-width)*0.5f+x*width,(display.y-height)*0.5f+y*height},size{width*0.125f,height/30};
    ImGui::SetNextWindowPos(position,ImGuiCond_Always);ImGui::SetNextWindowSize(size,ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::Begin("Fly FPV pit entry",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNav);
    bool clicked=ImGui::InvisibleButton("Fly FPV",size);auto draw=ImGui::GetWindowDrawList();bool hover=ImGui::IsItemHovered();
    draw->AddRectFilledMultiColor(position,{position.x+size.x,position.y+size.y},hover?IM_COL32(119,143,164,255):IM_COL32(103,116,128,255),hover?IM_COL32(119,143,164,255):IM_COL32(103,116,128,255),IM_COL32(60,74,89,255),IM_COL32(60,74,89,255));
    draw->AddRect(position,{position.x+size.x,position.y+size.y},IM_COL32(156,170,184,255));float font=std::clamp(height*0.02f,13.0f,30.0f);
    auto text=ImGui::GetFont()->CalcTextSizeA(font,1000,0,"Fly FPV");draw->AddText(ImGui::GetFont(),font,{position.x+(size.x-text.x)*0.5f,position.y+(size.y-text.y)*0.5f},IM_COL32(240,240,240,255),"Fly FPV");
    if(clicked)spectatorRequested=true;
    if(hover)ImGui::SetTooltip("Fly as a drone spectator while the race continues. F8 or Escape returns to the pits.");
    ImGui::End();ImGui::PopStyleVar();
    if(!spectatorStatus.empty()){
        ImGui::SetNextWindowPos({position.x,position.y-45});ImGui::SetNextWindowSize({460,0});ImGui::Begin("FPV pit status",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings);ImGui::TextWrapped("%s",spectatorStatus.c_str());ImGui::End();
    }
}
void RatePlot() {
    auto origin=ImGui::GetCursorScreenPos();ImVec2 size{ImGui::GetContentRegionAvail().x,160};ImGui::InvisibleButton("rates graph",size);
    auto d=ImGui::GetWindowDrawList();d->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(237,240,244,255));
    float maximum=1;for(int a=0;a<3;a++)maximum=std::max(maximum,std::abs(AxisRate(1,flightSettings,a)));
    for(int i=0;i<=4;i++){float x=origin.x+size.x*i/4,y=origin.y+size.y*i/4;d->AddLine({x,origin.y},{x,origin.y+size.y},IM_COL32(155,164,176,100));d->AddLine({origin.x,y},{origin.x+size.x,y},IM_COL32(155,164,176,100));}
    ImU32 colors[]={IM_COL32(195,60,50,255),IM_COL32(40,145,70,255),IM_COL32(50,100,210,255)};
    for(int a=0;a<3;a++){ImVec2 previous{};for(int i=0;i<=100;i++){float x=i/100.0f;ImVec2 p{origin.x+x*size.x,origin.y+size.y-std::abs(AxisRate(x,flightSettings,a))/maximum*size.y};if(i)d->AddLine(previous,p,colors[a],2);previous=p;}}
    ImGui::Text("0 - 100%% stick     Roll / pitch / yaw max: %.0f / %.0f / %.0f deg/s",AxisRate(1,flightSettings,0),AxisRate(1,flightSettings,1),AxisRate(1,flightSettings,2));
}
void Overlay() {
    if(!menuOpen){if(settingsScreen){auto screen=ImGui::GetIO().DisplaySize;ImGui::SetNextWindowPos({screen.x-28,screen.y-60},ImGuiCond_Always,{1,1});
        ImGui::SetNextWindowBgAlpha(0);ImGui::Begin("FPV entry",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings);
        if(updater.Status().available)ImGui::TextColored({0.1f,0.45f,0.15f,1},"FPV update available");
        if(ImGui::Button("FPV Drone   [F9]",{220,31}))menuOpen=true;ImGui::End();}return;}
    std::lock_guard<std::mutex> settingsLock(flightMutex);const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({display.x*0.5f,display.y*0.5f},ImGuiCond_Appearing,{0.5f,0.5f});
    ImGui::SetNextWindowSize({std::min(1030.0f,display.x-40),std::min(735.0f,display.y-60)},ImGuiCond_Always);
    bool open=true;ImGui::Begin("MX BIKES  |  FPV DRONE SETTINGS",&open,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize);
    if(!open){menuOpen=false;ImGui::End();return;}
    static int page{};bool changed=false;static int binding=-1;static ULONGLONG bindTime{};static std::array<float,8> baseline{};
    ImGui::BeginChild("Navigation",{155,-51},ImGuiChildFlags_Borders);
    const char* pages[]={"Receiver","Rates & throttle","Aircraft","Audio & display","Multiplayer","Rider","Updates"};
    for(int i=0;i<7;i++){if(ImGui::Selectable(pages[i],page==i,0,{0,35})){page=i;binding=-1;}}
    ImGui::Spacing();ImGui::Separator();ImGui::TextWrapped("F8: fly / return\nR: reset drone\nF9: settings");ImGui::EndChild();ImGui::SameLine();
    ImGui::BeginChild("Settings page",{0,-51},ImGuiChildFlags_Borders);
    ImGui::SeparatorText(pages[page]);ImGui::PushItemWidth(280);
    if(page==0){
        ImGui::TextColored(liveInput.connected?ImVec4(0.1f,0.45f,0.15f,1):ImVec4(0.7f,0.2f,0.1f,1),"%s",liveInput.connected?"Connected":"Controller disconnected");
        ImGui::SameLine();ImGui::TextUnformatted(Utf8(controller.Name()).c_str());
        int backend=deviceSettings.useDirectInput?1:0;if(ImGui::Combo("Input device",&backend,"Auto / gamepad\0USB FPV radio\0")){deviceSettings.useDirectInput=backend==1;changed=true;}
        ImGui::SameLine();if(ImGui::Button("Reconnect"))changed=true;
        ImGui::TextWrapped("Bind each function, move that stick, then check its live value. All on-screen controls use this mapping.");
        int* mappings[]={&deviceSettings.rollAxis,&deviceSettings.pitchAxis,&deviceSettings.yawAxis,&deviceSettings.throttleAxis};
        bool* inversion[]={&deviceSettings.invertRoll,&deviceSettings.invertPitch,&deviceSettings.invertYaw,&deviceSettings.invertThrottle};
        const char* names[]={"Roll","Pitch","Yaw","Throttle"};float values[]={liveInput.roll,liveInput.pitch,liveInput.yaw,liveInput.throttle};
        if(binding>=0){ImGui::Text("Move %s...",names[binding]);auto axes=controller.Axes();int selected=-1;float largest=0.45f;for(int i=0;i<8;i++){float delta=std::abs(axes[i]-baseline[i]);if(delta>largest){largest=delta;selected=i;}}
            if(selected>=0){*mappings[binding]=selected;binding=-1;changed=true;}else if(GetTickCount64()-bindTime>10000)binding=-1;}
        if(ImGui::BeginTable("Channel mapping",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Function",ImGuiTableColumnFlags_WidthFixed,76);ImGui::TableSetupColumn("Axis",ImGuiTableColumnFlags_WidthFixed,105);ImGui::TableSetupColumn("Bind",ImGuiTableColumnFlags_WidthFixed,65);ImGui::TableSetupColumn("Invert",ImGuiTableColumnFlags_WidthFixed,52);ImGui::TableSetupColumn("Live");ImGui::TableHeadersRow();
            for(int i=0;i<4;i++){ImGui::PushID(i);ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(names[i]);ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);changed=ImGui::Combo("##axis",mappings[i],"0 / X\0" "1 / Y\0" "2 / Z\0" "3 / Rx\0" "4 / Ry\0" "5 / Rz\0" "6 / S1\0" "7 / S2\0")||changed;
                ImGui::TableSetColumnIndex(2);if(ImGui::Button(binding==i?"Move":"Bind",{-1,0})){binding=i;baseline=controller.Axes();bindTime=GetTickCount64();}ImGui::TableSetColumnIndex(3);changed=ImGui::Checkbox("##invert",inversion[i])||changed;
                ImGui::TableSetColumnIndex(4);ChannelBar("mapped",values[i],i==3,{ImGui::GetContentRegionAvail().x,25});ImGui::PopID();}ImGui::EndTable();}
        if(ImGui::Button("DJI FPV default mapping")){deviceSettings.rollAxis=0;deviceSettings.pitchAxis=1;deviceSettings.yawAxis=3;deviceSettings.throttleAxis=2;deviceSettings.invertRoll=deviceSettings.invertPitch=deviceSettings.invertYaw=deviceSettings.invertThrottle=false;deviceSettings.useDirectInput=true;changed=true;}
        changed=ImGui::SliderFloat("Stick deadband",&deviceSettings.deadzone,0,0.3f,"%.3f")||changed;
        ImGui::SeparatorText("Mode switch");ImGui::SetNextItemWidth(100);if(ImGui::InputInt("Keyboard code (F8 = 119)",&toggleKey))toggleKey=std::clamp(toggleKey,1,254);
        static bool bindButton=false;static int previousPhysical=-1;
        if(ImGui::Button(bindButton?"Press radio switch...":"Bind radio switch")){bindButton=!bindButton;previousPhysical=liveButton;}
        if(bindButton){int b=liveButton;if(b>=0&&b!=previousPhysical){deviceSettings.toggleButton=b;bindButton=false;changed=true;}previousPhysical=b;}
        ImGui::SameLine();ImGui::Text("Button %d",deviceSettings.toggleButton);ImGui::SameLine();if(ImGui::Button("Clear")){deviceSettings.toggleButton=-1;changed=true;}
        if(ImGui::CollapsingHeader("Device details / raw channels")){
            ImGui::TextWrapped("%s",Utf8(deviceSettings.guid).c_str());if(ImGui::Button("Use detected radio")&&!controller.Guid().empty()){deviceSettings.guid=controller.Guid();deviceSettings.useDirectInput=true;changed=true;}ImGui::SameLine();if(ImGui::Button("Auto-select")){deviceSettings.guid=L"auto";changed=true;}
            auto axes=controller.Axes();for(int i=0;i<8;i++){ImGui::PushID(100+i);ImGui::Text("%d",i);ImGui::SameLine();ChannelBar("raw",axes[i],false,{ImGui::GetContentRegionAvail().x,18});ImGui::PopID();}}
    }else if(page==1){
        if(ImGui::Combo("Rate type",&flightSettings.rateModel,"Betaflight\0Raceflight\0KISS\0Actual\0Quick\0")){
            for(auto& r:flightSettings.rates){r.expo=0;if(flightSettings.rateModel==1){r.rate=80;r.super=70;}else if(flightSettings.rateModel>=3){r.rate=200;r.super=670;}else{r.rate=0.8f;r.super=0.7f;}}flightSettings.individualRates=true;}
        ImGui::TextWrapped("Independent roll, pitch and yaw curves. Rates are in degrees per second; expo softens the center.");
        const char* names[]={"Roll","Pitch","Yaw"};
        if(ImGui::BeginTable("Rates",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame)){
            ImGui::TableSetupColumn("Axis");ImGui::TableSetupColumn(flightSettings.rateModel>=3?"Center sensitivity":"RC rate");ImGui::TableSetupColumn(flightSettings.rateModel>=3?"Maximum rate":flightSettings.rateModel==1?"Acro+":"Super rate");ImGui::TableSetupColumn("Expo");ImGui::TableHeadersRow();
            for(int i=0;i<3;i++){auto& r=flightSettings.rates[i];ImGui::PushID(i);ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(names[i]);ImGui::TableSetColumnIndex(1);ImGui::SetNextItemWidth(-1);
                bool rateChanged=ImGui::DragFloat("##rate",&r.rate,flightSettings.rateModel>=1&&flightSettings.rateModel!=2?1:0.01f,0.01f,flightSettings.rateModel>=3?1000.0f:flightSettings.rateModel==1?200.0f:3.0f,"%.2f");
                ImGui::TableSetColumnIndex(2);ImGui::SetNextItemWidth(-1);rateChanged=ImGui::DragFloat("##super",&r.super,flightSettings.rateModel>=1&&flightSettings.rateModel!=2?1:0.01f,0,flightSettings.rateModel>=3?1998.0f:flightSettings.rateModel==1?100.0f:0.99f,"%.2f")||rateChanged;
                ImGui::TableSetColumnIndex(3);ImGui::SetNextItemWidth(-1);rateChanged=ImGui::SliderFloat("##expo",&r.expo,0,1,"%.2f")||rateChanged;if(rateChanged)flightSettings.individualRates=true;ImGui::PopID();}ImGui::EndTable();}
        if(flightSettings.rateModel==4)ImGui::Checkbox("Quick: apply expo to RC input",&flightSettings.quickRcExpo);
        RatePlot();ImGui::SeparatorText("Throttle curve");
        ImGui::SliderFloat("Throttle mid",&flightSettings.throttleMid,0.05f,0.95f,"%.2f");ImGui::SliderFloat("Throttle expo",&flightSettings.throttleExpo,0,1,"%.2f");float limit=flightSettings.throttleLimit*100;if(ImGui::SliderFloat("Throttle limit",&limit,10,100,"%.0f%%"))flightSettings.throttleLimit=limit/100;
        ImGui::Checkbox("3D mode: reversible motors",&flightSettings.mode3d);
        if(flightSettings.mode3d){ImGui::SliderFloat("3D neutral deadband",&flightSettings.neutral3d,0.01f,0.25f,"%.2f");ImGui::TextWrapped("Center throttle stops thrust. Above center pushes upward; below center reverses thrust for inverted flight.");}
    }else if(page==2){
        const auto& presets=Presets();if(ImGui::BeginCombo("Preset",presets[presetIndex].name)){for(int i=0;i<static_cast<int>(presets.size());i++)if(ImGui::Selectable(presets[i].name,i==presetIndex)){
            auto old=flightSettings;presetIndex=i;flightSettings=presets[i].settings;
            flightSettings.rates=old.rates;flightSettings.rateModel=old.rateModel;flightSettings.individualRates=old.individualRates;flightSettings.quickRcExpo=old.quickRcExpo;flightSettings.mode3d=old.mode3d;flightSettings.neutral3d=old.neutral3d;flightSettings.throttleMid=old.throttleMid;flightSettings.throttleExpo=old.throttleExpo;flightSettings.throttleLimit=old.throttleLimit;flightSettings.collisions=old.collisions;flightSettings.vehicleCollisions=old.vehicleCollisions;}ImGui::EndCombo();}
        ImGui::Text("Thrust / weight %.1f : 1   |   Level hover about %.0f%%",flightSettings.ThrustToWeight(),flightSettings.HoverThrottle()*100);
        ImGui::SliderFloat("Mass",&flightSettings.massGrams,50,1800,"%.0f g");ImGui::SliderFloat("Prop diameter",&flightSettings.propInches,1.5f,7,"%.1f inch");ImGui::SliderFloat("Prop pitch",&flightSettings.propPitch,1,6,"%.1f inch");ImGui::SliderInt("Blades",&flightSettings.blades,2,5);
        ImGui::Combo("Prop type",&flightSettings.propType,"Freestyle\0Race\0Ducted / cinewhoop\0");ImGui::SliderInt("Battery",&flightSettings.cells,2,8,"%d S");ImGui::SliderFloat("Thrust per motor",&flightSettings.thrustPerMotorGrams,60,2200,"%.0f g");
        ImGui::SliderFloat("Camera tilt",&flightSettings.cameraTilt,0,60,"%.0f deg");ImGui::SliderFloat("Field of view",&cameraFov,60,120,"%.0f deg");
        ImGui::Checkbox("Ground collision",&flightSettings.collisions);ImGui::Checkbox("Crash on bikes and riders",&flightSettings.vehicleCollisions);
        if(ImGui::CollapsingHeader("Advanced handling / ground response")){ImGui::SliderFloat("Motor spool",&flightSettings.motorResponseMs,10,100,"%.0f ms");ImGui::SliderFloat("Motor thrust exponent",&flightSettings.throttleCurve,0.7f,2,"%.2f");ImGui::SliderFloat("Air drag area",&flightSettings.dragArea,0.001f,0.1f,"%.3f");ImGui::SliderFloat("Bounce",&flightSettings.restitution,0,0.8f,"%.2f");ImGui::SliderFloat("Ground friction",&flightSettings.groundFriction,0,1,"%.2f");ImGui::SliderFloat("Collision radius",&flightSettings.radius,0.05f,0.3f,"%.2f m");}
    }else if(page==3){
        ImGui::Checkbox("Drone motor sound",&audioSettings.enabled);ImGui::SliderFloat("Motor volume",&audioSettings.volume,0,2,"%.2f");ImGui::Checkbox("Hear other pilots' drones",&audioSettings.remote);ImGui::SliderFloat("Other drone volume",&audioSettings.remoteVolume,0,2,"%.2f");ImGui::SliderFloat("Audible distance",&audioSettings.range,30,300,"%.0f m");ImGui::TextDisabled("%s",droneAudio.Status());
        ImGui::SeparatorText("Flight display");ImGui::Checkbox("Drone icon and drone speed in dashboard",&showDroneHud);ImGui::Checkbox("Mapped stick / throttle display",&showControls);ImGui::Checkbox("Replace bike controls and instruments while in FPV",&replaceBikeHud);
        ImGui::TextWrapped("Speed uses MX Bikes' current units. Roll, pitch and yaw follow Receiver mapping. Normal throttle is a bottom-to-top bar; 3D throttle has a center neutral point.");
        if(ImGui::CollapsingHeader("Map",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::Checkbox("Map with numbered riders and drone icons",&mapSettings.enabled);
            ImGui::Checkbox("Use existing MaxHUD map when available",&mapSettings.external);
            ImGui::SliderFloat("Fallback map size",&mapSettings.size,180,460,"%.0f px");
            ImGui::SetNextItemWidth(120);if(ImGui::InputInt("Map key (M = 77)",&mapSettings.key))mapSettings.key=std::clamp(mapSettings.key,1,254);
            ImGui::TextWrapped("The map is available while riding and flying. Your drone is green; other pilots are orange. MaxHUD's map and standings remain available. The FPV map uses the current track centerline if no usable HUD map is found.");
        }
    }else if(page==4){
        ImGui::TextWrapped("Multiplayer is tested and working on public servers. Install FPV on each viewer's PC. Drones, sound, goggles and controller poses are shared automatically through the game server.");
        bool reconnect=ImGui::Checkbox("Enable drone sharing",&syncSettings.enabled);
        reconnect=ImGui::Combo("Connection",&syncSettings.transport,"Automatic / current game server\0Manual UDP relay (fallback)\0")||reconnect;
        if(syncSettings.transport==0){
            ImGui::TextWrapped("No IP address, extra port or host setup is needed. The game server can remain unmodified. Each viewer needs the FPV mod to render and hear the drones.");
            if(ImGui::Button("Reconnect discovery"))reconnect=true;
        }else{
            ImGui::Checkbox("Host the relay on this PC",&syncSettings.host);
            static char address[64]{},room[64]{};static bool filled=false;if(!filled){strncpy_s(address,syncSettings.address.c_str(),_TRUNCATE);strncpy_s(room,syncSettings.room.c_str(),_TRUNCATE);filled=true;}
            ImGui::InputText("Host IPv4",address,sizeof(address));ImGui::InputText("Room",room,sizeof(room));ImGui::InputInt("UDP port",&syncSettings.port);syncSettings.port=std::clamp(syncSettings.port,1024,65535);
            if(ImGui::Button("Connect / apply")){syncSettings.address=address;syncSettings.room=room;reconnect=true;}
            ImGui::TextWrapped("Fallback: one PC hosts a reachable UDP relay. Other clients use its address, port and room. Use LAN, VPN or port forwarding.");
        }
        if(reconnect)peerSync.Configure(syncSettings);ImGui::TextWrapped("%s",peerSync.Status().c_str());
    }else if(page==5){
        ImGui::Checkbox("FPV goggles",&visualSettings.goggles);ImGui::Checkbox("Handheld controller",&visualSettings.controller);ImGui::Checkbox("Hands on controller",&visualSettings.pose);ImGui::Checkbox("Visible remote drones",&visualSettings.drones);
        if(ImGui::CollapsingHeader("Model fit")){ImGui::SliderFloat("Controller below head",&visualSettings.controllerDown,0.2f,0.8f,"%.2f m");ImGui::SliderFloat("Controller ahead",&visualSettings.controllerForward,0.1f,0.6f,"%.2f m");ImGui::SliderFloat("Goggles offset",&visualSettings.gogglesForward,0.02f,0.25f,"%.2f m");}riderVisuals.Configure(gameBase,visualSettings);
    }else if(page==6){
        ImGui::Text("MXB FPV %s  |  by Moonie the Great",Version);
        ImGui::TextWrapped("Updates come from mooniethegreat/mxb-fpv-releases on GitHub. Checking never interrupts your flight or installs anything automatically.");
        auto update=updater.Status();
        ImGui::Spacing();ImGui::Checkbox("Check on launch (6-hour cache)",&automaticUpdates);
        ImGui::BeginDisabled(update.busy);
        if(ImGui::Checkbox("Include early releases (optional)",&previewUpdates))updater.Check(previewUpdates);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Click Save to keep these preferences.");
        ImGui::Spacing();
        if(update.available)ImGui::TextColored({0.1f,0.45f,0.15f,1},"Version %s is available",update.version.c_str());
        else if(!update.version.empty())ImGui::Text("Latest published version: %s",update.version.c_str());
        ImGui::TextWrapped("%s",update.message.c_str());
        ImGui::BeginDisabled(update.busy);
        if(ImGui::Button("Check for updates",{170,30}))updater.Check(previewUpdates);
        ImGui::SameLine();if(ImGui::Button("View GitHub releases",{185,30}))ShellExecuteW(gameWindow,L"open",ReleasesUrl,nullptr,nullptr,SW_SHOWNORMAL);
        if(!update.version.empty()&&update.compatible){
            if(ImGui::Button("Download verified installer",{260,30}))updater.Download();
            ImGui::SameLine();ImGui::Text("%.1f MB",update.bytes/(1024.0*1024.0));
        }
        ImGui::EndDisabled();
        if(!update.downloaded.empty()){
            if(ImGui::Button("Open download folder",{220,30}))ShellExecuteW(gameWindow,L"open",update.downloaded.parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            ImGui::TextWrapped("Close the game before running the installer. Your saved radio mapping is kept; Setup also keeps the previous plugin for rollback.");
        }
        if(!update.notes.empty()){ImGui::SeparatorText("Release notes");ImGui::TextWrapped("%s",update.notes.c_str());}
        ImGui::Spacing();ImGui::SeparatorText("Compatibility");
        ImGui::TextWrapped("This version supports MX Bikes beta 21e, Steam build 23566986. Updates are checked against the supported game executable before downloading. Installer and plugin check the executable again before use.");
    }
    ImGui::PopItemWidth();ImGui::EndChild();ImGui::Separator();static ULONGLONG saved{};
    if(ImGui::Button("Save",{130,30})){SaveSettings();saved=GetTickCount64();}ImGui::SameLine();if(ImGui::Button("Back",{130,30})){menuOpen=false;binding=-1;}
    ImGui::SameLine();if(GetTickCount64()-saved<3000)ImGui::TextUnformatted("Settings saved");
    if(changed)ReloadController();ImGui::End();
}
