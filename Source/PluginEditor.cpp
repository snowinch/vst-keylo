#include "PluginEditor.h"
#include "BinaryData.h"

// ── Fonts ─────────────────────────────────────────────────────────────────────
juce::Typeface::Ptr KeyloAudioProcessorEditor::panchangTypeface()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
        BinaryData::PanchangBold_ttf, BinaryData::PanchangBold_ttfSize);
    return tf;
}

juce::Font KeyloAudioProcessorEditor::panchang(float size)
{
    return juce::Font(juce::FontOptions(panchangTypeface()).withHeight(size));
}

juce::Font KeyloAudioProcessorEditor::uiFont(float size, bool bold)
{
    auto opts = juce::FontOptions().withHeight(size)
                    .withStyle(bold ? juce::String("Bold") : juce::String("Regular"));
    return juce::Font(opts);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
KeyloAudioProcessorEditor::NoteDisplay
KeyloAudioProcessorEditor::noteDisplayForKey(int key)
{
    static const char* letters[] = { "C","C","D","D","E","F","F","G","G","A","A","B" };
    static const char* accs[]    = { "", "#","b","#","b","", "#","b","#","b","#","b" };
    if (key < 0 || key > 11) return { "-", "" };
    return { letters[key], accs[key] };
}

juce::String KeyloAudioProcessorEditor::modeLabel(Mode mode)
{
    switch (mode)
    {
        case Mode::Major:             return "major";
        case Mode::NaturalMinor:      return "minor";
        case Mode::HarmonicMinor:     return "harmonic minor";
        case Mode::Dorian:            return "dorian";
        case Mode::Mixolydian:        return "mixolydian";
        case Mode::Phrygian:          return "phrygian";
        case Mode::PhrygianDominant:  return "phrygian dominant";
        default:                      return {};
    }
}

juce::String KeyloAudioProcessorEditor::candidateLabel(const KeyCandidate& c)
{
    if (c.key < 0) return {};
    static const char* notes[] = { "C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B" };
    return juce::String(notes[c.key]) + " " + modeNameShort(c.mode);
}

// ── Layout ────────────────────────────────────────────────────────────────────
juce::Rectangle<int> KeyloAudioProcessorEditor::resetButtonBounds() const
{
    int bw = 70, bh = 22;
    int x  = K::W - K::kPad - bw;
    int y  = K::kFootY + (K::kFootH - bh) / 2;
    return { x, y, bw, bh };
}

bool KeyloAudioProcessorEditor::isOverResetButton(juce::Point<float> p) const
{
    return resetButtonBounds().toFloat().contains(p);
}

bool KeyloAudioProcessorEditor::isSilentCollecting() const noexcept
{
    return cachedState == AnalyserState::Collecting
        && cachedLevelDB <= Analyser::kAutoStartDB;
}

// Mode pills (LOOP / MELODY) — left group, no gap between them (shared border)
juce::Rectangle<int> KeyloAudioProcessorEditor::modePillBounds(int idx) const
{
    static constexpr int pw = 46, ph = 16;
    // Anchor group left of center, after logo clearance
    static constexpr int groupX = 104;
    int y0 = (K::kHeaderH - ph) / 2;
    return { groupX + idx * pw, y0, pw, ph };
}

// CAM pill — separate group, right of mode group
juce::Rectangle<int> KeyloAudioProcessorEditor::camPillBounds() const
{
    static constexpr int pw = 38, ph = 16;
    auto modeRight = modePillBounds(1).getRight() + 10;
    int y0 = (K::kHeaderH - ph) / 2;
    return { modeRight, y0, pw, ph };
}

// Shared pill renderer — leftInGroup / rightInGroup control corner rounding
void KeyloAudioProcessorEditor::drawPill(juce::Graphics& g, juce::Rectangle<float> r,
                                          const char* label, bool active,
                                          bool leftInGroup, bool rightInGroup)
{
    juce::Colour bg  = active ? K::Accent   : K::Surface;
    juce::Colour bdr = active ? K::Accent   : K::Border;
    juce::Colour txt = active ? juce::Colours::white : K::TextDim;

    constexpr float radius = 4.f;
    juce::Path path;
    path.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                             radius, radius,
                             leftInGroup, rightInGroup,   // top corners
                             leftInGroup, rightInGroup);  // bottom corners

    g.setColour(bg);
    g.fillPath(path);
    g.setColour(bdr);
    g.strokePath(path, juce::PathStrokeType(0.8f));
    g.setColour(txt);
    g.setFont(uiFont(8.f, active));
    g.drawText(label, r.toNearestInt(), juce::Justification::centred, false);
}

// ── Editor ────────────────────────────────────────────────────────────────────
KeyloAudioProcessorEditor::KeyloAudioProcessorEditor(KeyloAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(K::W, K::H);
    setOpaque(true);
    startTimerHz(30);
}

KeyloAudioProcessorEditor::~KeyloAudioProcessorEditor() { stopTimer(); }

void KeyloAudioProcessorEditor::timerCallback()
{
    processor.getAnalyser().pollAutoStart();   // safe: UI thread only
    cachedResult  = processor.getAnalyser().getResult();
    cachedState   = processor.getAnalyser().getState();
    cachedLevelDB = processor.getAnalyser().getLevelDB();
    ++animTick;

    float target = 0.f;
    if (cachedState == AnalyserState::Collecting)
        target = juce::jlimit(0.f, 1.f,
            processor.getAnalyser().getSecondsAccum() / Analyser::kSampleSecs);
    else if (cachedState == AnalyserState::Analysing || cachedState == AnalyserState::Ready)
        target = 1.f;

    uiProgress += (target - uiProgress) * 0.18f;
    repaint();
}

void KeyloAudioProcessorEditor::resized() {}

void KeyloAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if ((cachedState == AnalyserState::Ready || isSilentCollecting())
        && isOverResetButton(e.position))
    {
        processor.getAnalyser().reset();
        return;
    }
    // Mode pill clicks: LOOP / MELODY — click active pill to return to Auto
    for (int i = 0; i < 2; ++i)
    {
        if (modePillBounds(i).toFloat().contains(e.position))
        {
            InputMode clicked = (i == 0) ? InputMode::Loop : InputMode::Melody;
            InputMode current = processor.getInputMode();
            processor.setInputMode(current == clicked ? InputMode::Auto : clicked);
            repaint();
            return;
        }
    }

    // CAM pill: toggle Camelot display
    if (camPillBounds().toFloat().contains(e.position))
    {
        processor.setCamelotDisplay(!processor.getCamelotDisplay());
        repaint();
        return;
    }
}

void KeyloAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    bool over = (cachedState == AnalyserState::Ready || isSilentCollecting())
                && isOverResetButton(e.position);
    if (over != resetHover) { resetHover = over; repaint(); }
}

void KeyloAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    if (resetHover) { resetHover = false; repaint(); }
}

// ── Draw: progress bar ────────────────────────────────────────────────────────
void KeyloAudioProcessorEditor::drawProgressBar(juce::Graphics& g,
                                                juce::Rectangle<float> r,
                                                float progress)
{
    g.setColour(K::Border);
    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
    if (progress > 0.002f)
    {
        auto fill = r.withWidth(r.getWidth() * juce::jlimit(0.f, 1.f, progress));
        g.setColour(K::Accent);
        g.fillRoundedRectangle(fill, r.getHeight() * 0.5f);
    }
}

// ── Draw: note letter + accidental ───────────────────────────────────────────
void KeyloAudioProcessorEditor::drawNoteLetter(juce::Graphics& g,
                                               juce::Rectangle<float> area,
                                               const NoteDisplay& note)
{
    if (note.letter == "-")
    {
        g.setColour(K::TextDim);
        g.setFont(uiFont(48.f));
        g.drawText("-", area.toNearestInt(), juce::Justification::centred, false);
        return;
    }

    float letterSize = juce::jlimit(40.f, 88.f, area.getHeight() * 0.72f);
    auto  letterFont = uiFont(letterSize, true);
    auto  accFont    = uiFont(letterSize * 0.44f, false);

    juce::GlyphArrangement gl;
    gl.addLineOfText(letterFont, note.letter, 0.f, 0.f);
    float letterW = gl.getBoundingBox(0, -1, true).getWidth();

    float accW = 0.f;
    if (note.accidental.isNotEmpty())
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(accFont, note.accidental, 0.f, 0.f);
        accW = ga.getBoundingBox(0, -1, true).getWidth();
    }

    float totalW = letterW + accW + 3.f;
    float x0     = area.getCentreX() - totalW * 0.5f;
    float y0     = area.getCentreY() - letterFont.getHeight() * 0.45f;

    g.setColour(K::TextPri);
    g.setFont(letterFont);
    g.drawText(note.letter,
               juce::Rectangle<float>(x0, y0, letterW + 4.f, letterFont.getHeight()).toNearestInt(),
               juce::Justification::centredLeft, false);

    if (note.accidental.isNotEmpty())
    {
        g.setFont(accFont);
        g.drawText(note.accidental,
                   juce::Rectangle<float>(x0 + letterW + 2.f,
                                          y0 - letterSize * 0.12f,
                                          accW + 6.f,
                                          accFont.getHeight() + 4.f).toNearestInt(),
                   juce::Justification::centredLeft, false);
    }
}

// ── Draw: listening animation (three pulsing dots) ────────────────────────────
void KeyloAudioProcessorEditor::drawListeningState(juce::Graphics& g,
                                                    juce::Rectangle<float> area)
{
    float cx = area.getCentreX();
    float r = 4.f;
    float spacing = 14.f;

    if (isSilentCollecting())
    {
        // Silence: dots static and dimmed, label below
        float cy = area.getCentreY() + 4.f;
        for (int i = 0; i < 3; ++i)
        {
            g.setColour(K::TextDim);
            float x = cx + (float)(i - 1) * spacing;
            g.fillEllipse(x - r, cy - r, r * 2.f, r * 2.f);
        }
        g.setColour(K::TextDim);
        g.setFont(uiFont(10.f, false));
        g.drawText("no audio input", area.withTop(area.getCentreY() + 14.f).toNearestInt(),
                   juce::Justification::centredTop, false);
    }
    else
    {
        // Audio present: pulsing dots
        float cy = area.getCentreY() + 12.f;
        for (int i = 0; i < 3; ++i)
        {
            float phase = (float)((animTick + i * 8) % 24) / 24.f;
            float alpha = 0.25f + 0.6f * std::abs(std::sin((float)M_PI * phase));
            g.setColour(K::Accent.withAlpha(alpha));
            float x = cx + (float)(i - 1) * spacing;
            g.fillEllipse(x - r, cy - r, r * 2.f, r * 2.f);
        }
    }
}

// ── Draw: analysing state ─────────────────────────────────────────────────────
void KeyloAudioProcessorEditor::drawAnalysingState(juce::Graphics& g,
                                                    juce::Rectangle<float> area)
{
    g.setColour(K::TextSec);
    g.setFont(uiFont(12.f, false));
    g.drawText("analysing", area.toNearestInt(), juce::Justification::centred, false);
}

// ── Draw: key hero (Ready state) ─────────────────────────────────────────────
// Normal mode: big note letter, mode + camelot small below.
// CAM mode:    big Camelot code (e.g. "12A"), note + mode small below.
void KeyloAudioProcessorEditor::drawKeyHero(juce::Graphics& g, juce::Rectangle<float> area)
{
    auto ml  = modeLabel(cachedResult.mode);
    auto cam = juce::String(cachedResult.camelot);
    bool camMode = processor.getCamelotDisplay();

    float letterSize = juce::jlimit(40.f, 82.f, area.getHeight() * 0.55f);
    float subSize    = 13.f;
    float gap        = 8.f;
    float subH       = subSize + 4.f;
    float blockH     = letterSize + 8.f + gap + subH;
    float blockY     = area.getCentreY() - blockH * 0.5f;

    float conf = juce::jlimit(0.f, 1.f, cachedResult.confidence);
    float alpha = juce::jmap(conf, 0.5f, 1.f, 0.45f, 1.f);
    juce::Colour keyColour = K::TextPri.withAlpha(juce::jlimit(0.45f, 1.f, alpha));

    auto bigArea = juce::Rectangle<float>(area.getX(), blockY, area.getWidth(), letterSize + 8.f);
    auto subArea = juce::Rectangle<float>(area.getX(), blockY + letterSize + 8.f + gap,
                                          area.getWidth(), subH);

    if (camMode && cam.isNotEmpty())
    {
        // ── CAM mode: big Camelot code ────────────────────────────────────────
        float ls = juce::jlimit(40.f, 88.f, bigArea.getHeight() * 0.72f);
        g.setColour(keyColour);
        g.setFont(uiFont(ls, true));
        g.drawText(cam, bigArea.toNearestInt(), juce::Justification::centred, false);

        // Sub: note + mode small
        auto nd = noteDisplayForKey(cachedResult.key);
        juce::String sub = nd.letter + nd.accidental;
        if (ml.isNotEmpty()) sub += "  " + ml;
        g.setColour(K::TextSec);
        g.setFont(uiFont(subSize, false));
        g.drawText(sub, subArea.toNearestInt(), juce::Justification::centred, false);
    }
    else
    {
        // ── Normal mode: big note letter ──────────────────────────────────────
        auto nd = noteDisplayForKey(cachedResult.key);
        if (nd.letter == "-")
        {
            g.setColour(K::TextDim);
            g.setFont(uiFont(48.f));
            g.drawText("-", bigArea.toNearestInt(), juce::Justification::centred, false);
        }
        else
        {
            float ls = juce::jlimit(40.f, 88.f, bigArea.getHeight() * 0.72f);
            auto  lf = uiFont(ls, true);
            auto  af = uiFont(ls * 0.44f, false);

            juce::GlyphArrangement gl;
            gl.addLineOfText(lf, nd.letter, 0.f, 0.f);
            float lw = gl.getBoundingBox(0,-1,true).getWidth();
            float aw = 0.f;
            if (nd.accidental.isNotEmpty())
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText(af, nd.accidental, 0.f, 0.f);
                aw = ga.getBoundingBox(0,-1,true).getWidth();
            }
            float tw = lw + aw + 3.f;
            float x0 = bigArea.getCentreX() - tw * 0.5f;
            float y0 = bigArea.getCentreY() - lf.getHeight() * 0.45f;

            g.setColour(keyColour);
            g.setFont(lf);
            g.drawText(nd.letter,
                       juce::Rectangle<float>(x0, y0, lw+4.f, lf.getHeight()).toNearestInt(),
                       juce::Justification::centredLeft, false);
            if (nd.accidental.isNotEmpty())
            {
                g.setFont(af);
                g.drawText(nd.accidental,
                           juce::Rectangle<float>(x0+lw+2.f, y0-ls*0.12f,
                                                  aw+6.f, af.getHeight()+4.f).toNearestInt(),
                           juce::Justification::centredLeft, false);
            }
        }

        // Sub: mode + camelot
        juce::String sub = ml;
        if (cam.isNotEmpty()) sub += "   " + cam;
        if (sub.isNotEmpty())
        {
            g.setColour(K::TextSec);
            g.setFont(uiFont(subSize, false));
            g.drawText(sub, subArea.toNearestInt(), juce::Justification::centred, false);
        }
    }
}

// ── Draw: control pills (header) ─────────────────────────────────────────────
void KeyloAudioProcessorEditor::drawControlPills(juce::Graphics& g)
{
    InputMode mode = processor.getInputMode();
    bool camOn     = processor.getCamelotDisplay();

    // Group 1: LOOP | MELODY (shared border, left pill rounded-left, right pill rounded-right)
    drawPill(g, modePillBounds(0).toFloat(), "loop",
             mode == InputMode::Loop,   true,  false);
    drawPill(g, modePillBounds(1).toFloat(), "melody",
             mode == InputMode::Melody, false, true);

    // Group 2: CAM (standalone, fully rounded)
    drawPill(g, camPillBounds().toFloat(), "cam", camOn, true, true);
}

// ── Draw: key stability timeline (16 bars) ────────────────────────────────────
void KeyloAudioProcessorEditor::drawStabilityTimeline(juce::Graphics& g)
{
    const auto& tl = cachedResult.stabilityTimeline;
    if ((int)tl.size() < 16) return;

    static constexpr int barW = 12, barGap = 3;
    int barBottom = K::kFootY + K::kFootH - 6;
    int maxBarH   = K::kFootH - 10;

    for (int i = 0; i < 16; ++i)
    {
        float stab = juce::jlimit(0.f, 1.f, tl[(size_t)i]);
        int   bh   = std::max(3, (int)(stab * (float)maxBarH));
        int   bx   = K::kPad + i * (barW + barGap);
        int   by   = barBottom - bh;

        juce::Colour col;
        if      (stab < 0.4f)  col = K::TextDim.interpolatedWith(K::AccentDim, stab / 0.4f);
        else if (stab < 0.7f)  col = K::AccentDim.interpolatedWith(K::Accent, (stab - 0.4f) / 0.3f);
        else                   col = K::Accent.interpolatedWith(K::Green, (stab - 0.7f) / 0.3f);

        g.setColour(col);
        g.fillRoundedRectangle((float)bx, (float)by, (float)barW, (float)bh, 2.f);
    }
}

// ── Draw: header ──────────────────────────────────────────────────────────────
void KeyloAudioProcessorEditor::drawHeader(juce::Graphics& g)
{
    auto r = juce::Rectangle<int>(0, 0, K::W, K::kHeaderH).toFloat();

    // Logo
    g.setColour(K::TextPri);
    g.setFont(panchang(15.f));
    g.drawText("keylo",
               juce::Rectangle<float>((float)K::kPad, 8.f, 80.f, 20.f).toNearestInt(),
               juce::Justification::centredLeft, false);

    // State indicator (right side)
    juce::String stateStr;
    juce::Colour dotCol = K::TextDim;
    switch (cachedState)
    {
        case AnalyserState::Idle:       stateStr = "idle";       dotCol = K::TextDim;  break;
        case AnalyserState::Collecting: stateStr = "listening";  dotCol = K::Green;    break;
        case AnalyserState::Analysing:  stateStr = "analysing";  dotCol = K::Accent;   break;
        case AnalyserState::Ready:      stateStr = "ready";      dotCol = K::Green;    break;
    }

    // Dot
    float dotR = 3.5f;
    float dotX = K::W - K::kPad - 4.f;
    float dotY = r.getCentreY();
    // Pulse alpha for collecting state
    float dotAlpha = 1.f;
    if (cachedState == AnalyserState::Collecting)
        dotAlpha = 0.5f + 0.5f * std::abs(std::sin((float)animTick * 0.15f));
    g.setColour(dotCol.withAlpha(dotAlpha));
    g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.f, dotR * 2.f);

    // Label
    g.setColour(K::TextDim);
    g.setFont(uiFont(9.f, false));
    g.drawText(stateStr,
               juce::Rectangle<float>((float)K::kPad + 80.f, 8.f,
                                       (float)(K::W - K::kPad * 2 - 80 - 12), 20.f).toNearestInt(),
               juce::Justification::centredRight, false);

    // Control pills
    drawControlPills(g);

    // Separator line
    g.setColour(K::Border);
    g.drawHorizontalLine(K::kHeaderH - 1, (float)K::kPad, (float)(K::W - K::kPad));
}

// ── Draw: hero area ───────────────────────────────────────────────────────────
void KeyloAudioProcessorEditor::drawHero(juce::Graphics& g)
{
    auto area = juce::Rectangle<int>(K::kPad, K::kHeroY, K::W - K::kPad * 2, K::kHeroH).toFloat();

    switch (cachedState)
    {
        case AnalyserState::Idle:
        {
            // Waiting — show subtle hint text
            g.setColour(K::TextDim);
            g.setFont(uiFont(11.f, false));
            g.drawText("play audio to analyse", area.toNearestInt(),
                       juce::Justification::centred, false);
            break;
        }
        case AnalyserState::Collecting:
            drawListeningState(g, area);
            break;
        case AnalyserState::Analysing:
            drawAnalysingState(g, area);
            break;
        case AnalyserState::Ready:
            drawKeyHero(g, area);
            break;
    }
}

// ── Draw: BPM + confidence strip ──────────────────────────────────────────────
void KeyloAudioProcessorEditor::drawBpmStrip(juce::Graphics& g)
{
    if (cachedState != AnalyserState::Ready) return;

    auto strip = juce::Rectangle<int>(K::kPad, K::kBpmY, K::W - K::kPad * 2, K::kBpmH).toFloat();
    auto inner = strip.reduced(0.f, 4.f);

    // Divider
    g.setColour(K::Border);
    g.drawHorizontalLine(K::kBpmY, (float)K::kPad, (float)(K::W - K::kPad));

    // BPM left
    if (cachedResult.bpm > 0.f)
    {
        juce::String bpmStr = juce::String(cachedResult.bpm, 1) + " bpm";
        g.setColour(K::TextSec);
        g.setFont(uiFont(11.f, false));
        g.drawText(bpmStr, inner.withWidth(inner.getWidth() * 0.4f).toNearestInt(),
                   juce::Justification::centredLeft, false);
    }

    // Confidence bar + label right
    float conf = juce::jlimit(0.f, 1.f, cachedResult.confidence);
    int confPct = (int)std::round(conf * 100.f);
    juce::String confStr = juce::String(confPct) + "%";

    // Right portion
    auto rightArea = inner.withTrimmedLeft(inner.getWidth() * 0.45f);
    // Confidence label
    g.setColour(K::TextSec);
    g.setFont(uiFont(9.f, false));
    g.drawText(confStr, rightArea.removeFromRight(32.f).toNearestInt(),
               juce::Justification::centredRight, false);
    // Bar
    auto barArea = rightArea.reduced(0.f, 10.f);
    drawProgressBar(g, barArea, conf);
}

// ── Draw: alternatives row ────────────────────────────────────────────────────
void KeyloAudioProcessorEditor::drawAltRow(juce::Graphics& g)
{
    if (cachedState != AnalyserState::Ready) return;
    if (cachedResult.alt1.key < 0 && cachedResult.alt2.key < 0) return;

    auto row = juce::Rectangle<int>(K::kPad, K::kAltY, K::W - K::kPad * 2, K::kAltH).toFloat();

    // Divider
    g.setColour(K::Border);
    g.drawHorizontalLine(K::kAltY, (float)K::kPad, (float)(K::W - K::kPad));

    g.setColour(K::TextDim);
    g.setFont(uiFont(9.f, false));
    g.drawText("also:", row.removeFromLeft(30.f).toNearestInt(),
               juce::Justification::centredLeft, false);

    auto label1 = candidateLabel(cachedResult.alt1);
    auto label2 = candidateLabel(cachedResult.alt2);

    juce::String altText = label1;
    if (label2.isNotEmpty()) altText += "   /   " + label2;

    g.setColour(K::TextSec);
    g.setFont(uiFont(10.f, false));
    g.drawText(altText, row.toNearestInt(), juce::Justification::centredLeft, false);
}

// ── Draw: footer (progress bar + reset button) ───────────────────────────────
void KeyloAudioProcessorEditor::drawFooter(juce::Graphics& g)
{
    // Divider
    g.setColour(K::Border);
    g.drawHorizontalLine(K::kFootY, (float)K::kPad, (float)(K::W - K::kPad));

    if (cachedState == AnalyserState::Ready)
    {
        // Stability timeline replaces progress bar in Ready state
        drawStabilityTimeline(g);
    }
    else if (cachedState == AnalyserState::Collecting || cachedState == AnalyserState::Analysing)
    {
        float barRight = isSilentCollecting()
                         ? (float)(resetButtonBounds().getX() - 8)
                         : (float)(K::W - K::kPad);
        auto barRect = juce::Rectangle<float>(
            (float)K::kPad,
            (float)K::kFootY + (float)K::kFootH * 0.5f + 4.f,
            barRight - (float)K::kPad, 3.f);
        drawProgressBar(g, barRect, uiProgress);
    }

    // Reset button — in Ready state OR when collecting with no audio input
    if (cachedState == AnalyserState::Ready || isSilentCollecting())
    {
        auto btn = resetButtonBounds().toFloat();

        juce::Colour btnBg  = resetHover ? K::Accent               : K::Surface;
        juce::Colour btnBdr = resetHover ? K::Accent               : K::Border;
        juce::Colour btnTxt = resetHover ? juce::Colours::white     : K::TextSec;

        g.setColour(btnBg);
        g.fillRoundedRectangle(btn, 5.f);
        g.setColour(btnBdr);
        g.drawRoundedRectangle(btn, 5.f, 1.f);
        g.setColour(btnTxt);
        g.setFont(uiFont(10.f, false));
        g.drawText("reset", btn.toNearestInt(), juce::Justification::centred, false);
    }
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void KeyloAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(K::Bg);
    drawHeader(g);
    drawHero(g);
    drawBpmStrip(g);
    drawAltRow(g);
    drawFooter(g);
}
