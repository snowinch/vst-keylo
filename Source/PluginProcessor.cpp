#include "PluginProcessor.h"
#include "PluginEditor.h"

#if defined(_WIN32)
  #include <windows.h>
#endif

KeyloAudioProcessor::KeyloAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
#if defined(_WIN32)
    // onnxruntime.dll is bundled alongside Keylo.vst3 (DLL) in x86_64-win/.
    // Windows does NOT search the loaded DLL's own directory, so hosts can't
    // find it via normal DLL search order.
    //
    // We use /DELAYLOAD so onnxruntime.dll is NOT resolved at DLL load time,
    // then load it explicitly here — before make_unique<Analyser>() which
    // triggers the first ORT call.
    //
    // GetModuleHandleExW(FROM_ADDRESS) is used instead of
    // juce::File::currentApplicationFile because the JUCE API may return the
    // host EXE path on some Windows DAWs. FROM_ADDRESS is guaranteed to return
    // the HMODULE of the DLL containing this code (Keylo.vst3), regardless
    // of what the host process is.
    {
        HMODULE hSelf = nullptr;
        BOOL ok = GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&createPluginFilter),  // any addr in this DLL
            &hSelf);

        if (ok && hSelf != nullptr)
        {
            wchar_t dllPath[MAX_PATH] = {};
            DWORD len = GetModuleFileNameW(hSelf, dllPath, MAX_PATH);
            if (len > 0 && len < MAX_PATH)
            {
                // Strip filename → directory path (with trailing backslash)
                wchar_t* lastSlash = wcsrchr(dllPath, L'\\');
                if (lastSlash != nullptr)
                {
                    *(lastSlash + 1) = L'\0';
                    // Also add our directory to the DLL search path so any
                    // sub-dependencies of onnxruntime.dll resolve correctly.
                    SetDllDirectoryW(dllPath);

                    wchar_t ortPath[MAX_PATH] = {};
                    wcsncpy_s(ortPath, MAX_PATH, dllPath, _TRUNCATE);
                    wcsncat_s(ortPath, MAX_PATH, L"onnxruntime.dll", _TRUNCATE);
                    LoadLibraryW(ortPath);
                }
            }
        }
    }
#endif

    analyser = std::make_unique<Analyser>();
}

KeyloAudioProcessor::~KeyloAudioProcessor() {}

void KeyloAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    analyser->prepare(sampleRate, samplesPerBlock);
    // Auto-start: no user action needed
}

void KeyloAudioProcessor::releaseResources() {}

bool KeyloAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() &&
        out != juce::AudioChannelSet::stereo()) return false;
    return in == out;
}

void KeyloAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;
    analyser->pushBuffer(buffer);
    // Pure analyser — audio unmodified, passes through untouched
}

juce::AudioProcessorEditor* KeyloAudioProcessor::createEditor()
{
    return new KeyloAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KeyloAudioProcessor();
}
