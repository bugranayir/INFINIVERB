#include "PresetBrowser.h"
#include "Manual.h"
#include "Layout.h"

using namespace infiniverb;

namespace
{
    // The browser's palette: the panel's dark greys, with the display's phosphor
    // as the single accent (Figma "INFINIVERB / Preset Browser").
    const juce::Colour kBackground { 0xff1f1f1f };
    const juce::Colour kField      { 0xff151515 };
    const juce::Colour kDivider    { 0xff2e2e2e };
    const juce::Colour kOutline    { 0xff3a3a3a };
    const juce::Colour kText       { 0xffe8e8e8 };
    const juce::Colour kTextDim    { 0xff8a8a8a };
    const juce::Colour kHeading    { 0xff7c7c7c };
    const juce::Colour kSelected   { 0xff283130 };

    constexpr float kHeaderH = 48.0f, kColumnHeadH = 29.0f, kFooterH = 44.0f, kRowH = 26.0f;
    constexpr float kBankW = 170.0f, kCategoryW = 220.0f;

    // The system's interface face: JUCE's default sans is the system one on
    // macOS but Verdana on Windows, which is too wide for these columns.
    juce::FontOptions uiFace (float size)
    {
       #if JUCE_WINDOWS
        return juce::FontOptions ("Segoe UI", size, juce::Font::plain);
       #else
        return juce::FontOptions (size);
       #endif
    }

    juce::Font uiFont (float size) { return juce::Font (uiFace (size)); }

    juce::Font spaced (float size, float tracking)
    {
        return juce::Font (uiFace (size).withKerningFactor (tracking / size));
    }

    void drawStar (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, bool filled)
    {
        juce::Path star;
        star.addStar (c, 5, r * 0.45f, r, 0.0f);
        g.setColour (colour);
        if (filled) g.fillPath (star);
        else        g.strokePath (star, juce::PathStrokeType (1.0f));
    }

    // A flat text button in the browser's style.
    class FlatButton : public juce::Button
    {
    public:
        enum class Style { Text, Outline, Accent };

        FlatButton (const juce::String& text, Style s) : juce::Button (text), style (s) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool enabled = isEnabled();

            if (style == Style::Accent)
            {
                g.setColour (kPhosphor.withMultipliedAlpha (down ? 0.8f : (over ? 0.92f : 1.0f)));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (juce::Colour (0xff0b2620));
            }
            else
            {
                if (style == Style::Outline)
                {
                    g.setColour (kOutline);
                    g.drawRoundedRectangle (r, 3.0f, 1.0f);
                }
                g.setColour (! enabled ? kTextDim.withAlpha (0.6f) : (over ? juce::Colours::white : kText));
            }

            g.setFont (uiFont (12.5f));
            g.drawText (getButtonText(), r, juce::Justification::centred);
        }

    private:
        Style style;
    };
}

// ---- the browser ------------------------------------------------------------------

class PresetBrowserOverlay::Browser : public juce::Component,
                                      private juce::ListBoxModel
{
    // Behind the dialog's fields, in front of everything else; also swallows
    // clicks so the lists under the dialog cannot be used while it is up.
    struct Backdrop : juce::Component
    {
        std::function<void (juce::Graphics&)> draw;
        void paint (juce::Graphics& g) override { if (draw) draw (g); }
    };

public:
    Browser (PresetManager& pm, std::function<void()> closeFn, std::function<void()> changedFn)
        : presets (pm), onClose (std::move (closeFn)), onChanged (std::move (changedFn))
    {
        setWantsKeyboardFocus (true);

        search.setTextToShowWhenEmpty ("Search presets", kTextDim);
        search.setFont (uiFont (12.5f));
        search.setColour (juce::TextEditor::backgroundColourId, kField);
        search.setColour (juce::TextEditor::outlineColourId, kOutline);
        search.setColour (juce::TextEditor::focusedOutlineColourId, kPhosphor.withAlpha (0.6f));
        search.setColour (juce::TextEditor::textColourId, kText);
        search.setIndents (28, 7);
        search.onTextChange = [this] { refilter(); };
        addAndMakeVisible (search);

        for (auto* b : { &saveButton, &newButton, &renameButton, &deleteButton, &manualButton, &cancelButton, &loadButton })
            addAndMakeVisible (*b);

        saveButton.onClick   = [this] { save(); };
        newButton.onClick    = [this] { askName (Mode::New, {}); };
        renameButton.onClick = [this] { if (auto* p = selectedPreset()) askName (Mode::Rename, p->name); };
        deleteButton.onClick = [this] { if (selectedPreset() != nullptr) askName (Mode::Delete, {}); };
        cancelButton.onClick = [this] { onClose(); };
        loadButton.onClick   = [this] { loadSelected(); };
        manualButton.onClick = [] { openManual(); };

        list.setModel (this);
        list.setRowHeight ((int) kRowH);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, kPhosphor.withAlpha (0.7f));
        addAndMakeVisible (list);

        setSize ((int) kBrowserSize.x, (int) kBrowserSize.y);
    }

    void refresh()
    {
        presets.rescan();
        // Open where the current preset lives, with it selected.
        const int current = presets.currentIndex();
        if (current >= 0)
        {
            const auto& p = presets.all()[(size_t) current];
            selectedId = p.id();
            bank = p.factory ? 0 : 1;
            category = 0;
        }
        refilter();
    }

    // ---- painting -------------------------------------------------------------------

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (kBackground);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (kDivider);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        // Title: the product name in phosphor, then PRESETS.
        g.setFont (spaced (12.5f, 3.0f));
        g.setColour (kPhosphor);
        g.drawText ("INFINIVERB", juce::Rectangle<float> (20.0f, 0.0f, 120.0f, kHeaderH), juce::Justification::centredLeft);
        g.setColour (kText.withAlpha (0.85f));
        g.drawText ("PRESETS", juce::Rectangle<float> (115.0f, 0.0f, 100.0f, kHeaderH), juce::Justification::centredLeft);

        // Favourites filter.
        g.setColour (favouritesOnly ? kPhosphor.withAlpha (0.12f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (starBox, 3.0f);
        g.setColour (kOutline);
        g.drawRoundedRectangle (starBox.reduced (0.5f), 3.0f, 1.0f);
        drawStar (g, starBox.getCentre(), 6.5f, kPhosphor, true);

        // Close.
        g.setColour (closeOver ? juce::Colours::white : kText);
        const auto x = closeBox.reduced (8.0f);
        g.drawLine ({ x.getTopLeft(), x.getBottomRight() }, 1.6f);
        g.drawLine ({ x.getTopRight(), x.getBottomLeft() }, 1.6f);

        // Rules and column headings.
        g.setColour (kDivider);
        g.fillRect (0.0f, kHeaderH, r.getWidth(), 1.0f);
        g.fillRect (0.0f, kHeaderH + kColumnHeadH, r.getWidth(), 1.0f);
        g.fillRect (0.0f, r.getHeight() - kFooterH, r.getWidth(), 1.0f);
        g.fillRect (kBankW, kHeaderH, 1.0f, r.getHeight() - kHeaderH - kFooterH);
        g.fillRect (kBankW + kCategoryW, kHeaderH, 1.0f, r.getHeight() - kHeaderH - kFooterH);

        g.setFont (spaced (10.0f, 2.0f));
        g.setColour (kHeading);
        const float hy = kHeaderH, hh = kColumnHeadH;
        g.drawText ("BANK",     juce::Rectangle<float> (20.0f, hy, 140.0f, hh), juce::Justification::centredLeft);
        g.drawText ("CATEGORY", juce::Rectangle<float> (kBankW + 20.0f, hy, 180.0f, hh), juce::Justification::centredLeft);
        g.drawText ("PRESET",   juce::Rectangle<float> (kBankW + kCategoryW + 20.0f, hy, 200.0f, hh), juce::Justification::centredLeft);

        // Bank and category columns are short, fixed lists: drawn here.
        drawColumn (g, banks(), bank, kBankArea());
        drawColumn (g, categoryNames(), category, kCategoryArea());

        if (filtered.empty())
        {
            g.setFont (uiFont (12.5f));
            g.setColour (kTextDim);
            g.drawText (bank == 1 ? "No user presets yet. Use + New to save one." : "Nothing here.",
                        list.getBounds().toFloat().withHeight (kRowH * 2.0f), juce::Justification::centred);
        }
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // The search field's magnifier, over the field.
        const auto sb = search.getBounds().toFloat();
        g.setColour (kTextDim);
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ sb.getX() + 14.0f, sb.getCentreY() - 1.0f }), 1.3f);
        g.drawLine (sb.getX() + 17.5f, sb.getCentreY() + 2.5f, sb.getX() + 21.0f, sb.getCentreY() + 6.0f, 1.3f);

    }

    // Painted by the backdrop, which sits behind the dialog's own fields: drawn
    // over the children, the box covered its name field and buttons and the
    // dialog came up blank.
    void paintDialog (juce::Graphics& g)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        const auto box = dialogBox();
        g.setColour (kBackground.brighter (0.06f));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (kOutline);
        g.drawRoundedRectangle (box, 6.0f, 1.0f);

        g.setFont (uiFont (13.0f));
        g.setColour (kText);
        const auto* p = selectedPreset();
        const auto title = dialog == Mode::New    ? juce::String ("Save current settings as a new preset")
                         : dialog == Mode::Rename ? juce::String ("Rename preset")
                                                  : "Delete \"" + (p != nullptr ? p->name : juce::String()) + "\"?";
        g.drawText (title, box.reduced (16.0f, 12.0f).withHeight (20.0f), juce::Justification::centredLeft);
    }

    void resized() override
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        backdrop.setBounds (getLocalBounds());
        search.setBounds (juce::Rectangle<float> (250.0f, 10.0f, 299.0f, 28.0f).toNearestInt());
        starBox  = { 562.0f, 10.0f, 28.0f, 28.0f };
        saveButton.setBounds (juce::Rectangle<float> (600.0f, 10.0f, 72.0f, 28.0f).toNearestInt());
        closeBox = { w - 44.0f, 6.0f, 36.0f, 36.0f };

        list.setBounds (juce::Rectangle<float> (kBankW + kCategoryW + 8.0f, kHeaderH + kColumnHeadH + 4.0f,
                                                w - kBankW - kCategoryW - 12.0f,
                                                h - kHeaderH - kColumnHeadH - kFooterH - 8.0f).toNearestInt());

        const float fy = h - kFooterH + 8.0f;
        newButton   .setBounds (juce::Rectangle<float> ( 12.0f, fy, 60.0f, 28.0f).toNearestInt());
        renameButton.setBounds (juce::Rectangle<float> ( 76.0f, fy, 64.0f, 28.0f).toNearestInt());
        deleteButton.setBounds (juce::Rectangle<float> (144.0f, fy, 56.0f, 28.0f).toNearestInt());
        manualButton.setBounds (juce::Rectangle<float> ((w - 104.0f) * 0.5f, fy, 104.0f, 28.0f).toNearestInt());
        cancelButton.setBounds (juce::Rectangle<float> (w - 166.0f, fy, 70.0f, 28.0f).toNearestInt());
        loadButton  .setBounds (juce::Rectangle<float> (w - 92.0f, fy, 72.0f, 28.0f).toNearestInt());

        layoutDialog();
    }

    // ---- mouse and keys ---------------------------------------------------------------

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.position;

        if (dialog != Mode::None)
            return;

        if (closeBox.contains (pos))   { onClose(); return; }
        if (starBox.contains (pos))    { favouritesOnly = ! favouritesOnly; refilter(); return; }

        if (auto i = rowAt (kBankArea(), pos, (int) banks().size()); i >= 0)          { bank = i; refilter(); return; }
        if (auto i = rowAt (kCategoryArea(), pos, (int) categoryNames().size()); i >= 0) { category = i; refilter(); return; }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool over = closeBox.contains (e.position);
        if (over != closeOver) { closeOver = over; repaint(); }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (dialog != Mode::None) closeDialog();
            else onClose();
            return true;
        }
        if (key == juce::KeyPress::returnKey && dialog == Mode::None) { loadSelected(); return true; }
        return false;
    }

private:
    enum class Mode { None, New, Rename, Delete };

    // ---- the fixed columns --------------------------------------------------------------

    static const juce::StringArray& banks()
    {
        static const juce::StringArray b { "Factory", "User", "Favourites" };
        return b;
    }

    static juce::StringArray categoryNames()
    {
        juce::StringArray c { "All" };
        c.addArray (PresetManager::categories());
        return c;
    }

    static juce::Rectangle<float> kBankArea()     { return { 8.0f, kHeaderH + kColumnHeadH + 4.0f, kBankW - 16.0f, 260.0f }; }
    static juce::Rectangle<float> kCategoryArea() { return { kBankW + 8.0f, kHeaderH + kColumnHeadH + 4.0f, kCategoryW - 16.0f, 260.0f }; }

    static int rowAt (juce::Rectangle<float> area, juce::Point<float> p, int rows)
    {
        if (! area.contains (p)) return -1;
        const int i = (int) ((p.y - area.getY()) / kRowH);
        return i < rows ? i : -1;
    }

    void drawColumn (juce::Graphics& g, const juce::StringArray& items, int selected, juce::Rectangle<float> area)
    {
        g.setFont (uiFont (13.0f));
        for (int i = 0; i < items.size(); ++i)
        {
            const juce::Rectangle<float> row (area.getX(), area.getY() + kRowH * (float) i, area.getWidth(), kRowH);
            if (i == selected)
            {
                g.setColour (kSelected);
                g.fillRoundedRectangle (row.reduced (0.0f, 1.0f), 3.0f);
                g.setColour (kPhosphor);
                g.fillRect (row.getX(), row.getY() + 1.0f, 3.0f, row.getHeight() - 2.0f);
            }
            g.setColour (kText);
            g.drawText (items[i], row.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
        }
    }

    // ---- the preset list ---------------------------------------------------------------

    void refilter()
    {
        filtered.clear();
        const auto cats = categoryNames();
        const auto query = search.getText().trim();

        for (int i = 0; i < (int) presets.all().size(); ++i)
        {
            const auto& p = presets.all()[(size_t) i];
            const bool fav = presets.isFavourite (p);

            if (bank == 0 && ! p.factory) continue;
            if (bank == 1 && p.factory) continue;
            if (bank == 2 && ! fav) continue;
            if (category > 0 && p.category != cats[category]) continue;
            if (favouritesOnly && ! fav) continue;
            if (query.isNotEmpty() && ! p.name.containsIgnoreCase (query)) continue;
            filtered.push_back (i);
        }

        list.updateContent();
        int row = -1;
        for (int r = 0; r < (int) filtered.size(); ++r)
            if (presets.all()[(size_t) filtered[(size_t) r]].id() == selectedId)
                row = r;
        if (row >= 0) list.selectRow (row);
        else          list.deselectAllRows();

        updateButtons();
        repaint();
    }

    const PresetManager::Preset* selectedPreset() const
    {
        const int row = list.getSelectedRow();
        if (row < 0 || row >= (int) filtered.size()) return nullptr;
        return &presets.all()[(size_t) filtered[(size_t) row]];
    }

    int selectedIndex() const
    {
        const int row = list.getSelectedRow();
        return row < 0 || row >= (int) filtered.size() ? -1 : filtered[(size_t) row];
    }

    void updateButtons()
    {
        const auto* p = selectedPreset();
        const bool user = p != nullptr && ! p->factory;
        renameButton.setEnabled (user);
        deleteButton.setEnabled (user);
        loadButton.setEnabled (p != nullptr);
    }

    int getNumRows() override { return (int) filtered.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) filtered.size()) return;
        const int index = filtered[(size_t) row];
        const auto& p = presets.all()[(size_t) index];
        const juce::Rectangle<float> r (0.0f, 0.0f, (float) width, (float) height);

        if (selected)
        {
            g.setColour (kSelected);
            g.fillRoundedRectangle (r.reduced (0.0f, 1.0f), 3.0f);
            g.setColour (kPhosphor);
            g.fillRect (0.0f, 1.0f, 3.0f, r.getHeight() - 2.0f);
        }

        if (presets.isFavourite (p))
            drawStar (g, { 18.0f, r.getCentreY() }, 6.0f, kPhosphor, true);
        else
            drawStar (g, { 18.0f, r.getCentreY() }, 6.0f, kTextDim.withAlpha (0.35f), false);

        g.setFont (uiFont (13.0f));
        g.setColour (kText);
        g.drawText (p.name, r.withTrimmedLeft (32.0f).withTrimmedRight (40.0f), juce::Justification::centredLeft);

        g.setColour (kTextDim);
        g.drawText (juce::String (index + 1).paddedLeft ('0', 2), r.withTrimmedRight (14.0f), juce::Justification::centredRight);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= (int) filtered.size()) return;
        const auto& p = presets.all()[(size_t) filtered[(size_t) row]];
        selectedId = p.id();

        // The star column toggles the favourite.
        if (e.x < 30)
        {
            presets.setFavourite (p, ! presets.isFavourite (p));
            refilter();
            return;
        }
        updateButtons();
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { loadSelected(); }
    void selectedRowsChanged (int) override { updateButtons(); }
    void returnKeyPressed (int) override { loadSelected(); }

    // ---- actions --------------------------------------------------------------------------

    void loadSelected()
    {
        if (presets.load (selectedIndex()))
        {
            onChanged();
            onClose();
        }
    }

    void save()
    {
        // Save overwrites a selected user preset; anything else becomes a new one.
        const auto* p = selectedPreset();
        if (p != nullptr && ! p->factory)
        {
            const auto name = p->name, cat = p->category;
            if (presets.saveUser (name, cat) >= 0) { selectedId = "user/" + cat + "/" + name; onChanged(); refresh(); }
            return;
        }
        askName (Mode::New, {});
    }

    void askName (Mode m, const juce::String& initial)
    {
        dialog = m;
        nameField.setText (initial, false);

        categoryBox.clear (juce::dontSendNotification);
        categoryBox.addItem ("(none)", 1);
        int id = 2;
        for (const auto& c : PresetManager::categories())
            categoryBox.addItem (c, id++);
        categoryBox.setSelectedId (category > 0 ? category + 1 : 1, juce::dontSendNotification);

        backdrop.draw = [this] (juce::Graphics& g) { paintDialog (g); };
        addChildComponent (backdrop);
        backdrop.setVisible (true);
        backdrop.toFront (false);
        for (auto* c : std::initializer_list<juce::Component*> { &nameField, &categoryBox, &okButton, &dialogCancel })
        {
            addChildComponent (c);
            c->toFront (false);
        }

        nameField.setVisible (m != Mode::Delete);
        categoryBox.setVisible (m == Mode::New);
        okButton.setVisible (true);
        dialogCancel.setVisible (true);
        okButton.setButtonText (m == Mode::Delete ? "Delete" : (m == Mode::Rename ? "Rename" : "Save"));

        styleField (nameField);
        nameField.setTextToShowWhenEmpty ("Preset name", kTextDim);
        nameField.onReturnKey = [this] { confirmDialog(); };
        okButton.onClick = [this] { confirmDialog(); };
        dialogCancel.onClick = [this] { closeDialog(); };

        for (auto* c : { &search })
            c->setEnabled (false);

        layoutDialog();
        repaint();
        if (m != Mode::Delete) nameField.grabKeyboardFocus();
    }

    void confirmDialog()
    {
        const int index = selectedIndex();
        bool ok = false;

        if (dialog == Mode::New)
        {
            const int cat = categoryBox.getSelectedId() - 2;
            const auto catName = cat >= 0 ? PresetManager::categories()[cat] : juce::String();
            const int saved = presets.saveUser (nameField.getText(), catName);
            ok = saved >= 0;
            if (ok) { selectedId = presets.all()[(size_t) saved].id(); bank = 1; onChanged(); }
        }
        else if (dialog == Mode::Rename)
        {
            ok = presets.renameUser (index, nameField.getText());
            if (ok) onChanged();
        }
        else if (dialog == Mode::Delete)
        {
            ok = presets.deleteUser (index);
            if (ok) onChanged();
        }

        if (ok) { closeDialog(); refresh(); }
    }

    void closeDialog()
    {
        dialog = Mode::None;
        backdrop.setVisible (false);
        for (auto* c : std::initializer_list<juce::Component*> { &nameField, &categoryBox, &okButton, &dialogCancel })
            c->setVisible (false);
        search.setEnabled (true);
        grabKeyboardFocus();
        repaint();
    }

    juce::Rectangle<float> dialogBox() const
    {
        return juce::Rectangle<float> (360.0f, 150.0f).withCentre (getLocalBounds().toFloat().getCentre());
    }

    void layoutDialog()
    {
        const auto box = dialogBox().reduced (16.0f);
        nameField.setBounds (juce::Rectangle<float> (box.getX(), box.getY() + 32.0f, box.getWidth(), 28.0f).toNearestInt());
        categoryBox.setBounds (juce::Rectangle<float> (box.getX(), box.getY() + 66.0f, 170.0f, 26.0f).toNearestInt());
        okButton.setBounds (juce::Rectangle<float> (box.getRight() - 76.0f, box.getBottom() - 28.0f, 76.0f, 28.0f).toNearestInt());
        dialogCancel.setBounds (juce::Rectangle<float> (box.getRight() - 160.0f, box.getBottom() - 28.0f, 76.0f, 28.0f).toNearestInt());
    }

    static void styleField (juce::TextEditor& t)
    {
        t.setFont (uiFont (13.0f));
        t.setColour (juce::TextEditor::backgroundColourId, kField);
        t.setColour (juce::TextEditor::outlineColourId, kOutline);
        t.setColour (juce::TextEditor::focusedOutlineColourId, kPhosphor.withAlpha (0.6f));
        t.setColour (juce::TextEditor::textColourId, kText);
        t.setIndents (8, 7);
    }

    PresetManager& presets;
    std::function<void()> onClose, onChanged;

    juce::TextEditor search;
    juce::ListBox list;
    FlatButton saveButton   { "Save",   FlatButton::Style::Outline },
               newButton    { "+ New",  FlatButton::Style::Text },
               renameButton { "Rename", FlatButton::Style::Text },
               deleteButton { "Delete", FlatButton::Style::Text },
               manualButton { "View Manual", FlatButton::Style::Outline },
               cancelButton { "Cancel", FlatButton::Style::Text },
               loadButton   { "Load",   FlatButton::Style::Accent },
               okButton     { "Save",   FlatButton::Style::Accent },
               dialogCancel { "Cancel", FlatButton::Style::Text };
    juce::TextEditor nameField;
    juce::ComboBox categoryBox;
    Backdrop backdrop;

    juce::Rectangle<float> starBox, closeBox;
    bool closeOver = false, favouritesOnly = false;
    int bank = 0, category = 0;
    juce::String selectedId;
    std::vector<int> filtered;
    Mode dialog = Mode::None;
};

// ---- the overlay ---------------------------------------------------------------------------

PresetBrowserOverlay::PresetBrowserOverlay (PresetManager& pm)
{
    browser = std::make_unique<Browser> (pm,
                                         [this] { close(); },
                                         [this] { if (onPresetChanged) onPresetChanged(); });
    addAndMakeVisible (*browser);
    setVisible (false);
}

PresetBrowserOverlay::~PresetBrowserOverlay() = default;

void PresetBrowserOverlay::open()
{
    browser->refresh();
    setVisible (true);
    toFront (true);
    browser->grabKeyboardFocus();
}

void PresetBrowserOverlay::close() { setVisible (false); }

void PresetBrowserOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
}

void PresetBrowserOverlay::resized()
{
    browser->setCentrePosition (getLocalBounds().getCentre());
}

void PresetBrowserOverlay::mouseDown (const juce::MouseEvent& e)
{
    // A click outside the browser closes it.
    if (! browser->getBounds().contains (e.getPosition()))
        close();
}
