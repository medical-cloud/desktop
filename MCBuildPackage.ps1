# Execute in an elevated PS with:
# 
# powershell.exe -ExecutionPolicy bypass -File "\\vboxsrv\nextcloud\desktop\MCBuildPackage.ps1"
#

Set-StrictMode -Version Latest

$startTime = $(get-date)

## Cleanup
#Write-Host -NoNewLine "Cleanup previous sources... "
#Remove-Item -Force -Recurse C:\Users\User\Desktop\WIP\desktop
#Remove-Item -Force -Recurse C:\Users\User\Desktop\WIP\qtkeychain
#Write-Host "OK"
#
## The above removal is allowed to fail (it will surely fail on the first iteration of the script on a fresh VM)
## But after that point we want to stop on failures
#$ErrorActionPreference = "Stop"
#
## Copy files
#Write-Host -NoNewLine "Copy new sources... "
#Copy-Item -Recurse -Path "E:\qtkeychain\" -Destination "C:\Users\User\Desktop\WIP\" 
#Copy-Item -Recurse -Path "E:\desktop\" -Destination "C:\Users\User\Desktop\WIP\" 
#Write-Host "OK"
#
## Build and install qtkeychain
#Write-Host "Qtkeychain:"
#Set-Location -Path "C:\Users\User\Desktop\WIP\qtkeychain"
#
#& C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake -G "Visual Studio 15 2017 Win64" . `
#  -DCMAKE_INSTALL_PREFIX="C:\Users\User\Desktop\WIP\qtkeychain" `
#  -DCMAKE_BUILD_TYPE=Release `
#  -DQT_QMAKE_EXECUTABLE="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/bin/qmake.exe" `
#  -DQt5_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5" `
#  -DQt5Core_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5Core" `
#  -DQt5LinguistTools_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5LinguistTools"
#
#& C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake --build . `
#  --config Release `
#  --target install
#

#Write-Host "Build and package the client"
#New-Item -Path "C:\Users\User\Desktop\WIP\desktop" -Name "build" -ItemType "directory"
#Set-Location -Path "C:\Users\User\Desktop\WIP\desktop\build"
#
#& C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake -G "Visual Studio 15 2017 Win64" .. `
#  -DCMAKE_INSTALL_PREFIX="C:\Users\User\Desktop\WIP\desktop\build" `
#  -DCMAKE_BUILD_TYPE=Release `
#  -DNO_SHIBBOLETH="1" `
#  -DQT_QMAKE_EXECUTABLE="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/bin/qmake.exe" `
#  -DQt5_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5" `
#  -DQt5Core_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5Core" `
#  -DQt5LinguistTools_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5LinguistTools" `
#  -DZLIB_LIBRARY="C:\Users\User\Desktop\WIP\zlib\msvc2015_64\lib\zlib\zlib.lib" `
#  -DZLIB_INCLUDE_DIR="C:\Users\User\Desktop\WIP\zlib\msvc2015_64\include\zlib" `
#  -DQTKEYCHAIN_LIBRARY="C:\Users\User\Desktop\WIP\qtkeychain\lib\qt5keychain.lib" `
#  -DQTKEYCHAIN_INCLUDE_DIR="C:\Users\User\Desktop\WIP\qtkeychain\include\qt5keychain" `
#  -DPng2Ico_EXECUTABLE="C:\Users\User\Desktop\WIP\png2ico\png2ico.exe" `
#  -DAPPLICATION_UPDATE_URL="%APPLICATION_UPDATE_URL%" `
#  -DAPPLICATION_HELP_URL="%APPLICATION_HELP_URL%" `
#  -DAPPLICATION_SERVER_URL="%APPLICATION_SERVER_URL%"
#
#& C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake --build . `
#  --config Release `
#  --target install
#
#
## Signing requires network (must be run in a elevated PS)
#Write-Host -NoNewLine "Enabling network... "
#powershell 'Start-Process powershell -Verb RunAs -Wait -Argumentlist ''-Command "&{ cmd /c echo y | powershell ''''Enable-NetAdapter -Name * '''' }" '' '
#
## Wait 5 seconds for the network to be fully up
#Start-Sleep -Seconds 5 
#Write-Host "OK"
#
## With signtool we can add a description and an URL, but I did not find where it is shown, so for now prefer the Cmdlet
## Also as per https://stackoverflow.com/questions/28563734/how-can-i-specify-an-e-mail-address-when-signing-a-binary-file
## when we will create a new CSR, add an emailAddress ...
##& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.17134.0\x64\signtool.exe" sign /debug /v /n "MEDICALCLOUD SAS" /d "MedicalCloud Sync client" /du "https://www.medicalcloud.fr" /tr $Env:CERTIFICATE_TIMESTAMP_SERVER /td "sha256" /fd "sha256" /f $Env:CERTIFICATE_FILE /p $Env:CERTIFICATE_PWD "C:\Users\User\Desktop\WIP\qtkeychain\bin\qt5keychain.dll"
#
#$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2
#$cert.Import($Env:CERTIFICATE_FILE, $Env:CERTIFICATE_PWD, 'DefaultKeySet')
#
#Set-AuthenticodeSignature -FilePath "C:/Users/User/Desktop/WIP/qtkeychain/bin/qt5keychain.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\opengl32sw.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#
## Now we can disable again the network
#Write-Host -NoNewLine "Disabling network... "
#powershell 'Start-Process powershell -Verb RunAs -Wait -Argumentlist ''-Command "&{ cmd /c echo y | powershell ''''Disable-NetAdapter -Name * '''' }" '' '
#Write-Host "OK"
#
#Copy-Item -Path "C:\Users\User\Desktop\WIP\qtkeychain\bin\qt5keychain.dll"     -Destination "C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin"
#& C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin\windeployqt "C:\Users\User\Desktop\WIP\desktop\build\bin"
#
#Set-Location -Path "C:\Users\User\Desktop\WIP\desktop\build"
#Copy-Item -Path "C:\Users\User\Desktop\WIP\vcredist_x64.exe"                   -Destination "C:\Users\User\Desktop\WIP\desktop\build" 
#Copy-Item -Path "C:\Users\User\Desktop\WIP\vcredist_x86.exe"                   -Destination "C:\Users\User\Desktop\WIP\desktop\build" 
#Copy-Item -Path "C:\Users\User\Desktop\WIP\zlib\msvc2015_64\lib\zlib\zlib.dll" -Destination "C:\Users\User\Desktop\WIP\desktop\build\bin" 
#Copy-Item -Path "C:\Users\User\Desktop\WIP\openssl\libeay32.dll"               -Destination "C:\Users\User\Desktop\WIP\desktop\build\bin" 
#Copy-Item -Path "C:\Users\User\Desktop\WIP\openssl\ssleay32.dll"               -Destination "C:\Users\User\Desktop\WIP\desktop\build\bin" 
#
## Signing requires network (must be run in a elevated PS)
#Write-Host -NoNewLine "Enabling network... "
#powershell 'Start-Process powershell -Verb RunAs -Wait -Argumentlist ''-Command "&{ cmd /c echo y | powershell ''''Enable-NetAdapter -Name * '''' }" '' '
#
## Wait 5 seconds for the network to be fully up
#Start-Sleep -Seconds 5 
#Write-Host "OK"
#
## With signtool we can add a description and an URL, but I did not find where it is shown, so for now prefer the Cmdlet
## Also as per https://stackoverflow.com/questions/28563734/how-can-i-specify-an-e-mail-address-when-signing-a-binary-file
## when we will create a new CSR, add an emailAddress ...
##& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.17134.0\x64\signtool.exe" sign /debug /v /n "MEDICALCLOUD SAS" /d "MedicalCloud Sync client" /du "https://www.medicalcloud.fr" /tr $Env:CERTIFICATE_TIMESTAMP_SERVER /td "sha256" /fd "sha256" /f $Env:CERTIFICATE_FILE /p $Env:CERTIFICATE_PWD "C:\Users\User\Desktop\WIP\qtkeychain\bin\qt5keychain.dll"
#
#$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2
#$cert.Import($Env:CERTIFICATE_FILE, $Env:CERTIFICATE_PWD, 'DefaultKeySet')
#
#Set-AuthenticodeSignature -FilePath "bin/MedicalCloud/ocsync.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/MedicalCloud.exe" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/MedicalCloudcmd.exe" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/MedicalCloudsync.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/OCContextMenu.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/OCOverlays.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/OCUtil.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/libeay32.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/ssleay32.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#Set-AuthenticodeSignature -FilePath "bin/zlib.dll" -TimestampServer $Env:CERTIFICATE_TIMESTAMP_SERVER -Certificate $cert -HashAlgorithm "SHA256"
#
## Now we can disable again the network
#Write-Host -NoNewLine "Disabling network... "
#powershell 'Start-Process powershell -Verb RunAs -Wait -Argumentlist ''-Command "&{ cmd /c echo y | powershell ''''Disable-NetAdapter -Name * '''' }" '' '
#Write-Host "OK"

# Signing requires network (must be run in a elevated PS)
Write-Host -NoNewLine "Enabling network... "
powershell 'Start-Process powershell -Verb RunAs -Wait -Argumentlist ''-Command "&{ cmd /c echo y | powershell ''''Enable-NetAdapter -Name * '''' }" '' '

# Wait 5 seconds for the network to be fully up
Start-Sleep -Seconds 5 
Write-Host "OK"

# copy C:\Users\User\Desktop\WIP\desktop\cmake\modules\NSIS.template.in E:\desktop\cmake\modules\NSIS.template.in
# copy E:\desktop\cmake\modules\NSIS.template.in C:\Users\User\Desktop\WIP\desktop\cmake\modules\NSIS.template.in
Set-Location -Path "C:\Users\User\Desktop\WIP\desktop\build"
& C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake --build . `
  --config Release `
  --target package

# Now we can disable again the network
Write-Host -NoNewLine "Disabling network... "
powershell 'Start-Process powershell -Verb RunAs -Wait -Argumentlist ''-Command "&{ cmd /c echo y | powershell ''''Disable-NetAdapter -Name * '''' }" '' '
Write-Host "OK"

Write-Host -NoNewLine "Copy the just built installer... "
Copy-Item -Path "C:\Users\User\Desktop\WIP\desktop\build\Medical Cloud-2*.exe" -Destination "E:\" 
Write-Host "OK"

Set-Location -Path "C:\"

$elapsedTime = $(get-date) - $startTime
$totalTime = "{0:HH:mm:ss}" -f ([datetime]$elapsedTime.Ticks)
echo "Complete ($totalTime)"
