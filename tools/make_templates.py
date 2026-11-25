#!/usr/bin/env python3
"""Generates the MANDELBROT project templates (LMMS .mpt XML) and the CI render tests.

Every track of a template has a fractal role (the <fractalgen> element read by
FractalGenerator), so a new project plays the moment you press play.

  python3 tools/make_templates.py            -> lmms/data/projects/templates/MANDELBROT - *.mpt
                                                ci/renders/*.mpt (same, plus a 16 bar length marker)
"""
import os
from xml.sax.saxutils import quoteattr

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ROLES = ["Off", "Kick", "Snare", "Clap", "Closed hat", "Open hat", "Rim", "Perc", "Low tom", "High tom",
         "Cymbal", "Shaker", "Bass", "Chords", "Pad", "Lead", "Arp", "Bells", "Texture", "Drone", "Break"]
STYLES = {"IDM": 0, "Ambient": 1, "Breakbeat": 2}


def attrs(d):
    return " ".join(f"{k}={quoteattr(str(v))}" for k, v in d.items())


def env(tag, amt=0.0, att=0.0, dec=0.5, sus=0.5, rel=0.1, hold=0.0):
    d = dict(pdel=0, att=att, hold=hold, dec=dec, sustain=sus, rel=rel, amt=amt, lshp=0, lpdel=0, latt=0,
             lspd=0.1, lamt=0, x100=0, ctlenvamt=0, userwavefile="", lspd_syncmode=0, lspd_numerator=4, lspd_denominator=4)
    return "<" + tag + " " + attrs(d) + "/>"


def eldata(vol=None, cut=None, fcut=14000, fres=0.5, ftype=0, fwet=0):
    vol = vol or {}
    cut = cut or {}
    return (f'<eldata {attrs(dict(fwet=fwet, ftype=ftype, fcut=fcut, fres=fres))}>'
            + env("elvol", **vol) + env("elcut", **cut) + env("elres") + "</eldata>")


def triple(oscs):
    """oscs: 3 dicts with wave, vol, coarse, finel, finer, (mod: modulation of next osc)"""
    a = {}
    for i, o in enumerate(oscs):
        a[f"vol{i}"] = o.get("vol", 33)
        a[f"pan{i}"] = 0
        a[f"coarse{i}"] = o.get("coarse", 0)
        a[f"finel{i}"] = o.get("finel", 0)
        a[f"finer{i}"] = o.get("finer", 0)
        a[f"phoffset{i}"] = 0
        a[f"stphdetun{i}"] = o.get("stereo", 0)
        a[f"wavetype{i}"] = o.get("wave", 0)
        a[f"userwavefile{i}"] = ""
        a[f"useWaveTable{i + 1}"] = 1
    a["modalgo1"] = oscs[0].get("mod", 2)
    a["modalgo2"] = oscs[1].get("mod", 2)
    a["modalgo3"] = 2
    return f'<instrument name="tripleoscillator"><tripleoscillator {attrs(a)}/></instrument>'


def afp(src, amp=100):
    d = dict(src=src, amp=amp, reversed=0, looped=0, sframe=0, eframe=1, lframe=0, stutter=0, interp=1)
    return '<instrument name="audiofileprocessor"><audiofileprocessor ' + attrs(d) + '/></instrument>'


def slicert(src, bpm, slices=16):
    a = dict(version=1, src=src, totalSlices=slices + 1, fadeOut=10, threshold=0.6, origBPM=bpm, syncEnable=1)
    for i in range(slices + 1):
        a[f"slice_{i}"] = round(i / slices, 6)
    return f'<instrument name="slicert"><slicert {attrs(a)}/></instrument>'


def reverb(wet=0.35, size=0.9, colour=8000):
    return (f'<effect name="reverbsc" on="1" wet="{wet}" autoquit="1" gate="0">'
            f'<ReverbSCControls input_gain="0" size="{size}" color="{colour}" output_gain="0"/><key/></effect>')


def delay(wet=0.3, time=0.375, fb=0.35):
    return (f'<effect name="delay" on="1" wet="{wet}" autoquit="1" gate="0">'
            f'<Delay DelayTimeSamples="{time}" FeebackAmount="{fb}" LfoFrequency="2" LfoAmount="0" OutGain="0"/><key/></effect>')


def cloud(wet=0.5, grain=140, density=12, pitch=0, spray=7, scatter=0.8, width=0.8, fractal=0.5, inkey=1, freeze=0):
    """MANDELBROT granular cloud (plugins/MandelCloud)"""
    a = attrs(dict(grain=grain, density=density, pitch=pitch, spray=spray, scatter=scatter, width=width,
                   fractal=fractal, gain=1, freeze=freeze, inkey=inkey))
    return f'<effect name="mandelcloud" on="1" wet="{wet}" autoquit="1" gate="0"><MandelCloudControls {a}/><key/></effect>'


def spectral(wet=0.45, decay=0.75, damp=0.5, blur=0.35, shift=0, fractal=0.5):
    """MANDELBROT phase-vocoder reverb / blur (plugins/MandelSpectral)"""
    a = attrs(dict(decay=decay, damp=damp, blur=blur, shift=shift, fractal=fractal, gain=1, freeze=0))
    return f'<effect name="mandelspectral" on="1" wet="{wet}" autoquit="1" gate="0"><MandelSpectralControls {a}/><key/></effect>'


def resonator(wet=0.35, decay=5, voices=4, octave=0, bright=0.5, drive=0.3, fractal=0.5):
    """MANDELBROT waveguide bank that follows the fractal's chords (plugins/MandelResonator)"""
    a = attrs(dict(decay=decay, voices=voices, octave=octave, root=9, bright=bright, drive=drive, fractal=fractal,
                   gain=1, follow=1))
    return f'<effect name="mandelresonator" on="1" wet="{wet}" autoquit="1" gate="0"><MandelResonatorControls {a}/><key/></effect>'


def fxchain(effects):
    if not effects:
        return '<fxchain numofeffects="0" enabled="0"/>'
    return f'<fxchain numofeffects="{len(effects)}" enabled="1">' + "".join(effects) + "</fxchain>"


def track(name, instrument, role, vol=100, pan=0, density=0.5, octave=0, variation=0.5, velocity=1.0, gate=1.0,
          eld=None, effects=None, basenote=57, length_clip=0):
    fg = attrs(dict(role=ROLES.index(role), density=density, octave=octave, variation=variation, velocity=velocity, gate=gate))
    clip = ""
    if length_clip:
        clip = f'<pattern {attrs(dict(type=0, muted=0, steps=16, name="length", pos=0, len=length_clip * 192))}/>'
    return (f'<track {attrs(dict(type=0, muted=0, solo=0, name=name))}>'
            f'<instrumenttrack {attrs(dict(vol=vol, pan=pan, mixch=0, usemasterpitch=1, pitchrange=1, pitch=0, basenote=basenote))}>'
            + instrument + (eld or eldata())
            + '<chordcreator chord="0" chordrange="1" chord-enabled="0"/>'
            + '<arpeggiator arptime="100" arprange="1" arpskip="0" arptime_denominator="4" arptime_syncmode="0" arpmode="0" arpcycle="0" arp-enabled="0" arp="0" arptime_numerator="4" arpdir="0" arpmiss="0" arpgate="100"/>'
            + '<midiport inputcontroller="0" fixedoutputvelocity="-1" inputchannel="0" outputcontroller="0" writable="0" outputchannel="1" fixedinputvelocity="-1" fixedoutputnote="-1" outputprogram="1" basevelocity="63" readable="0"/>'
            + fxchain(effects or [])
            + f"<fractalgen {fg}/>"
            + "</instrumenttrack>" + clip + "</track>")


# ------------------------------------------------------------------ sounds
def bass_sound():
    return triple([dict(wave=2, vol=55), dict(wave=3, vol=40, coarse=-12), dict(wave=0, vol=45, coarse=-12)]), \
        eldata(vol=dict(amt=1, att=0.002, dec=0.35, sus=0.55, rel=0.08), cut=dict(amt=0.35, dec=0.18, sus=0.0),
               fcut=700, fres=0.35, fwet=1)


def sub_sound():
    return triple([dict(wave=0, vol=80), dict(wave=1, vol=25, coarse=12), dict(wave=0, vol=0)]), \
        eldata(vol=dict(amt=1, att=0.003, dec=0.5, sus=0.85, rel=0.12), fcut=500, fres=0.2, fwet=1)


def keys_sound():
    return triple([dict(wave=1, vol=45), dict(wave=2, vol=22, finel=-7, finer=7), dict(wave=0, vol=25, coarse=12)]), \
        eldata(vol=dict(amt=1, att=0.008, dec=0.7, sus=0.45, rel=0.35), cut=dict(amt=0.2, dec=0.4, sus=0.3),
               fcut=2600, fres=0.3, fwet=1)


def lead_sound():
    return triple([dict(wave=2, vol=40, finel=-4, finer=4), dict(wave=3, vol=22, coarse=12), dict(wave=0, vol=20)]), \
        eldata(vol=dict(amt=1, att=0.006, dec=0.35, sus=0.65, rel=0.28), cut=dict(amt=0.25, dec=0.3, sus=0.4),
               fcut=3200, fres=0.25, fwet=1)


def pluck_sound():
    return triple([dict(wave=3, vol=40), dict(wave=1, vol=30, coarse=12), dict(wave=0, vol=0)]), \
        eldata(vol=dict(amt=1, att=0.001, dec=0.2, sus=0.0, rel=0.15), cut=dict(amt=0.45, dec=0.12, sus=0.0),
               fcut=1800, fres=0.4, fwet=1)


def pad_sound():
    return triple([dict(wave=2, vol=30, finel=-11, finer=9, stereo=40), dict(wave=2, vol=30, finel=8, finer=-12, stereo=60),
                   dict(wave=1, vol=30, coarse=-12)]), \
        eldata(vol=dict(amt=1, att=0.9, dec=1.0, sus=0.8, rel=1.6), cut=dict(amt=0.2, att=1.2, dec=1.5, sus=0.6),
               fcut=1500, fres=0.2, fwet=1)


def drone_sound():
    return triple([dict(wave=0, vol=50), dict(wave=2, vol=18, coarse=0, finel=-6, finer=6, stereo=90), dict(wave=1, vol=25, coarse=-12)]), \
        eldata(vol=dict(amt=1, att=1.6, dec=1.0, sus=1.0, rel=2.0), fcut=700, fres=0.15, fwet=1)


def bell_sound():
    # FM bell: osc 1 modulated by osc 2 (frequency modulation, inharmonic ratio)
    return triple([dict(wave=0, vol=60, mod=4), dict(wave=0, vol=45, coarse=19, finel=-14, finer=14), dict(wave=0, vol=0)]), \
        eldata(vol=dict(amt=1, att=0.001, dec=1.6, sus=0.0, rel=1.2))


def texture_sound():
    return triple([dict(wave=1, vol=35, finel=-3, finer=3, stereo=120), dict(wave=0, vol=30, coarse=12), dict(wave=6, vol=6)]), \
        eldata(vol=dict(amt=1, att=0.03, dec=0.4, sus=0.2, rel=0.6), fcut=4500, fres=0.2, fwet=1)


DRUMS = "drums-cc0/"


def project(style, tracks, bpm, root=9, scale=1, master=100):
    eng = attrs(dict(enabled=1, style=STYLES[style], root=root, scale=scale, fractalkey=0, drift=0.35 if style != "Ambient" else 0.25,
                     zoom=0.02, complexity=0.5 if style != "Ambient" else 0.35, fractalcontrol=0.6, swing=0 if style != "Breakbeat" else 0.05,
                     novelty=0.8, noveltymode=1, chordspeed=0, phrasebars=0))
    return ('<?xml version="1.0"?>\n<!DOCTYPE lmms-project>\n'
            '<lmms-project version="1.0" creator="LMMS" creatorversion="1.2.0" type="song">\n'
            f'  <head timesig_numerator="4" mastervol="{master}" timesig_denominator="4" bpm="{bpm}" masterpitch="0"/>\n'
            '  <song>\n'
            '    <trackcontainer width="600" x="5" y="5" maximized="0" height="300" visible="1" type="song" minimized="0">\n'
            + "\n".join("      " + t for t in tracks) + "\n"
            '    </trackcontainer>\n'
            '    <mixer width="543" x="5" y="310" maximized="0" height="335" visible="0" minimized="0">\n'
            '      <mixerchannel num="0" muted="0" volume="1" name="Master" soloed="0"><fxchain numofeffects="0" enabled="0"/></mixerchannel>\n'
            '    </mixer>\n'
            f'    <fractalengine {eng}/>\n'
            '  </song>\n</lmms-project>\n')


def idm(length=0):
    b, be = bass_sound(); k, ke = keys_sound(); l, le = lead_sound(); p, pe = pluck_sound()
    t = [
        track("Kick", afp(DRUMS + "kick-808-punch.flac"), "Kick", vol=95, length_clip=length),
        track("Snare", afp(DRUMS + "snare-studio.flac"), "Snare", vol=80),
        track("Clap", afp(DRUMS + "clap-808.flac"), "Clap", vol=65),
        track("Closed hat", afp(DRUMS + "hat-studio-closed.flac"), "Closed hat", vol=55, pan=12),
        track("Open hat", afp(DRUMS + "hat-studio-open.flac"), "Open hat", vol=45, pan=-10),
        track("Rim", afp(DRUMS + "rimshot-808.flac"), "Rim", vol=45, pan=20),
        track("Perc", afp(DRUMS + "conga-808-mid.flac"), "Perc", vol=50, pan=-25),
        track("Toms", afp(DRUMS + "tom-808-mid.flac"), "Low tom", vol=55),
        track("Crash", afp(DRUMS + "crash-studio.flac"), "Cymbal", vol=40),
        track("Bass", b, "Bass", vol=85, eld=be),
        track("Chords", k, "Chords", vol=55, eld=ke, effects=[reverb(0.35, 0.85)]),
        track("Lead", l, "Lead", vol=60, eld=le, effects=[delay(0.25, 0.375, 0.3), reverb(0.25, 0.8)]),
        track("Arp", p, "Arp", vol=40, pan=15, eld=pe, effects=[delay(0.3, 0.1875, 0.4), cloud(0.2, 60, 8, 12, 0, 0.3, 0.9)]),
    ]
    # master at 50 %: the first CI render of 13 full-level tracks clipped 4.5 % of samples
    return project("IDM", t, 128, master=50)


def ambient(length=0):
    b, be = sub_sound(); pa, pae = pad_sound(); d, de = drone_sound(); bl, ble = bell_sound()
    tx, txe = texture_sound(); l, le = lead_sound(); p, pe = pluck_sound()
    big = reverb(0.55, 0.97, 6000)
    t = [
        track("Drone", d, "Drone", vol=60, eld=de, effects=[resonator(0.3, 6, 4, 0, 0.4, 0.25), reverb(0.45, 0.95, 5000)],
              length_clip=length),
        track("Pad", pa, "Pad", vol=50, eld=pae, effects=[spectral(0.45, 0.8, 0.5, 0.4), big]),
        track("Low end", b, "Bass", vol=60, density=0.4, eld=be),
        track("Bells", bl, "Bells", vol=45, pan=-20, eld=ble,
              effects=[cloud(0.35, 220, 10, 0, 12, 1.5, 0.9), delay(0.35, 0.5, 0.45), big]),
        track("Texture", tx, "Texture", vol=40, pan=25, eld=txe,
              effects=[cloud(0.6, 320, 16, 0, 7, 2.5, 1.0), spectral(0.35, 0.7, 0.6, 0.6), big]),
        track("Melody", l, "Lead", vol=45, density=0.35, eld=le, effects=[delay(0.35, 0.75, 0.4), big]),
        track("Glass arp", p, "Arp", vol=30, density=0.35, eld=pe, effects=[delay(0.4, 0.5, 0.5), big]),
        track("Shaker", afp(DRUMS + "egg-shaker.flac"), "Shaker", vol=35, pan=30, effects=[reverb(0.4, 0.9)]),
        track("Frame drum", afp(DRUMS + "frame-drum-muted.flac"), "Perc", vol=45, effects=[reverb(0.4, 0.9)]),
        track("Soft kick", afp(DRUMS + "kick-studio-soft.flac"), "Kick", vol=55),
        track("Swell", afp(DRUMS + "cymbal-swell.flac"), "Cymbal", vol=30, effects=[big]),
    ]
    return project("Ambient", t, 76, root=2, scale=3)


def breakbeat(length=0):
    b, be = sub_sound(); k, ke = keys_sound(); l, le = lead_sound(); p, pe = pluck_sound()
    t = [
        track("Break", slicert("beats/jungle01 - 168 BPM.ogg", 168, 16), "Break", vol=90, density=0.6, length_clip=length),
        track("Kick", afp(DRUMS + "kick-808-deep.flac"), "Kick", vol=70),
        track("Sub", b, "Bass", vol=90, eld=be),
        track("Stabs", k, "Chords", vol=50, eld=ke, effects=[reverb(0.4, 0.88)]),
        track("Lead", l, "Lead", vol=55, density=0.4, eld=le, effects=[delay(0.3, 0.375, 0.35), reverb(0.3, 0.85)]),
        track("Arp", p, "Arp", vol=35, eld=pe, effects=[delay(0.3, 0.1875, 0.45)]),
    ]
    return project("Breakbeat", t, 170, root=9, scale=5, master=50)  # clipped 3.5 % at 100


def main():
    import sys
    # usage: make_templates.py [templates_dir renders_dir]
    tdir = sys.argv[1] if len(sys.argv) > 2 else os.path.join(ROOT, "lmms", "data", "projects", "templates")
    rdir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "ci", "renders")
    os.makedirs(tdir, exist_ok=True)
    os.makedirs(rdir, exist_ok=True)
    for name, fn in (("IDM", idm), ("Ambient", ambient), ("Breakbeat", breakbeat)):
        with open(os.path.join(tdir, f"MANDELBROT - {name}.mpt"), "w") as f:
            f.write(fn(0))
        with open(os.path.join(rdir, f"render_{name.lower()}.mpt"), "w") as f:
            f.write(fn(16))
        if name == "IDM":  # File > New starts the generative IDM setup
            with open(os.path.join(tdir, "default.mpt"), "w") as f:
                f.write(fn(0))
    print("templates written to", tdir, "and", rdir)


if __name__ == "__main__":
    main()
