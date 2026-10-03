#include "PresetManager.h"
#include "FactoryPresets.h"

namespace
{
    const juce::Identifier kPresetTag    { "INFINIVERBPreset" };
    const juce::Identifier kParamTag     { "PARAM" };
    const juce::Identifier kCurrentId    { "presetId" };
    constexpr const char*  kExtension    = ".infiniverb";

    // Bypass belongs to the session, Kill to the performance, and HotSwitch
    // selects between two settings rather than being one; Freeze is inert.
    // None of them is part of a sound.
    bool storedInPresets (const juce::String& id)
    {
        return id != "bypass" && id != "kill" && id != "hotswitch" && id != "freeze";
    }
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    loadFavourites();
    rescan();

    // A new instance opens on Everyday Space. A restored session overwrites
    // this with whatever it saved.
    if (! apvts.state.hasProperty (kCurrentId))
        load (0);
}

const juce::StringArray& PresetManager::categories()
{
    static const juce::StringArray list { "Halls", "Plates", "Ambient", "Swells", "Infinite", "Modulated", "Experimental" };
    return list;
}

juce::File PresetManager::userDirectory() const
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile ("Library/Audio/Presets/fatbird Studios/INFINIVERB");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("fatbird Studios/INFINIVERB/Presets");
   #endif
}

void PresetManager::rescan()
{
    presets.clear();

    // The factory bank, embedded from presets/factory/ and ordered by each
    // file's order attribute. Everyday Space comes first: it is what a new
    // instance opens on.
    std::vector<std::pair<int, Preset>> factory;
    for (int i = 0; i < FactoryPresets::namedResourceListSize; ++i)
    {
        int size = 0;
        const auto* data = FactoryPresets::getNamedResource (FactoryPresets::namedResourceList[i], size);
        auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size));
        if (xml == nullptr || ! xml->hasTagName (kPresetTag.toString()))
            continue;

        Preset p;
        p.name = xml->getStringAttribute ("name");
        p.category = xml->getStringAttribute ("category");
        p.factory = true;
        p.values = juce::ValueTree::fromXml (*xml);
        factory.emplace_back (xml->getIntAttribute ("order", 1000), p);
    }

    std::stable_sort (factory.begin(), factory.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [order, p] : factory)
        presets.push_back (p);

    std::vector<Preset> user;
    for (const auto& f : userDirectory().findChildFiles (juce::File::findFiles, true, juce::String ("*") + kExtension))
    {
        Preset p;
        p.name = f.getFileNameWithoutExtension();
        p.category = f.getParentDirectory() == userDirectory() ? juce::String() : f.getParentDirectory().getFileName();
        p.file = f;
        user.push_back (p);
    }

    std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b)
               { return a.name.compareNatural (b.name) < 0; });
    presets.insert (presets.end(), user.begin(), user.end());
}

int PresetManager::currentIndex() const
{
    const auto id = apvts.state.getProperty (kCurrentId).toString();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].id() == id)
            return (int) i;
    return -1;
}

juce::String PresetManager::currentLabel() const
{
    const int i = currentIndex();
    if (i < 0)
        return "-- CUSTOM";

    return juce::String (i + 1).paddedLeft ('0', 2) + " " + presets[(size_t) i].name.toUpperCase();
}

juce::ValueTree PresetManager::captureValues() const
{
    juce::ValueTree v (kPresetTag);
    for (auto* p : apvts.processor.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (storedInPresets (r->getParameterID()))
                v.appendChild (juce::ValueTree (kParamTag, { { "id", r->getParameterID() },
                                                             { "value", r->convertFrom0to1 (r->getValue()) } }), nullptr);
    return v;
}

void PresetManager::applyValues (const juce::ValueTree& v)
{
    for (auto* p : apvts.processor.getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (r == nullptr || ! storedInPresets (r->getParameterID()))
            continue;

        // A preset written before a parameter existed leaves it at its default,
        // and so does a value that is not a number: a damaged or crafted file
        // must not put one into the parameters and from there into the reverb.
        auto child = v.getChildWithProperty ("id", r->getParameterID());
        const float stored = child.isValid() ? (float) child.getProperty ("value") : 0.0f;
        const float normalised = child.isValid() && std::isfinite (stored) ? r->convertTo0to1 (stored)
                                                                          : r->getDefaultValue();
        r->beginChangeGesture();
        r->setValueNotifyingHost (normalised);
        r->endChangeGesture();
    }
}

void PresetManager::setCurrent (const Preset& p)
{
    apvts.state.setProperty (kCurrentId, p.id(), nullptr);
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;

    const auto& p = presets[(size_t) index];
    juce::ValueTree values = p.values;

    if (! p.factory)
    {
        auto xml = juce::XmlDocument::parse (p.file);
        if (xml == nullptr)
            return false;
        values = juce::ValueTree::fromXml (*xml);
        if (! values.hasType (kPresetTag))
            return false;
    }

    applyValues (values);
    setCurrent (p);
    return true;
}

void PresetManager::step (int delta)
{
    if (presets.empty())
        return;

    const int n = (int) presets.size();
    const int from = currentIndex();
    const int to = from < 0 ? (delta > 0 ? 0 : n - 1) : ((from + delta) % n + n) % n;
    load (to);
}

int PresetManager::saveUser (const juce::String& rawName, const juce::String& category)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return -1;

    auto dir = category.isEmpty() ? userDirectory() : userDirectory().getChildFile (category);
    if (! dir.createDirectory())
        return -1;

    auto values = captureValues();
    values.setProperty ("name", name, nullptr);
    values.setProperty ("category", category, nullptr);

    const auto file = dir.getChildFile (name + kExtension);
    if (auto xml = values.createXml(); xml == nullptr || ! xml->writeTo (file))
        return -1;

    rescan();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].file == file)
        {
            setCurrent (presets[i]);
            return (int) i;
        }
    return -1;
}

bool PresetManager::renameUser (int index, const juce::String& rawName)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].factory)
        return false;

    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    auto p = presets[(size_t) index];
    const bool wasCurrent = currentIndex() == index;
    const bool wasFavourite = isFavourite (p);
    const auto target = p.file.getSiblingFile (name + kExtension);

    if (target.exists() || ! p.file.moveFileTo (target))
        return false;

    if (wasFavourite) setFavourite (p, false);
    rescan();

    for (auto& q : presets)
        if (q.file == target)
        {
            if (wasFavourite) setFavourite (q, true);
            if (wasCurrent)   setCurrent (q);
        }
    return true;
}

bool PresetManager::deleteUser (int index)
{
    if (index < 0 || index >= (int) presets.size() || presets[(size_t) index].factory)
        return false;

    const auto p = presets[(size_t) index];
    if (! p.file.deleteFile())
        return false;

    setFavourite (p, false);
    rescan();
    return true;
}

bool PresetManager::isFavourite (const Preset& p) const { return favourites.contains (p.id()); }

void PresetManager::setFavourite (const Preset& p, bool fav)
{
    if (fav) favourites.addIfNotAlreadyThere (p.id());
    else     favourites.removeString (p.id());
    saveFavourites();
}

void PresetManager::loadFavourites()
{
    favourites.clear();
    if (auto xml = juce::XmlDocument::parse (userDirectory().getChildFile ("favourites.xml")))
        for (auto* e : xml->getChildIterator())
            favourites.add (e->getStringAttribute ("id"));
}

void PresetManager::saveFavourites() const
{
    juce::XmlElement xml ("Favourites");
    for (const auto& id : favourites)
        xml.createNewChildElement ("Preset")->setAttribute ("id", id);

    userDirectory().createDirectory();
    xml.writeTo (userDirectory().getChildFile ("favourites.xml"));
}
