; INFINIVERB — Windows installer (Inno Setup 6).
;
; Installs the VST3 bundle into the system's VST3 folder. Built by CI
; (.github/workflows/build.yml), which passes:
;   /DAppVersion=1.0.0             from CMakeLists.txt
;   /DVst3Dir=<path to INFINIVERB.vst3>
;   /DReadme=<installer README with the notices appended, UTF-8 with BOM>
;   /DLicense=<LICENSE>
; and optionally /DLabel=-rc1 for the output file name.

#ifndef AppVersion
  #error AppVersion must be defined (/DAppVersion=...)
#endif
#ifndef Label
  #define Label ""
#endif

[Setup]
; Never change the AppId: Windows recognises upgrades and the uninstaller by it.
AppId={{23C0A2ED-FE8D-4E44-86AC-D65CA2C758C7}
AppName=INFINIVERB
AppVersion={#AppVersion}
AppVerName=INFINIVERB {#AppVersion}
AppPublisher=fatbird Studios
AppPublisherURL=https://fatbird-studios.com
AppSupportURL=https://fatbird-studios.com
AppCopyright=Copyright (C) 2026 fatbird Studios
VersionInfoVersion={#AppVersion}
VersionInfoProductName=INFINIVERB

; The VST3 folder is fixed by the VST3 specification; hosts scan it.
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DisableReadyPage=no
UsePreviousAppDir=no
UninstallDisplayName=INFINIVERB
UninstallFilesDir={commonpf64}\fatbird Studios\INFINIVERB\Uninstall

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin

LicenseFile={#License}
InfoBeforeFile={#Readme}

OutputBaseFilename=INFINIVERB-{#AppVersion}{#Label}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#Vst3Dir}\*"; DestDir: "{commoncf64}\VST3\INFINIVERB.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
; Only what the installer put there. The user's presets (in their AppData) stay.
Type: dirifempty; Name: "{commoncf64}\VST3\INFINIVERB.vst3"
