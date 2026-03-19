; YipOS Installer Script
; For NSIS (Nullsoft Scriptable Install System)

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "WinVer.nsh"

; Define installer name and output file
Name "YipOS"
OutFile "..\YipOS v1.0 Setup.exe"

; Default installation directory
InstallDir "$PROGRAMFILES\YipOS"

; Registry key for uninstaller
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\YipOS"

; Visual C++ Redistributable URL and registry detection
!define VCREDIST_URL "https://aka.ms/vs/17/release/vc_redist.x64.exe"
!define VCREDIST_KEY "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"

; Request application privileges
RequestExecutionLevel admin

; Interface settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Variables
Var Dialog
Var VCRedistInstalled
Var InstallVCRedist

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
Page custom VCRedistPage VCRedistPageLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"


; Function to check if VC++ Redistributable is installed
Function CheckVCRedist
    StrCpy $VCRedistInstalled "0"
    ReadRegDWORD $0 HKLM "${VCREDIST_KEY}" "Installed"
    ${If} $0 == "1"
        StrCpy $VCRedistInstalled "1"
    ${EndIf}
FunctionEnd

; Visual C++ Redistributable Page
Function VCRedistPage
    Call CheckVCRedist
    ${If} $VCRedistInstalled == "1"
        Abort
    ${EndIf}

    StrCpy $InstallVCRedist "1"

    !insertmacro MUI_HEADER_TEXT "Visual C++ Redistributable" "Install Microsoft Visual C++ Redistributable (recommended)"

    nsDialogs::Create 1018
    Pop $Dialog

    ${If} $Dialog == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 40u "YipOS requires the Microsoft Visual C++ Redistributable to run properly. This component is not installed on your system.$\r$\n$\r$\nIt's recommended to install this component."
    Pop $0

    ${NSD_CreateCheckbox} 0 50u 100% 10u "Install Visual C++ Redistributable"
    Pop $InstallVCRedist
    ${NSD_Check} $InstallVCRedist

    nsDialogs::Show
FunctionEnd

Function VCRedistPageLeave
    ${NSD_GetState} $InstallVCRedist $InstallVCRedist
FunctionEnd

; Installation section
Section "Install"
    SetOutPath "$INSTDIR"

    ; Install Visual C++ Redistributable if needed
    Call CheckVCRedist
    ${If} $VCRedistInstalled == "0"
        ${If} $InstallVCRedist == "1"
            DetailPrint "Downloading Visual C++ Redistributable..."
            NSISdl::download "${VCREDIST_URL}" "$TEMP\vc_redist.x64.exe"
            Pop $0
            ${If} $0 == "success"
                DetailPrint "Installing Visual C++ Redistributable..."
                ExecWait '"$TEMP\vc_redist.x64.exe" /quiet /norestart' $0
                ${If} $0 != "0"
                    DetailPrint "Visual C++ Redistributable installation failed with code $0"
                    MessageBox MB_ICONEXCLAMATION|MB_OK "The Visual C++ Redistributable installation failed. YipOS may not work correctly without it.$\n$\nYou can download and install it manually from: ${VCREDIST_URL}"
                ${Else}
                    DetailPrint "Visual C++ Redistributable installed successfully"
                ${EndIf}
            ${Else}
                DetailPrint "Failed to download Visual C++ Redistributable: $0"
                MessageBox MB_ICONEXCLAMATION|MB_OK "Failed to download the Visual C++ Redistributable. YipOS may not work correctly without it.$\n$\nYou can download and install it manually from: ${VCREDIST_URL}"
            ${EndIf}
            Delete "$TEMP\vc_redist.x64.exe"
        ${EndIf}
    ${EndIf}

    ; Install application files
    SetOutPath "$INSTDIR"
    File "..\build_win\yip_os.exe"

    ; Assets
    SetOutPath "$INSTDIR\assets"
    File /nonfatal "..\assets\vq_codebook.npy"

    ; Default images for IMG screen
    SetOutPath "$INSTDIR\assets\images"
    File /nonfatal "..\assets\images\*.*"

    ; Create shortcuts
    CreateDirectory "$SMPROGRAMS\YipOS"
    CreateShortcut "$SMPROGRAMS\YipOS\YipOS.lnk" "$INSTDIR\yip_os.exe"
    CreateShortcut "$SMPROGRAMS\YipOS\Uninstall.lnk" "$INSTDIR\uninstall.exe"
    CreateShortcut "$DESKTOP\YipOS.lnk" "$INSTDIR\yip_os.exe"

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"

    ; Write uninstall information to registry
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "YipOS"
    WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\yip_os.exe,0"
    WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "Foxipso"
    WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "1.0"
    WriteRegStr HKLM "${UNINSTKEY}" "URLInfoAbout" "https://foxipso.com"

    ; Get size of installation directory
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

; Uninstallation section
Section "Uninstall"
    ; Remove application files
    Delete "$INSTDIR\yip_os.exe"
    Delete "$INSTDIR\uninstall.exe"
    Delete "$INSTDIR\assets\vq_codebook.npy"
    RMDir /r "$INSTDIR\assets\images"
    RMDir "$INSTDIR\assets"
    RMDir "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\YipOS\YipOS.lnk"
    Delete "$SMPROGRAMS\YipOS\Uninstall.lnk"
    RMDir "$SMPROGRAMS\YipOS"
    Delete "$DESKTOP\YipOS.lnk"

    ; Ask user if they want to remove settings
    MessageBox MB_YESNO "Do you want to remove your YipOS settings as well?$\n$\nThis will delete your config, logs, and whisper models from AppData." IDNO KeepSettings
    RMDir /r "$APPDATA\yip_os"

    KeepSettings:
    ; Remove registry entries
    DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd
