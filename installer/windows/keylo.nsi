Unicode True

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef STAGING
  !define STAGING "..\..\nsis_staging"
!endif

!define PRODUCT_NAME    "Keylo"
!define PRODUCT_VENDOR  "Snowinch"
!define VST3_DIR        "$COMMONFILES\VST3"
!define DOCS_DIR        "$DOCUMENTS\Keylo"
!define UNINSTALL_KEY   "Software\Microsoft\Windows\CurrentVersion\Uninstall\Keylo"

Name "${PRODUCT_NAME} ${VERSION}"
OutFile "Keylo-${VERSION}-Windows-x64-Setup.exe"
InstallDir "${VST3_DIR}"
RequestExecutionLevel admin
SetCompressor lzma

Page welcome
Page directory
Page instfiles
Page finish

Section "VST3 Plugin" SecVST3
    SectionIn RO

    SetOutPath "$INSTDIR\Keylo.vst3\Contents\Resources"
    File "${STAGING}\Keylo.vst3\Contents\Resources\moduleinfo.json"

    SetOutPath "$INSTDIR\Keylo.vst3\Contents\x86_64-win"
    File "${STAGING}\Keylo.vst3\Contents\x86_64-win\Keylo.vst3"
    File "${STAGING}\Keylo.vst3\Contents\x86_64-win\onnxruntime.dll"
SectionEnd

Section "Documentation" SecDocs
    SetOutPath "${DOCS_DIR}"
    File "${STAGING}\Keylo_Technical_Documentation_v3.pdf"
    File "${STAGING}\README.txt"
SectionEnd

Section -WriteUninstaller
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName"    "${PRODUCT_NAME} ${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher"      "${PRODUCT_VENDOR}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$INSTDIR\Uninstall-Keylo.exe"
    WriteUninstaller "$INSTDIR\Uninstall-Keylo.exe"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\Keylo.vst3\Contents\x86_64-win\Keylo.vst3"
    Delete "$INSTDIR\Keylo.vst3\Contents\x86_64-win\onnxruntime.dll"
    Delete "$INSTDIR\Keylo.vst3\Contents\Resources\moduleinfo.json"
    RMDir  "$INSTDIR\Keylo.vst3\Contents\x86_64-win"
    RMDir  "$INSTDIR\Keylo.vst3\Contents\Resources"
    RMDir  "$INSTDIR\Keylo.vst3\Contents"
    RMDir  "$INSTDIR\Keylo.vst3"
    Delete "${DOCS_DIR}\Keylo_Technical_Documentation_v3.pdf"
    Delete "${DOCS_DIR}\README.txt"
    RMDir  "${DOCS_DIR}"
    Delete "$INSTDIR\Uninstall-Keylo.exe"
    DeleteRegKey HKLM "${UNINSTALL_KEY}"
SectionEnd
