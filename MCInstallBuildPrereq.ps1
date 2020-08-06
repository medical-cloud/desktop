# Execute in an elevated PS with:
# 
# powershell.exe -ExecutionPolicy bypass -File "\\vboxsrv\nextcloud\desktop\MCInstallBuildPrereq.ps1"
#

$global:sourcePath="E:\prog_sources"
$global:targetPath="C:\Users\User\Desktop\WIP"

function getFile {
  param(
    [Parameter(Mandatory = $true,Position = 0)]
    [string]
    $Url,
    [Parameter(Mandatory = $false,Position = 1)]
    [string]
    $FileName
  )
  process {
    if ( $PSBoundParameters.ContainsKey('FileName') ) {
      $installer=$FileName
    } else {
      $installer=[System.IO.Path]::GetFileName([uri]::new($Url).LocalPath)
    }
    $instSourcePath="$sourcePath\$installer"
    $instTargetPath="$targetPath\$installer"
    if ( -not (Test-Path "$instSourcePath" -PathType Leaf) ) {
      Write-Host -NoNewline "Downloading from $Url ... "
      (New-Object System.Net.WebClient).DownloadFile($Url, $instTargetPath)
      Write-Host "Done"
      cp "$instTargetPath" "$instSourcePath"
    } else {
      Write-Host "Use local cache"
      cp "$instSourcePath" "$instTargetPath"
    }

    return "$instTargetPath"
  }
}

echo ">> CreateBuildEnv.ps1"
echo "$((Get-Date).ToString())"

echo ""
echo "Create drive"
Remove-PSDrive -Force -Name E -ErrorAction SilentlyContinue
New-PSDrive -Name E -PSProvider FileSystem -Root \\vboxsrv\nextcloud | Out-null

echo ""
echo "Create WIP dir"
rm -Recurse -Force -Path "$targetPath" -ErrorAction SilentlyContinue
mkdir $targetPath | Out-null

# Setup a comfy env with basic tools (7zip, N++, Sysinternals)
echo ""
echo "Install 7zip"
$instTargetPath = getFile -Url "https://www.7-zip.org/a/7z1900-x64.exe" 
# Use a silent installation
Start-Process "$instTargetPath" -ArgumentList "/S" -NoNewWindow -Wait
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"
# Setup the exe path (now that we can use it)
$7Zexe="C:\Program Files\7-Zip\7z.exe"

echo ""
echo "Install N++"
$instTargetPath = getFile -Url "https://github.com/notepad-plus-plus/notepad-plus-plus/releases/download/v7.8.6/npp.7.8.6.Installer.x64.exe"
# Use a silent installation
Start-Process "$instTargetPath" -ArgumentList "/S" -NoNewWindow -Wait
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"
echo ""

echo "Install WinMerge"
$instTargetPath = getFile -Url "https://netix.dl.sourceforge.net/project/winmerge/stable/2.16.6/WinMerge-2.16.6-Setup.exe"
# Use a silent installation
Start-Process "$instTargetPath" -ArgumentList "/SILENT /ALLUSERS" -NoNewWindow -Wait
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

echo ""
echo "Install Sysinternals"
$instTargetPath = getFile -Url "https://download.sysinternals.com/files/SysinternalsSuite.zip"
# Installation (overwrite mode aoa) into System32 to be automatically in the path ...
& $7zexe x -aoa -o"C:\Windows\System32\" "$instTargetPath" | out-null
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

# Now the real build dependencies 

echo ""
echo "Install Git"
$instTargetPath=getFile -Url "https://github.com/git-for-windows/git/releases/download/v2.26.2.windows.1/Git-2.26.2-64-bit.exe"
# Installation
Start-Process "$instTargetPath" -ArgumentList "/SILENT" -NoNewWindow -Wait
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"
# Setup the exe path
$git="C:\Program Files\Git\bin\git.exe"

echo ""
echo "Get qtkeychain sources"
$sourceProjectName="qtkeychain"
$sourcesPath="E:\$sourceProjectName"
$targetWIPPath="$targetPath\$sourceProjectName"
if ( -not (Test-Path "$sourcesPath" -PathType Container) ) {
  & $git clone -b v0.10.0 https://github.com/frankosterfeld/qtkeychain "$targetWIPPath"
  cp -Recurse "$targetWIPPath" "$sourcesPath"
}
else {
  cp -Recurse "$sourcesPath" "$targetWIPPath"
}

echo ""
echo "Get nextcloud desktop sources"
$sourceProjectName="desktop"
$sourcesPath="E:\$sourceProjectName"
$targetWIPPath="$targetPath\$sourceProjectName"
if ( -not (Test-Path "$sourcesPath" -PathType Container) ) {
  & $git clone -b v2.6.4 https://github.com/nextcloud/desktop.git "$targetPath\$sourcesPath" 
  cp -Recurse "$targetWIPPath" "$sourcesPath"
}
else {
  cp -Recurse "$sourcesPath" "$targetWIPPath"
}

echo ""
echo "Install CMake (unzip)"
$instTargetPath=getFile -Url "https://github.com/Kitware/CMake/releases/download/v3.17.1/cmake-3.17.1-win64-x64.zip"
# Installation
& $7zexe x -o"$targetPath" "$instTargetPath" | out-null
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

echo ""
echo "Install Zlib (unzip)"
$instTargetPath=getFile -Url "https://www.bruot.org/hp/media/files/libraries/zlib_1_2_8_msvc2015_64.zip"
# Installation
& $7zexe x -o"$targetPath\zlib" "$instTargetPath" | out-null
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

echo ""
echo "Install PNG2ICO (unzip)"
$instTargetPath=getFile -Url "https://github.com/hiiamok/png2ImageMagickICO/releases/download/v1.0/x64.zip"
# Installation (use e to flatten the file hierachy to have the png2ico.exe file direclty inside the target)
& $7zexe e -o"$targetPath\png2ico" "$instTargetPath" | out-null
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

echo ""
echo "Install OpenSSL for building"
$instTargetPath=getFile -Url "https://slproweb.com/download/Win64OpenSSL-1_1_1g.exe"
# Installation 
Start-Process "$instTargetPath" -ArgumentList "/SILENT" -NoNewWindow -Wait
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

echo ""
echo "Install OpenSSL for packaging"
$instTargetPath=getFile -Url "https://indy.fulgan.com/SSL/openssl-1.0.2u-x64_86-win64.zip"
# Installation 
& $7zexe x -o"$targetPath\openssl" "$instTargetPath" | out-null
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

echo ""
echo "Install NSIS"
$instTargetPath=getFile -Url "https://netcologne.dl.sourceforge.net/project/nsis/NSIS%203/3.05/nsis-3.05-setup.exe"
# Installation 
Start-Process "$instTargetPath" -ArgumentList "/S" -NoNewWindow -Wait
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"

$instTargetPath=getFile -Url "https://nsis.sourceforge.io/mediawiki/images/8/8f/UAC.zip"
# Installation (use e to flatten the file hierachy to have the png2ico.exe file direclty inside the target)
& $7zexe e -aoa -o"$targetPath\UAC" "$instTargetPath" | out-null
cp "$targetpath\UAC\UAC.dll" "c:\program files (x86)\nsis\plugins\x86-unicode\UAC.dll"
cp "$targetpath\UAC\UAC.nsh" "c:\program files (x86)\nsis\include\UAC.nsh"
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"
rm -Recurse -Force -Path "$targetPath\UAC"

$instTargetPath=getFile -Url "https://nsis.sourceforge.io/mediawiki/images/1/18/NsProcess.zip"
# Installation (use e to flatten the file hierachy to have the png2ico.exe file direclty inside the target)
& $7zexe e -aoa -o"$targetPath\NsProcess" "$instTargetPath" | out-null
cp "$targetpath\NsProcess\nsProcessW.dll" "c:\program files (x86)\nsis\plugins\x86-unicode\nsProcess.dll"
cp "$targetpath\NsProcess\nsProcess.nsh"  "c:\program files (x86)\nsis\include\nsProcess.nsh"
# Cleanup
rm -Recurse -Force -Path "$instTargetPath"
rm -Recurse -Force -Path "$targetPath\NsProcess"

echo ""
echo "Get vcredist_x86.exe"
$instTargetPath=getFile -Url "http://download.microsoft.com/download/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x86.exe"

echo ""
echo "Get vcredist_x64.exe"
$instTargetPath=getFile -Url "http://download.microsoft.com/download/2/E/6/2E61CFA4-993B-4DD4-91DA-3737CD5CD6E3/vcredist_x64.exe"

echo ""
echo "Install QT"
# To update QT, you must also update the noninteractive config file (look for instruction their)
$instTargetPath=getFile -Url "http://mirrors.ukfast.co.uk/sites/qt.io/archive/qt/5.14/5.14.2/qt-opensource-windows-x86-5.14.2.exe"
# Installation 
# Disconnect all network adapter to simulate a network failure and trigger the installer credential bypass behavior
cmd /c echo y | powershell "Disable-NetAdapter -Name '*' "

#Also copy the installation script file
cp "E:\overlay\qt-installer-noninteractive.qs" "$targetPath"

echo "$((Get-Date).ToString())"
# Use that to keep the cmd window open
Start-Process "cmd" -Verb RunAs -ArgumentList "/c cd $targetPath && $instTargetPath --verbose --proxy --script qt-installer-noninteractive.qs && exit" -Wait

# Cleanup
rm -Recurse -Force -Path "$instTargetPath"
rm -Recurse -Force -Path "$targetPath\qt-installer-noninteractive.qs"
cmd /c echo y | powershell "Enable-NetAdapter -Name '*' "

# Last big peace, I did not find a way to download it directly from the net, instead on a windows machine we must do:
# Download https://aka.ms/vs/15/release/vs_community.exe then
#
# mkdir C:\vs2017layout_en
# vs_community.exe --layout C:\vs2017layout_en --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --lang en-US
#
# or for fr:
# mkdir C:\vs2017layout_fr 
# vs_community.exe --layout C:\vs2017layout_fr --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --lang fr-FR
#
# Then copy and store somewhere the C:\vs2017layout_XX folder
#

# For some reasons the existing installation does not seems to works, so remove and re-install ...
echo ""
echo "Remove existing Visual Studio"
& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\resources\app\layout\InstallCleanup.exe" "-f"

echo ""
echo "Installing Visual Studio"
$vsPath="C:\vs2017layout_en"
cp -r "$sourcePath\vs2017layout_en" "$vsPath"
Start-Process "$vsPath\vs_community.exe" -ArgumentList "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive --norestart" -NoNewWindow -Wait
rm -Recurse -Force -Path "$vsPath" -ErrorAction SilentlyContinue

echo ""
echo "$((Get-Date).ToString())"
echo "<< CreateBuildEnv.ps1"

