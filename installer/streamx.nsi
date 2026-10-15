; StreamX — instalador Windows (NSIS 3)
; Compilar: makensis /DVERSION=1.0.0 installer\streamx.nsi
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!define APP "StreamX"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

Name "${APP}"
OutFile "..\dist\StreamX-Setup-${VERSION}.exe"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "Software\${APP}" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 64
BrandingText "${APP} ${VERSION}"
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "Instalador do ${APP}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "GPL-2.0-or-later"

!define MUI_ICON "..\resources\icons\streamx.ico"
!define MUI_UNICON "..\resources\icons\streamx.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Instalar o ${APP} ${VERSION}"
!define MUI_WELCOMEPAGE_TEXT "Gravação de tela, captura de jogos, clipes instantâneos e transmissão ao vivo.$\r$\n$\r$\nA instalação é só para o seu usuário e não precisa de permissão de administrador."
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\64bit\StreamX.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Abrir o ${APP} agora"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "PortugueseBR"

Function .onInit
  ; não instala por cima de um StreamX aberto
  FindWindow $0 "" "StreamX"
  StrCmp $0 0 +3
    MessageBox MB_OK|MB_ICONEXCLAMATION "Feche o StreamX antes de instalar."
    Abort
FunctionEnd

Section "StreamX" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\rundir\LICENSE.txt"
  File "..\rundir\THIRD_PARTY_NOTICES.md"
  SetOutPath "$INSTDIR\bin"
  File /r /x "*.pdb" "..\rundir\bin\*"
  SetOutPath "$INSTDIR\data"
  File /r "..\rundir\data\*"
  SetOutPath "$INSTDIR\obs-plugins"
  File /r /x "*.pdb" "..\rundir\obs-plugins\*"

  ; atalhos (o diretório de trabalho é a pasta do executável)
  SetOutPath "$INSTDIR\bin\64bit"
  CreateDirectory "$SMPROGRAMS\${APP}"
  CreateShortCut "$SMPROGRAMS\${APP}\${APP}.lnk" "$INSTDIR\bin\64bit\StreamX.exe" "" "$INSTDIR\bin\64bit\StreamX.exe" 0
  CreateShortCut "$SMPROGRAMS\${APP}\Desinstalar ${APP}.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortCut "$DESKTOP\${APP}.lnk" "$INSTDIR\bin\64bit\StreamX.exe" "" "$INSTDIR\bin\64bit\StreamX.exe" 0

  ; pasta padrão das gravações
  CreateDirectory "$VIDEOS\StreamX"

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\${APP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "StreamX"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\bin\64bit\StreamX.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Uninstall"
  ; remove somente o que o instalador colocou (nunca apaga a pasta escolhida às cegas)
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\data"
  RMDir /r "$INSTDIR\obs-plugins"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  Delete "$DESKTOP\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\${APP}.lnk"
  Delete "$SMPROGRAMS\${APP}\Desinstalar ${APP}.lnk"
  RMDir "$SMPROGRAMS\${APP}"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\${APP}"
  ; configurações: só com confirmação (padrão: manter). Gravações e clipes nunca são apagados.
  IfSilent keep
  MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "Apagar também as configurações, perfis e cenas do StreamX?$\r$\n$\r$\nSuas gravações e clipes NÃO serão apagados." IDNO keep
    RMDir /r "$APPDATA\StreamX"
  keep:
SectionEnd
