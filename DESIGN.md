# MyAmp design

## Goal

A small, portable Windows music player with two compact, independently minimizable windows: a player and a playlist. It starts with an empty playlist on every launch and never changes or deletes music files.

## Interface

- A compact, classic-player look with small controls and a dark display.
- The player window shows artist/title when tags are available, otherwise the filename; elapsed and total time; a seek slider; volume; Previous, Play/Pause, Stop, and Next; and audio details such as bitrate, channel count (mono/stereo), and sample rate.
- The playlist is a separate compact window. Rows show a track number, title information, and duration. Hovering a row shows its full path and filename.
- Single-click selects a row; double-click starts it. Multiple rows can be selected and removed with the keyboard Delete key. Removing a row never removes its file.
- Both windows can be minimized independently. Minimizing the player leaves playback running. Closing the player exits the app and stops playback. Closing the playlist hides it; a control in the player shows it again.

## Playlist behavior

- Accept one or more MP3/OGG files and one or more folders dropped from Explorer. Folder contents are collected recursively and sorted alphabetically by full path.
- Sequential mode advances in playlist order.
- Random mode selects an unplayed track at random and records it as played, so it will not be selected again until random mode is toggled off and on or the current cycle ends. Turning random off and on clears that history.
- Repeat-song is a manual toggle. While enabled, the current track loops; pressing Next or choosing another track starts that track with repeat-song still enabled.
- Repeat-playlist starts a new cycle after the last sequential track or after all tracks in a random cycle have played. A new random cycle picks a fresh random flow.
- If the currently playing row is removed, playback advances to the next applicable track in the remaining playlist.

## Implementation

- Native C++ and the Windows API for the two-window UI, controls, tooltips, drag-and-drop, and file enumeration. No .NET runtime or installer is required.
- Bundle the audio engine and decoders into the executable. Use miniaudio for output and MP3 playback, plus an embedded Vorbis decoder for OGG playback. This keeps the codecs independent of what happens to be installed on the laptop.
- WAV and FLAC can be included as optional formats if they add no meaningful deployment weight.
- Parse MP3 ID3 tags and Ogg Vorbis comments for display only; do not write tags.
- Build a single Windows executable with statically linked non-system runtime code. Windows system APIs remain the only platform dependencies.
- Target Windows 10 and 11; older Windows compatibility is a nice-to-have if it does not complicate the app.

## Out of scope

Tag editing, conversion, persistent playlists/settings, and deleting or modifying media files.

## References

- [miniaudio](https://github.com/mackron/miniaudio) documents a single-source C audio library with built-in MP3 decoding and Windows backends.
- [stb_vorbis](https://github.com/nothings/stb/blob/master/stb_vorbis.c) provides a single-file Ogg Vorbis decoder.
- [Microsoft Media Foundation supported formats](https://learn.microsoft.com/windows/win32/medfound/supported-media-formats-in-media-foundation) lists MP3 as native but does not list Ogg among its native containers/codecs, so the Ogg decoder is bundled.
