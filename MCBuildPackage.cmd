@echo off
rem usage E:\overlay\build.cmd

set startTime=%time%
cd C:\

rem cleanup
rmdir /Q /S C:\Users\User\Desktop\WIP\desktop
rmdir /Q /S C:\Users\User\Desktop\WIP\qtkeychain

rem copy files
robocopy /S /NP /NFL /NDL E:\qtkeychain\ C:\Users\User\Desktop\WIP\qtkeychain\
robocopy /S /NP /NFL /NDL E:\desktop\ C:\Users\User\Desktop\WIP\desktop
robocopy /S /NP /NFL /NDL E:\overlay\ C:\Users\User\Desktop\WIP\desktop\

rem Build and install qtkeychain
cd C:\Users\User\Desktop\WIP\qtkeychain
C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake -G "Visual Studio 15 2017 Win64" . -DCMAKE_INSTALL_PREFIX="C:\Users\User\Desktop\WIP\qtkeychain" -DCMAKE_BUILD_TYPE=Release -DQT_QMAKE_EXECUTABLE="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/bin/qmake.exe" -DQt5_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5" -DQt5Core_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5Core" -DQt5LinguistTools_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5LinguistTools"
C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake --build . --config Release --target install

rem Build and install the client
cd C:\Users\User\Desktop\WIP\desktop
mkdir build
cd build

C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake -G "Visual Studio 15 2017 Win64" .. -DCMAKE_INSTALL_PREFIX="C:\Users\User\Desktop\WIP\desktop\build" -DCMAKE_BUILD_TYPE=Release -DNO_SHIBBOLETH="1" -DQT_QMAKE_EXECUTABLE="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/bin/qmake.exe" -DQt5_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5" -DQt5Core_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5Core" -DQt5LinguistTools_DIR="C:/Qt/Qt5.14.2/5.14.2/msvc2017_64/lib/cmake/Qt5LinguistTools" -DZLIB_LIBRARY="C:\Users\User\Desktop\WIP\zlib\msvc2015_64\lib\zlib\zlib.lib" -DZLIB_INCLUDE_DIR="C:\Users\User\Desktop\WIP\zlib\msvc2015_64\include\zlib" -DQTKEYCHAIN_LIBRARY="C:\Users\User\Desktop\WIP\qtkeychain\lib\qt5keychain.lib" -DQTKEYCHAIN_INCLUDE_DIR="C:\Users\User\Desktop\WIP\qtkeychain\include\qt5keychain" -DPng2Ico_EXECUTABLE="C:\Users\User\Desktop\WIP\png2ico\png2ico.exe"

C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake --build . --config Release --target install

copy /Y C:\Users\User\Desktop\WIP\qtkeychain\bin\qt5keychain.dll C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin
cd C:\Qt\Qt5.14.2\5.14.2\msvc2017_64\bin 
windeployqt C:\Users\User\Desktop\WIP\desktop\build\bin

cd C:\Users\User\Desktop\WIP\desktop\build
copy /Y C:\Users\User\Desktop\WIP\vcredist_x64.exe                   C:\Users\User\Desktop\WIP\desktop\build
copy /Y C:\Users\User\Desktop\WIP\vcredist_x86.exe                   C:\Users\User\Desktop\WIP\desktop\build
copy /Y C:\Users\User\Desktop\WIP\zlib\msvc2015_64\lib\zlib\zlib.dll C:\Users\User\Desktop\WIP\desktop\build\bin
copy /Y C:\OpenSSL-Win64\bin\libcrypto-1_1-x64.dll                   C:\Users\User\Desktop\WIP\desktop\build\bin
copy /Y C:\Users\User\Desktop\WIP\openssl\libeay32.dll               C:\Users\User\Desktop\WIP\desktop\build\bin
copy /Y C:\Users\User\Desktop\WIP\openssl\ssleay32.dll               C:\Users\User\Desktop\WIP\desktop\build\bin

C:\Users\User\Desktop\WIP\cmake-3.17.1-win64-x64\bin\cmake --build . --config Release --target package

rem copy the built installer
copy /Y "C:\Users\User\Desktop\WIP\desktop\build\Medical Cloud-2*.exe" E:\
cd /

echo Start  Time: %startTime%
echo Finish Time: %time%
