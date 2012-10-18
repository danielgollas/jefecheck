::Delete all tempFolders
del /S /Q /F forAppData
del /S /Q /F forProgramFiles

::Create the app folder that will go in Documents and Settings: FXs, LUTs and Guides
MKDIR forAppData\JefeCorp\JefeCheck\JefeCheck\FX\
copy .\..\..\common\FX\* .\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\
copy .\..\..\common\Manual\JefeCheckManual.pdf .\forAppData\JefeCorp\JefeCheck\JefeCheck\
copy .\..\..\common\Manual\JefeCheckQuickStart.pdf .\forAppData\JefeCorp\JefeCheck\JefeCheck\

::Create the Received folder for remote sessions.
MKDIR forAppData\JefeCorp\JefeCheck\JefeCheck\Received

::Copy a default preferences file.
copy .\..\..\common\windowsConfig.ini .\forAppData\JefeCorp\JefeCheck\JefeCheck\prefs.ini

::Copy the info that will go in the Program files folder, executable, dlls and EULA.

MKDIR forProgramFiles\
copy .\..\x64\Release\JefeCheck.exe .\forProgramFiles\JefeCheck.exe
copy .\..\x64\Release\*.dll .\forProgramFiles\
copy .\..\..\common\EULA.rtf .\forProgramFiles\

::Copy the VC++ runtime files and manifest
MKDIR .\forProgramFiles\Microsoft.VC80.CRT
xcopy /E .\..\..\Microsoft.VC80.CRT .\forProgramFiles\Microsoft.VC80.CRT\

copy .\..\..\msvcr71.dll .\forProgramFiles\

::Also copy the configuration file to redirect CRT dependencies to the newest CRT
copy .\..\..\JefeCheck.exe.config .\forProgramFiles\

::Remove all the .svn folders
FOR /F "tokens=*" %%G IN ('DIR /B /AD /S forAppData\JefeCorp\*.svn*') DO RMDIR /S /Q "%%G"
FOR /F "tokens=*" %%G IN ('DIR /B /AD /S forProgramFiles\*.svn*') DO RMDIR /S /Q "%%G"



