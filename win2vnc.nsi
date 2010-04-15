
; The name of the installer
Name "Win2VNC"

; The file to write
OutFile "Win2VNC.exe"


LicenseText "Please read the license:"
LicenseData "LICENCE.txt"

; The default installation directory
InstallDir $PROGRAMFILES\Win2VNC
InstallDirRegKey HKLM Software\Win2VNC ""

; The text to prompt the user to enter a directory
DirText "This will install Win2VNC on your computer. Choose a directory"

Section "Win2VNC config file shell extention"
  SectionIn 123

  ; back up old value of .nsi
  ReadRegStr $1 HKCR ".w2v" ""
  StrCmp $1 "" Label1
    StrCmp $1 "NSISFile" Label1
    WriteRegStr HKCR ".w2v" "backup_val" $1
Label1:

  WriteRegStr HKCR ".w2v" "" "W2VFile"
  WriteRegStr HKCR "W2VFile" "" "Win2VNC Connection File"
  WriteRegStr HKCR "W2VFile\shell" "" "open"
  WriteRegStr HKCR "W2VFile\DefaultIcon" "" $INSTDIR\win2vnc.exe,0
  WriteRegStr HKCR "W2VFile\shell\open\command" "" '"$INSTDIR\win2vnc.exe" -open "%1"'
SectionEnd

Section "Start Menu Group"
  SectionIn 123
  SetOutPath $SMPROGRAMS\Win2VNC
  Delete "$SMPROGRAMS\Win2VNC\Win2VNC Home Page.lnk"
  WriteINIStr "$SMPROGRAMS\Win2VNC\Win2VNC Home Page.url" \
              "InternetShortcut" "URL" "http://fredrik.hubbe.net/win2vnc.html"
  CreateShortCut "$SMPROGRAMS\Win2VNC\Uninstall Win2VNC.lnk" \
                 "$INSTDIR\uninst.exe"
  CreateShortCut "$SMPROGRAMS\Win2VNC\Win2VNC.lnk" "$INSTDIR\Win2VNC.exe"
  SetOutPath $INSTDIR
SectionEnd


Function SetupRegKeys
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Win2VNC" "DisplayName" "Win2VNC (remove only)"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Win2VNC" "UninstallString" '"$INSTDIR\uninst.exe"'
  WriteRegStr HKLM Software\Win2VNC "" $INSTDIR
FunctionEnd


; The stuff to install
Section "ThisNameIsIgnoredSoWhyBother?"
  ; Set output path to the installation directory.
  SetOutPath $INSTDIR
  ; Put file there
  File "Profile\win2vnc.exe"
  Call SetupRegKeys
SectionEnd ; end the section

UninstallEXEName "uninst.exe"
UninstallText "This will uninstall the Win2VNC from your system."

Section Uninstall
  ReadRegStr $1 HKCR ".win2vnc" ""
  StrCmp $1 "W2VFile" 0 NoOwn ; only do this if we own it
    ReadRegStr $1 HKCR ".w2v" "backup_val"
    StrCmp $1 "" 0 RestoreBackup ; if backup == "" then delete the whole key
      DeleteRegKey HKCR ".w2v"
    Goto NoOwn
    RestoreBackup:
      WriteRegStr HKCR ".w2v" "" $1
      DeleteRegValue HKCR ".w2v" "backup_val"
  NoOwn:

  DeleteRegKey HKCR "W2VFile"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Win2VNC"

  ; Start menu
  Delete $SMPROGRAMS\Win2VNC\*.lnk
  Delete $SMPROGRAMS\Win2VNC\*.url
  RMDir $SMPROGRAMS\Win2VNC

  DeleteRegKey HKLM Software\Win2VNC
  Delete $INSTDIR\uninst.exe
  Delete $INSTDIR\win2vnc.exe
  RMDir $INSTDIR

SectionEnd


; eof
