#include "ui_model.h"
#include <QDir>
#include <QFileInfo>

namespace {
QStringList &detectedStoresStorage()
{
    static QStringList stores;
    return stores;
}
QStringList &unknownRomsStorage()
{
    static QStringList paths;
    return paths;
}
}

void setKadiaDetectedStores(const QStringList &stores)
{
    QStringList clean = stores;
    clean.removeDuplicates();
    clean.sort(Qt::CaseInsensitive);
    detectedStoresStorage() = clean;
}

QStringList kadiaDetectedStores()
{
    return detectedStoresStorage();
}

void setKadiaUnknownRoms(const QStringList &paths)
{
    QStringList clean = paths;
    clean.removeDuplicates();
    clean.sort(Qt::CaseInsensitive);
    unknownRomsStorage() = clean;
}


const QVector<KadiaSectionInfo> &kadiaSections()
{
    static const QVector<KadiaSectionInfo> baseData = {
        { QStringLiteral("Home"), QStringLiteral("Games, media and entertainment"), {
            { QStringLiteral("Continue"), QStringLiteral(""), QStringLiteral("Continue"), QStringLiteral("Resume something you recently played or watched.") },
            { QStringLiteral("All Games"), QStringLiteral(""), QStringLiteral("All Games"), QStringLiteral("Browse the complete game library across every configured system.") },
            { QStringLiteral("Live TV"), QStringLiteral(""), QStringLiteral("Live TV"), QStringLiteral("Open live television when a compatible tuner or source is configured.") },
            { QStringLiteral("Music"), QStringLiteral(""), QStringLiteral("Music"), QStringLiteral("Jump directly into the music library.") },
            { QStringLiteral("Favorites"), QStringLiteral(""), QStringLiteral("Favorites"), QStringLiteral("Games and media you have pinned for quick access.") },
            { QStringLiteral("Search"), QStringLiteral(""), QStringLiteral("Search"), QStringLiteral("Search across games, music, videos, pictures and recordings.") },
        } },
        { QStringLiteral("Games"), QStringLiteral("Browse and launch"), {
            { QStringLiteral("Systems"), QStringLiteral(""), QStringLiteral("Systems"), QStringLiteral("Browse games by emulated system.") },
            { QStringLiteral("All Games"), QStringLiteral(""), QStringLiteral("All Games"), QStringLiteral("Every game from all visible systems and collections.") },
            { QStringLiteral("Search"), QStringLiteral(""), QStringLiteral("Search Games"), QStringLiteral("Find a title by name.") },
            { QStringLiteral("Random Game"), QStringLiteral(""), QStringLiteral("Random Game"), QStringLiteral("Choose a random title from the current library.") },
            { QStringLiteral("Filter"), QStringLiteral(""), QStringLiteral("Filter Games"), QStringLiteral("Filter the current list using available metadata.") },
            { QStringLiteral("Sort"), QStringLiteral(""), QStringLiteral("Sort Games"), QStringLiteral("Change the ordering of the current gamelist.") },
            { QStringLiteral("Jump A-Z"), QStringLiteral(""), QStringLiteral("Jump to Letter"), QStringLiteral("Jump directly to titles beginning with a selected letter.") },
            { QStringLiteral("View Style"), QStringLiteral(""), QStringLiteral("View Style"), QStringLiteral("Change the gamelist presentation for the current theme.") },
            { QStringLiteral("Game Options"), QStringLiteral(""), QStringLiteral("Game Options"), QStringLiteral("Open per-game actions, metadata and advanced emulator options.") },
        } },
        { QStringLiteral("Consoles"), QStringLiteral("Home systems"), {
            { QStringLiteral("Nintendo"), QStringLiteral(""), QStringLiteral("Nintendo"), QStringLiteral("Nintendo home-console libraries.") },
            { QStringLiteral("Super Nintendo"), QStringLiteral(""), QStringLiteral("Super Nintendo"), QStringLiteral("16-bit Super Nintendo library.") },
            { QStringLiteral("Sega"), QStringLiteral(""), QStringLiteral("Sega"), QStringLiteral("Master System, Genesis / Mega Drive, Saturn and Dreamcast.") },
            { QStringLiteral("PlayStation"), QStringLiteral(""), QStringLiteral("PlayStation"), QStringLiteral("PlayStation family libraries.") },
            { QStringLiteral("Xbox"), QStringLiteral(""), QStringLiteral("Xbox"), QStringLiteral("Xbox-family libraries when configured.") },
            { QStringLiteral("Atari"), QStringLiteral(""), QStringLiteral("Atari"), QStringLiteral("Atari home-console collections.") },
            { QStringLiteral("NEC"), QStringLiteral(""), QStringLiteral("NEC"), QStringLiteral("PC Engine / TurboGrafx and related systems.") },
        } },
        { QStringLiteral("Handhelds"), QStringLiteral("Portable systems"), {
            { QStringLiteral("Game Boy"), QStringLiteral(""), QStringLiteral("Game Boy"), QStringLiteral("Nintendo Game Boy library.") },
            { QStringLiteral("Game Boy Color"), QStringLiteral(""), QStringLiteral("Game Boy Color"), QStringLiteral("Game Boy Color collection.") },
            { QStringLiteral("Game Boy Advance"), QStringLiteral(""), QStringLiteral("Game Boy Advance"), QStringLiteral("32-bit Nintendo handheld collection.") },
            { QStringLiteral("Nintendo DS"), QStringLiteral(""), QStringLiteral("Nintendo DS"), QStringLiteral("Dual-screen Nintendo library.") },
            { QStringLiteral("Nintendo 3DS"), QStringLiteral(""), QStringLiteral("Nintendo 3DS"), QStringLiteral("Nintendo 3DS collection when configured.") },
            { QStringLiteral("PSP"), QStringLiteral(""), QStringLiteral("PSP"), QStringLiteral("PlayStation Portable library.") },
            { QStringLiteral("PS Vita"), QStringLiteral(""), QStringLiteral("PS Vita"), QStringLiteral("PlayStation Vita library when configured.") },
            { QStringLiteral("Game Gear"), QStringLiteral(""), QStringLiteral("Game Gear"), QStringLiteral("Sega Game Gear collection.") },
            { QStringLiteral("Atari Lynx"), QStringLiteral(""), QStringLiteral("Atari Lynx"), QStringLiteral("Atari Lynx handheld collection.") },
        } },
        { QStringLiteral("Arcade"), QStringLiteral("Arcade collections"), {
            { QStringLiteral("Arcade"), QStringLiteral(""), QStringLiteral("Arcade"), QStringLiteral("All games identified as arcade titles.") },
            { QStringLiteral("MAME"), QStringLiteral(""), QStringLiteral("MAME"), QStringLiteral("MAME-based arcade library.") },
            { QStringLiteral("FBNeo"), QStringLiteral(""), QStringLiteral("FinalBurn Neo"), QStringLiteral("FinalBurn Neo arcade library.") },
            { QStringLiteral("Vertical"), QStringLiteral(""), QStringLiteral("Vertical Games"), QStringLiteral("Automatic collection of vertically oriented games.") },
            { QStringLiteral("Lightgun"), QStringLiteral(""), QStringLiteral("Lightgun Games"), QStringLiteral("Automatic collection of lightgun-compatible titles.") },
            { QStringLiteral("2 Players"), QStringLiteral(""), QStringLiteral("2 Players"), QStringLiteral("Automatic collection of two-player games.") },
            { QStringLiteral("4 Players"), QStringLiteral(""), QStringLiteral("4 Players"), QStringLiteral("Automatic collection of four-player games.") },
        } },
        { QStringLiteral("PC Games"), QStringLiteral("Windows and storefronts"), {
            { QStringLiteral("Windows"), QStringLiteral(""), QStringLiteral("Windows Games"), QStringLiteral("Native Windows games and shortcuts.") },
            { QStringLiteral("Steam"), QStringLiteral(""), QStringLiteral("Steam"), QStringLiteral("Scanned Steam library.") },
            { QStringLiteral("Epic"), QStringLiteral(""), QStringLiteral("Epic Games"), QStringLiteral("Games detected from Epic Games.") },
            { QStringLiteral("GOG"), QStringLiteral(""), QStringLiteral("GOG"), QStringLiteral("Games detected from GOG.") },
            { QStringLiteral("EA"), QStringLiteral(""), QStringLiteral("EA Games"), QStringLiteral("Games detected from the EA app.") },
            { QStringLiteral("Amazon"), QStringLiteral(""), QStringLiteral("Amazon Games"), QStringLiteral("Games detected from Amazon Games.") },
            { QStringLiteral("Ubisoft"), QStringLiteral(""), QStringLiteral("Ubisoft Connect"), QStringLiteral("Games detected from Ubisoft Connect.") },
            { QStringLiteral("Battle.net"), QStringLiteral(""), QStringLiteral("Battle.net"), QStringLiteral("Games detected from Battle.net.") },
        } },
        { QStringLiteral("Collections"), QStringLiteral("Automatic and custom collections"), {
            { QStringLiteral("All Games"), QStringLiteral(""), QStringLiteral("All Games"), QStringLiteral("Automatic collection containing every visible game.") },
            { QStringLiteral("Last Played"), QStringLiteral(""), QStringLiteral("Last Played"), QStringLiteral("Games ordered by recent activity.") },
            { QStringLiteral("Favorites"), QStringLiteral(""), QStringLiteral("Favorites"), QStringLiteral("Titles marked as favorites.") },
            { QStringLiteral("2 Players"), QStringLiteral(""), QStringLiteral("2 Players"), QStringLiteral("Games identified as supporting two players.") },
            { QStringLiteral("4 Players"), QStringLiteral(""), QStringLiteral("4 Players"), QStringLiteral("Games identified as supporting four players.") },
            { QStringLiteral("Never Played"), QStringLiteral(""), QStringLiteral("Never Played"), QStringLiteral("Games with no recorded play activity.") },
            { QStringLiteral("RetroAchievements"), QStringLiteral(""), QStringLiteral("RetroAchievements"), QStringLiteral("Games with RetroAchievements support.") },
            { QStringLiteral("Arcade"), QStringLiteral(""), QStringLiteral("Arcade"), QStringLiteral("Automatic arcade collection.") },
            { QStringLiteral("Vertical"), QStringLiteral(""), QStringLiteral("Vertical Games"), QStringLiteral("Automatic vertical-screen collection.") },
            { QStringLiteral("Lightgun"), QStringLiteral(""), QStringLiteral("Lightgun Games"), QStringLiteral("Automatic lightgun collection.") },
            { QStringLiteral("Custom"), QStringLiteral(""), QStringLiteral("Custom Collections"), QStringLiteral("Editable and dynamic collections created by the user.") },
        } },
        { QStringLiteral("Unknowns"), QStringLiteral("ROM images awaiting identification"), {
        } },
        { QStringLiteral("Recent"), QStringLiteral("Jump back in"), {
            { QStringLiteral("Aero Quest"), QStringLiteral(""), QStringLiteral("Aero Quest"), QStringLiteral("Played 18 minutes ago.") },
            { QStringLiteral("Moon Harbor"), QStringLiteral(""), QStringLiteral("Moon Harbor"), QStringLiteral("Played yesterday.") },
            { QStringLiteral("Velvet Racer"), QStringLiteral(""), QStringLiteral("Velvet Racer"), QStringLiteral("Played 3 days ago.") },
            { QStringLiteral("Crystal Circuit"), QStringLiteral(""), QStringLiteral("Crystal Circuit"), QStringLiteral("Played last week.") },
            { QStringLiteral("Last Played"), QStringLiteral(""), QStringLiteral("Last Played"), QStringLiteral("Open the complete automatic Last Played collection.") },
        } },
        { QStringLiteral("Favorites"), QStringLiteral("Pinned games and media"), {
            { QStringLiteral("Aero Quest"), QStringLiteral(""), QStringLiteral("Aero Quest"), QStringLiteral("Favorite - Super Nintendo") },
            { QStringLiteral("Solar Garden"), QStringLiteral(""), QStringLiteral("Solar Garden"), QStringLiteral("Favorite - Game Boy Advance") },
            { QStringLiteral("Night Signal"), QStringLiteral(""), QStringLiteral("Night Signal"), QStringLiteral("Favorite - PlayStation") },
            { QStringLiteral("Crystal Circuit"), QStringLiteral(""), QStringLiteral("Crystal Circuit"), QStringLiteral("Favorite - Genesis") },
            { QStringLiteral("All Favorites"), QStringLiteral(""), QStringLiteral("All Favorites"), QStringLiteral("Open the full Favorites collection.") },
        } },
        { QStringLiteral("Achievements"), QStringLiteral("RetroAchievements"), {
            { QStringLiteral("RetroAchievements"), QStringLiteral(""), QStringLiteral("RetroAchievements"), QStringLiteral("Enable and configure RetroAchievements integration.") },
            { QStringLiteral("Supported Games"), QStringLiteral(""), QStringLiteral("Supported Games"), QStringLiteral("Browse titles in the RetroAchievements automatic collection.") },
            { QStringLiteral("Progress"), QStringLiteral(""), QStringLiteral("Achievement Progress"), QStringLiteral("View achievement progress associated with your library.") },
            { QStringLiteral("Account"), QStringLiteral(""), QStringLiteral("Account"), QStringLiteral("Manage RetroAchievements account integration.") },
        } },
        { QStringLiteral("TV + Movies"), QStringLiteral("Watch television and movies"), {
            { QStringLiteral("Live TV"), QStringLiteral(""), QStringLiteral("Live TV"), QStringLiteral("Watch the currently tuned television source.") },
            { QStringLiteral("Guide"), QStringLiteral(""), QStringLiteral("Guide"), QStringLiteral("Browse television listings.") },
            { QStringLiteral("Recorded TV"), QStringLiteral(""), QStringLiteral("Recorded TV"), QStringLiteral("Browse television recordings.") },
            { QStringLiteral("Movies"), QStringLiteral(""), QStringLiteral("Movies"), QStringLiteral("Browse movies available to the media library.") },
            { QStringLiteral("Play DVD"), QStringLiteral(""), QStringLiteral("Play DVD"), QStringLiteral("Play an inserted DVD or compatible optical-video disc.") },
            { QStringLiteral("Search"), QStringLiteral(""), QStringLiteral("TV + Movies Search"), QStringLiteral("Search television, recordings and movies.") },
            { QStringLiteral("Set Up TV"), QStringLiteral(""), QStringLiteral("Set Up TV"), QStringLiteral("Configure television sources and tuner settings.") },
        } },
        { QStringLiteral("Music"), QStringLiteral("Music library"), {
            { QStringLiteral("Music Library"), QStringLiteral(""), QStringLiteral("Music Library"), QStringLiteral("Browse albums, artists, songs and genres.") },
            { QStringLiteral("Play All"), QStringLiteral(""), QStringLiteral("Play All"), QStringLiteral("Begin playing the music library.") },
            { QStringLiteral("Playlists"), QStringLiteral(""), QStringLiteral("Playlists"), QStringLiteral("Browse saved playlists.") },
            { QStringLiteral("Radio"), QStringLiteral(""), QStringLiteral("Radio"), QStringLiteral("Open available radio sources.") },
            { QStringLiteral("Search"), QStringLiteral(""), QStringLiteral("Search Music"), QStringLiteral("Search the music library.") },
            { QStringLiteral("Now Playing"), QStringLiteral(""), QStringLiteral("Now Playing"), QStringLiteral("Return to the current music session.") },
        } },
        { QStringLiteral("Pictures + Videos"), QStringLiteral("Photos, slideshows and video"), {
            { QStringLiteral("Picture Library"), QStringLiteral(""), QStringLiteral("Picture Library"), QStringLiteral("Browse pictures and photo folders.") },
            { QStringLiteral("Slide Show"), QStringLiteral(""), QStringLiteral("Slide Show"), QStringLiteral("Start a picture slideshow.") },
            { QStringLiteral("Video Library"), QStringLiteral(""), QStringLiteral("Video Library"), QStringLiteral("Browse locally available video files.") },
            { QStringLiteral("Play All"), QStringLiteral(""), QStringLiteral("Play Videos"), QStringLiteral("Play compatible videos in sequence.") },
            { QStringLiteral("Search"), QStringLiteral(""), QStringLiteral("Search Media"), QStringLiteral("Search pictures and videos.") },
        } },
        { QStringLiteral("Sports"), QStringLiteral("Sports center"), {
            { QStringLiteral("On Now"), QStringLiteral(""), QStringLiteral("On Now"), QStringLiteral("See sports programming available now.") },
            { QStringLiteral("On Later"), QStringLiteral(""), QStringLiteral("On Later"), QStringLiteral("Browse upcoming sports programming.") },
            { QStringLiteral("Scores"), QStringLiteral(""), QStringLiteral("Scores"), QStringLiteral("Sports score summaries when a provider is available.") },
            { QStringLiteral("Statistics"), QStringLiteral(""), QStringLiteral("Statistics"), QStringLiteral("Player and team statistics when available.") },
        } },
        { QStringLiteral("Online Media"), QStringLiteral("Connected entertainment"), {
            { QStringLiteral("Internet TV"), QStringLiteral(""), QStringLiteral("Internet TV"), QStringLiteral("Online television services when configured.") },
            { QStringLiteral("Online Media"), QStringLiteral(""), QStringLiteral("Online Media"), QStringLiteral("Browse installed or configured online-media providers.") },
            { QStringLiteral("Online Games"), QStringLiteral(""), QStringLiteral("Online Games"), QStringLiteral("Launch online entertainment plug-ins when available.") },
            { QStringLiteral("Services"), QStringLiteral(""), QStringLiteral("Services"), QStringLiteral("Manage compatible online-media extensions.") },
        } },
        { QStringLiteral("Extras"), QStringLiteral("Extras library and utilities"), {
            { QStringLiteral("Extras Library"), QStringLiteral(""), QStringLiteral("Extras Library"), QStringLiteral("Media Center-style extras and plug-ins.") },
            { QStringLiteral("User Manual"), QStringLiteral(""), QStringLiteral("User Manual"), QStringLiteral("Open the frontend user manual.") },
            { QStringLiteral("Screensaver"), QStringLiteral(""), QStringLiteral("Screensaver"), QStringLiteral("Start or configure the frontend screensaver.") },
            { QStringLiteral("Skip Song"), QStringLiteral(""), QStringLiteral("Skip Song"), QStringLiteral("Skip the current interface background-music track.") },
            { QStringLiteral("Plug-ins"), QStringLiteral(""), QStringLiteral("Plug-ins"), QStringLiteral("Open installed media-center extensions.") },
        } },
        { QStringLiteral("Game Options"), QStringLiteral("Actions for the selected game"), {
            { QStringLiteral("Manual"), QStringLiteral(""), QStringLiteral("Game Manual"), QStringLiteral("Open the selected game's manual when available.") },
            { QStringLiteral("Video + Media"), QStringLiteral(""), QStringLiteral("Video + Media"), QStringLiteral("View scraped video and media associated with the game.") },
            { QStringLiteral("Manage Game"), QStringLiteral(""), QStringLiteral("Manage Game"), QStringLiteral("Open management actions for the selected title.") },
            { QStringLiteral("Saves"), QStringLiteral(""), QStringLiteral("Saves"), QStringLiteral("Manage saved data and savestates.") },
            { QStringLiteral("Similar Games"), QStringLiteral(""), QStringLiteral("Similar Games"), QStringLiteral("Find games similar to the selected title.") },
            { QStringLiteral("Favorite"), QStringLiteral(""), QStringLiteral("Favorite"), QStringLiteral("Add or remove the selected title from Favorites.") },
            { QStringLiteral("Scrape Game"), QStringLiteral(""), QStringLiteral("Scrape Game"), QStringLiteral("Scrape metadata and media for only this title.") },
            { QStringLiteral("Edit Metadata"), QStringLiteral(""), QStringLiteral("Edit Metadata"), QStringLiteral("Edit game information, artwork and flags.") },
            { QStringLiteral("Advanced"), QStringLiteral(""), QStringLiteral("Advanced Game Options"), QStringLiteral("Override emulator features for this specific game.") },
            { QStringLiteral("Delete Game"), QStringLiteral(""), QStringLiteral("Delete Game"), QStringLiteral("Remove the selected game file after confirmation.") },
        } },
        { QStringLiteral("Game Settings"), QStringLiteral("Global emulation configuration"), {
            { QStringLiteral("Update Gamelist"), QStringLiteral(""), QStringLiteral("Update Gamelist"), QStringLiteral("Refresh configured gamelists and detect newly added ROMs.") },
            { QStringLiteral("Shader Set"), QStringLiteral(""), QStringLiteral("Shader Set"), QStringLiteral("Select a global predefined shader set.") },
            { QStringLiteral("Decorations"), QStringLiteral(""), QStringLiteral("Decorations"), QStringLiteral("Choose default bezels and decorations.") },
            { QStringLiteral("Video Mode"), QStringLiteral(""), QStringLiteral("Video Mode"), QStringLiteral("Define display resolution and refresh rate.") },
            { QStringLiteral("Aspect Ratio"), QStringLiteral(""), QStringLiteral("Game Aspect Ratio"), QStringLiteral("Choose the default game aspect ratio.") },
            { QStringLiteral("Integer Scaling"), QStringLiteral(""), QStringLiteral("Integer Scaling"), QStringLiteral("Enable pixel-perfect integer scaling where supported.") },
            { QStringLiteral("Smooth Games"), QStringLiteral(""), QStringLiteral("Smooth Games"), QStringLiteral("Apply global bilinear filtering.") },
            { QStringLiteral("Controller Auto"), QStringLiteral(""), QStringLiteral("Autoconfigure Controllers"), QStringLiteral("Enable or disable RetroBat controller autoconfiguration.") },
            { QStringLiteral("Compression"), QStringLiteral(""), QStringLiteral("Compression"), QStringLiteral("Configure decompression and compressed-game handling.") },
            { QStringLiteral("RA Video"), QStringLiteral(""), QStringLiteral("RetroArch Video"), QStringLiteral("Configure RetroArch video parameters.") },
            { QStringLiteral("Screen Sync"), QStringLiteral(""), QStringLiteral("Screen Sync"), QStringLiteral("Configure synchronization and VSync-related options.") },
            { QStringLiteral("RA Audio"), QStringLiteral(""), QStringLiteral("RetroArch Audio"), QStringLiteral("Configure RetroArch audio parameters.") },
            { QStringLiteral("Emulation"), QStringLiteral(""), QStringLiteral("Emulation"), QStringLiteral("Configure emulation features such as rewind.") },
            { QStringLiteral("Latency"), QStringLiteral(""), QStringLiteral("Latency Reduction"), QStringLiteral("Configure latency-reduction features.") },
            { QStringLiteral("AI Translation"), QStringLiteral(""), QStringLiteral("AI Game Translation"), QStringLiteral("Configure supported game-translation functionality.") },
            { QStringLiteral("RA Interface"), QStringLiteral(""), QStringLiteral("RetroArch Interface"), QStringLiteral("Configure RetroArch notifications and menu elements.") },
            { QStringLiteral("Drivers"), QStringLiteral(""), QStringLiteral("Drivers"), QStringLiteral("Select video, audio and controller drivers.") },
            { QStringLiteral("Auto Save / Load"), QStringLiteral(""), QStringLiteral("Auto Save / Load"), QStringLiteral("Automatically save and load game state.") },
            { QStringLiteral("Incremental States"), QStringLiteral(""), QStringLiteral("Incremental Savestates"), QStringLiteral("Configure automatic incremental savestate management.") },
            { QStringLiteral("Savestate Manager"), QStringLiteral(""), QStringLiteral("Savestate Manager"), QStringLiteral("Choose whether to display Savestate Manager before launch.") },
            { QStringLiteral("Per-System"), QStringLiteral(""), QStringLiteral("Per-System Advanced"), QStringLiteral("Open advanced configuration for an individual system.") },
            { QStringLiteral("Achievements"), QStringLiteral(""), QStringLiteral("RetroAchievements Settings"), QStringLiteral("Enable and configure RetroAchievements.") },
            { QStringLiteral("NetPlay"), QStringLiteral(""), QStringLiteral("NetPlay Settings"), QStringLiteral("Enable and configure network play.") },
            { QStringLiteral("Missing BIOS"), QStringLiteral(""), QStringLiteral("Missing BIOS Check"), QStringLiteral("Display required BIOS files that are currently missing.") },
            { QStringLiteral("BIOS Precheck"), QStringLiteral(""), QStringLiteral("Check BIOS Before Game"), QStringLiteral("Choose whether BIOS validation occurs before launch.") },
        } },
        { QStringLiteral("Interface Settings"), QStringLiteral("Appearance and navigation"), {
            { QStringLiteral("Theme"), QStringLiteral(""), QStringLiteral("Theme"), QStringLiteral("Choose the interface theme.") },
            { QStringLiteral("Background"), QStringLiteral(""), QStringLiteral("Background"), QStringLiteral("Choose the Kadia background, desktop wallpaper translucency and image opacity.") },
            { QStringLiteral("Theme Config"), QStringLiteral(""), QStringLiteral("Theme Configuration"), QStringLiteral("Configure options exposed by the active theme.") },
            { QStringLiteral("Transitions"), QStringLiteral(""), QStringLiteral("Transitions"), QStringLiteral("Choose interface transition behavior.") },
            { QStringLiteral("Show Clock"), QStringLiteral(""), QStringLiteral("Show Clock"), QStringLiteral("Show or hide the interface clock.") },
            { QStringLiteral("Screensaver"), QStringLiteral(""), QStringLiteral("Screensaver Settings"), QStringLiteral("Configure screensaver behavior.") },
            { QStringLiteral("Gamelist Style"), QStringLiteral(""), QStringLiteral("Gamelist Style"), QStringLiteral("Choose the preferred gamelist presentation.") },
            { QStringLiteral("Favorites on Top"), QStringLiteral(""), QStringLiteral("Favorites on Top"), QStringLiteral("Place favorite games before other titles.") },
            { QStringLiteral("Game Icons"), QStringLiteral(""), QStringLiteral("Game Icons"), QStringLiteral("Configure gamelist status and metadata icons.") },
            { QStringLiteral("Filenames"), QStringLiteral(""), QStringLiteral("Filenames"), QStringLiteral("Use ROM filenames instead of metadata names when desired.") },
            { QStringLiteral("Display Options"), QStringLiteral(""), QStringLiteral("Display Options"), QStringLiteral("Configure additional frontend display behavior.") },
        } },
        { QStringLiteral("Controllers"), QStringLiteral("Controller configuration"), {
            { QStringLiteral("Configure"), QStringLiteral(""), QStringLiteral("Configure Controller"), QStringLiteral("Configure or remap a connected controller.") },
            { QStringLiteral("Player 1"), QStringLiteral(""), QStringLiteral("Player 1"), QStringLiteral("Assign a default controller to Player 1.") },
            { QStringLiteral("Player 2"), QStringLiteral(""), QStringLiteral("Player 2"), QStringLiteral("Assign a default controller to Player 2.") },
            { QStringLiteral("Player 3"), QStringLiteral(""), QStringLiteral("Player 3"), QStringLiteral("Assign a default controller to Player 3.") },
            { QStringLiteral("Player 4"), QStringLiteral(""), QStringLiteral("Player 4"), QStringLiteral("Assign a default controller to Player 4.") },
            { QStringLiteral("Battery Icons"), QStringLiteral(""), QStringLiteral("Battery Icons"), QStringLiteral("Show or hide controller battery indicators.") },
            { QStringLiteral("Activity Icons"), QStringLiteral(""), QStringLiteral("Controller Activity"), QStringLiteral("Show or hide controller activity indicators.") },
            { QStringLiteral("Autoconfigure"), QStringLiteral(""), QStringLiteral("Autoconfigure Controllers"), QStringLiteral("Manage automatic emulator controller configuration.") },
        } },
        { QStringLiteral("Sound"), QStringLiteral("Frontend audio"), {
            { QStringLiteral("Music Volume"), QStringLiteral(""), QStringLiteral("Music Volume"), QStringLiteral("Adjust interface music volume.") },
            { QStringLiteral("Interface Music"), QStringLiteral(""), QStringLiteral("Interface Music"), QStringLiteral("Enable or disable frontend background music.") },
            { QStringLiteral("Music Options"), QStringLiteral(""), QStringLiteral("Music Configuration"), QStringLiteral("Configure how interface music is selected and played.") },
            { QStringLiteral("Navigation Sounds"), QStringLiteral(""), QStringLiteral("Navigation Sounds"), QStringLiteral("Enable or disable interface navigation sounds.") },
            { QStringLiteral("Mute"), QStringLiteral(""), QStringLiteral("Mute"), QStringLiteral("Mute frontend audio.") },
        } },
        { QStringLiteral("Collection Settings"), QStringLiteral("Configure game collections"), {
            { QStringLiteral("Collections to Display"), QStringLiteral(""), QStringLiteral("Collections to Display"), QStringLiteral("Choose systems and game collections shown in the frontend.") },
            { QStringLiteral("Automatic"), QStringLiteral(""), QStringLiteral("Automatic Collections"), QStringLiteral("Enable automatic collections such as Favorites and Last Played.") },
            { QStringLiteral("Genre Collections"), QStringLiteral(""), QStringLiteral("Genre Collections"), QStringLiteral("Enable collections grouped by genre.") },
            { QStringLiteral("Arcade Collections"), QStringLiteral(""), QStringLiteral("Arcade Collections"), QStringLiteral("Enable automatic arcade-system collections.") },
            { QStringLiteral("New Editable"), QStringLiteral(""), QStringLiteral("Create Editable Collection"), QStringLiteral("Create a collection managed manually.") },
            { QStringLiteral("New Dynamic"), QStringLiteral(""), QStringLiteral("Create Dynamic Collection"), QStringLiteral("Create a collection populated from filters.") },
            { QStringLiteral("Grouped Systems"), QStringLiteral(""), QStringLiteral("Grouped Systems"), QStringLiteral("Choose which related systems are grouped together.") },
            { QStringLiteral("Show Hidden"), QStringLiteral(""), QStringLiteral("Show Hidden Systems"), QStringLiteral("Configure visibility of hidden systems and collections.") },
            { QStringLiteral("Sorting"), QStringLiteral(""), QStringLiteral("Collection Sorting"), QStringLiteral("Configure collection display and ordering options.") },
        } },
        { QStringLiteral("Scraper"), QStringLiteral("Metadata and media"), {
            { QStringLiteral("Scrape Now"), QStringLiteral(""), QStringLiteral("Scrape"), QStringLiteral("Start metadata and media scraping.") },
            { QStringLiteral("Source"), QStringLiteral(""), QStringLiteral("Scraper Source"), QStringLiteral("Choose ScreenScraper, TheGamesDB, HFSDB, ArcadeDB or IGDB when configured.") },
            { QStringLiteral("Missing All Media"), QStringLiteral(""), QStringLiteral("Missing All Media"), QStringLiteral("Limit scraping to games missing all media.") },
            { QStringLiteral("Missing Any Media"), QStringLiteral(""), QStringLiteral("Missing Any Media"), QStringLiteral("Limit scraping to games missing one or more media items.") },
            { QStringLiteral("Systems"), QStringLiteral(""), QStringLiteral("Systems to Scrape"), QStringLiteral("Include or exclude systems from the scraping operation.") },
            { QStringLiteral("Game Info"), QStringLiteral(""), QStringLiteral("Game Information"), QStringLiteral("Scrape title, year, players, description and related metadata.") },
            { QStringLiteral("Ratings"), QStringLiteral(""), QStringLiteral("Ratings"), QStringLiteral("Scrape available game ratings.") },
            { QStringLiteral("Box Art"), QStringLiteral(""), QStringLiteral("Box Art"), QStringLiteral("Scrape front or back box imagery.") },
            { QStringLiteral("Wheel / Marquee"), QStringLiteral(""), QStringLiteral("Wheel / Marquee"), QStringLiteral("Scrape game logos and marquees.") },
            { QStringLiteral("Videos"), QStringLiteral(""), QStringLiteral("Videos"), QStringLiteral("Scrape preview videos.") },
            { QStringLiteral("Fanart"), QStringLiteral(""), QStringLiteral("Fanart"), QStringLiteral("Scrape background artwork.") },
            { QStringLiteral("Screenshots"), QStringLiteral(""), QStringLiteral("Screenshots"), QStringLiteral("Scrape title-screen or in-game screenshots.") },
            { QStringLiteral("Maps"), QStringLiteral(""), QStringLiteral("Maps"), QStringLiteral("Scrape game maps when available.") },
            { QStringLiteral("Manuals"), QStringLiteral(""), QStringLiteral("Manuals"), QStringLiteral("Scrape game manuals when available.") },
            { QStringLiteral("Pad2Key"), QStringLiteral(""), QStringLiteral("Pad-to-Key"), QStringLiteral("Scrape available pad-to-key configuration.") },
        } },
        { QStringLiteral("Updates + Downloads"), QStringLiteral("Update and add content"), {
            { QStringLiteral("Check Updates"), QStringLiteral(""), QStringLiteral("Check for Updates"), QStringLiteral("Check for RetroBat/frontend updates.") },
            { QStringLiteral("Update"), QStringLiteral(""), QStringLiteral("Update Frontend"), QStringLiteral("Download and install an available update.") },
            { QStringLiteral("Free Games"), QStringLiteral(""), QStringLiteral("Free Games"), QStringLiteral("Download supported free-to-use game content.") },
            { QStringLiteral("Bezels"), QStringLiteral(""), QStringLiteral("Bezels"), QStringLiteral("Download bezel and decoration packs.") },
            { QStringLiteral("Themes"), QStringLiteral(""), QStringLiteral("Themes"), QStringLiteral("Browse and download interface themes.") },
            { QStringLiteral("Downloaded Content"), QStringLiteral(""), QStringLiteral("Downloaded Content"), QStringLiteral("Review content installed through the download manager.") },
        } },
        { QStringLiteral("System Settings"), QStringLiteral("Frontend and system"), {
            { QStringLiteral("Information"), QStringLiteral(""), QStringLiteral("System Information"), QStringLiteral("Display system and frontend information.") },
            { QStringLiteral("Language"), QStringLiteral(""), QStringLiteral("Language"), QStringLiteral("Choose the interface language.") },
            { QStringLiteral("12 / 24 Hour"), QStringLiteral(""), QStringLiteral("Clock Format"), QStringLiteral("Choose 12-hour or 24-hour clock display.") },
            { QStringLiteral("Power Saving"), QStringLiteral(""), QStringLiteral("Power Saving Mode"), QStringLiteral("Configure frontend power-saving behavior.") },
            { QStringLiteral("Screen Reader"), QStringLiteral(""), QStringLiteral("Screen Reader"), QStringLiteral("Enable or disable text-to-speech accessibility.") },
            { QStringLiteral("Interface Mode"), QStringLiteral(""), QStringLiteral("User Interface Mode"), QStringLiteral("Choose Full, Kid or Kiosk interface mode.") },
            { QStringLiteral("Video Options"), QStringLiteral(""), QStringLiteral("Advanced Video Options"), QStringLiteral("Configure VRAM, framerate display, VSync and related settings.") },
            { QStringLiteral("Developer Tools"), QStringLiteral(""), QStringLiteral("Developer Tools"), QStringLiteral("Open logging and developer-oriented tools.") },
            { QStringLiteral("Data Management"), QStringLiteral(""), QStringLiteral("Data Management"), QStringLiteral("Open advanced data-management options.") },
            { QStringLiteral("Optimizations"), QStringLiteral(""), QStringLiteral("Optimizations"), QStringLiteral("Configure advanced frontend optimizations.") },
        } },
        { QStringLiteral("Tasks"), QStringLiteral("Windows Media Center-style tasks"), {
            { QStringLiteral("Settings"), QStringLiteral(""), QStringLiteral("Settings"), QStringLiteral("Open general entertainment-center settings.") },
            { QStringLiteral("Burn Disc"), QStringLiteral(""), QStringLiteral("Burn CD / DVD"), QStringLiteral("Create a compatible audio, data or video optical disc.") },
            { QStringLiteral("Sync Device"), QStringLiteral(""), QStringLiteral("Sync Device"), QStringLiteral("Synchronize supported media with another device.") },
            { QStringLiteral("Extender"), QStringLiteral(""), QStringLiteral("Media Center Extender"), QStringLiteral("Configure a compatible Media Center extender.") },
            { QStringLiteral("Media Only"), QStringLiteral(""), QStringLiteral("Media Only"), QStringLiteral("Enter a focused media-only presentation mode.") },
            { QStringLiteral("Display Setup"), QStringLiteral(""), QStringLiteral("Display Setup"), QStringLiteral("Configure display behavior for the entertainment interface.") },
            { QStringLiteral("Media Libraries"), QStringLiteral(""), QStringLiteral("Media Libraries"), QStringLiteral("Choose folders and libraries used for music, pictures, videos and recordings.") },
        } },
        { QStringLiteral("Power"), QStringLiteral("Session and power"), {
            { QStringLiteral("Sleep"), QStringLiteral(""), QStringLiteral("Sleep"), QStringLiteral("Put the system into a low-power sleep state.") },
            { QStringLiteral("Restart"), QStringLiteral(""), QStringLiteral("Restart"), QStringLiteral("Restart Windows after confirmation.") },
            { QStringLiteral("Shut Down"), QStringLiteral(""), QStringLiteral("Shut Down"), QStringLiteral("Power off the system after confirmation.") },
            { QStringLiteral("Exit to Windows"), QStringLiteral(""), QStringLiteral("Exit to Windows"), QStringLiteral("Close Mathery Kadia! and return to the Windows desktop.") },
            { QStringLiteral("Quit Frontend"), QStringLiteral(""), QStringLiteral("Quit Frontend"), QStringLiteral("Close the frontend.") },
            { QStringLiteral("Cancel"), QStringLiteral(""), QStringLiteral("Cancel"), QStringLiteral("Return to the previous menu.") },
        } },
    };

    static QVector<KadiaSectionInfo> filtered;
    static QString lastStoreKey;
    static QString lastUnknownKey;
    const QString currentStoreKey = detectedStoresStorage().join(QStringLiteral("|"));
    const QString currentUnknownKey = unknownRomsStorage().join(QStringLiteral("|"));
    if (filtered.isEmpty() || currentStoreKey != lastStoreKey || currentUnknownKey != lastUnknownKey) {
        filtered = baseData;
        lastStoreKey = currentStoreKey;
        lastUnknownKey = currentUnknownKey;
        const QStringList storeLabels = QStringList()
            << QStringLiteral("Steam") << QStringLiteral("Epic") << QStringLiteral("GOG")
            << QStringLiteral("EA") << QStringLiteral("Amazon") << QStringLiteral("Ubisoft")
            << QStringLiteral("Battle.net");
        for (int sidx = 0; sidx < filtered.size(); ++sidx) {
            if (filtered[sidx].name != QStringLiteral("PC Games"))
                continue;
            QVector<KadiaTileInfo> kept;
            const QVector<KadiaTileInfo> original = filtered[sidx].tiles;
            for (int i = 0; i < original.size(); ++i) {
                const QString label = original[i].label;
                if (!storeLabels.contains(label, Qt::CaseInsensitive) ||
                    detectedStoresStorage().contains(label, Qt::CaseInsensitive))
                    kept.push_back(original[i]);
            }
            filtered[sidx].tiles = kept;
            break;
        }

        for (int sidx = filtered.size() - 1; sidx >= 0; --sidx) {
            if (filtered[sidx].name != QStringLiteral("Unknowns"))
                continue;
            if (unknownRomsStorage().isEmpty()) {
                filtered.remove(sidx);
            } else {
                QVector<KadiaTileInfo> romTiles;
                for (int r = 0; r < unknownRomsStorage().size(); ++r) {
                    const QString path = unknownRomsStorage()[r];
                    QFileInfo fi(path);
                    romTiles.push_back(KadiaTileInfo{fi.fileName(), QString(), fi.completeBaseName(),
                        QStringLiteral("Unclassified ROM image: %1").arg(QDir::toNativeSeparators(path))});
                }
                filtered[sidx].tiles = romTiles;
            }
            break;
        }
    }
    return filtered;
}

const QVector<KadiaGameInfo> &kadiaGames()
{
    static const QVector<KadiaGameInfo> data = {
        { QStringLiteral("Aero Quest"), QStringLiteral("1994 - Adventure - 1 Player"), QStringLiteral("A bright console adventure presented inside Mathery Kadia!'s dark Media Center shell.") },
        { QStringLiteral("Crystal Circuit"), QStringLiteral("1995 - Action - 1 Player"), QStringLiteral("Fast arcade action with a polished 16-bit presentation.") },
        { QStringLiteral("Moon Harbor"), QStringLiteral("1993 - RPG - 1 Player"), QStringLiteral("A calm nocturnal journey through a strange coastal world.") },
        { QStringLiteral("Velvet Racer"), QStringLiteral("1996 - Racing - 1-2 Players"), QStringLiteral("Late-night racing with glossy roads and warm dashboard light.") },
        { QStringLiteral("Solar Garden"), QStringLiteral("2001 - Platformer - 1 Player"), QStringLiteral("A portable adventure full of strange plants and tiny planets.") },
        { QStringLiteral("Night Signal"), QStringLiteral("1998 - Adventure - 1 Player"), QStringLiteral("An atmospheric mystery built around radio towers and distant lights.") },
    };
    return data;
}
