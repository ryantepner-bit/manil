#include "FractalView.h"
#include <thread>

// ============================================================================ renderer
FractalRenderer::FractalRenderer() : juce::Thread ("Mandelbrot renderer") { startThread (juce::Thread::Priority::normal); }
FractalRenderer::~FractalRenderer() { signalThreadShouldExit(); wake.signal(); stopThread (4000); }

juce::StringArray FractalRenderer::paletteNames() { return { "Nebula", "Acid", "Ice", "Ember", "Spectral", "Mono", "Circuit" }; }

juce::Colour FractalRenderer::paletteColour (int palette, double t)
{
    // cosine palettes: a + b * cos(2pi (c t + d))
    static const double P[7][12] = {
        { 0.5, 0.5, 0.5,  0.5, 0.5, 0.5,  1.0, 1.0, 1.0,  0.00, 0.10, 0.20 },
        { 0.5, 0.5, 0.5,  0.5, 0.5, 0.5,  1.0, 1.0, 0.5,  0.80, 0.90, 0.30 },
        { 0.4, 0.6, 0.8,  0.4, 0.4, 0.3,  1.0, 1.0, 1.0,  0.50, 0.55, 0.60 },
        { 0.6, 0.3, 0.2,  0.5, 0.3, 0.2,  1.0, 1.0, 1.0,  0.00, 0.15, 0.25 },
        { 0.5, 0.5, 0.5,  0.5, 0.5, 0.5,  1.0, 1.0, 1.0,  0.00, 0.33, 0.67 },
        { 0.5, 0.5, 0.5,  0.5, 0.5, 0.5,  1.0, 1.0, 1.0,  0.00, 0.00, 0.00 },
        { 0.2, 0.5, 0.5,  0.2, 0.5, 0.5,  2.0, 1.0, 1.0,  0.00, 0.25, 0.25 },
    };
    const auto* p = P[juce::jlimit (0, 6, palette)];
    auto ch = [&] (int i) { return juce::jlimit (0.0, 1.0, p[i] + p[i + 3] * std::cos (juce::MathConstants<double>::twoPi * (p[i + 6] * t + p[i + 9]))); };
    return juce::Colour::fromFloatRGBA ((float) ch (0), (float) ch (1), (float) ch (2), 1.0f);
}

void FractalRenderer::request (const View& v)
{
    const juce::ScopedLock sl (lock);
    pending = v;
    hasPending = true;
    ++version;
    wake.signal();
}

bool FractalRenderer::fetch (juce::Image& img, View& v)
{
    const juce::ScopedLock sl (lock);
    if (! finished.isValid()) return false;
    img = finished;
    v = finishedView;
    finished = {};
    return true;
}

void FractalRenderer::run()
{
    while (! threadShouldExit())
    {
        wake.wait (200);
        View v;
        juce::uint32 ver;
        {
            const juce::ScopedLock sl (lock);
            if (! hasPending) continue;
            v = pending; hasPending = false; ver = version.load();
        }
        if (v.w <= 0 || v.h <= 0) continue;
        if (lutPalette != v.palette)
        {
            for (size_t i = 0; i < lut.size(); ++i) lut[i] = paletteColour (v.palette, i / (double) lut.size());
            lutPalette = v.palette;
        }
        for (int step : { 6, 1 })
        {
            juce::Image img (juce::Image::RGB, v.w, v.h, false);
            renderPass (v, step, img, ver);
            if (version.load() != ver || threadShouldExit()) break;
            {
                const juce::ScopedLock sl (lock);
                finished = img; finishedView = v;
            }
            juce::MessageManager::callAsync ([this] { if (onImageReady) onImageReady(); });
        }
    }
}

void FractalRenderer::renderPass (const View& v, int step, juce::Image& img, juce::uint32 ver)
{
    const int W = v.w, H = v.h;
    const double sx = v.scale / W;
    const double x0 = v.cx - v.scale * 0.5, y0 = v.cy + sx * H * 0.5;
    const int ss = (step == 1 && v.supersample) ? 2 : 1;
    const int maxIt = v.iters;
    const juce::Colour inside (0xff05060a);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);

    auto shade = [&] (double px, double py) -> juce::Colour
    {
        const auto r = MandelMath::iterate (px, py, maxIt);
        if (! r.escaped) return inside;
        const double t = std::fmod (std::log1p (r.smooth) * 0.35 + r.smooth * 0.004, 1.0);
        return lut[(size_t) juce::jlimit (0, 1023, (int) (t * 1023.0))];
    };

    const int numThreads = juce::jmax (1, (int) std::thread::hardware_concurrency() - 1);
    std::vector<std::thread> workers;
    for (int ti = 0; ti < numThreads; ++ti)
        workers.emplace_back ([&, ti]
        {
            for (int y = ti * step; y < H; y += step * numThreads)
            {
                if (version.load() != ver) return;
                for (int x = 0; x < W; x += step)
                {
                    juce::Colour c;
                    if (ss == 1) c = shade (x0 + (x + 0.5) * sx, y0 - (y + 0.5) * sx);
                    else
                    {
                        float rr = 0, gg = 0, bb = 0;
                        for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i)
                        {
                            auto s = shade (x0 + (x + 0.25 + 0.5 * i) * sx, y0 - (y + 0.25 + 0.5 * j) * sx);
                            rr += s.getFloatRed(); gg += s.getFloatGreen(); bb += s.getFloatBlue();
                        }
                        c = juce::Colour::fromFloatRGBA (rr / 4, gg / 4, bb / 4, 1.0f);
                    }
                    for (int dy = 0; dy < step && y + dy < H; ++dy)
                        for (int dx = 0; dx < step && x + dx < W; ++dx)
                            bd.setPixelColour (x + dx, y + dy, c);
                }
            }
        });
    for (auto& w : workers) w.join();
}

// ============================================================================ canvas
FractalCanvas::FractalCanvas (AppState& s, Composer& c) : state (s), composer (c)
{
    renderer.onImageReady = [this]
    {
        if (renderer.fetch (image, imageView)) repaint();
    };
    view.cx = -0.5; view.cy = 0.0; view.scale = 3.2;
    startTimerHz (20);
}

juce::Point<double> FractalCanvas::toComplex (juce::Point<float> p) const
{
    const double w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    return { view.cx + (p.x / w - 0.5) * view.scale, view.cy - (p.y / h - 0.5) * view.scale * h / w };
}

juce::Point<float> FractalCanvas::toScreen (double x, double y) const
{
    const double w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    return { (float) (((x - view.cx) / view.scale + 0.5) * w), (float) ((-(y - view.cy) / (view.scale * h / w) + 0.5) * h) };
}

void FractalCanvas::applyAutoIterations()
{
    if (! autoIterations) return;
    view.iters = juce::jlimit (150, 6000, (int) (180.0 + 110.0 * juce::jmax (0.0, std::log2 (3.0 / view.scale))));
}

void FractalCanvas::requestRender()
{
    view.w = getWidth(); view.h = getHeight();
    if (view.w > 0 && view.h > 0) renderer.request (view);
    if (onViewChanged) onViewChanged();
    if (state.probeFollowsView) composer.setProbe (view.cx, view.cy, view.scale * 0.25);
    repaint();
}

void FractalCanvas::setView (double cx, double cy, double scale, bool addHistory)
{
    view.cx = cx; view.cy = cy; view.scale = juce::jlimit (1e-13, 8.0, scale);
    applyAutoIterations();
    requestRender();
    if (addHistory) pushHistory();
}

void FractalCanvas::pushHistory()
{
    if (historyPos >= 0 && historyPos < (int) history.size())
    {
        const auto& h = history[(size_t) historyPos];
        if (h.cx == view.cx && h.cy == view.cy && h.scale == view.scale) return;
    }
    history.resize ((size_t) (historyPos + 1));
    history.push_back (view);
    if (history.size() > 200) history.erase (history.begin());
    historyPos = (int) history.size() - 1;
}

void FractalCanvas::back()
{
    if (historyPos <= 0) return;
    const auto& h = history[(size_t) --historyPos];
    setView (h.cx, h.cy, h.scale, false);
}

void FractalCanvas::forward()
{
    if (historyPos + 1 >= (int) history.size()) return;
    const auto& h = history[(size_t) ++historyPos];
    setView (h.cx, h.cy, h.scale, false);
}

void FractalCanvas::timerCallback()
{
    if (wheelPending && juce::Time::getMillisecondCounter() - lastWheel > 400) { wheelPending = false; pushHistory(); }
    repaint();   // probe / orbit overlay follows feature updates and path replay
}

void FractalCanvas::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff05060a));
    if (image.isValid() && imageView.w > 0)
    {
        const double w = getWidth(), h = getHeight();
        const double k = imageView.scale / view.scale * w / imageView.w;
        const double tx = (imageView.cx - view.cx) / view.scale * w + w * 0.5 - 0.5 * imageView.w * k;
        const double ty = (view.cy - imageView.cy) / view.scale * w + h * 0.5 - 0.5 * imageView.h * k;
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageTransformed (image, juce::AffineTransform::scale ((float) k).translated ((float) tx, (float) ty));
    }

    // orbit of the probe point
    const double pxv = composer.probeX(), pyv = composer.probeY();
    auto orb = MandelMath::orbit (pxv, pyv, 48, 0);
    if (! orb.empty())
    {
        juce::Path path;
        auto p0 = toScreen (orb[0].real(), orb[0].imag());
        path.startNewSubPath (p0);
        for (auto& z : orb) path.lineTo (toScreen (z.real(), z.imag()));
        g.setColour (Theme::accent2.withAlpha (0.55f));
        g.strokePath (path, juce::PathStrokeType (1.2f));
        for (auto& z : orb)
        {
            auto p = toScreen (z.real(), z.imag());
            g.fillEllipse (p.x - 2, p.y - 2, 4, 4);
        }
    }
    // probe crosshair + neighbourhood
    auto pp = toScreen (pxv, pyv);
    const float rad = (float) (composer.probeScale() / view.scale * getWidth() * 0.5);
    g.setColour (Theme::accent);
    g.drawLine (pp.x - 12, pp.y, pp.x - 4, pp.y, 1.5f); g.drawLine (pp.x + 4, pp.y, pp.x + 12, pp.y, 1.5f);
    g.drawLine (pp.x, pp.y - 12, pp.x, pp.y - 4, 1.5f); g.drawLine (pp.x, pp.y + 4, pp.x, pp.y + 12, 1.5f);
    if (rad > 6 && rad < getWidth())
    {
        g.setColour (Theme::accent.withAlpha (0.35f));
        g.drawRect (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (pp), 1.0f);
    }
    if (selecting)
    {
        g.setColour (Theme::warn.withAlpha (0.18f)); g.fillRect (selection);
        g.setColour (Theme::warn); g.drawRect (selection, 1.0f);
    }
    // replayed / recorded path
    if (auto path = composer.getReplayPath(); path.isValid() && path.getNumChildren() > 1)
    {
        juce::Path pth;
        for (int i = 0; i < path.getNumChildren(); ++i)
        {
            auto k = path.getChild (i);
            auto p = toScreen (k[IDs::cx], k[IDs::cy]);
            if (i == 0) pth.startNewSubPath (p); else pth.lineTo (p);
        }
        g.setColour (Theme::good.withAlpha (0.7f));
        g.strokePath (pth, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
    }
    g.setColour (Theme::text.withAlpha (0.8f));
    g.setFont (Theme::mono (11.0f));
    g.drawText (juce::String (hover.x, 12) + "  " + juce::String (hover.y, 12) + "i", getLocalBounds().reduced (8).removeFromBottom (14), juce::Justification::bottomLeft);
    if (composer.isRecordingPath())
    {
        g.setColour (Theme::danger);
        g.fillEllipse (12, 12, 10, 10);
        g.drawText ("RECORDING PATH", 28, 8, 200, 18, juce::Justification::centredLeft);
    }
}

void FractalCanvas::mouseMove (const juce::MouseEvent& e) { hover = toComplex (e.position); }

void FractalCanvas::mouseDown (const juce::MouseEvent& e)
{
    dragStart = e.position;
    dragView = view;
    dragging = false;
    selecting = e.mods.isShiftDown();
    selection = {};
    if (e.mods.isPopupMenu())
    {
        selecting = false;
        juce::PopupMenu m;
        m.addItem (1, "Set probe here");
        m.addItem (2, "Zoom in here (x4)");
        m.addItem (3, "Zoom out (x4)");
        m.addItem (4, "Bookmark this view");
        const auto c = toComplex (e.position);
        m.showMenuAsync (juce::PopupMenu::Options(), [this, c] (int r)
        {
            if (r == 1) composer.setProbe (c.x, c.y, view.scale * 0.25, true);
            if (r == 2) setView (c.x, c.y, view.scale / 4.0, true);
            if (r == 3) setView (view.cx, view.cy, view.scale * 4.0, true);
            if (r == 4)
            {
                juce::ValueTree bm (IDs::BOOKMARK);
                bm.setProperty (IDs::name, "View " + juce::String (state.project.getChildWithName (IDs::BOOKMARKS).getNumChildren() + 1), nullptr);
                bm.setProperty (IDs::cx, view.cx, nullptr); bm.setProperty (IDs::cy, view.cy, nullptr);
                bm.setProperty (IDs::scaleF, view.scale, nullptr); bm.setProperty (IDs::iters, view.iters, nullptr);
                state.beginEdit ("Bookmark");
                state.project.getOrCreateChildWithName (IDs::BOOKMARKS, nullptr).appendChild (bm, &state.undo);
            }
        });
    }
}

void FractalCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (e.getDistanceFromDragStart() > 3) dragging = true;
    if (! dragging) return;
    if (selecting)
    {
        selection = juce::Rectangle<float> (dragStart, e.position);
        repaint();
        return;
    }
    const double w = juce::jmax (1, getWidth());
    view.cx = dragView.cx - (e.position.x - dragStart.x) / w * dragView.scale;
    view.cy = dragView.cy + (e.position.y - dragStart.y) / w * dragView.scale;
    requestRender();
}

void FractalCanvas::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (selecting && dragging && selection.getWidth() > 4)
    {
        // region selection: probe at the region centre, neighbourhood = region size
        const auto a = toComplex (selection.getTopLeft()), b = toComplex (selection.getBottomRight());
        const double cx = (a.x + b.x) * 0.5, cy = (a.y + b.y) * 0.5, sz = std::abs (b.x - a.x);
        const bool follow = state.probeFollowsView;
        state.probeFollowsView = false;
        if (e.mods.isCommandDown()) setView (cx, cy, sz, true);
        composer.setProbe (cx, cy, sz, true);
        state.probeFollowsView = follow;
        if (state.setStatus) state.setStatus ("Region analysed: " + state.features.describe());
    }
    else if (! dragging)
    {
        const auto c = toComplex (e.position);
        composer.setProbe (c.x, c.y, view.scale * 0.25, true);
    }
    else pushHistory();
    selecting = false; dragging = false;
    repaint();
}

void FractalCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto c = toComplex (e.position);
    setView (c.x, c.y, view.scale * (e.mods.isShiftDown() ? 2.0 : 0.5), true);
}

void FractalCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& d)
{
    const auto before = toComplex (e.position);
    const double factor = std::pow (0.82, (double) d.deltaY * 4.0);
    view.scale = juce::jlimit (1e-13, 8.0, view.scale * factor);
    const auto after = toComplex (e.position);
    view.cx += before.x - after.x;
    view.cy += before.y - after.y;
    applyAutoIterations();
    requestRender();
    lastWheel = juce::Time::getMillisecondCounter();
    wheelPending = true;
}

// ============================================================================ page
FractalPage::FractalPage (AppState& s, Composer& c) : state (s), composer (c), canvas (s, c)
{
    addAndMakeVisible (canvas);
    canvas.onViewChanged = [this] { viewChanged(); };
    composer.onPathMove = [this] (double x, double y, double sc) { canvas.setView (x, y, sc, false); };

    coords.setFont (Theme::mono (12.0f));
    coords.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (coords);
    featureText.setFont (Theme::mono (11.0f));
    featureText.setJustificationType (juce::Justification::topLeft);
    featureText.setColour (juce::Label::textColourId, Theme::text);
    addAndMakeVisible (featureText);

    iterSlider.setSliderStyle (juce::Slider::LinearBar);
    iterSlider.setRange (50, 6000, 1);
    iterSlider.setTextValueSuffix (" iterations");
    iterSlider.setColour (juce::Slider::trackColourId, Theme::accent.withAlpha (0.3f));
    iterSlider.onValueChange = [this] { autoIter.setToggleState (false, juce::dontSendNotification); canvas.autoIterations = false; canvas.setIterations ((int) iterSlider.getValue()); };
    addAndMakeVisible (iterSlider);
    palette.addItemList (FractalRenderer::paletteNames(), 1);
    palette.onChange = [this] { canvas.setPalette (palette.getSelectedItemIndex()); if (project.isValid()) project.getChildWithName (IDs::FRACTAL).setProperty (IDs::palette, palette.getSelectedItemIndex(), nullptr); };
    addAndMakeVisible (palette);
    autoIter.setToggleState (true, juce::dontSendNotification);
    autoIter.onClick = [this] { canvas.autoIterations = autoIter.getToggleState(); auto v = canvas.getView(); canvas.setView (v.cx, v.cy, v.scale, false); };
    hq.onClick = [this] { canvas.setSupersample (hq.getToggleState()); };
    follow.setToggleState (true, juce::dontSendNotification);
    follow.onClick = [this] { state.probeFollowsView = follow.getToggleState(); };
    for (auto* b : { &autoIter, &hq, &follow }) addAndMakeVisible (b);

    backBtn.onClick = [this] { canvas.back(); };
    fwdBtn.onClick = [this] { canvas.forward(); };
    resetBtn.onClick = [this] { canvas.setView (-0.5, 0.0, 3.2, true); };
    pngBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save fractal image", juce::File::getSpecialLocation (juce::File::userPicturesDirectory).getChildFile ("mandelbrot.png"), "*.png");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            auto v = canvas.getView();
            // high resolution export (2x canvas, all cores) on a background thread
            juce::Component::SafePointer<FractalPage> sp (this);
            if (state.setStatus) state.setStatus ("Rendering " + juce::String (v.w * 2) + " x " + juce::String (v.h * 2) + " image...");
            juce::Thread::launch ([v, f, sp]
            {
                juce::Image img (juce::Image::RGB, juce::jmax (64, v.w * 2), juce::jmax (64, v.h * 2), false);
                const double sx = v.scale / img.getWidth();
                const double x0 = v.cx - v.scale * 0.5, y0 = v.cy + sx * img.getHeight() * 0.5;
                const int H = img.getHeight(), W = img.getWidth();
                std::vector<juce::Colour> px ((size_t) (W * H));
                const int nt = juce::jmax (1, (int) std::thread::hardware_concurrency());
                std::vector<std::thread> ws;
                for (int t = 0; t < nt; ++t)
                    ws.emplace_back ([&, t]
                    {
                        for (int y = t; y < H; y += nt)
                            for (int x = 0; x < W; ++x)
                            {
                                const auto r = MandelMath::iterate (x0 + (x + 0.5) * sx, y0 - (y + 0.5) * sx, v.iters);
                                px[(size_t) (y * W + x)] = r.escaped ? FractalRenderer::paletteColour (v.palette, std::fmod (std::log1p (r.smooth) * 0.35 + r.smooth * 0.004, 1.0)) : juce::Colour (0xff05060a);
                            }
                    });
                for (auto& w : ws) w.join();
                for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) img.setPixelAt (x, y, px[(size_t) (y * W + x)]);
                auto out = f.withFileExtension ("png");
                out.deleteFile();
                juce::FileOutputStream os (out);
                const bool ok = os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os);
                juce::MessageManager::callAsync ([sp, ok, out] { if (sp != nullptr && sp->state.setStatus) sp->state.setStatus ((ok ? "Saved " : "Failed to save ") + out.getFullPathName()); });
            });
        });
    };
    for (auto* b : { &backBtn, &fwdBtn, &resetBtn, &pngBtn }) addAndMakeVisible (b);

    addAndMakeVisible (bookmarks);
    bookmarks.setTextWhenNothingSelected ("Bookmarks");
    bookmarks.onChange = [this]
    {
        auto bm = project.getChildWithName (IDs::BOOKMARKS).getChild (bookmarks.getSelectedItemIndex());
        if (bm.isValid())
        {
            canvas.setView (bm[IDs::cx], bm[IDs::cy], bm[IDs::scaleF], true);
        }
    };
    addBm.onClick = [this]
    {
        auto v = canvas.getView();
        juce::ValueTree bm (IDs::BOOKMARK);
        auto list = project.getOrCreateChildWithName (IDs::BOOKMARKS, nullptr);
        bm.setProperty (IDs::name, "View " + juce::String (list.getNumChildren() + 1) + "  (" + juce::String (v.cx, 5) + ", " + juce::String (v.cy, 5) + ")", nullptr);
        bm.setProperty (IDs::cx, v.cx, nullptr); bm.setProperty (IDs::cy, v.cy, nullptr);
        bm.setProperty (IDs::scaleF, v.scale, nullptr); bm.setProperty (IDs::iters, v.iters, nullptr);
        state.beginEdit ("Bookmark");
        list.appendChild (bm, &state.undo);
        refreshLists();
        bookmarks.setSelectedItemIndex (list.getNumChildren() - 1, juce::dontSendNotification);
    };
    delBm.onClick = [this]
    {
        auto list = project.getChildWithName (IDs::BOOKMARKS);
        const int i = bookmarks.getSelectedItemIndex();
        if (i >= 0) { state.beginEdit ("Delete bookmark"); list.removeChild (i, &state.undo); refreshLists(); }
    };
    for (auto* b : { &addBm, &delBm }) addAndMakeVisible (b);

    addAndMakeVisible (paths);
    paths.setTextWhenNothingSelected ("Exploration paths");
    paths.onChange = [this] { if (replayPath.getToggleState()) composer.setReplayPath (project.getChildWithName (IDs::PATHS).getChild (paths.getSelectedItemIndex())); };
    recPath.setColour (juce::TextButton::buttonOnColourId, Theme::danger);
    recPath.onClick = [this]
    {
        if (composer.isRecordingPath()) composer.stopRecording(); else composer.startRecording();
        recPath.setToggleState (composer.isRecordingPath(), juce::dontSendNotification);
        refreshLists();
        if (! composer.isRecordingPath()) paths.setSelectedItemIndex (paths.getNumItems() - 1, juce::dontSendNotification);
    };
    replayPath.setColour (juce::TextButton::buttonOnColourId, Theme::good);
    replayPath.onClick = [this]
    {
        const bool on = ! replayPath.getToggleState();
        replayPath.setToggleState (on, juce::dontSendNotification);
        composer.setReplayPath (on ? project.getChildWithName (IDs::PATHS).getChild (paths.getSelectedItemIndex()) : juce::ValueTree());
        if (on && ! composer.getReplayPath().isValid()) { replayPath.setToggleState (false, juce::dontSendNotification); if (state.setStatus) state.setStatus ("Select a recorded path first"); }
    };
    renderPath.onClick = [this]
    {
        auto p = project.getChildWithName (IDs::PATHS).getChild (paths.getSelectedItemIndex());
        if (! p.isValid()) { if (state.setStatus) state.setStatus ("Select a recorded path first"); return; }
        if (project.getChildWithName (IDs::GENERATORS).getNumChildren() == 0) { if (state.setStatus) state.setStatus ("Add generators in the Generative workspace first"); return; }
        const int n = composer.renderPathToArrangement (p);
        if (state.setStatus) state.setStatus ("Rendered path into " + juce::String (n) + " clips");
    };
    delPath.onClick = [this]
    {
        auto list = project.getChildWithName (IDs::PATHS);
        const int i = paths.getSelectedItemIndex();
        if (i >= 0) { if (composer.getReplayPath() == list.getChild (i)) composer.setReplayPath ({}); state.beginEdit ("Delete path"); list.removeChild (i, &state.undo); refreshLists(); }
    };
    for (auto* b : { &recPath, &replayPath, &renderPath, &delPath }) addAndMakeVisible (b);
    genAll.setColour (juce::TextButton::buttonColourId, Theme::accent3.withAlpha (0.35f));
    genAll.onClick = [this]
    {
        if (project.getChildWithName (IDs::GENERATORS).getNumChildren() == 0) { if (state.setStatus) state.setStatus ("No generators yet: add some in the Generative workspace"); return; }
        composer.generateAll (state.engine.getPosition());
    };
    addAndMakeVisible (genAll);
    state.featuresChanged.addChangeListener (this);
    startTimerHz (4);
}

FractalPage::~FractalPage() { state.featuresChanged.removeChangeListener (this); }

void FractalPage::setProject (juce::ValueTree p)
{
    project = {};   // don't let the view callbacks below write into the new project before it is read
    auto fr = p.getChildWithName (IDs::FRACTAL);
    const double cx = fr.getProperty (IDs::cx, -0.5), cy = fr.getProperty (IDs::cy, 0.0), sc = fr.getProperty (IDs::scaleF, 3.2);
    const int pal = fr.getProperty (IDs::palette, 0);
    palette.setSelectedItemIndex (pal, juce::dontSendNotification);
    const bool follow = state.probeFollowsView;
    state.probeFollowsView = false;
    canvas.setPalette (pal);
    project = p;
    canvas.setView (cx, cy, sc, true);
    state.probeFollowsView = follow;
    refreshLists();
    changeListenerCallback (nullptr);
}

void FractalPage::refreshLists()
{
    bookmarks.clear (juce::dontSendNotification);
    int i = 1;
    for (auto b : project.getChildWithName (IDs::BOOKMARKS)) bookmarks.addItem (b[IDs::name].toString(), i++);
    const int sel = paths.getSelectedItemIndex();
    paths.clear (juce::dontSendNotification);
    i = 1;
    for (auto p : project.getChildWithName (IDs::PATHS))
        paths.addItem (p[IDs::name].toString() + "  (" + juce::String (p.getNumChildren()) + " keys, "
                       + juce::String (p.getNumChildren() > 0 ? (double) p.getChild (p.getNumChildren() - 1)[IDs::beat] : 0.0, 1) + " beats)", i++);
    if (sel >= 0) paths.setSelectedItemIndex (juce::jmin (sel, paths.getNumItems() - 1), juce::dontSendNotification);
}

void FractalPage::timerCallback()
{
    if (composer.isRecordingPath()) refreshLists();
}

void FractalPage::viewChanged()
{
    auto v = canvas.getView();
    coords.setText ("Re  " + juce::String (v.cx, 14) + "\nIm  " + juce::String (v.cy, 14)
                    + "\nZoom  x" + juce::String (3.2 / v.scale, 2) + "   width " + juce::String (v.scale, 3)
                    + "\nIter  " + juce::String (v.iters), juce::dontSendNotification);
    if (! iterSlider.isMouseButtonDown()) iterSlider.setValue (v.iters, juce::dontSendNotification);
    if (project.isValid())
    {
        auto fr = project.getChildWithName (IDs::FRACTAL);
        fr.setProperty (IDs::cx, v.cx, nullptr); fr.setProperty (IDs::cy, v.cy, nullptr);
        fr.setProperty (IDs::scaleF, v.scale, nullptr); fr.setProperty (IDs::iters, v.iters, nullptr);
    }
}

void FractalPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto& f = state.features;
    Gen::Settings s;
    const auto tr = Gen::MusicalTraits::from (f, s);
    juce::String t;
    t << "PROBE  " << juce::String (f.cx, 8) << ", " << juce::String (f.cy, 8) << "\n"
      << (f.inside ? "inside the set" : "outside the set") << (f.period > 0 ? "   period " + juce::String (f.period) : juce::String()) << "\n\n"
      << "escape      " << juce::String (f.escape, 3) << "   -> velocity/accent\n"
      << "boundary    " << juce::String (f.boundary, 3) << "   -> syncopation\n"
      << "chaos       " << juce::String (f.chaos, 3) << "   -> ratchets/legato\n"
      << "entropy     " << juce::String (f.entropy, 3) << "   -> harmonic rhythm\n"
      << "roughness   " << juce::String (f.roughness, 3) << "   -> density\n"
      << "inside %    " << juce::String (f.insideFraction * 100.0, 1) << "    -> consonance\n"
      << "angle       " << juce::String (f.angle, 3) << "   -> break template\n\n"
      << "MUSIC  grid 1/" << tr.subdivision << (tr.irregular ? " irregular" : "") << "\n"
      << "       poly cycle " << tr.polyCycle << "   chords/bar " << tr.chordsPerBar << "\n"
      << "       syncopation " << juce::String (tr.syncopation, 2) << "   consonance " << juce::String (tr.consonance, 2);
    featureText.setText (t, juce::dontSendNotification);
    canvas.repaint();
}

void FractalPage::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);
    auto side = getLocalBounds().removeFromRight (330).reduced (6);
    Theme::drawPanel (g, side.toFloat(), "Fractal Explorer");
}

void FractalPage::resized()
{
    auto r = getLocalBounds();
    auto side = r.removeFromRight (330).reduced (14, 12);
    canvas.setBounds (r.reduced (6));
    side.removeFromTop (18);
    coords.setBounds (side.removeFromTop (66));
    auto row = [&side] (int h) { auto x = side.removeFromTop (h); side.removeFromTop (5); return x; };
    iterSlider.setBounds (row (22));
    { auto rr = row (24); palette.setBounds (rr.removeFromLeft (150)); rr.removeFromLeft (6); autoIter.setBounds (rr); }
    { auto rr = row (22); hq.setBounds (rr.removeFromLeft (150)); follow.setBounds (rr); }
    { auto rr = row (26); const int w = rr.getWidth() / 4; backBtn.setBounds (rr.removeFromLeft (w).reduced (2, 0)); fwdBtn.setBounds (rr.removeFromLeft (w).reduced (2, 0)); resetBtn.setBounds (rr.removeFromLeft (w).reduced (2, 0)); pngBtn.setBounds (rr.reduced (2, 0)); }
    side.removeFromTop (6);
    bookmarks.setBounds (row (24));
    { auto rr = row (24); addBm.setBounds (rr.removeFromLeft (rr.getWidth() / 2).reduced (2, 0)); delBm.setBounds (rr.reduced (2, 0)); }
    side.removeFromTop (6);
    paths.setBounds (row (24));
    { auto rr = row (24); const int w = rr.getWidth() / 3; recPath.setBounds (rr.removeFromLeft (w).reduced (2, 0)); replayPath.setBounds (rr.removeFromLeft (w).reduced (2, 0)); delPath.setBounds (rr.reduced (2, 0)); }
    renderPath.setBounds (row (24));
    genAll.setBounds (side.removeFromBottom (30));
    side.removeFromBottom (6);
    featureText.setBounds (side);
}
