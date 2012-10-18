call prepareInstall.bat
cd NSIS
makensis.exe ../JefeCheck.nsi
cd ..
copy .\JefeCheck_Windows_x64_setup.exe .\..\..\installers\JefeCheck_win32_x86_64_setup.exe
