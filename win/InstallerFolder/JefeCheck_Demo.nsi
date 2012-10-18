############################################################################################
#      NSIS Installation Script created by NSIS Quick Setup Script Generator v1.09.18
#               Entirely Edited with NullSoft Scriptable Installation System                
#              by Vlasis K. Barkas aka Red Wine red_wine@freemail.gr Sep 2006               
############################################################################################

!define APP_NAME "JefeCheck_Demo"
!define COMP_NAME "JefeCorp"
!define WEB_SITE "http://www.jefecorp.com"
!define VERSION "0.1.4.03"
!define COPYRIGHT "JefeCorp  © 2006-2009"
!define DESCRIPTION "JefeCheck Review System Demo"
!define LICENSE_TXT "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\EULA.rtf"
!define INSTALLER_NAME "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\JefeCheckDemo_Win32_setup.exe"
!define MAIN_APP_EXE "JefeCheck_Demo.exe"
!define INSTALL_TYPE "SetShellVarContext current"
!define REG_ROOT "HKCU"
!define REG_APP_PATH "Software\Microsoft\Windows\CurrentVersion\App Paths\${MAIN_APP_EXE}"
!define UNINSTALL_PATH "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

!define REG_START_MENU "Start Menu Folder"

var SM_Folder

######################################################################

VIProductVersion  "${VERSION}"
VIAddVersionKey "ProductName"  "${APP_NAME}"
VIAddVersionKey "CompanyName"  "${COMP_NAME}"
VIAddVersionKey "LegalCopyright"  "${COPYRIGHT}"
VIAddVersionKey "FileDescription"  "${DESCRIPTION}"
VIAddVersionKey "FileVersion"  "${VERSION}"

######################################################################

SetCompressor ZLIB
Name "${APP_NAME}"
Caption "${APP_NAME}"
OutFile "${INSTALLER_NAME}"
BrandingText "${APP_NAME}"
XPStyle on
InstallDirRegKey "${REG_ROOT}" "${REG_APP_PATH}" ""
InstallDir "$PROGRAMFILES\JefeCheck_Demo"

######################################################################

!include "MUI.nsh"

!define MUI_ABORTWARNING
!define MUI_UNABORTWARNING

!insertmacro MUI_PAGE_WELCOME

!ifdef LICENSE_TXT
!insertmacro MUI_PAGE_LICENSE "${LICENSE_TXT}"
!endif

!insertmacro MUI_PAGE_DIRECTORY

!ifdef REG_START_MENU
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "JefeCheck_Demo"
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "${REG_ROOT}"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "${UNINSTALL_PATH}"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "${REG_START_MENU}"
!insertmacro MUI_PAGE_STARTMENU Application $SM_Folder
!endif

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN "$INSTDIR\${MAIN_APP_EXE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM

!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

######################################################################

Section -MainProgram
${INSTALL_TYPE}
SetOverwrite ifnewer
SetOutPath "$INSTDIR"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\EULA.rtf"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\glew32.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\glut32.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\Half.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\Iex.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\IlmImf.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\IlmThread.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\Imath.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\JefeCheck.exe.config"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\JefeCheck_Demo.exe"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\libeay32.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\libgfl265.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\libgfl290.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\libgfle265.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\libgfle290.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\msvcr71.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\ssleay32.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\zlib1.dll"
SetOutPath "$INSTDIR\Microsoft.VC80.CRT"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\Microsoft.VC80.CRT\Microsoft.VC80.CRT.manifest"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\Microsoft.VC80.CRT\msvcm80.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\Microsoft.VC80.CRT\msvcp80.dll"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forProgramFiles\Microsoft.VC80.CRT\msvcr80.dll"
SectionEnd

######################################################################

Section -Additional
SetOutPath "$APPDATA"
SetOutPath "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\JefeCheckManual.pdf"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\JefeCheckQuickStart.pdf"
SetOutPath "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\1DLUT.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\1DLUT.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\3DLUT.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\3DLUT.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ADD.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ADD.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\AOVER.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\AOVER.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ASPECT_BARS.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ASPECT_BARS.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BCS.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BCS.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUECHROMA.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUECHROMA.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVERAGE.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVERAGE.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVG_3x3.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVG_3x3.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_GAUSSIAN_3x3.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_GAUSSIAN_3x3.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CHANNEL.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CHANNEL.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CONVOLUTION_3x3.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CONVOLUTION_3x3.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CSTEREO.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CSTEREO.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\DIFFMATTE.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\DIFFMATTE.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\exrControls.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\EXRCONTROLS.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FADE.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FADE.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FIELDS.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FIELDS.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\fixed.vert"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\fotokemNeg.cub"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FUN.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FUN.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\gamma.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\GAMMA.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\GREENCHROMA.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\GREENCHROMA.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\INSIDE.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\INSIDE.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\invert.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\InvertExampleCube.cub"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LinToLog.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LinToLog_SoftClip.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\Lin_to_sRGB.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\log2lin.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\log2lin.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\log2linsRGB.tga"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LogToLin.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LogToLinToSRGB.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LogToLin_SoftClip.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MIX.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MIX.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\move2D.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\move2D.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MULTIPLY.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MULTIPLY.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ondita.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ONDITA.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\OVER.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\OVER.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\PCC.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\PCC.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\printFotokem.cube"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\RadialTransition.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\RadialTransition.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SPLIT.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SPLIT.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\sRGB_to_Lin.lut"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SUBSTRACT.frag"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SUBSTRACT.jfx"
File "C:\projects\JefeCheckSVN\trunk\WindowsResources\Installers\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck_Demo\FX\UnitCube.tga"
SectionEnd

######################################################################

Section -Icons_Reg
SetOutPath "$INSTDIR"
WriteUninstaller "$INSTDIR\uninstall.exe"

!ifdef REG_START_MENU
!insertmacro MUI_STARTMENU_WRITE_BEGIN Application
CreateDirectory "$SMPROGRAMS\$SM_Folder"
CreateShortCut "$SMPROGRAMS\$SM_Folder\${APP_NAME}.lnk" "$INSTDIR\${MAIN_APP_EXE}"
CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${MAIN_APP_EXE}"
CreateShortCut "$SMPROGRAMS\$SM_Folder\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"

!ifdef WEB_SITE
WriteIniStr "$INSTDIR\${APP_NAME} website.url" "InternetShortcut" "URL" "${WEB_SITE}"
CreateShortCut "$SMPROGRAMS\$SM_Folder\${APP_NAME} Website.lnk" "$INSTDIR\${APP_NAME} website.url"
!endif
!insertmacro MUI_STARTMENU_WRITE_END
!endif

!ifndef REG_START_MENU
CreateDirectory "$SMPROGRAMS\JefeCheck_Demo"
CreateShortCut "$SMPROGRAMS\JefeCheck_Demo\${APP_NAME}.lnk" "$INSTDIR\${MAIN_APP_EXE}"
CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${MAIN_APP_EXE}"
CreateShortCut "$SMPROGRAMS\JefeCheck_Demo\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"

!ifdef WEB_SITE
WriteIniStr "$INSTDIR\${APP_NAME} website.url" "InternetShortcut" "URL" "${WEB_SITE}"
CreateShortCut "$SMPROGRAMS\JefeCheck_Demo\${APP_NAME} Website.lnk" "$INSTDIR\${APP_NAME} website.url"
!endif
!endif

WriteRegStr ${REG_ROOT} "${REG_APP_PATH}" "" "$INSTDIR\${MAIN_APP_EXE}"
WriteRegStr ${REG_ROOT} "${UNINSTALL_PATH}"  "DisplayName" "${APP_NAME}"
WriteRegStr ${REG_ROOT} "${UNINSTALL_PATH}"  "UninstallString" "$INSTDIR\uninstall.exe"
WriteRegStr ${REG_ROOT} "${UNINSTALL_PATH}"  "DisplayIcon" "$INSTDIR\${MAIN_APP_EXE}"
WriteRegStr ${REG_ROOT} "${UNINSTALL_PATH}"  "DisplayVersion" "${VERSION}"
WriteRegStr ${REG_ROOT} "${UNINSTALL_PATH}"  "Publisher" "${COMP_NAME}"

!ifdef WEB_SITE
WriteRegStr ${REG_ROOT} "${UNINSTALL_PATH}"  "URLInfoAbout" "${WEB_SITE}"
!endif
SectionEnd

######################################################################

Section Uninstall
${INSTALL_TYPE}
Delete "$INSTDIR\EULA.rtf"
Delete "$INSTDIR\glew32.dll"
Delete "$INSTDIR\glut32.dll"
Delete "$INSTDIR\Half.dll"
Delete "$INSTDIR\Iex.dll"
Delete "$INSTDIR\IlmImf.dll"
Delete "$INSTDIR\IlmThread.dll"
Delete "$INSTDIR\Imath.dll"
Delete "$INSTDIR\JefeCheck.exe.config"
Delete "$INSTDIR\JefeCheck_Demo.exe"
Delete "$INSTDIR\libeay32.dll"
Delete "$INSTDIR\libgfl265.dll"
Delete "$INSTDIR\libgfl290.dll"
Delete "$INSTDIR\libgfle265.dll"
Delete "$INSTDIR\libgfle290.dll"
Delete "$INSTDIR\msvcr71.dll"
Delete "$INSTDIR\ssleay32.dll"
Delete "$INSTDIR\zlib1.dll"
Delete "$INSTDIR\Microsoft.VC80.CRT\Microsoft.VC80.CRT.manifest"
Delete "$INSTDIR\Microsoft.VC80.CRT\msvcm80.dll"
Delete "$INSTDIR\Microsoft.VC80.CRT\msvcp80.dll"
Delete "$INSTDIR\Microsoft.VC80.CRT\msvcr80.dll"
 
RmDir "$INSTDIR\Microsoft.VC80.CRT"
 
Delete "$INSTDIR\uninstall.exe"
!ifdef WEB_SITE
Delete "$INSTDIR\${APP_NAME} website.url"
!endif

RmDir "$INSTDIR"

!ifndef NEVER_UNINSTALL
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\JefeCheckManual.pdf"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\JefeCheckQuickStart.pdf"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\1DLUT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\1DLUT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\3DLUT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\3DLUT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ADD.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ADD.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\AOVER.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\AOVER.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ASPECT_BARS.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ASPECT_BARS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BCS.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BCS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUECHROMA.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUECHROMA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVERAGE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVERAGE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVG_3x3.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_AVG_3x3.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_GAUSSIAN_3x3.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\BLUR_GAUSSIAN_3x3.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CHANNEL.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CHANNEL.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CONVOLUTION_3x3.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CONVOLUTION_3x3.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CSTEREO.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\CSTEREO.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\DIFFMATTE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\DIFFMATTE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\exrControls.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\EXRCONTROLS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FADE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FADE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FIELDS.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FIELDS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\fixed.vert"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\fotokemNeg.cub"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FUN.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\FUN.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\gamma.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\GAMMA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\GREENCHROMA.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\GREENCHROMA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\INSIDE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\INSIDE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\invert.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\InvertExampleCube.cub"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LinToLog.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LinToLog_SoftClip.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\Lin_to_sRGB.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\log2lin.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\log2lin.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\log2linsRGB.tga"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LogToLin.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LogToLinToSRGB.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\LogToLin_SoftClip.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MIX.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MIX.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\move2D.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\move2D.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MULTIPLY.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\MULTIPLY.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ondita.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\ONDITA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\OVER.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\OVER.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\PCC.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\PCC.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\printFotokem.cube"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\RadialTransition.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\RadialTransition.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SPLIT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SPLIT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\sRGB_to_Lin.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SUBSTRACT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\SUBSTRACT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX\UnitCube.tga"
 
RmDir "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo\FX"
RmDir "$APPDATA\JefeCorp\JefeCheck\JefeCheck_Demo"
!endif

!ifdef REG_START_MENU
!insertmacro MUI_STARTMENU_GETFOLDER "Application" $SM_Folder
Delete "$SMPROGRAMS\$SM_Folder\${APP_NAME}.lnk"
Delete "$SMPROGRAMS\$SM_Folder\Uninstall ${APP_NAME}.lnk"
!ifdef WEB_SITE
Delete "$SMPROGRAMS\$SM_Folder\${APP_NAME} Website.lnk"
!endif
Delete "$DESKTOP\${APP_NAME}.lnk"

RmDir "$SMPROGRAMS\$SM_Folder"
!endif

!ifndef REG_START_MENU
Delete "$SMPROGRAMS\JefeCheck_Demo\${APP_NAME}.lnk"
Delete "$SMPROGRAMS\JefeCheck_Demo\Uninstall ${APP_NAME}.lnk"
!ifdef WEB_SITE
Delete "$SMPROGRAMS\JefeCheck_Demo\${APP_NAME} Website.lnk"
!endif
Delete "$DESKTOP\${APP_NAME}.lnk"

RmDir "$SMPROGRAMS\JefeCheck_Demo"
!endif

DeleteRegKey ${REG_ROOT} "${REG_APP_PATH}"
DeleteRegKey ${REG_ROOT} "${UNINSTALL_PATH}"
SectionEnd

######################################################################

