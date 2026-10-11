#include "LibraryPanel.h"
#include "analysis/Analysis.h"

namespace merge
{

enum Columns { ColStatus = 1, ColTitle, ColArtist, ColBpm, ColKey, ColTime, ColGenre, ColPlays };

LibraryPanel::LibraryPanel (AppContext& c) : ctx (c)
{
    sources.setModel (this);
    sources.setRowHeight (26);
    sources.setColour (juce::ListBox::backgroundColourId, theme::bg1);
    addAndMakeVisible (sources);

    table.setModel (this);
    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, theme::bg1);
    table.setMultipleSelectionEnabled (true);
    auto& h = table.getHeader();
    h.addColumn ("", ColStatus, 18, 18, 18, juce::TableHeaderComponent::notSortable);
    h.addColumn ("Title", ColTitle, 260, 80, 800);
    h.addColumn ("Artist", ColArtist, 170, 60, 500);
    h.addColumn ("BPM", ColBpm, 64, 50, 90);
    h.addColumn ("Key", ColKey, 52, 40, 80);
    h.addColumn ("Time", ColTime, 56, 44, 80);
    h.addColumn ("Genre", ColGenre, 110, 50, 300);
    h.addColumn ("Plays", ColPlays, 50, 40, 70);
    h.setStretchToFitActive (true);
    addAndMakeVisible (table);

    search.setTextToShowWhenEmpty ("Search title, artist, genre, key or BPM...", theme::textFaint);
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);
    compatible.setTooltip ("Only show tracks in a key that mixes with the master deck (Camelot)");
    compatible.onClick = [this] { rebuild(); };
    bpmMatch.setTooltip ("Only show tracks within 6% of the master tempo");
    bpmMatch.onClick = [this] { rebuild(); };
    addAndMakeVisible (compatible);
    addAndMakeVisible (bpmMatch);

    addFolder.onClick = [this]
    {
        chooser = std::make_shared<juce::FileChooser> ("Add a music folder", juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().isDirectory())
                                  {
                                      ctx.library->addFolder (fc.getResult());
                                      ctx.setStatus ("Scanning " + fc.getResult().getFullPathName() + "...");
                                  }
                              });
    };
    addCrate.onClick = [this]
    {
        auto* aw = new juce::AlertWindow ("New crate", {}, juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor ("name", "New crate");
        aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int r)
        {
            if (r == 1 && aw->getTextEditorContents ("name").isNotEmpty())
            {
                ctx.library->db().createCrate (aw->getTextEditorContents ("name"));
                rebuild();
            }
        }), true);
    };
    importXml.onClick = [this]
    {
        chooser = std::make_shared<juce::FileChooser> ("Import a Rekordbox collection (File > Export Collection in xml format)", juce::File(), "*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (! fc.getResult().existsAsFile()) return;
                                  auto r = ctx.library->importRekordbox (fc.getResult());
                                  ctx.setStatus ("Imported " + juce::String (r.tracks) + " tracks, " + juce::String (r.cues) + " hot cues and "
                                                 + juce::String (r.playlists) + " playlists from Rekordbox"
                                                 + (r.missing > 0 ? " (" + juce::String (r.missing) + " files not found)" : juce::String()));
                              });
    };
    for (auto* b : { &addFolder, &addCrate, &importXml })
        addAndMakeVisible (*b);
    info.setFont (theme::font (11.0f));
    info.setColour (juce::Label::textColourId, theme::textDim);
    addAndMakeVisible (info);

    ctx.library->addChangeListener (this);
    ctx.decks->addChangeListener (this);
    rebuild();
}

LibraryPanel::~LibraryPanel()
{
    ctx.library->removeChangeListener (this);
    ctx.decks->removeChangeListener (this);
}

juce::int64 LibraryPanel::currentCrate() const
{
    return currentSource >= 0 && currentSource < (int) sourceList.size() ? sourceList[(size_t) currentSource].crate : -1;
}

void LibraryPanel::rebuild()
{
    auto& db = ctx.library->db();
    sourceList.clear();
    sourceList.push_back ({ "All tracks", -1 });
    for (auto& c : db.crates())
        sourceList.push_back ({ c.name, c.id });
    currentSource = juce::jlimit (0, (int) sourceList.size() - 1, currentSource);
    sources.updateContent();
    sources.repaint();

    const auto crate = currentCrate();
    std::vector<TrackInfo> base;
    if (crate < 0)
        base = ctx.library->tracks();
    else
        for (auto id : db.crateTracks (crate))
            if (auto* t = ctx.library->find (id)) base.push_back (*t);

    // master deck reference for the filters
    const int m = ctx.dj.currentMasterDeck.load();
    MusicalKey masterKey;
    double masterBpm = 0.0;
    if (m >= 0 && ctx.decks->state (m).track != nullptr)
    {
        masterKey = ctx.decks->state (m).track->key;
        masterBpm = ctx.dj.deck (m).effectiveBpm.load();
    }

    const auto q = search.getText().trim().toLowerCase();
    rows.clear();
    for (auto& t : base)
    {
        if (q.isNotEmpty())
        {
            const auto hay = (t.title + " " + t.artist + " " + t.genre + " " + t.album + " "
                              + MusicalKey::fromIndex (t.key).toCamelot() + " " + MusicalKey::fromIndex (t.key).toString() + " "
                              + juce::String ((int) std::round (t.bpm))).toLowerCase();
            if (! hay.contains (q)) continue;
        }
        if (compatible.getToggleState() && masterKey.isValid() && ! analysis::keysCompatible (masterKey, MusicalKey::fromIndex (t.key)))
            continue;
        if (bpmMatch.getToggleState() && masterBpm > 0 && t.bpm > 0)
        {
            const double r = t.bpm / masterBpm;
            const bool near = std::abs (r - 1.0) < 0.06 || std::abs (r * 2.0 - 1.0) < 0.06 || std::abs (r * 0.5 - 1.0) < 0.06;
            if (! near) continue;
        }
        rows.push_back (t);
    }

    if (sortColumn > 0)
    {
        auto cmp = [this] (const TrackInfo& a, const TrackInfo& b)
        {
            switch (sortColumn)
            {
                case ColTitle: return a.title.compareIgnoreCase (b.title) < 0;
                case ColArtist: return a.artist.compareIgnoreCase (b.artist) < 0;
                case ColBpm: return a.bpm < b.bpm;
                case ColKey: return MusicalKey::fromIndex (a.key).toCamelot().compareNatural (MusicalKey::fromIndex (b.key).toCamelot()) < 0;
                case ColTime: return a.duration < b.duration;
                case ColGenre: return a.genre.compareIgnoreCase (b.genre) < 0;
                case ColPlays: return a.playCount < b.playCount;
                default: return false;
            }
        };
        std::stable_sort (rows.begin(), rows.end(), cmp);
        if (! sortForwards) std::reverse (rows.begin(), rows.end());
    }
    table.updateContent();
    table.repaint();
    const int pending = ctx.library->pendingAnalyses();
    info.setText (juce::String ((int) rows.size()) + " tracks" + (pending > 0 ? "  -  analysing " + juce::String (pending) + "..." : juce::String())
                      + (ctx.library->isScanning() ? "  -  scanning folders..." : juce::String()),
                  juce::dontSendNotification);
}

int LibraryPanel::getNumRows()
{
    return (int) rows.size();
}

void LibraryPanel::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    g.fillAll (selected ? theme::bg3 : (row % 2 ? theme::bg1 : theme::bg1.brighter (0.025f)));
}

void LibraryPanel::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (row >= (int) rows.size()) return;
    const auto& t = rows[(size_t) row];
    // highlight tracks that are loaded
    juce::Colour loaded;
    for (int d = 0; d < ctx.dj.numDecks.load(); ++d)
        if (ctx.decks->state (d).info.id == t.id && ctx.decks->state (d).track != nullptr)
            loaded = theme::deck (d);

    g.setFont (theme::font (12.5f, col == ColTitle));
    g.setColour (theme::text);
    juce::String text;
    switch (col)
    {
        case ColStatus:
            if (loaded != juce::Colour()) { g.setColour (loaded); g.fillEllipse (5.0f, h * 0.5f - 4.0f, 8.0f, 8.0f); }
            else if (! t.analyzed) { g.setColour (theme::warn.withAlpha (0.7f)); g.drawEllipse (5.0f, h * 0.5f - 4.0f, 8.0f, 8.0f, 1.5f); }
            return;
        case ColTitle: text = t.title; break;
        case ColArtist: text = t.artist; g.setColour (theme::textDim); break;
        case ColBpm: text = t.bpm > 0 ? juce::String (t.bpm, std::abs (t.bpm - std::round (t.bpm)) < 0.01 ? 0 : 1) : juce::String ("-"); break;
        case ColKey:
        {
            const auto k = MusicalKey::fromIndex (t.key);
            text = k.toCamelot();
            const int m = ctx.dj.currentMasterDeck.load();
            if (m >= 0 && ctx.decks->state (m).track != nullptr && k.isValid())
                g.setColour (analysis::keysCompatible (ctx.decks->state (m).track->key, k) ? theme::good : theme::textDim);
            break;
        }
        case ColTime: text = t.duration > 0 ? juce::String ((int) t.duration / 60) + ":" + juce::String ((int) t.duration % 60).paddedLeft ('0', 2) : ""; g.setColour (theme::textDim); break;
        case ColGenre: text = t.genre; g.setColour (theme::textDim); break;
        case ColPlays: text = t.playCount > 0 ? juce::String (t.playCount) : ""; g.setColour (theme::textDim); break;
        default: break;
    }
    g.drawText (text, 4, 0, w - 6, h, col == ColBpm || col == ColPlays ? juce::Justification::centredRight : juce::Justification::centredLeft);
}

void LibraryPanel::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (row < (int) rows.size())
        ctx.decks->loadTrack (ctx.decks->firstFreeDeck(), rows[(size_t) row].id);
}

void LibraryPanel::cellClicked (int row, int, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showTrackMenu (row);
}

void LibraryPanel::sortOrderChanged (int col, bool forwards)
{
    sortColumn = col;
    sortForwards = forwards;
    rebuild();
}

juce::var LibraryPanel::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    if (selected.isEmpty()) return {};
    const int row = selected[0];
    return row < (int) rows.size() ? juce::var ("track:" + juce::String (rows[(size_t) row].id)) : juce::var();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool)
{
    if (row >= (int) sourceList.size()) return;
    const bool sel = row == currentSource;
    g.fillAll (sel ? theme::bg3 : theme::bg1);
    if (sel)
    {
        g.setColour (theme::mix);
        g.fillRect (0, 4, 3, h - 8);
    }
    g.setColour (sel ? theme::text : theme::textDim);
    g.setFont (theme::font (12.5f, sel));
    g.drawText (sourceList[(size_t) row].name, 12, 0, w - 16, h, juce::Justification::centredLeft);
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) sourceList.size()) return;
    currentSource = row;
    rebuild();
    if (e.mods.isPopupMenu() && sourceList[(size_t) row].crate >= 0)
    {
        const auto crate = sourceList[(size_t) row].crate;
        juce::PopupMenu m;
        m.addItem (1, "Delete crate");
        showMenu (m, &sources, [this, crate] (int r)
        {
            if (r == 1)
            {
                ctx.library->db().deleteCrate (crate);
                currentSource = 0;
                rebuild();
            }
        });
    }
}

void LibraryPanel::showTrackMenu (int row)
{
    if (row >= (int) rows.size()) return;
    const auto t = rows[(size_t) row];
    juce::PopupMenu m;
    for (int d = 0; d < ctx.dj.numDecks.load(); ++d)
        m.addItem (1 + d, "Load to deck " + juce::String::charToString ((juce::juce_wchar) ('A' + d)));
    m.addSeparator();
    m.addItem (10, "Add to Automix queue");
    juce::PopupMenu crates;
    auto all = ctx.library->db().crates();
    for (size_t i = 0; i < all.size(); ++i)
        crates.addItem (100 + (int) i, all[i].name);
    m.addSubMenu ("Add to crate", crates);
    if (currentCrate() >= 0)
        m.addItem (11, "Remove from this crate");
    m.addSeparator();
    m.addItem (12, "Analyse again");
    m.addItem (13, "Split stems now");
    m.addItem (14, "Show in folder");
    m.addItem (15, "Remove from library");
    showMenu (m, &table, [this, t, all] (int r)
    {
        auto& db = ctx.library->db();
        if (r >= 1 && r <= 4) ctx.decks->loadTrack (r - 1, t.id);
        else if (r == 10) { db.addToCrate (ctx.library->automixCrate(), t.id); rebuild(); }
        else if (r == 11) { db.removeFromCrate (currentCrate(), t.id); rebuild(); }
        else if (r == 12) ctx.library->reanalyse (t.id);
        else if (r == 13 && onSplitStems) onSplitStems (t.id);
        else if (r == 14) t.file.revealToUser();
        else if (r == 15) { db.removeTrack (t.id); ctx.library->refresh(); }
        else if (r >= 100 && r - 100 < (int) all.size()) { db.addToCrate (all[(size_t) (r - 100)].id, t.id); rebuild(); }
    });
}

void LibraryPanel::filesDropped (const juce::StringArray& files, int, int)
{
    ctx.library->addFiles (files);
    if (currentCrate() >= 0)
        for (auto& f : files)
            if (auto t = ctx.library->db().findByPath (juce::File (f)))
                ctx.library->db().addToCrate (currentCrate(), t->id);
    rebuild();
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto left = r.removeFromLeft (190);
    auto lb = left.removeFromBottom (92);
    addFolder.setBounds (lb.removeFromTop (28));
    lb.removeFromTop (4);
    addCrate.setBounds (lb.removeFromTop (28));
    lb.removeFromTop (4);
    importXml.setBounds (lb.removeFromTop (28));
    left.removeFromBottom (6);
    sources.setBounds (left);
    r.removeFromLeft (8);
    auto top = r.removeFromTop (28);
    info.setBounds (top.removeFromRight (220));
    bpmMatch.setBounds (top.removeFromRight (90).reduced (2, 0));
    compatible.setBounds (top.removeFromRight (130).reduced (2, 0));
    top.removeFromRight (6);
    search.setBounds (top);
    r.removeFromTop (6);
    table.setBounds (r);
}

} // namespace merge
