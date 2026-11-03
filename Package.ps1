param([ValidateSet('preview','stable')][string]$Channel='stable')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$versionText=Get-Content -LiteralPath (Join-Path $root 'mod\src\version.h') -Raw
if($versionText -notmatch 'Version\[\]\s*=\s*"(\d+\.\d+(?:\.\d+)?)"'){throw 'Version header could not be read'}
$version=$Matches[1]
$out=Join-Path $root "out\$version"
$dist=Join-Path $root 'mod\dist'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$installerName="MXB-FPV-$version-Setup.exe"
$zipName=if($Channel -eq 'stable'){"MXB-FPV-$version.zip"}else{"MXB-FPV-$version-Test-Candidate.zip"}
Copy-Item -LiteralPath (Join-Path $dist 'MXB-FPV-Setup.exe') -Destination (Join-Path $out $installerName) -Force
$files=@('MXB-FPV-Setup.exe','mxb_fpv.dlo','mxb_fpv.ini','README.txt','MinHook-LICENSE.txt','ImGui-LICENSE.txt','FrostMod-LICENSE.txt','THIRD-PARTY-NOTICES.txt','Kino-AUDIO-PROVENANCE.txt') | ForEach-Object {Join-Path $dist $_}
Compress-Archive -LiteralPath $files -DestinationPath (Join-Path $out $zipName) -Force
# Archive only the curated source tree. Build output and local user configuration stay out.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$sourceZip=Join-Path $out "MXB-FPV-$version-Source.zip"
if(Test-Path -LiteralPath $sourceZip){Remove-Item -LiteralPath $sourceZip}
$archive=[IO.Compression.ZipFile]::Open($sourceZip,[IO.Compression.ZipArchiveMode]::Create)
try {
    $sourceFiles=@(Get-ChildItem -LiteralPath (Join-Path $root 'mod\src'),(Join-Path $root 'mod\assets'),(Join-Path $root 'mod\vendor'),(Join-Path $root 'mod\tests'),(Join-Path $root 'assets') -Recurse -File)
    $sourceFiles+=Get-ChildItem -LiteralPath $root,(Join-Path $root 'mod') -File | Where-Object {$_.Extension -in '.md','.txt','.ps1','.ini','.json' -or $_.Name -eq '.gitignore'}
    foreach($file in $sourceFiles){
        $relative=[IO.Path]::GetRelativePath($root,$file.FullName).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative,[IO.Compression.CompressionLevel]::Optimal)|Out-Null
    }
} finally {$archive.Dispose()}
$names=@($installerName,$zipName,"MXB-FPV-$version-Source.zip")
$entries=@($names | ForEach-Object {$file=Get-Item -LiteralPath (Join-Path $out $_);[pscustomobject]@{File=$file.Name;Bytes=$file.Length;SHA256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()}})
$entries | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'manifest.json') -Encoding utf8NoBOM
$entries | ForEach-Object {"$($_.SHA256)  $($_.File)"} | Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
$manifestPath=Join-Path $root 'update.json'
$feed=if(Test-Path -LiteralPath $manifestPath){Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -AsHashtable}else{@{schema=1;stable=$null;preview=$null}}
$installer=$entries | Where-Object File -EQ $installerName
if($versionText -notmatch 'SupportedGameHash\[\]\s*=\s*"([0-9a-f]{64})"'){throw 'Supported game hash could not be read'}
$feed[$Channel]=@{version=$version;notes="MXB FPV v$version stable release. Multiplayer is tested on public servers. Includes pit-screen drone spectator flight, rider/drone maps, centered controls, spatial sound, in-game tuning and verified updates. Reviewed flight behavior is unchanged.";game_sha256=@($Matches[1]);installer=@{sha256=$installer.SHA256;bytes=$installer.Bytes}}
if($Channel -eq 'stable' -and (!$feed.preview -or [version]$feed.preview.version -le [version]$version)){$feed.preview=$feed.stable}
$feed | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $out 'update.json') -Encoding utf8NoBOM
Write-Output "Prepared FPV $version assets in $out. The live update feed was not changed."
