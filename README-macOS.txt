Keylo 1.0.0
Key + BPM detector for trap / hip-hop beatmakers
by Snowinch — snowinch.com

─────────────────────────────────────
REQUIREMENTS
─────────────────────────────────────
macOS 11 or later, Apple Silicon (M1/M2/M3/M4)
VST3 or AU compatible DAW (Ableton, FL Studio, Logic, etc.)

─────────────────────────────────────
INSTALL
─────────────────────────────────────
VST3:
  Copy Keylo.vst3 → ~/Library/Audio/Plug-Ins/VST3/

AU:
  Copy Keylo.component → ~/Library/Audio/Plug-Ins/Components/

Restart your DAW after installing.

─────────────────────────────────────
FIRST LAUNCH — GATEKEEPER
─────────────────────────────────────
macOS will block the plugin on first use because it is not notarized.
Run these commands once in Terminal after installing:

  xattr -rd com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Keylo.vst3
  xattr -rd com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Keylo.component

Then restart your DAW.

─────────────────────────────────────
HOW TO USE
─────────────────────────────────────
1. Insert Keylo on any track or bus with audio playing through it.
2. Press Play in your DAW — Keylo starts listening automatically.
3. After ~12 seconds it shows the detected key and BPM.
4. Check "also:" for two alternative key suggestions.
5. Press Reset to analyse a different section.

Modes:
  loop   — optimised for full arrangements with 808/bass
  melody — optimised for solo instruments or sparse harmony
  cam    — switches hero display to Camelot notation

─────────────────────────────────────
SUPPORT
─────────────────────────────────────
manuel.tardivo@snowinch.com
