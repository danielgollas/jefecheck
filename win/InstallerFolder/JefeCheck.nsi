############################################################################################
#      NSIS Installation Script created by NSIS Quick Setup Script Generator v1.09.18
#               Entirely Edited with NullSoft Scriptable Installation System                
#              by Vlasis K. Barkas aka Red Wine red_wine@freemail.gr Sep 2006               
############################################################################################

!define APP_NAME "JefeCheck"
!define COMP_NAME "JefeCorp"
!define WEB_SITE "http://jefecheck.jefecorp.com"
!define VERSION "00.1.5.1"
!define COPYRIGHT "JefeCorp  © 2006-2012"
!define DESCRIPTION "JefeCheck Review System"
!define LICENSE_TXT "C:\projects\jefecheck2\common\EULA.rtf"
!define INSTALLER_NAME "C:\projects\jefecheck2\win\InstallerFolder\JefeCheck_Windows_x64_setup.exe"
!define MAIN_APP_EXE "JefeCheck.exe"
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
InstallDir "$PROGRAMFILES\JefeCheck"

######################################################################

!include "MUI.nsh"

!define MUI_ABORTWARNING
!define MUI_UNABORTWARNING

!insertmacro MUI_PAGE_WELCOME

!ifdef LICENSE_TXT
!insertmacro MUI_PAGE_LICENSE "${LICENSE_TXT}"
!endif

!ifdef REG_START_MENU
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "JefeCheck"
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
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\boost_date_time-vc100-mt-1_46_1.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\boost_filesystem-vc100-mt-1_46_1.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\boost_program_options-vc100-mt-1_46_1.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\boost_system-vc100-mt-1_46_1.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\boost_thread-vc100-mt-1_46_1.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\EULA.rtf"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\glut64.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\Half.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\Iex.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\IlmImf.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\IlmThread.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\Imath.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\JefeCheck.exe"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\libeay32.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\libgfl340.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\libgfle340.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\msvcp100.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\msvcp100d.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\msvcr100.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\msvcr100d.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\ssleay32.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\zlib1.dll"
File "C:\projects\jefecheck2\win\InstallerFolder\forProgramFiles\zlibwapi.dll"
SectionEnd

######################################################################

Section -Additional
SetOutPath "$APPDATA"
SetOutPath "$APPDATA\JefeCorp\JefeCheck\JefeCheck"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\JefeCheckManual.pdf"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\JefeCheckQuickStart.pdf"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\prefs.ini"
SetOutPath "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\1DLUT.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\1DLUT.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\3DLUT.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\3DLUT.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\ADD.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\ADD.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\AOVER.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\AOVER.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\ASPECT_BARS.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\ASPECT_BARS.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BCS.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BCS.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUECHROMA.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUECHROMA.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVERAGE.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVERAGE.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVG_3x3.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVG_3x3.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_GAUSSIAN_3x3.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_GAUSSIAN_3x3.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\CHANNEL.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\CHANNEL.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\CONVOLUTION_3x3.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\CONVOLUTION_3x3.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\CSTEREO.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\CSTEREO.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\DIFFMATTE.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\DIFFMATTE.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\exrControls.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\EXRCONTROLS.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\FADE.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\FADE.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\FIELDS.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\FIELDS.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\fixed.vert"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\fotokemNeg.cub"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\FUN.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\FUN.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\gamma.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\GAMMA.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\GREENCHROMA.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\GREENCHROMA.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\INSIDE.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\INSIDE.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\invert.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\InvertExampleCube.cub"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\LinToLog.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\LinToLog_SoftClip.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\Lin_to_sRGB.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\log2lin.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\log2lin.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\log2linsRGB.tga"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\LogToLin.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\LogToLinToSRGB.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\LogToLin_SoftClip.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\MIX.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\MIX.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\move2D.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\move2D.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\MULTIPLY.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\MULTIPLY.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\ondita.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\ONDITA.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\OVER.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\OVER.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\PCC.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\PCC.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\printFotokem.cube"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\RadialTransition.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\RadialTransition.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\SPLIT.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\SPLIT.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\sRGB_to_Lin.lut"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\SUBSTRACT.frag"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\SUBSTRACT.jfx"
File "C:\projects\jefecheck2\win\InstallerFolder\forAppData\JefeCorp\JefeCheck\JefeCheck\FX\UnitCube.tga"
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
CreateDirectory "$SMPROGRAMS\JefeCheck"
CreateShortCut "$SMPROGRAMS\JefeCheck\${APP_NAME}.lnk" "$INSTDIR\${MAIN_APP_EXE}"
CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${MAIN_APP_EXE}"
CreateShortCut "$SMPROGRAMS\JefeCheck\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"

!ifdef WEB_SITE
WriteIniStr "$INSTDIR\${APP_NAME} website.url" "InternetShortcut" "URL" "${WEB_SITE}"
CreateShortCut "$SMPROGRAMS\JefeCheck\${APP_NAME} Website.lnk" "$INSTDIR\${APP_NAME} website.url"
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
Delete "$INSTDIR\boost_date_time-vc100-mt-1_46_1.dll"
Delete "$INSTDIR\boost_filesystem-vc100-mt-1_46_1.dll"
Delete "$INSTDIR\boost_program_options-vc100-mt-1_46_1.dll"
Delete "$INSTDIR\boost_system-vc100-mt-1_46_1.dll"
Delete "$INSTDIR\boost_thread-vc100-mt-1_46_1.dll"
Delete "$INSTDIR\EULA.rtf"
Delete "$INSTDIR\glut64.dll"
Delete "$INSTDIR\Half.dll"
Delete "$INSTDIR\Iex.dll"
Delete "$INSTDIR\IlmImf.dll"
Delete "$INSTDIR\IlmThread.dll"
Delete "$INSTDIR\Imath.dll"
Delete "$INSTDIR\JefeCheck.exe"
Delete "$INSTDIR\libeay32.dll"
Delete "$INSTDIR\libgfl340.dll"
Delete "$INSTDIR\libgfle340.dll"
Delete "$INSTDIR\msvcp100.dll"
Delete "$INSTDIR\msvcp100d.dll"
Delete "$INSTDIR\msvcr100.dll"
Delete "$INSTDIR\msvcr100d.dll"
Delete "$INSTDIR\ssleay32.dll"
Delete "$INSTDIR\zlib1.dll"
Delete "$INSTDIR\zlibwapi.dll"
 
 
Delete "$INSTDIR\uninstall.exe"
!ifdef WEB_SITE
Delete "$INSTDIR\${APP_NAME} website.url"
!endif

RmDir "$INSTDIR"

!ifndef NEVER_UNINSTALL
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\JefeCheckManual.pdf"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\JefeCheckQuickStart.pdf"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\prefs.ini"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\1DLUT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\1DLUT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\3DLUT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\3DLUT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\ADD.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\ADD.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\AOVER.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\AOVER.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\ASPECT_BARS.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\ASPECT_BARS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BCS.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BCS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUECHROMA.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUECHROMA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVERAGE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVERAGE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVG_3x3.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_AVG_3x3.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_GAUSSIAN_3x3.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\BLUR_GAUSSIAN_3x3.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\CHANNEL.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\CHANNEL.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\CONVOLUTION_3x3.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\CONVOLUTION_3x3.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\CSTEREO.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\CSTEREO.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\DIFFMATTE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\DIFFMATTE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\exrControls.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\EXRCONTROLS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\FADE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\FADE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\FIELDS.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\FIELDS.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\fixed.vert"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\fotokemNeg.cub"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\FUN.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\FUN.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\gamma.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\GAMMA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\GREENCHROMA.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\GREENCHROMA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\INSIDE.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\INSIDE.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\invert.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\InvertExampleCube.cub"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\LinToLog.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\LinToLog_SoftClip.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\Lin_to_sRGB.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\log2lin.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\log2lin.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\log2linsRGB.tga"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\LogToLin.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\LogToLinToSRGB.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\LogToLin_SoftClip.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\MIX.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\MIX.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\move2D.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\move2D.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\MULTIPLY.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\MULTIPLY.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\ondita.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\ONDITA.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\OVER.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\OVER.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\PCC.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\PCC.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\printFotokem.cube"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\RadialTransition.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\RadialTransition.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\SPLIT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\SPLIT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\sRGB_to_Lin.lut"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\SUBSTRACT.frag"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\SUBSTRACT.jfx"
Delete "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX\UnitCube.tga"
 
RmDir "$APPDATA\JefeCorp\JefeCheck\JefeCheck\FX"
RmDir "$APPDATA\JefeCorp\JefeCheck\JefeCheck"
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
Delete "$SMPROGRAMS\JefeCheck\${APP_NAME}.lnk"
Delete "$SMPROGRAMS\JefeCheck\Uninstall ${APP_NAME}.lnk"
!ifdef WEB_SITE
Delete "$SMPROGRAMS\JefeCheck\${APP_NAME} Website.lnk"
!endif
Delete "$DESKTOP\${APP_NAME}.lnk"

RmDir "$SMPROGRAMS\JefeCheck"
!endif

DeleteRegKey ${REG_ROOT} "${REG_APP_PATH}"
DeleteRegKey ${REG_ROOT} "${UNINSTALL_PATH}"
SectionEnd

######################################################################

