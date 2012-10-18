::Delete all tempFolders
del /S /Q /F forAppData
del /S /Q /F forProgramFiles

::Create the app folder that will go in Documents and Settings: FXs, LUTs and Guides
MKDIR forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\
copy .\..\..\..\CommonResources\FX\* .\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\
copy .\..\..\..\CommonResources\Manual\JefeCheckManual.pdf .\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\
copy .\..\..\..\CommonResources\Manual\JefeCheckQuickStart.pdf .\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\

::Create the Received folder for remote sessions.
MKDIR forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\Received

::Copy a default preferences file.
copy .\..\..\..\CommonResources\windowsConfig.ini .\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\prefs.ini

::Copy the info that will go in the Program files folder, executable, dlls and EULA.

MKDIR forProgramFiles\
copy .\..\..\..\src\Release\JefeCheck.exe .\forProgramFiles\JefeCheck_Demo.exe
copy .\..\..\..\src\Release\*.dll .\forProgramFiles\
copy .\..\..\..\CommonResources\EULA.rtf .\forProgramFiles\

::Copy the VC++ runtime files and manifest
MKDIR .\forProgramFiles\Microsoft.VC80.CRT
xcopy /E .\..\..\Microsoft.VC80.CRT .\forProgramFiles\Microsoft.VC80.CRT\

copy .\..\..\msvcr71.dll .\forProgramFiles\

::Also copy the configuration file to redirect CRT dependencies to the newest CRT
copy .\..\..\JefeCheck.exe.config .\forProgramFiles\

::Remove all the .svn folders
FOR /F "tokens=*" %%G IN ('DIR /B /AD /S forAppData\JefeCorp\*.svn*') DO RMDIR /S /Q "%%G"
FOR /F "tokens=*" %%G IN ('DIR /B /AD /S forProgramFiles\*.svn*') DO RMDIR /S /Q "%%G"



