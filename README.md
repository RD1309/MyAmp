# MyAmp

A vibecoded compact, portable Windows music player with separate player and playlist windows.

## Playback

- MP3 and Ogg Vorbis playback; WAV and FLAC are also accepted.
- Play/pause toggle, Stop, Previous, Next, seek, and volume controls.
- Shows title/artist tags when available and falls back to the filename. Playlist tooltips show the full path.
- Displays average bitrate, channel count, sample rate, elapsed time, and duration.
- Sequential and random playback. One repeat button cycles through repeat-playlist (two dots, the startup state), repeat-current-song (one dot), and repeat-off (no dots). Random mode avoids tracks already played until random is toggled off/on or the playlist cycle ends. Repeat-current stays enabled when you move to another track.
- Drop individual files or folders (including multiple items) onto either window. Folders are scanned recursively; each drop batch is sorted alphabetically by path.
- Opening supported audio files from Windows adds them to the playlist and starts the first opened track. Further file opens are sent to the running MyAmp instance.
- Select multiple playlist rows and press Delete to remove them from the playlist only. Removing the playing row advances to the next applicable track.
- Right-click a playlist row to remove it from the playlist or open a new File Explorer window at its folder. Removing a track never deletes the file.
- Closing the player exits and stops playback. Closing the playlist hides it; minimizing the player hides the playlist and restores its prior visibility when the player is restored. Minimizing either window does not stop playback.
- When both windows overlap, the player stays above the playlist, while other applications can still cover the player.
- The playlist is not saved between launches.
- Right-click the player and choose a scale from 50% to 200%; the player and playlist contents scale together.

The player uses the Windows API for its interface and bundles its decoders. It does not require .NET, an installer, or separately installed audio codecs.

## Build

The project is C and targets a 32-bit Windows executable so it can run on both 32-bit and 64-bit Windows. The build script uses Zig as a compiler; Zig is a build-time tool only and is not included in the app.

```powershell
./build.ps1 -ZigPath C:\path\to\zig.exe
```

The output filename includes the version from `src/myamp.c`, for example `MyAmp1_0_11.exe`.
