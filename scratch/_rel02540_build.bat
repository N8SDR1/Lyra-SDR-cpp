@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cd /d "Y:\Claude local\SDRProject\Lyra-P2"
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build --target lyra -j || exit /b 1
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build --target test_oc_control test_p2_g2_profile -j || exit /b 1
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\lyra.iss || exit /b 1
dir dist\Lyra-Setup-0.25.40.exe
exit /b 0
