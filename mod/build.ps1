param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$vs = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1"
if (!(Test-Path -LiteralPath $vs)) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
& $vs -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'
New-Item -ItemType Directory -Force -Path $build,$dist | Out-Null
Push-Location $build
try {
    $cflags = @('/nologo','/O2','/MT','/DNOMINMAX','/DUNICODE','/D_UNICODE')
    $cppflags = $cflags + @('/std:c++17','/EHsc','/W4')
    $minhook = Join-Path $root 'vendor\minhook\src'
    foreach ($name in @('buffer','hook','trampoline')) {
        & cl @cflags /c /TC (Join-Path $minhook "$name.c") "/Fo$name.obj"
        if ($LASTEXITCODE) { throw "MinHook $name failed" }
    }
    & cl @cflags /c /TC (Join-Path $minhook 'hde\hde64.c') /Fohde64.obj
    if ($LASTEXITCODE) { throw 'MinHook decoder failed' }
    $imgui = Join-Path $root 'vendor\imgui'
    $uiSources = @('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','backends\imgui_impl_win32.cpp','backends\imgui_impl_opengl3.cpp') | ForEach-Object { Join-Path $imgui $_ }
    $sources = @('plugin.cpp','controller.cpp','terrain.cpp','peer_sync.cpp','rider_visuals.cpp','drone_audio.cpp','traffic_collision.cpp','updater.cpp') | ForEach-Object { Join-Path $root "src\$_" }
    $audioResources = for($i=0;$i -lt 6;$i++) { $asset=Join-Path $root "assets\audio\kino-drone-$i.wav"; if(!(Test-Path -LiteralPath $asset)){throw "Missing drone audio asset $asset"}; "$($i+201) RCDATA `"$($asset.Replace('\','/'))`"" }
    Set-Content -LiteralPath 'drone_audio.rc' -Value $audioResources -Encoding ascii
    & rc /nologo /fo drone_audio.res drone_audio.rc
    if ($LASTEXITCODE) { throw 'Drone audio resource build failed' }
    & cl @cppflags "/I$imgui" /LD @sources @uiSources buffer.obj hook.obj trampoline.obj hde64.obj drone_audio.res /link dinput8.lib dxguid.lib user32.lib gdi32.lib ole32.lib bcrypt.lib opengl32.lib ws2_32.lib winhttp.lib windowsapp.lib shell32.lib "/OUT:$dist\mxb_fpv.dlo"
    if ($LASTEXITCODE) { throw 'Plugin build failed' }
    & cl @cppflags (Join-Path $root 'src\controls.cpp') (Join-Path $root 'src\controller.cpp') /link /SUBSYSTEM:WINDOWS dinput8.lib dxguid.lib user32.lib gdi32.lib ole32.lib comctl32.lib "/OUT:$build\MXB-FPV-Controls.exe"
    if ($LASTEXITCODE) { throw 'Controls build failed' }
    Copy-Item -LiteralPath (Join-Path $root 'mxb_fpv.ini') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $root 'README.txt') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $root 'vendor\minhook\LICENSE.txt') -Destination (Join-Path $dist 'MinHook-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $root 'vendor\imgui\LICENSE.txt') -Destination (Join-Path $dist 'ImGui-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $root 'vendor\frostmod-notice\LICENSE.txt') -Destination (Join-Path $dist 'FrostMod-LICENSE.txt')
    Copy-Item -LiteralPath (Join-Path $root 'THIRD-PARTY-NOTICES.txt') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $root 'assets\audio\PROVENANCE.txt') -Destination (Join-Path $dist 'Kino-AUDIO-PROVENANCE.txt')
    $rc = "101 RCDATA `"$($dist.Replace('\','/'))/mxb_fpv.dlo`"`n102 RCDATA `"$($dist.Replace('\','/'))/mxb_fpv.ini`"`n104 RCDATA `"$($dist.Replace('\','/'))/README.txt`"`n105 RCDATA `"$($dist.Replace('\','/'))/MinHook-LICENSE.txt`"`n106 RCDATA `"$($dist.Replace('\','/'))/ImGui-LICENSE.txt`"`n107 RCDATA `"$($dist.Replace('\','/'))/FrostMod-LICENSE.txt`"`n108 RCDATA `"$($dist.Replace('\','/'))/THIRD-PARTY-NOTICES.txt`"`n"
    $rc += "109 RCDATA `"$($dist.Replace('\','/'))/Kino-AUDIO-PROVENANCE.txt`"`n"
    Set-Content -LiteralPath 'payload.rc' -Value $rc -Encoding ascii
    & rc /nologo /fo payload.res payload.rc
    if ($LASTEXITCODE) { throw 'Resource build failed' }
    & cl @cppflags (Join-Path $root 'src\installer.cpp') payload.res /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib ole32.lib shell32.lib advapi32.lib bcrypt.lib "/OUT:$dist\MXB-FPV-Setup.exe"
    if ($LASTEXITCODE) { throw 'Installer build failed' }
    if (!$SkipTests) {
    & cl @cppflags (Join-Path $root 'tests\flight_test.cpp') /link "/OUT:$build\flight_test.exe"
    if ($LASTEXITCODE) { throw 'Flight check build failed' }
    & '.\flight_test.exe'
    if ($LASTEXITCODE) { throw 'Flight checks failed' }
    & cl @cppflags (Join-Path $root 'tests\plugin_contract.cpp') /link "/OUT:$build\plugin_contract.exe"
    if ($LASTEXITCODE) { throw 'Plugin contract check build failed' }
    & '.\plugin_contract.exe' (Join-Path $dist 'mxb_fpv.dlo')
    if ($LASTEXITCODE) { throw 'Plugin contract check failed' }
    }
    Get-FileHash -Algorithm SHA256 (Join-Path $dist 'MXB-FPV-Setup.exe'),(Join-Path $dist 'mxb_fpv.dlo') | Format-Table
} finally { Pop-Location }
