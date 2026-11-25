#include "MixerPage.h"

// ============================================================================ ChannelStrip
ChannelStrip::ChannelStrip (AppState& s, juce::ValueTree n, Kind k, int busCount, std::function<void (juce::ValueTree)> editSlot)
    : node (n), kind (k), state (s)
{
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader.setRange (-60.0, 6.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.getValueObject().referTo (node.getPropertyAsValue (IDs::volume, &state.undo));
    fader.onDragStart = [this] { state.beginEdit ("Volume"); };
    fader.onValueChange = [this] { dbLabel.setText (fader.getValue() <= -59.9 ? "-inf" : juce::String (fader.getValue(), 1) + " dB", juce::dontSendNotification); };
    fader.onValueChange();
    if (kind == TrackStrip) fader.setColour (juce::Slider::trackColourId, juce::Colour ((juce::uint32) (juce::int64) node[IDs::colour]));
    addAndMakeVisible (fader);
    dbLabel.setFont (Theme::mono (10.5f));
    dbLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (dbLabel);
    pan.defaultValue = 0.0f;
    pan.bind (node, IDs::pan, &state.undo);
    addAndMakeVisible (pan);

    if (kind != MasterStrip)
    {
        mute.bind (node, IDs::mute, &state.undo);
        addAndMakeVisible (mute);
        if (kind == TrackStrip) { solo.bind (node, IDs::solo, &state.undo); addAndMakeVisible (solo); }
    }
    if (kind == TrackStrip)
    {
        auto buses = Model::buses (state.project);
        for (int i = 0; i < busCount && i < MixP::numSends; ++i)
        {
            auto* sk = sends.add (new ParamKnob (buses.getChild (i)[IDs::name].toString().substring (0, 8), 0, 1));
            sk->setColour (Theme::accent3);
            sk->bind (node, juce::Identifier ("send" + juce::String (i)), &state.undo);
            addAndMakeVisible (sk);
        }
        output.addItem ("-> Master", 1);
        for (int i = 0; i < busCount; ++i) output.addItem ("-> " + buses.getChild (i)[IDs::name].toString(), i + 2);
        output.setSelectedId ((int) node.getProperty (IDs::output, -1) + 2, juce::dontSendNotification);
        output.onChange = [this] { state.beginEdit ("Routing"); node.setProperty (IDs::output, output.getSelectedId() - 2, &state.undo); };
        addAndMakeVisible (output);
    }

    auto ins = node.getChildWithName (IDs::INSERTS);
    for (int i = 0; i < MixP::numInserts; ++i)
    {
        auto slot = ins.getChild (i);
        auto* cb = slotBoxes.add (new juce::ComboBox());
        cb->addItemList (FxP::typeNames(), 1);
        cb->setSelectedItemIndex ((int) slot.getProperty (IDs::fx, 0), juce::dontSendNotification);
        cb->onChange = [this, slot, cb, editSlot]() mutable
        {
            const int t = cb->getSelectedItemIndex();
            state.beginEdit ("Insert effect");
            slot.setProperty (IDs::fx, t, &state.undo);
            for (int p = 0; p < 4; ++p) slot.setProperty (juce::Identifier ("p" + juce::String (p)), FxP::defaultValue (t, p), &state.undo);
            if (editSlot) editSlot (slot);
        };
        addAndMakeVisible (cb);
        auto* eb = slotEdit.add (new juce::TextButton ("e"));
        eb->setTooltip ("Edit effect parameters");
        eb->onClick = [slot, editSlot] { if (editSlot) editSlot (slot); };
        addAndMakeVisible (eb);
    }
    ChannelState* ch = nullptr;
    if (kind == TrackStrip) { if (auto* tp = state.engine.getTrackProcessor (node[IDs::id])) ch = &tp->ch; }
    else if (kind == BusStrip) { if (auto* bp = state.engine.getBusProcessor (node.getParent().indexOf (node))) ch = &bp->ch; }
    else ch = &state.engine.getMasterChannel();
    if (ch != nullptr) meter.setSource (&ch->dispL, &ch->dispR);
    addAndMakeVisible (meter);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool sel = kind == TrackStrip && state.selectedTrack == node;
    g.setColour (kind == MasterStrip ? Theme::panelLight : (sel ? Theme::panelLight : Theme::panel));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    auto top = r.removeFromTop (24.0f);
    g.setColour (kind == TrackStrip ? juce::Colour ((juce::uint32) (juce::int64) node[IDs::colour]) : (kind == BusStrip ? Theme::accent3 : Theme::accent));
    g.fillRoundedRectangle (top.reduced (4.0f, 4.0f).removeFromTop (3.0f), 1.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::font (12.0f, true));
    g.drawText (kind == MasterStrip ? juce::String ("MASTER") : node[IDs::name].toString(), top.withTrimmedTop (6), juce::Justification::centred, true);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (9.5f));
    g.drawText ("INSERTS", getLocalBounds().withY (30).withHeight (12).reduced (8, 0), juce::Justification::centredLeft);
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    r.removeFromTop (38);
    for (int i = 0; i < slotBoxes.size(); ++i)
    {
        auto row = r.removeFromTop (22);
        slotEdit[i]->setBounds (row.removeFromRight (22));
        row.removeFromRight (2);
        slotBoxes[i]->setBounds (row);
        r.removeFromTop (3);
    }
    r.removeFromTop (4);
    if (! sends.isEmpty())
    {
        auto row = r.removeFromTop (56);
        const int w = row.getWidth() / juce::jmax (1, juce::jmin (2, sends.size()));
        for (int i = 0; i < sends.size(); ++i)
        {
            if (i == 2) { row = r.removeFromTop (56); }
            sends[i]->setBounds (row.removeFromLeft (w));
        }
    }
    if (output.isVisible()) { r.removeFromTop (2); output.setBounds (r.removeFromTop (22)); }
    r.removeFromTop (4);
    pan.setBounds (r.removeFromTop (54).withSizeKeepingCentre (54, 54));
    auto btns = r.removeFromBottom (24);
    if (mute.isVisible()) { mute.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (2, 0)); solo.setBounds (btns.reduced (2, 0)); }
    dbLabel.setBounds (r.removeFromBottom (16));
    auto faderArea = r.reduced (0, 4);
    meter.setBounds (faderArea.removeFromRight (12));
    fader.setBounds (faderArea);
}

void ChannelStrip::mouseDown (const juce::MouseEvent&)
{
    if (kind == TrackStrip) state.selectTrack (node);
}

// ============================================================================ SlotEditor
SlotEditor::SlotEditor (AppState& s) : state (s)
{
    for (int i = 0; i < 4; ++i) { auto* k = knobs.add (new ParamKnob ("p" + juce::String (i), 0, 1)); k->setColour (Theme::accent2); addAndMakeVisible (k); }
    addAndMakeVisible (bypass);
    setSlot ({}, {});
}

void SlotEditor::setSlot (juce::ValueTree s, const juce::String& ownerName)
{
    // knob labels depend on the effect type, so rebuild them
    slot = s; owner = ownerName;
    const int type = slot.isValid() ? (int) slot.getProperty (IDs::fx, 0) : 0;
    const auto labels = FxP::paramLabels (type);
    knobs.clear();
    for (int i = 0; i < 4; ++i)
    {
        auto* k = knobs.add (new ParamKnob (labels[i], 0, 1));
        k->defaultValue = FxP::defaultValue (type, i);
        k->setColour (Theme::accent2);
        if (slot.isValid() && type != FxP::None) k->bind (slot, juce::Identifier ("p" + juce::String (i)), &state.undo); else k->unbind();
        addAndMakeVisible (k);
    }
    if (slot.isValid()) bypass.bind (slot, IDs::bypass, &state.undo);
    bypass.setEnabled (slot.isValid() && type != FxP::None);
    resized();
    repaint();
}

void SlotEditor::paint (juce::Graphics& g)
{
    const int type = slot.isValid() ? (int) slot.getProperty (IDs::fx, 0) : 0;
    Theme::drawPanel (g, getLocalBounds().toFloat(), slot.isValid() ? "Insert: " + FxP::typeNames()[type] + "  on " + owner : juce::String ("Insert editor - click 'e' on a slot"));
}

void SlotEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 24);
    bypass.setBounds (r.removeFromRight (80).withSizeKeepingCentre (80, 26));
    for (auto* k : knobs) k->setBounds (r.removeFromLeft (80));
}

// ============================================================================ MixerPage
MixerPage::MixerPage (AppState& s) : state (s), slotEditor (s)
{
    viewport.setViewedComponent (&strips, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addAndMakeVisible (slotEditor);
    addBus.onClick = [this]
    {
        if (Model::buses (project).getNumChildren() >= MixP::numSends) { if (state.setStatus) state.setStatus ("Maximum of 4 buses"); return; }
        state.beginEdit ("Add bus");
        Model::addBus (project, "Bus " + juce::String (Model::buses (project).getNumChildren() + 1), &state.undo);
    };
    addAndMakeVisible (addBus);
}

MixerPage::~MixerPage() { if (project.isValid()) project.removeListener (this); }

void MixerPage::setProject (juce::ValueTree p)
{
    if (project.isValid()) project.removeListener (this);
    project = p;
    project.addListener (this);
    rebuild();
}

void MixerPage::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier& p)
{
    // deferred: the change may come from a control inside a strip that rebuild() would delete
    if (((t.hasType (IDs::TRACK) || t.hasType (IDs::BUS)) && p == IDs::name) || (t.hasType (IDs::SLOT) && p == IDs::fx) || (t.hasType (IDs::TRACK) && p == IDs::output))
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<MixerPage> (this)] { if (sp) sp->rebuild(); });
}

void MixerPage::rebuild()
{
    // engine processors are created by the snapshot builder; make sure they exist before meters bind
    state.engine.rebuildNow();
    juce::Component::SafePointer<MixerPage> sp (this);
    channelStrips.clear();
    const int busCount = Model::buses (project).getNumChildren();
    auto edit = [sp] (juce::ValueTree slot)
    {
        if (sp == nullptr) return;
        auto owner = slot.getParent().getParent();
        sp->slotEditor.setSlot (slot, owner.hasType (IDs::MASTER) ? juce::String ("Master") : owner[IDs::name].toString());
    };
    for (auto t : Model::tracks (project)) strips.addAndMakeVisible (channelStrips.add (new ChannelStrip (state, t, ChannelStrip::TrackStrip, busCount, edit)));
    for (auto b : Model::buses (project)) strips.addAndMakeVisible (channelStrips.add (new ChannelStrip (state, b, ChannelStrip::BusStrip, busCount, edit)));
    strips.addAndMakeVisible (channelStrips.add (new ChannelStrip (state, Model::master (project), ChannelStrip::MasterStrip, busCount, edit)));
    slotEditor.setSlot ({}, {});
    resized();
}

void MixerPage::paint (juce::Graphics& g) { g.fillAll (Theme::bg); }

void MixerPage::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto bottom = r.removeFromBottom (120);
    addBus.setBounds (bottom.removeFromRight (80).withSizeKeepingCentre (80, 30));
    bottom.removeFromRight (8);
    slotEditor.setBounds (bottom);
    r.removeFromBottom (8);
    viewport.setBounds (r);
    const int stripW = 132;
    strips.setSize (juce::jmax (r.getWidth(), stripW * channelStrips.size() + 8 * 2), r.getHeight() - 10);
    int x = 0;
    for (auto* s : channelStrips)
    {
        if (s->kind == ChannelStrip::BusStrip && x > 0 && channelStrips[channelStrips.indexOf (s) - 1]->kind == ChannelStrip::TrackStrip) x += 8;
        if (s->kind == ChannelStrip::MasterStrip) x += 8;
        s->setBounds (x, 0, stripW, strips.getHeight());
        x += stripW;
    }
}
