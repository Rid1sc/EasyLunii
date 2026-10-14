; Installeur Windows d'EasyLunii (NSIS)
Unicode true
!include "MUI2.nsh"

Name "EasyLunii ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\EasyLunii"
InstallDirRegKey HKLM "Software\EasyLunii" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\easylunii.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "French"

Section "EasyLunii"
    SetOutPath "$INSTDIR"
    File /r "${STAGE}\*.*"
    WriteRegStr HKLM "Software\EasyLunii" "InstallDir" "$INSTDIR"
    WriteUninstaller "$INSTDIR\Desinstaller.exe"
    CreateDirectory "$SMPROGRAMS\EasyLunii"
    CreateShortcut "$SMPROGRAMS\EasyLunii\EasyLunii.lnk" "$INSTDIR\easylunii.exe"
    CreateShortcut "$DESKTOP\EasyLunii.lnk" "$INSTDIR\easylunii.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EasyLunii" "DisplayName" "EasyLunii"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EasyLunii" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EasyLunii" "DisplayIcon" "$INSTDIR\easylunii.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EasyLunii" "UninstallString" "$INSTDIR\Desinstaller.exe"
SectionEnd

Section "Uninstall"
    RMDir /r "$INSTDIR"
    Delete "$DESKTOP\EasyLunii.lnk"
    RMDir /r "$SMPROGRAMS\EasyLunii"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\EasyLunii"
    DeleteRegKey HKLM "Software\EasyLunii"
SectionEnd
