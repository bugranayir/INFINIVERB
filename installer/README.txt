INFINIVERB — beta test build
============================

Thank you for testing INFINIVERB, an algorithmic reverb from fatbird Studios. This is
a test build: the sound is finished, the interface is a working version, and
some features are not in it yet.

System:   macOS 11 or later on Apple Silicon, macOS 10.13 or later on Intel.
Formats:  VST3 (Ableton Live, Cubase, Studio One, Reaper, Bitwig, FL Studio and
          most other hosts) and Audio Unit (Logic Pro, GarageBand, MainStage).
Installs: /Library/Audio/Plug-Ins/VST3/INFINIVERB.vst3
          /Library/Audio/Plug-Ins/Components/INFINIVERB.component
          Choose Customize in the installer to leave either one out.
          Rescan plug-ins in your DAW after installing if INFINIVERB does not appear.
Removes:  delete those files. Your own presets are kept in
          ~/Library/Audio/Presets/fatbird Studios/INFINIVERB.


THE CONTROLS
------------

GRAVITY     The big dial. At the centre: the shortest, densest reverb.
            Turn right for longer and longer decays, up to about a minute.
            Turn left for reverse: the sound swells in and then falls away,
            more slowly and later the further you go.
SIZE        How big the space is.
FEEDBACK    Sends the whole reverb back into itself, with the predelay inside
            the loop — long predelays make rhythmic, re-swelling repeats.
MIX         Dry to wet.
INPUT       Input level, +/-12 dB.
PREDELAY    Time before the reverb starts, up to two seconds.
DEPTH/RATE  Movement in the tail.
LO / HI     How much low and high end the tail keeps. Centre is flat.
RESONANCE   A resonant peak at the Lo and Hi corners; only does something
            when Lo or Hi is away from the centre.
LEVEL       Output level, +/-12 dB.
KILL        Stops sound entering the reverb; the tail keeps ringing.
BYPASS      Bypasses the plug-in; the bypass indicator lights.
HOT SWITCH  Two complete settings, A and B. Switching keeps what you left and
            recalls the other. If those knobs are automated, your DAW treats
            the switch like you turning them and suspends their automation
            until you re-enable it.

Double-click any knob to return it to its default.

The display shows the preset, Gravity, the decay time and the shape of the
reverb your settings produce. Click it to open the preset browser, where you
can save your own presets. Preset down / up step through them.

The factory bank opens on Everyday Space and has three presets in each
category; your own presets sit beside them in the browser.

With Feedback up and a long Gravity setting the reverb can sustain
indefinitely. Its output is held under 0 dBFS, but keep your monitoring level
moderate when you explore the extremes.


YOUR FEEDBACK
-------------

Please send your impressions — what you liked, what confused you, anything
that sounded or behaved wrong, with the settings you were using — to the
person who gave you this build.


SOURCE CODE AND LICENCE
-----------------------

INFINIVERB is free software, licensed under the GNU Affero General Public License,
version 3 (shown on the next page). The complete corresponding source for this
build is provided alongside this installer as INFINIVERB-<version>-source.tar.gz. If
you did not receive it, ask the person who gave you this build and it will be
provided.

The names INFINIVERB, RVB1 and fatbird Studios and the fatbird Studios branding are not
covered by that licence; see the notices below.
