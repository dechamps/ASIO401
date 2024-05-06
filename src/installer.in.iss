; Note: this Inno Setup installer script is meant to run as part of
; installer.cmake. It will not work on its own.

[Setup]
AppID=ASIO401
AppName=ASIO401
AppVerName=ASIO401 @ASIO401_VERSION@
AppVersion=@ASIO401_VERSION@
AppPublisher=Etienne Dechamps
AppPublisherURL=https://github.com/dechamps/ASIO401
AppSupportURL=https://github.com/dechamps/ASIO401/issues
AppUpdatesURL=https://github.com/dechamps/ASIO401/releases
AppReadmeFile=https://github.com/dechamps/ASIO401/blob/@DECHAMPS_CMAKEUTILS_GIT_DESCRIPTION@/README.md
AppContact=etienne@edechamps.fr

DefaultDirName={commonpf}\ASIO401
AppendDefaultDirName=no
ArchitecturesInstallIn64BitMode=x64

[Files]
Source:"install\x64-Release\bin\ASIO401.dll"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit; Check: Is64BitInstallMode
Source:"install\x64-Release\bin\*"; DestDir: "{app}\x64"; Flags: ignoreversion 64bit; Check: Is64BitInstallMode
Source:"install\x86-Release\bin\ASIO401.dll"; DestDir: "{app}\x86"; Flags: ignoreversion regserver
Source:"install\x86-Release\bin\*"; DestDir: "{app}\x86"; Flags: ignoreversion
Source:"..\..\*.txt"; DestDir:"{app}"; Flags: ignoreversion
Source:"..\..\*.md"; DestDir:"{app}"; Flags: ignoreversion
Source:"..\..\*.jpg"; DestDir:"{app}"; Flags: ignoreversion

[Run]
Filename:"https://github.com/dechamps/ASIO401/blob/@DECHAMPS_CMAKEUTILS_GIT_DESCRIPTION@/README.md"; Description:"Open README"; Flags: postinstall shellexec nowait skipifsilent
