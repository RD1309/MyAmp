#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

/* stb_vorbis is included first so miniaudio can wire it in as its Ogg decoder. */
#include "../vendor/stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "../vendor/miniaudio.h"

#define APP_NAME L"MyAmp"
#define APP_VERSION L"1.0.11"
#define APP_TITLE L"MyAmp " APP_VERSION
#define APP_ICON_ID 101
#define MYAMP_COPYDATA_TAG 0x4D59414Du
#define MYAMP_INSTANCE_MUTEX L"Local\\MyAmp_1_0_11_Instance"
#define MAX_TRACKS 100000
#define PLAYER_TIMER 1
#define PLAYER_TIMER_MS 200
#define PLAYLIST_SCROLL_WIDTH 16
#define PLAYLIST_ROW_HEIGHT 30

enum { IDM_SCALE_50 = 4101, IDM_SCALE_75, IDM_SCALE_100, IDM_SCALE_125, IDM_SCALE_150, IDM_SCALE_200 };
enum { IDM_PLAYLIST_REMOVE = 4201, IDM_PLAYLIST_LOCATION };

typedef struct Track {
    wchar_t *path;
    wchar_t *title;
    wchar_t *artist;
    uint64_t fileBytes;
    uint64_t durationFrames;
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t durationMs;
    uint32_t bitrateKbps;
    BOOL played;
    BOOL selected;
} Track;

typedef enum ButtonId {
    BTN_PREV, BTN_PLAY_PAUSE, BTN_STOP, BTN_NEXT,
    BTN_RANDOM, BTN_REPEAT, BTN_PLAYLIST,
    BTN_COUNT
} ButtonId;

typedef enum RepeatMode {
    REPEAT_PLAYLIST, REPEAT_SONG, REPEAT_OFF
} RepeatMode;

enum { TASKBAR_PREVIOUS = 1, TASKBAR_PLAY_PAUSE = 2, TASKBAR_NEXT = 3 };
enum { THUMB_ICON_PREVIOUS, THUMB_ICON_PLAY, THUMB_ICON_PAUSE, THUMB_ICON_NEXT };

static HINSTANCE g_instance;
static HWND g_playerWindow;
static HWND g_playlistWindow;
static HWND g_playlistOwner;
static HWND g_playlistView;
static HWND g_tooltip;
static HWND g_playlistTip;
static HFONT g_playlistFont;
static HFONT g_playerFont;
static ma_engine g_engine;
static ma_sound g_sound;
static BOOL g_engineReady;
static BOOL g_soundReady;
static BOOL g_comReady;
static BOOL g_paused;
static BOOL g_random;
static BOOL g_playerWasMinimized;
static BOOL g_restorePlaylistOnRestore;
static RepeatMode g_repeatMode = REPEAT_PLAYLIST;
static int g_uiScalePercent = 100;
static float g_volume = 0.78f;
static BOOL g_muted;
static Track *g_tracks;
static int g_trackCount;
static int g_trackCapacity;
static int g_current = -1;
static int *g_history;
static int g_historyCount;
static int g_historyCapacity;
static BOOL g_dragSeek;
static BOOL g_dragVolume;
static int g_seekPreview;
static RECT g_seekRect;
static RECT g_timeRect;
static RECT g_volumeRect;
static RECT g_muteRect;
static RECT g_buttonRects[BTN_COUNT];
static ITaskbarList3 *g_taskbar;
static THUMBBUTTON g_taskbarButtons[3];
static HIMAGELIST g_taskbarImageList;
static UINT g_taskbarButtonCreatedMessage;
static BOOL g_taskbarButtonsAdded;
static BOOL g_scrollDragging;
static int g_scrollDragOffset;
static int g_playlistTop;
static int g_playlistFocus = -1;
static int g_playlistAnchor = -1;
static HANDLE g_instanceMutex;

static void resize_playlist_view(HWND hwnd);
static void invalidate_playlist(void);
static void update_taskbar_buttons(void);
static void keep_player_above_playlist(void);
static void add_paths(const wchar_t **paths, int pathCount, BOOL playFirst);

static const COLORREF C_BG = RGB(28, 31, 34);
static const COLORREF C_PANEL = RGB(12, 17, 18);
static const COLORREF C_TEXT = RGB(219, 224, 221);
static const COLORREF C_MUTED = RGB(139, 151, 146);
static const COLORREF C_LED = RGB(137, 235, 157);
static const COLORREF C_ACCENT = RGB(49, 132, 83);
static const COLORREF C_BUTTON = RGB(50, 55, 58);
static const COLORREF C_BUTTON_ON = RGB(38, 94, 64);

static int ui_scale_px(int value) {
    return (int)(((int64_t)value * g_uiScalePercent + 50) / 100);
}

static int ui_unscale_px(int value) {
    return (int)(((int64_t)value * 100 + g_uiScalePercent / 2) / g_uiScalePercent);
}

static int ui_unscale_extent(int value) {
    return (int)(((int64_t)value * 100 + g_uiScalePercent - 1) / g_uiScalePercent);
}

static RECT ui_rect_to_pixels(RECT r) {
    r.left = ui_scale_px(r.left); r.top = ui_scale_px(r.top);
    r.right = ui_scale_px(r.right); r.bottom = ui_scale_px(r.bottom);
    return r;
}

static void set_ui_transform(HDC dc) {
    XFORM transform;
    float scale = (float)g_uiScalePercent / 100.0f;
    if (SetGraphicsMode(dc, GM_ADVANCED) == 0) return;
    transform.eM11 = scale; transform.eM12 = 0.0f;
    transform.eM21 = 0.0f; transform.eM22 = scale;
    transform.eDx = 0.0f; transform.eDy = 0.0f;
    SetWorldTransform(dc, &transform);
}

static void reset_ui_transform(HDC dc) {
    XFORM identity = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    SetWorldTransform(dc, &identity);
}

static HFONT create_player_ui_font(void) {
    LOGFONTW font;
    HDC dc;
    int dpi = 96;
    if (GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font) != sizeof(font)) return NULL;
    dc = GetDC(NULL);
    if (dc) { dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); }
    if (dpi > 0 && dpi != 96 && font.lfHeight != 0) {
        font.lfHeight = (LONG)((int64_t)font.lfHeight * 96 / dpi);
        if (font.lfHeight == 0) font.lfHeight = -1;
    }
    return CreateFontIndirectW(&font);
}

static void invalidate_player_rect(RECT logicalRect) {
    RECT pixels = ui_rect_to_pixels(logicalRect);
    if (g_playerWindow) InvalidateRect(g_playerWindow, &pixels, FALSE);
}

static wchar_t *dup_w(const wchar_t *s) {
    size_t n;
    wchar_t *r;
    if (s == NULL) s = L"";
    n = wcslen(s);
    r = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (r != NULL) memcpy(r, s, (n + 1) * sizeof(wchar_t));
    return r;
}

static void copy_w(wchar_t *dst, size_t cap, const wchar_t *src) {
    if (cap == 0) return;
    if (src == NULL) src = L"";
    wcsncpy(dst, src, cap - 1);
    dst[cap - 1] = 0;
}

static wchar_t *join_path(const wchar_t *a, const wchar_t *b) {
    size_t na = wcslen(a), nb = wcslen(b);
    BOOL slash = na > 0 && a[na - 1] != L'\\' && a[na - 1] != L'/';
    wchar_t *r = (wchar_t *)malloc((na + nb + (slash ? 2 : 1)) * sizeof(wchar_t));
    if (r == NULL) return NULL;
    memcpy(r, a, na * sizeof(wchar_t));
    if (slash) r[na++] = L'\\';
    memcpy(r + na, b, (nb + 1) * sizeof(wchar_t));
    return r;
}

static void utf8_to_w(const char *src, int bytes, wchar_t *dst, size_t cap) {
    int n;
    if (cap == 0) return;
    dst[0] = 0;
    if (src == NULL || bytes <= 0) return;
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, bytes, dst, (int)cap - 1);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, src, bytes, dst, (int)cap - 1);
    if (n < 0) n = 0;
    dst[n] = 0;
}

static void id3_text_to_w(const unsigned char *src, int bytes, wchar_t *dst, size_t cap) {
    unsigned char enc;
    int len;
    if (cap == 0) return;
    dst[0] = 0;
    if (src == NULL || bytes <= 1) return;
    enc = src[0];
    src++; bytes--;
    if (enc == 1 || enc == 2) {
        BOOL be = enc == 2;
        int i = 0, out = 0;
        if (enc == 1 && bytes >= 2 && src[0] == 0xFF && src[1] == 0xFE) { src += 2; bytes -= 2; }
        else if (enc == 1 && bytes >= 2 && src[0] == 0xFE && src[1] == 0xFF) { src += 2; bytes -= 2; be = TRUE; }
        len = bytes / 2;
        while (i < len && out < (int)cap - 1) {
            uint16_t ch = be ? (uint16_t)((src[i * 2] << 8) | src[i * 2 + 1]) : (uint16_t)(src[i * 2] | (src[i * 2 + 1] << 8));
            if (ch == 0) break;
            dst[out++] = (wchar_t)ch;
            i++;
        }
        dst[out] = 0;
    } else {
        int cp = enc == 3 ? CP_UTF8 : 1252;
        int n = MultiByteToWideChar(cp, enc == 3 ? MB_ERR_INVALID_CHARS : 0, (const char *)src, bytes, dst, (int)cap - 1);
        if (n <= 0 && cp == CP_UTF8) n = MultiByteToWideChar(CP_ACP, 0, (const char *)src, bytes, dst, (int)cap - 1);
        if (n < 0) n = 0;
        dst[n] = 0;
    }
}

static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t synchsafe32(const unsigned char *p) {
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (uint32_t)(p[3] & 0x7F);
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void parse_id3v1(FILE *f, wchar_t *title, size_t titleCap, wchar_t *artist, size_t artistCap) {
    unsigned char tag[128];
    long long end;
    if (_fseeki64(f, 0, SEEK_END) != 0) return;
    end = _ftelli64(f);
    if (end < 128 || _fseeki64(f, end - 128, SEEK_SET) != 0) return;
    if (fread(tag, 1, sizeof(tag), f) != sizeof(tag) || memcmp(tag, "TAG", 3) != 0) return;
    if (title[0] == 0) {
        char b[31] = {0}; memcpy(b, tag + 3, 30);
        MultiByteToWideChar(1252, 0, b, -1, title, (int)titleCap);
    }
    if (artist[0] == 0) {
        char b[31] = {0}; memcpy(b, tag + 33, 30);
        MultiByteToWideChar(1252, 0, b, -1, artist, (int)artistCap);
    }
}

static void parse_mp3_tags(const wchar_t *path, wchar_t *title, size_t titleCap, wchar_t *artist, size_t artistCap) {
    FILE *f = _wfopen(path, L"rb");
    unsigned char h[10];
    uint32_t tagSize = 0;
    long long pos;
    if (f == NULL) return;
    if (fread(h, 1, 10, f) == 10 && memcmp(h, "ID3", 3) == 0) {
        tagSize = synchsafe32(h + 6);
        pos = 10;
        while (pos + 10 <= 10 + (long long)tagSize) {
            unsigned char fr[10];
            uint32_t n;
            char id[5];
            unsigned char *data;
            if (_fseeki64(f, pos, SEEK_SET) != 0 || fread(fr, 1, 10, f) != 10) break;
            if (fr[0] == 0) break;
            memcpy(id, fr, 4); id[4] = 0;
            n = h[3] >= 4 ? synchsafe32(fr + 4) : be32(fr + 4);
            if (n == 0 || n > 1024 * 1024 || pos + 10 + n > 10 + (long long)tagSize) break;
            data = (unsigned char *)malloc(n);
            if (data == NULL) break;
            if (fread(data, 1, n, f) != n) { free(data); break; }
            if (strcmp(id, "TIT2") == 0 && title[0] == 0) id3_text_to_w(data, (int)n, title, titleCap);
            if (strcmp(id, "TPE1") == 0 && artist[0] == 0) id3_text_to_w(data, (int)n, artist, artistCap);
            free(data);
            pos += 10 + n;
        }
    }
    parse_id3v1(f, title, titleCap, artist, artistCap);
    fclose(f);
}

static void parse_ogg_tags(const wchar_t *path, wchar_t *title, size_t titleCap, wchar_t *artist, size_t artistCap) {
    FILE *f = _wfopen(path, L"rb");
    unsigned char *buf;
    size_t got, i;
    const size_t limit = 1024 * 1024;
    if (f == NULL) return;
    buf = (unsigned char *)malloc(limit);
    if (buf == NULL) { fclose(f); return; }
    got = fread(buf, 1, limit, f);
    fclose(f);
    for (i = 0; i + 16 < got; i++) {
        size_t p, j;
        uint32_t vendorLen, count;
        if (buf[i] != 3 || memcmp(buf + i + 1, "vorbis", 6) != 0) continue;
        p = i + 7;
        if (p + 4 > got) break;
        vendorLen = le32(buf + p); p += 4;
        if (vendorLen > got - p) continue;
        p += vendorLen;
        if (p + 4 > got) continue;
        count = le32(buf + p); p += 4;
        if (count > 50000) continue;
        for (j = 0; j < count && p + 4 <= got; j++) {
            uint32_t n = le32(buf + p);
            size_t eq;
            char *item;
            p += 4;
            if (n > got - p) break;
            item = (char *)malloc((size_t)n + 1);
            if (item == NULL) break;
            memcpy(item, buf + p, n); item[n] = 0;
            for (eq = 0; eq < n && item[eq] != '='; eq++) item[eq] = (char)toupper((unsigned char)item[eq]);
            if (eq < n) {
                if (eq == 5 && memcmp(item, "TITLE", 5) == 0 && title[0] == 0) utf8_to_w(item + eq + 1, (int)(n - eq - 1), title, titleCap);
                if (eq == 6 && memcmp(item, "ARTIST", 6) == 0 && artist[0] == 0) utf8_to_w(item + eq + 1, (int)(n - eq - 1), artist, artistCap);
            }
            free(item);
            p += n;
        }
        if (title[0] || artist[0]) break;
    }
    free(buf);
}

static BOOL has_audio_extension(const wchar_t *path) {
    const wchar_t *dot = wcsrchr(path, L'.');
    if (dot == NULL) return FALSE;
    return _wcsicmp(dot, L".mp3") == 0 || _wcsicmp(dot, L".ogg") == 0 ||
           _wcsicmp(dot, L".wav") == 0 || _wcsicmp(dot, L".flac") == 0;
}

static void base_title(const wchar_t *path, wchar_t *out, size_t cap) {
    const wchar_t *p = wcsrchr(path, L'\\');
    const wchar_t *q = wcsrchr(path, L'/');
    const wchar_t *dot;
    if (p == NULL || (q != NULL && q > p)) p = q;
    p = p ? p + 1 : path;
    copy_w(out, cap, p);
    dot = wcsrchr(out, L'.');
    if (dot != NULL) ((wchar_t *)dot)[0] = 0;
}

static void make_track(const wchar_t *path, Track *t) {
    wchar_t title[256] = L"", artist[256] = L"";
    ma_decoder dec;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_uint64 frames = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    memset(t, 0, sizeof(*t));
    t->path = dup_w(path);
    if (t->path == NULL) return;
    if (_wcsicmp(wcsrchr(path, L'.') ? wcsrchr(path, L'.') : L"", L".mp3") == 0) parse_mp3_tags(path, title, 256, artist, 256);
    else if (_wcsicmp(wcsrchr(path, L'.') ? wcsrchr(path, L'.') : L"", L".ogg") == 0) parse_ogg_tags(path, title, 256, artist, 256);
    if (title[0] == 0) base_title(path, title, 256);
    t->title = dup_w(title);
    t->artist = dup_w(artist);
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) {
        t->fileBytes = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    }
    if (ma_decoder_init_file_w(path, &cfg, &dec) == MA_SUCCESS) {
        ma_decoder_get_length_in_pcm_frames(&dec, &frames);
        t->durationFrames = frames;
        t->sampleRate = dec.outputSampleRate;
        t->channels = dec.outputChannels;
        if (t->sampleRate != 0) t->durationMs = (uint32_t)((frames * 1000ULL) / t->sampleRate);
        if (t->durationMs != 0) t->bitrateKbps = (uint32_t)((t->fileBytes * 8ULL) / t->durationMs);
        ma_decoder_uninit(&dec);
    }
}

static void free_track(Track *t) {
    free(t->path); free(t->title); free(t->artist);
    memset(t, 0, sizeof(*t));
}

static int track_cmp(const void *a, const void *b) {
    const Track *x = (const Track *)a, *y = (const Track *)b;
    return _wcsicmp(x->path, y->path);
}

static BOOL grow_tracks(Track **items, int *capacity, int need) {
    int n = *capacity ? *capacity : 32;
    Track *p;
    while (n < need) {
        if (n > MAX_TRACKS / 2) { n = MAX_TRACKS; break; }
        n *= 2;
    }
    if (n < need) return FALSE;
    p = (Track *)realloc(*items, (size_t)n * sizeof(Track));
    if (p == NULL) return FALSE;
    *items = p; *capacity = n;
    return TRUE;
}

static BOOL grow_history(int need) {
    int n = g_historyCapacity ? g_historyCapacity : 32;
    int *p;
    while (n < need) n *= 2;
    p = (int *)realloc(g_history, (size_t)n * sizeof(int));
    if (p == NULL) return FALSE;
    g_history = p;
    g_historyCapacity = n;
    return TRUE;
}

static void scan_path(const wchar_t *path, Track **batch, int *count, int *capacity, int depth) {
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return;
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        wchar_t *pattern;
        WIN32_FIND_DATAW fd;
        HANDLE h;
        if (depth > 64 || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return;
        pattern = join_path(path, L"*");
        if (pattern == NULL) return;
        h = FindFirstFileW(pattern, &fd);
        free(pattern);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            wchar_t *child;
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            child = join_path(path, fd.cFileName);
            if (child == NULL) continue;
            scan_path(child, batch, count, capacity, depth + 1);
            free(child);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return;
    }
    if (!has_audio_extension(path) || *count >= MAX_TRACKS) return;
    if (!grow_tracks(batch, capacity, *count + 1)) return;
    make_track(path, &(*batch)[*count]);
    if ((*batch)[*count].path != NULL) (*count)++;
}

static void format_time(uint32_t ms, wchar_t *out, size_t cap) {
    uint32_t s = ms / 1000;
    uint32_t m = s / 60;
    s %= 60;
    if (m >= 60) swprintf(out, cap, L"%u:%02u:%02u", m / 60, m % 60, s);
    else swprintf(out, cap, L"%u:%02u", m, s);
}

static void format_track_name(const Track *t, wchar_t *out, size_t cap) {
    if (t->artist != NULL && t->artist[0] != 0) swprintf(out, cap, L"%s - %s", t->artist, t->title ? t->title : L"");
    else copy_w(out, cap, t->title ? t->title : L"");
}

static void rebuild_playlist(void) {
    if (g_current >= 0 && g_current < g_trackCount) {
        g_playlistFocus = g_current;
        g_tracks[g_current].selected = TRUE;
    } else if (g_playlistFocus < 0 && g_trackCount > 0) {
        g_playlistFocus = 0;
    }
    if (g_playlistView) {
        RECT client;
        int page = 1;
        GetClientRect(g_playlistView, &client);
        page = ui_unscale_extent(client.bottom - client.top) / PLAYLIST_ROW_HEIGHT;
        if (page < 1) page = 1;
        if (g_playlistFocus >= 0) {
            if (g_playlistFocus < g_playlistTop) g_playlistTop = g_playlistFocus;
            else if (g_playlistFocus >= g_playlistTop + page) g_playlistTop = g_playlistFocus - page + 1;
        }
        resize_playlist_view(g_playlistWindow);
        invalidate_playlist();
    }
}

static void push_history(int idx) {
    if (!grow_history(g_historyCount + 1)) return;
    g_history[g_historyCount++] = idx;
    if (g_historyCount > 2048) {
        memmove(g_history, g_history + g_historyCount - 1024, 1024 * sizeof(int));
        g_historyCount = 1024;
    }
}

static void stop_sound(void) {
    if (g_soundReady) {
        ma_sound_stop(&g_sound);
        ma_sound_uninit(&g_sound);
        g_soundReady = FALSE;
    }
    g_paused = FALSE;
}

static void update_player(void) {
    if (g_playerWindow) InvalidateRect(g_playerWindow, NULL, FALSE);
    if (g_playlistView) invalidate_playlist();
    update_taskbar_buttons();
}

static void play_track(int idx, BOOL addHistory) {
    ma_result r;
    if (idx < 0 || idx >= g_trackCount || !g_engineReady) return;
    stop_sound();
    g_current = idx;
    g_tracks[idx].played = TRUE;
    r = ma_sound_init_from_file_w(&g_engine, g_tracks[idx].path,
                                  MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION,
                                  NULL, NULL, &g_sound);
    if (r != MA_SUCCESS) {
        update_player();
        MessageBoxW(g_playerWindow, L"This audio file could not be opened.", g_tracks[idx].path, MB_ICONERROR | MB_OK);
        return;
    }
    g_soundReady = TRUE;
    ma_sound_set_looping(&g_sound, g_repeatMode == REPEAT_SONG);
    ma_sound_set_volume(&g_sound, 1.0f);
    ma_sound_start(&g_sound);
    if (addHistory) push_history(idx);
    rebuild_playlist();
    update_player();
}

static void clear_played(void) {
    int i;
    for (i = 0; i < g_trackCount; i++) g_tracks[i].played = FALSE;
    if (g_current >= 0 && g_current < g_trackCount) g_tracks[g_current].played = TRUE;
}

static int choose_random_next(void) {
    int i, n = 0, pick;
    for (i = 0; i < g_trackCount; i++) if (!g_tracks[i].played) n++;
    if (n == 0 && g_repeatMode == REPEAT_PLAYLIST && g_trackCount > 0) {
        for (i = 0; i < g_trackCount; i++) g_tracks[i].played = FALSE;
        n = g_trackCount;
    }
    if (n == 0) return -1;
    pick = rand() % n;
    for (i = 0; i < g_trackCount; i++) {
        if (g_tracks[i].played) continue;
        if (pick-- == 0) return i;
    }
    return -1;
}

static int next_index(void) {
    if (g_trackCount == 0) return -1;
    if (g_random) return choose_random_next();
    if (g_current + 1 < g_trackCount) return g_current + 1;
    return g_repeatMode == REPEAT_PLAYLIST ? 0 : -1;
}

static void advance_next(void) {
    int idx;
    if (g_repeatMode == REPEAT_SONG && g_soundReady) {
        ma_sound_seek_to_pcm_frame(&g_sound, 0);
        ma_sound_start(&g_sound);
        update_player();
        return;
    }
    idx = next_index();
    if (idx < 0) {
        stop_sound();
        update_player();
        return;
    }
    play_track(idx, TRUE);
}

static void next_button(void) {
    int idx;
    if (g_trackCount == 0) return;
    if (g_random && g_current >= 0 && g_current < g_trackCount && !g_tracks[g_current].played) g_tracks[g_current].played = TRUE;
    idx = next_index();
    if (idx < 0) {
        stop_sound();
        update_player();
        return;
    }
    play_track(idx, TRUE);
}

static void previous_button(void) {
    ma_uint64 cursor = 0;
    if (g_current < 0 || g_trackCount == 0) return;
    if (g_soundReady) ma_sound_get_cursor_in_pcm_frames(&g_sound, &cursor);
    if (g_soundReady && g_tracks[g_current].sampleRate && cursor > (ma_uint64)g_tracks[g_current].sampleRate * 3) {
        ma_sound_seek_to_pcm_frame(&g_sound, 0);
        if (!g_paused) ma_sound_start(&g_sound);
        update_player();
        return;
    }
    if (g_random && g_historyCount > 1) {
        g_historyCount--;
        play_track(g_history[g_historyCount - 1], FALSE);
        return;
    }
    if (g_current > 0) play_track(g_current - 1, TRUE);
    else if (g_repeatMode == REPEAT_PLAYLIST && g_trackCount > 0) play_track(g_trackCount - 1, TRUE);
    else if (g_soundReady) { ma_sound_seek_to_pcm_frame(&g_sound, 0); ma_sound_start(&g_sound); }
}

static void toggle_random(void) {
    g_random = !g_random;
    clear_played();
    g_historyCount = 0;
    if (g_current >= 0) push_history(g_current);
    update_player();
}

static void cycle_repeat_mode(void) {
    g_repeatMode = (RepeatMode)((g_repeatMode + 1) % 3);
    if (g_soundReady) ma_sound_set_looping(&g_sound, g_repeatMode == REPEAT_SONG);
    update_player();
}

static void choose_track(int idx) {
    if (idx >= 0 && idx < g_trackCount) play_track(idx, TRUE);
}

static void play_pause(void) {
    if (!g_soundReady) {
        if (g_current >= 0) play_track(g_current, TRUE);
        else if (g_trackCount > 0) play_track(0, TRUE);
        return;
    }
    if (g_paused) {
        ma_sound_start(&g_sound);
        g_paused = FALSE;
    } else {
        ma_sound_stop(&g_sound);
        g_paused = TRUE;
    }
    update_player();
}

static void stop_button(void) {
    if (g_soundReady) { ma_sound_stop(&g_sound); ma_sound_seek_to_pcm_frame(&g_sound, 0); }
    g_paused = TRUE;
    update_player();
}

static void remove_selected(void) {
    BOOL *remove;
    int i, newCurrent = -1, oldCurrent = g_current, desiredOriginal = -1, desiredIndex = -1;
    BOOL removedCurrent = FALSE;
    if (!g_playlistView || g_trackCount == 0) return;
    remove = (BOOL *)calloc((size_t)g_trackCount, sizeof(BOOL));
    if (remove == NULL) return;
    for (i = 0; i < g_trackCount; i++) remove[i] = g_tracks[i].selected;
    for (i = 0; i < g_trackCount; i++) if (remove[i] && i == oldCurrent) removedCurrent = TRUE;
    if (removedCurrent && !g_random) {
        for (i = oldCurrent + 1; i < g_trackCount; i++) {
            if (!remove[i]) { desiredOriginal = i; break; }
        }
        if (desiredOriginal < 0 && g_repeatMode == REPEAT_PLAYLIST) {
            for (i = 0; i < g_trackCount; i++) {
                if (!remove[i]) { desiredOriginal = i; break; }
            }
        }
        if (desiredOriginal >= 0) {
            int removedBefore = 0;
            for (i = 0; i < desiredOriginal; i++) if (remove[i]) removedBefore++;
            desiredIndex = desiredOriginal - removedBefore;
        }
    }
    if (!removedCurrent && oldCurrent >= 0) {
        newCurrent = oldCurrent;
        for (i = 0; i < oldCurrent; i++) if (remove[i]) newCurrent--;
    }
    for (i = g_trackCount - 1; i >= 0; i--) {
        if (remove[i]) {
            free_track(&g_tracks[i]);
            memmove(&g_tracks[i], &g_tracks[i + 1], (size_t)(g_trackCount - i - 1) * sizeof(Track));
            g_trackCount--;
        }
    }
    free(remove);
    g_playlistFocus = -1;
    g_playlistAnchor = -1;
    if (g_current >= 0 && g_current < g_trackCount) {
        g_playlistFocus = g_current;
        g_tracks[g_current].selected = TRUE;
    } else if (g_trackCount > 0) {
        g_playlistFocus = g_current < 0 ? 0 : g_trackCount - 1;
        g_tracks[g_playlistFocus].selected = TRUE;
    }
    if (g_playlistTop >= g_trackCount) g_playlistTop = g_trackCount > 0 ? g_trackCount - 1 : 0;
    g_historyCount = 0;
    if (removedCurrent) {
        stop_sound();
        g_current = -1;
        rebuild_playlist();
        if (g_trackCount > 0) {
            int idx = -1;
            if (g_random) idx = choose_random_next();
            else idx = desiredIndex;
            if (idx >= 0) play_track(idx, TRUE);
        }
    } else {
        g_current = newCurrent;
        if (g_current >= g_trackCount) g_current = g_trackCount - 1;
        rebuild_playlist();
        update_player();
    }
}

static void open_track_location(int index) {
    PIDLIST_ABSOLUTE item = NULL;
    if (index < 0 || index >= g_trackCount || g_tracks[index].path == NULL) return;
    if (!g_comReady) return;
    if (SUCCEEDED(SHParseDisplayName(g_tracks[index].path, NULL, &item, 0, NULL)) && item != NULL) {
        SHOpenFolderAndSelectItems(item, 0, NULL, 0);
        CoTaskMemFree(item);
    }
}

static void add_paths(const wchar_t **paths, int pathCount, BOOL playFirst) {
    Track *batch = NULL;
    int count = 0, capacity = 0;
    int i, firstNew = g_trackCount;
    for (i = 0; i < pathCount; i++) {
        if (paths[i] == NULL || paths[i][0] == 0) continue;
        scan_path(paths[i], &batch, &count, &capacity, 0);
    }
    if (count == 0) { free(batch); return; }
    qsort(batch, (size_t)count, sizeof(Track), track_cmp);
    if (grow_tracks(&g_tracks, &g_trackCapacity, g_trackCount + count)) {
        for (i = 0; i < count; i++) g_tracks[g_trackCount++] = batch[i];
    } else {
        for (i = 0; i < count; i++) free_track(&batch[i]);
        free(batch);
        return;
    }
    free(batch);
    if (playFirst) {
        play_track(firstNew, TRUE);
        return;
    }
    rebuild_playlist();
    update_player();
    if (g_current < 0 && g_trackCount > 0) {
        g_playlistFocus = 0;
        g_tracks[0].selected = TRUE;
        invalidate_playlist();
    }
}

static void add_dropped_paths(HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0), i;
    wchar_t **paths = (wchar_t **)calloc(n > 0 ? n : 1, sizeof(wchar_t *));
    if (paths != NULL) for (i = 0; i < n; i++) {
        UINT cch = DragQueryFileW(drop, i, NULL, 0);
        paths[i] = (wchar_t *)malloc(((size_t)cch + 1) * sizeof(wchar_t));
        if (paths[i] != NULL) DragQueryFileW(drop, i, paths[i], cch + 1);
    }
    DragFinish(drop);
    if (paths != NULL) add_paths((const wchar_t **)paths, (int)n, FALSE);
    for (i = 0; i < n && paths != NULL; i++) free(paths[i]);
    free(paths);
}

static void draw_text(HDC dc, const wchar_t *s, RECT r, COLORREF color, UINT flags, HFONT font) {
    HGDIOBJ oldFont = NULL;
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    if (font) oldFont = SelectObject(dc, font);
    DrawTextW(dc, s, -1, &r, flags | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont) SelectObject(dc, oldFont);
}

static void fill_rect(HDC dc, RECT r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void frame_rect(HDC dc, RECT r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}

static void ui_draw_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL); LineTo(dc, x2, y2);
    SelectObject(dc, old); DeleteObject(pen);
}

static void ui_draw_polyline(HDC dc, POINT *points, int count, COLORREF color, int width, BOOL closed) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (count > 1) Polyline(dc, points, count);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(pen);
    if (closed && count > 1) ui_draw_line(dc, points[count - 1].x, points[count - 1].y,
                                          points[0].x, points[0].y, color, width);
}

static void ui_draw_outline_rect(HDC dc, RECT r, COLORREF color, int width, int radius) {
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (radius > 0) RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    else Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
}

static void fill_circle(HDC dc, int cx, int cy, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    Ellipse(dc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

static void draw_polygon(HDC dc, POINT *pts, int count, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    Polygon(dc, pts, count);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

static void draw_button_icon(HDC dc, ButtonId id, BOOL active) {
    RECT r = g_buttonRects[id];
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int h = r.bottom - r.top;
    int s = h / 2 - 5;
    COLORREF ink = active ? C_LED : C_TEXT;
    fill_rect(dc, r, active ? C_BUTTON_ON : C_BUTTON);
    frame_rect(dc, r, RGB(88, 96, 97));
    switch (id) {
        case BTN_PREV: {
            POINT p[3] = {{cx + s - 2, cy - s}, {cx + s - 2, cy + s}, {cx - s + 5, cy}};
            ui_draw_polyline(dc, p, 3, ink, 2, TRUE);
            ui_draw_line(dc, cx - s, cy - s, cx - s, cy + s, ink, 2); break;
        }
        case BTN_PLAY_PAUSE: {
            if (g_soundReady && !g_paused) {
                RECT a = {cx - 9, cy - s, cx - 3, cy + s};
                RECT b = {cx + 3, cy - s, cx + 9, cy + s};
                ui_draw_outline_rect(dc, a, ink, 2, 2); ui_draw_outline_rect(dc, b, ink, 2, 2);
            } else {
                POINT p[3] = {{cx - s / 2, cy - s}, {cx - s / 2, cy + s}, {cx + s, cy}};
                ui_draw_polyline(dc, p, 3, ink, 2, TRUE);
            }
            break;
        }
        case BTN_STOP: {
            RECT a = {cx - 8, cy - 8, cx + 8, cy + 8}; ui_draw_outline_rect(dc, a, ink, 2, 2); break;
        }
        case BTN_NEXT: {
            POINT p[3] = {{cx - s + 2, cy - s}, {cx - s + 2, cy + s}, {cx + s - 5, cy}};
            ui_draw_polyline(dc, p, 3, ink, 2, TRUE);
            ui_draw_line(dc, cx + s, cy - s, cx + s, cy + s, ink, 2); break;
        }
        case BTN_RANDOM: {
            POINT a[3] = {{cx + s - 5, cy - s / 2 - 4}, {cx + s, cy - s / 2}, {cx + s - 5, cy - s / 2 + 4}};
            POINT b[3] = {{cx + s - 5, cy + s / 2 - 4}, {cx + s, cy + s / 2}, {cx + s - 5, cy + s / 2 + 4}};
            ui_draw_line(dc, cx - s, cy - s / 2, cx + s - 2, cy + s / 2, ink, 2);
            ui_draw_line(dc, cx - s, cy + s / 2, cx + s - 2, cy - s / 2, ink, 2);
            ui_draw_polyline(dc, a, 3, ink, 2, FALSE); ui_draw_polyline(dc, b, 3, ink, 2, FALSE); break;
        }
        case BTN_REPEAT: {
            int left = cx - s, right = cx + s, top = cy - s + 1, bottom = cy + s - 1;
            ui_draw_line(dc, left + 2, top, right - 5, top, C_LED, 2);
            ui_draw_line(dc, right - 5, top, right - 5, cy - 5, C_LED, 2);
            ui_draw_line(dc, right - 10, cy - 9, right - 5, cy - 4, C_LED, 2);
            ui_draw_line(dc, right, cy - 9, right - 5, cy - 4, C_LED, 2);
            ui_draw_line(dc, right - 2, bottom, left + 5, bottom, C_LED, 2);
            ui_draw_line(dc, left + 5, bottom, left + 5, cy + 5, C_LED, 2);
            ui_draw_line(dc, left, cy + 9, left + 5, cy + 4, C_LED, 2);
            ui_draw_line(dc, left + 10, cy + 9, left + 5, cy + 4, C_LED, 2);
            if (g_repeatMode == REPEAT_PLAYLIST) {
                fill_circle(dc, cx - 3, cy, 2, C_LED);
                fill_circle(dc, cx + 3, cy, 2, C_LED);
            } else if (g_repeatMode == REPEAT_SONG) fill_circle(dc, cx, cy, 2, C_LED);
            break;
        }
        case BTN_PLAYLIST:
            { int j; for (j = -7; j <= 7; j += 7) { RECT dot = {cx - 10, cy + j - 1, cx - 7, cy + j + 2};
              fill_rect(dc, dot, ink); ui_draw_line(dc, cx - 3, cy + j, cx + 10, cy + j, ink, 2); } } break;
        default: break;
    }
}

static void update_slider_rects(int width) {
    int x = 12, y = 153, i;
    int bw = (width - 24 - (BTN_COUNT - 1) * 6) / BTN_COUNT;
    for (i = 0; i < BTN_COUNT; i++) {
        g_buttonRects[i].left = x + i * (bw + 6);
        g_buttonRects[i].right = g_buttonRects[i].left + bw;
        g_buttonRects[i].top = y;
        g_buttonRects[i].bottom = y + 36;
    }
    g_seekRect.left = 15; g_seekRect.right = width - 18; g_seekRect.top = 105; g_seekRect.bottom = 120;
    g_timeRect.left = 15; g_timeRect.right = width - 18; g_timeRect.top = 124; g_timeRect.bottom = 148;
    g_muteRect.left = 72; g_muteRect.right = 106; g_muteRect.top = 215; g_muteRect.bottom = 244;
    g_volumeRect.left = 116; g_volumeRect.right = width - 91; g_volumeRect.top = 222; g_volumeRect.bottom = 237;
    if (g_tooltip) {
        for (i = 0; i < BTN_COUNT; i++) {
            TOOLINFOW ti; memset(&ti, 0, sizeof(ti)); ti.cbSize = sizeof(ti); ti.hwnd = g_playerWindow; ti.uId = (UINT_PTR)(i + 1); ti.rect = ui_rect_to_pixels(g_buttonRects[i]);
            SendMessageW(g_tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
        }
        { TOOLINFOW ti; memset(&ti, 0, sizeof(ti)); ti.cbSize = sizeof(ti); ti.hwnd = g_playerWindow; ti.uId = 100; ti.rect = ui_rect_to_pixels(g_muteRect); SendMessageW(g_tooltip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti); }
    }
}

static uint32_t current_duration_ms(void) {
    ma_uint64 frames = 0;
    ma_uint32 channels = 0, rate = 0;
    ma_format fmt;
    if (!g_soundReady) return g_current >= 0 ? g_tracks[g_current].durationMs : 0;
    ma_sound_get_data_format(&g_sound, &fmt, &channels, &rate, NULL, 0);
    ma_sound_get_length_in_pcm_frames(&g_sound, &frames);
    if (rate == 0) return 0;
    return (uint32_t)((frames * 1000ULL) / rate);
}

static uint32_t current_position_ms(void) {
    ma_uint64 frames = 0;
    ma_uint32 channels = 0, rate = 0;
    ma_format fmt;
    if (!g_soundReady) return 0;
    ma_sound_get_data_format(&g_sound, &fmt, &channels, &rate, NULL, 0);
    ma_sound_get_cursor_in_pcm_frames(&g_sound, &frames);
    if (rate == 0) return 0;
    return (uint32_t)((frames * 1000ULL) / rate);
}

static void paint_player(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int pixelWidth = cr.right, pixelHeight = cr.bottom;
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, pixelWidth, pixelHeight);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    int w = ui_unscale_extent(pixelWidth);
    wchar_t title[600] = L"No track loaded", info[256] = L"Drop music files or folders into the playlist", time[80];
    wchar_t pos[32], duration[32];
    uint32_t dms, pms;
    HFONT led = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    cr.right = w; cr.bottom = ui_unscale_extent(pixelHeight);
    set_ui_transform(dc);
    update_slider_rects(w);
    fill_rect(dc, cr, C_BG);
    { RECT p = {12, 12, w - 12, 93}; fill_rect(dc, p, C_PANEL); frame_rect(dc, p, RGB(72, 81, 78)); }
    if (g_current >= 0 && g_current < g_trackCount) {
        Track *t = &g_tracks[g_current];
        format_track_name(t, title, 600);
        if (t->channels == 1) swprintf(info, 256, L"%u kbps   Mono   %u Hz", t->bitrateKbps, t->sampleRate);
        else if (t->channels == 2) swprintf(info, 256, L"%u kbps   Stereo   %u Hz", t->bitrateKbps, t->sampleRate);
        else swprintf(info, 256, L"%u kbps   %u channels   %u Hz", t->bitrateKbps, t->channels, t->sampleRate);
    }
    { RECT r = {22, 21, w - 24, 52}; draw_text(dc, title, r, C_LED, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS, led); }
    { RECT r = {22, 57, w - 24, 84}; draw_text(dc, info, r, C_MUTED, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS, g_playerFont); }
    dms = current_duration_ms(); pms = g_dragSeek ? (uint32_t)((uint64_t)g_seekPreview * dms / 1000) : current_position_ms();
    fill_rect(dc, g_seekRect, RGB(8, 11, 12));
    frame_rect(dc, g_seekRect, RGB(82, 91, 89));
    if (dms > 0) {
        RECT f = g_seekRect;
        f.right = f.left + (int)((uint64_t)(f.right - f.left) * pms / dms);
        if (f.right < f.left + 2) f.right = f.left + 2;
        fill_rect(dc, f, C_ACCENT);
    }
    format_time(pms, pos, 32); format_time(dms, duration, 32);
    swprintf(time, 80, L"%s / %s", pos, duration);
    { RECT r = g_timeRect; draw_text(dc, time, r, C_TEXT, DT_RIGHT | DT_VCENTER, g_playerFont); }
    draw_button_icon(dc, BTN_PREV, FALSE);
    draw_button_icon(dc, BTN_PLAY_PAUSE, g_soundReady && !g_paused);
    draw_button_icon(dc, BTN_STOP, FALSE);
    draw_button_icon(dc, BTN_NEXT, FALSE);
    draw_button_icon(dc, BTN_RANDOM, g_random);
    draw_button_icon(dc, BTN_REPEAT, g_repeatMode != REPEAT_OFF);
    draw_button_icon(dc, BTN_PLAYLIST, IsWindowVisible(g_playlistWindow));
    { RECT r = {15, 216, 64, 244}; draw_text(dc, L"VOL", r, C_MUTED, DT_LEFT | DT_VCENTER, g_playerFont); }
    fill_rect(dc, g_muteRect, g_muted ? C_BUTTON_ON : C_BUTTON);
    frame_rect(dc, g_muteRect, RGB(88, 96, 97));
    { int x = (g_muteRect.left + g_muteRect.right) / 2, y = (g_muteRect.top + g_muteRect.bottom) / 2;
      POINT sp[6] = {{x - 9,y - 4},{x - 4,y - 4},{x + 2,y - 9},{x + 2,y + 9},{x - 4,y + 4},{x - 9,y + 4}};
      draw_polygon(dc, sp, 6, g_muted ? C_LED : C_TEXT);
      if (g_muted) { ui_draw_line(dc, x + 6, y - 6, x + 12, y + 6, C_LED, 2); ui_draw_line(dc, x + 12, y - 6, x + 6, y + 6, C_LED, 2); }
      else { ui_draw_line(dc, x + 5, y - 5, x + 8, y - 2, C_TEXT, 2); ui_draw_line(dc, x + 8, y - 2, x + 8, y + 2, C_TEXT, 2); ui_draw_line(dc, x + 8, y + 2, x + 5, y + 5, C_TEXT, 2); }
    }
    fill_rect(dc, g_volumeRect, RGB(8, 11, 12)); frame_rect(dc, g_volumeRect, RGB(82, 91, 89));
    { RECT f = g_volumeRect; f.right = f.left + (int)((f.right - f.left) * g_volume); fill_rect(dc, f, C_ACCENT); }
    { RECT r = {w - 83, 216, w - 14, 244}; wchar_t v[20]; swprintf(v, 20, L"%u%%", (unsigned)(g_volume * 100)); draw_text(dc, v, r, C_TEXT, DT_RIGHT | DT_VCENTER, g_playerFont); }
    DeleteObject(led);
    reset_ui_transform(dc);
    BitBlt(screen, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
    SelectObject(dc, oldBitmap); DeleteObject(bitmap); DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

static void set_seek_from_x(int x) {
    int w = g_seekRect.right - g_seekRect.left;
    if (w <= 0) return;
    if (x < g_seekRect.left) x = g_seekRect.left;
    if (x > g_seekRect.right) x = g_seekRect.right;
    g_seekPreview = (int)((uint64_t)(x - g_seekRect.left) * 1000 / w);
    invalidate_player_rect(g_seekRect);
    invalidate_player_rect(g_timeRect);
}

static void set_volume_from_x(int x) {
    int w = g_volumeRect.right - g_volumeRect.left;
    if (w <= 0) return;
    if (x < g_volumeRect.left) x = g_volumeRect.left;
    if (x > g_volumeRect.right) x = g_volumeRect.right;
    g_volume = (float)(x - g_volumeRect.left) / (float)w;
    g_muted = FALSE;
    ma_engine_set_volume(&g_engine, g_volume);
    invalidate_player_rect(g_volumeRect);
    invalidate_player_rect(g_muteRect);
    { RECT r = {g_volumeRect.right, 210, g_volumeRect.right + 90, 250}; invalidate_player_rect(r); }
}

static void toggle_mute(void) {
    g_muted = !g_muted;
    ma_engine_set_volume(&g_engine, g_muted ? 0.0f : g_volume);
    invalidate_player_rect(g_muteRect);
}

static void click_button(int id) {
    switch (id) {
        case BTN_PREV: previous_button(); break;
        case BTN_PLAY_PAUSE: play_pause(); break;
        case BTN_STOP: stop_button(); break;
        case BTN_NEXT: next_button(); break;
        case BTN_RANDOM: toggle_random(); break;
        case BTN_REPEAT: cycle_repeat_mode(); break;
        case BTN_PLAYLIST:
            if (IsWindowVisible(g_playlistWindow)) ShowWindow(g_playlistWindow, SW_HIDE);
            else { ShowWindow(g_playlistWindow, SW_SHOWNORMAL); SetForegroundWindow(g_playlistWindow); keep_player_above_playlist(); }
            update_player(); break;
        default: break;
    }
}

static void create_player_tooltip(HWND hwnd) {
    static const wchar_t *hints[BTN_COUNT] = {
        L"Previous track", L"Play / pause", L"Stop", L"Next track",
        L"Random play", L"Repeat: playlist / song / off", L"Show / hide playlist"
    };
    int i;
    RECT client;
    GetClientRect(hwnd, &client);
    update_slider_rects(ui_unscale_extent(client.right));
    g_tooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                hwnd, NULL, g_instance, NULL);
    if (!g_tooltip) return;
    for (i = 0; i < BTN_COUNT; i++) {
        TOOLINFOW ti; memset(&ti, 0, sizeof(ti)); ti.cbSize = sizeof(ti); ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = hwnd; ti.uId = (UINT_PTR)(i + 1); ti.rect = ui_rect_to_pixels(g_buttonRects[i]); ti.lpszText = (LPWSTR)hints[i];
        SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    { TOOLINFOW ti; memset(&ti, 0, sizeof(ti)); ti.cbSize = sizeof(ti); ti.uFlags = TTF_SUBCLASS;
      ti.hwnd = hwnd; ti.uId = 100; ti.rect = ui_rect_to_pixels(g_muteRect); ti.lpszText = L"Mute / unmute";
      SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti); }
}

static void set_taskbar_bitmap_pixel(DWORD *pixels, int x, int y, DWORD color) {
    pixels[y * 32 + x] = color;
}

static HBITMAP create_taskbar_bitmap(int kind) {
    BITMAPINFO info;
    HBITMAP bitmap;
    DWORD *pixels = NULL;
    DWORD green = ((DWORD)GetRValue(C_LED) << 16) | ((DWORD)GetGValue(C_LED) << 8) | GetBValue(C_LED);
    DWORD key = 0x00FF00FFu;
    int x, y;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    if (!bitmap || !pixels) return NULL;
    for (y = 0; y < 32 * 32; y++) pixels[y] = key;
    if (kind == THUMB_ICON_PAUSE) {
        for (y = 6; y <= 25; y++) {
            for (x = 7; x <= 12; x++) set_taskbar_bitmap_pixel(pixels, x, y, green);
            for (x = 20; x <= 25; x++) set_taskbar_bitmap_pixel(pixels, x, y, green);
        }
    } else if (kind == THUMB_ICON_PLAY) {
        for (y = 4; y <= 27; y++) {
            int extent = y < 16 ? y - 4 : 27 - y;
            for (x = 6; x <= 6 + extent * 3 / 2; x++) set_taskbar_bitmap_pixel(pixels, x, y, green);
        }
    } else if (kind == THUMB_ICON_PREVIOUS) {
        for (y = 4; y <= 27; y++) {
            int extent = y < 16 ? y - 4 : 27 - y;
            for (x = 27 - extent * 3 / 2; x <= 27; x++) set_taskbar_bitmap_pixel(pixels, x, y, green);
        }
        for (y = 4; y <= 27; y++) for (x = 3; x <= 5; x++) set_taskbar_bitmap_pixel(pixels, x, y, green);
    } else {
        for (y = 4; y <= 27; y++) {
            int extent = y < 16 ? y - 4 : 27 - y;
            for (x = 4; x <= 4 + extent * 3 / 2; x++) set_taskbar_bitmap_pixel(pixels, x, y, green);
        }
        for (y = 4; y <= 27; y++) for (x = 26; x <= 28; x++) set_taskbar_bitmap_pixel(pixels, x, y, green);
    }
    return bitmap;
}

static BOOL create_taskbar_image_list(void) {
    int kind, expected = 0;
    if (g_taskbarImageList) return TRUE;
    g_taskbarImageList = ImageList_Create(32, 32, ILC_COLOR32 | ILC_MASK, 4, 0);
    if (!g_taskbarImageList) return FALSE;
    for (kind = THUMB_ICON_PREVIOUS; kind <= THUMB_ICON_NEXT; kind++) {
        HBITMAP bitmap = create_taskbar_bitmap(kind);
        int added;
        if (!bitmap) { ImageList_Destroy(g_taskbarImageList); g_taskbarImageList = NULL; return FALSE; }
        added = ImageList_AddMasked(g_taskbarImageList, bitmap, RGB(255, 0, 255));
        DeleteObject(bitmap);
        if (added != expected++) { ImageList_Destroy(g_taskbarImageList); g_taskbarImageList = NULL; return FALSE; }
    }
    return TRUE;
}

static void set_taskbar_button(THUMBBUTTON *button, UINT id, int imageIndex, const wchar_t *tip) {
    memset(button, 0, sizeof(*button));
    button->dwMask = THB_BITMAP | THB_TOOLTIP | THB_FLAGS;
    button->iId = id;
    button->iBitmap = (UINT)imageIndex;
    copy_w(button->szTip, sizeof(button->szTip) / sizeof(button->szTip[0]), tip);
    button->dwFlags = THBF_ENABLED;
}

static void update_taskbar_buttons(void) {
    if (!g_taskbar || !g_taskbarButtonsAdded || !g_playerWindow) return;
    set_taskbar_button(&g_taskbarButtons[0], TASKBAR_PREVIOUS, THUMB_ICON_PREVIOUS, L"Previous");
    set_taskbar_button(&g_taskbarButtons[1], TASKBAR_PLAY_PAUSE,
                       (g_soundReady && !g_paused) ? THUMB_ICON_PAUSE : THUMB_ICON_PLAY,
                       (g_soundReady && !g_paused) ? L"Pause" : L"Play");
    set_taskbar_button(&g_taskbarButtons[2], TASKBAR_NEXT, THUMB_ICON_NEXT, L"Next");
    ITaskbarList3_ThumbBarUpdateButtons(g_taskbar, g_playerWindow, 3, g_taskbarButtons);
}

static void add_taskbar_buttons(HWND hwnd) {
    HRESULT hr;
    if (!g_comReady) return;
    if (!g_taskbar) {
        hr = CoCreateInstance(&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
                              &IID_ITaskbarList3, (void **)&g_taskbar);
        if (FAILED(hr) || !g_taskbar) return;
        if (FAILED(ITaskbarList3_HrInit(g_taskbar))) {
            ITaskbarList3_Release(g_taskbar);
            g_taskbar = NULL;
            return;
        }
    } else {
        ITaskbarList3_HrInit(g_taskbar);
    }
    if (!create_taskbar_image_list()) return;
    if (FAILED(ITaskbarList3_ThumbBarSetImageList(g_taskbar, hwnd, g_taskbarImageList))) return;
    set_taskbar_button(&g_taskbarButtons[0], TASKBAR_PREVIOUS, THUMB_ICON_PREVIOUS, L"Previous");
    set_taskbar_button(&g_taskbarButtons[1], TASKBAR_PLAY_PAUSE,
                       (g_soundReady && !g_paused) ? THUMB_ICON_PAUSE : THUMB_ICON_PLAY,
                       (g_soundReady && !g_paused) ? L"Pause" : L"Play");
    set_taskbar_button(&g_taskbarButtons[2], TASKBAR_NEXT, THUMB_ICON_NEXT, L"Next");
    hr = ITaskbarList3_ThumbBarAddButtons(g_taskbar, hwnd, 3, g_taskbarButtons);
    g_taskbarButtonsAdded = SUCCEEDED(hr);
}

static void release_taskbar(void) {
    g_taskbarButtonsAdded = FALSE;
    if (g_taskbar) { ITaskbarList3_Release(g_taskbar); g_taskbar = NULL; }
    if (g_taskbarImageList) { ImageList_Destroy(g_taskbarImageList); g_taskbarImageList = NULL; }
}

static void keep_player_above_playlist(void) {
    if (!g_playerWindow || !g_playlistWindow || !IsWindow(g_playerWindow) || !IsWindow(g_playlistWindow)) return;
    if (!IsWindowVisible(g_playerWindow) || !IsWindowVisible(g_playlistWindow) || IsIconic(g_playerWindow)) return;
    SetWindowPos(g_playlistWindow, g_playerWindow, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static void resize_window_for_scale(HWND hwnd, int oldPercent, int newPercent) {
    DWORD style, exStyle;
    BOOL hasMenu;
    RECT client = {0, 0, 0, 0}, frame = {0, 0, 0, 0};
    if (!hwnd || !IsWindow(hwnd) || oldPercent <= 0) return;
    style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    exStyle = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    hasMenu = GetMenu(hwnd) != NULL;
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT placement;
        int outerWidth, outerHeight, clientWidth, clientHeight;
        int frameWidth, frameHeight, newOuterWidth, newOuterHeight;
        memset(&placement, 0, sizeof(placement)); placement.length = sizeof(placement);
        if (!GetWindowPlacement(hwnd, &placement)) return;
        outerWidth = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
        outerHeight = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
        if (!AdjustWindowRectEx(&frame, style, hasMenu, exStyle)) return;
        frameWidth = frame.right - frame.left; frameHeight = frame.bottom - frame.top;
        clientWidth = outerWidth - frameWidth; clientHeight = outerHeight - frameHeight;
        client.right = (int)(((int64_t)clientWidth * newPercent + oldPercent / 2) / oldPercent);
        client.bottom = (int)(((int64_t)clientHeight * newPercent + oldPercent / 2) / oldPercent);
        frame = client;
        if (!AdjustWindowRectEx(&frame, style, hasMenu, exStyle)) return;
        newOuterWidth = frame.right - frame.left; newOuterHeight = frame.bottom - frame.top;
        placement.rcNormalPosition.right = placement.rcNormalPosition.left + newOuterWidth;
        placement.rcNormalPosition.bottom = placement.rcNormalPosition.top + newOuterHeight;
        SetWindowPlacement(hwnd, &placement);
        return;
    }
    if (!GetClientRect(hwnd, &client)) return;
    client.right = (int)(((int64_t)client.right * newPercent + oldPercent / 2) / oldPercent);
    client.bottom = (int)(((int64_t)client.bottom * newPercent + oldPercent / 2) / oldPercent);
    frame = client;
    if (!AdjustWindowRectEx(&frame, style, hasMenu, exStyle)) return;
    SetWindowPos(hwnd, NULL, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

static void apply_ui_scale(int percent) {
    int oldPercent = g_uiScalePercent;
    if (percent == oldPercent) return;
    g_uiScalePercent = percent;
    resize_window_for_scale(g_playerWindow, oldPercent, percent);
    resize_window_for_scale(g_playlistWindow, oldPercent, percent);
    keep_player_above_playlist();
    if (g_playerWindow) InvalidateRect(g_playerWindow, NULL, FALSE);
    if (g_playlistView) InvalidateRect(g_playlistView, NULL, FALSE);
}

static void show_scale_menu(HWND hwnd, LPARAM lp) {
    HMENU menu = CreatePopupMenu(), scaleMenu = CreatePopupMenu();
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), command = 0;
    int selected = IDM_SCALE_100;
    if (!menu || !scaleMenu) { if (menu) DestroyMenu(menu); if (scaleMenu) DestroyMenu(scaleMenu); return; }
    AppendMenuW(scaleMenu, MF_STRING, IDM_SCALE_50, L"50%");
    AppendMenuW(scaleMenu, MF_STRING, IDM_SCALE_75, L"75%");
    AppendMenuW(scaleMenu, MF_STRING, IDM_SCALE_100, L"100%");
    AppendMenuW(scaleMenu, MF_STRING, IDM_SCALE_125, L"125%");
    AppendMenuW(scaleMenu, MF_STRING, IDM_SCALE_150, L"150%");
    AppendMenuW(scaleMenu, MF_STRING, IDM_SCALE_200, L"200%");
    switch (g_uiScalePercent) {
        case 50: selected = IDM_SCALE_50; break;
        case 75: selected = IDM_SCALE_75; break;
        case 125: selected = IDM_SCALE_125; break;
        case 150: selected = IDM_SCALE_150; break;
        case 200: selected = IDM_SCALE_200; break;
        default: break;
    }
    CheckMenuRadioItem(scaleMenu, IDM_SCALE_50, IDM_SCALE_200, selected, MF_BYCOMMAND);
    if (!AppendMenuW(menu, MF_POPUP, (UINT_PTR)scaleMenu, L"Scale")) {
        DestroyMenu(scaleMenu); DestroyMenu(menu); return;
    }
    if (x == -1 && y == -1) {
        RECT window; GetWindowRect(hwnd, &window);
        x = window.left + 24; y = window.top + 48;
    }
    command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, x, y, 0, hwnd, NULL);
    DestroyMenu(menu);
    switch (command) {
        case IDM_SCALE_50: apply_ui_scale(50); break;
        case IDM_SCALE_75: apply_ui_scale(75); break;
        case IDM_SCALE_100: apply_ui_scale(100); break;
        case IDM_SCALE_125: apply_ui_scale(125); break;
        case IDM_SCALE_150: apply_ui_scale(150); break;
        case IDM_SCALE_200: apply_ui_scale(200); break;
        default: break;
    }
}

static BOOL send_open_request(HWND target, int argc, wchar_t **argv) {
    size_t chars = 1;
    wchar_t *payload, *out;
    COPYDATASTRUCT cds;
    int i;
    if (argc <= 1) return TRUE;
    for (i = 1; i < argc; i++) chars += wcslen(argv[i]) + 1;
    if (chars > 32768 || chars * sizeof(wchar_t) > 0xFFFFFFFFu) return FALSE;
    payload = (wchar_t *)calloc(chars, sizeof(wchar_t));
    if (payload == NULL) return FALSE;
    out = payload;
    for (i = 1; i < argc; i++) {
        size_t n = wcslen(argv[i]);
        if (n == 0) continue;
        memcpy(out, argv[i], n * sizeof(wchar_t));
        out += n + 1;
    }
    memset(&cds, 0, sizeof(cds));
    cds.dwData = MYAMP_COPYDATA_TAG;
    cds.cbData = (DWORD)(chars * sizeof(wchar_t));
    cds.lpData = payload;
    i = (int)SendMessageW(target, WM_COPYDATA, 0, (LPARAM)&cds);
    free(payload);
    return i != 0;
}

static LRESULT CALLBACK player_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_taskbarButtonCreatedMessage && msg == g_taskbarButtonCreatedMessage) {
        add_taskbar_buttons(hwnd);
        return 0;
    }
    switch (msg) {
        case WM_CONTEXTMENU:
            show_scale_menu(hwnd, lp);
            return 0;
        case WM_ACTIVATE: {
            LRESULT result = DefWindowProcW(hwnd, msg, wp, lp);
            keep_player_above_playlist();
            return result;
        }
        case WM_CREATE:
            g_playerFont = create_player_ui_font();
            DragAcceptFiles(hwnd, TRUE);
            SetTimer(hwnd, PLAYER_TIMER, PLAYER_TIMER_MS, NULL);
            create_player_tooltip(hwnd);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_player(hwnd); return 0;
        case WM_SIZE: {
            RECT r;
            if (wp == SIZE_MINIMIZED) {
                if (!g_playerWasMinimized) {
                    g_playerWasMinimized = TRUE;
                    g_restorePlaylistOnRestore = IsWindowVisible(g_playlistWindow);
                    if (g_restorePlaylistOnRestore) ShowWindow(g_playlistWindow, SW_HIDE);
                }
            } else if (g_playerWasMinimized && (wp == SIZE_RESTORED || wp == SIZE_MAXIMIZED)) {
                g_playerWasMinimized = FALSE;
                if (g_restorePlaylistOnRestore) ShowWindow(g_playlistWindow, SW_SHOWNOACTIVATE);
                g_restorePlaylistOnRestore = FALSE;
                keep_player_above_playlist();
                update_player();
            }
            GetClientRect(hwnd, &r); update_slider_rects(ui_unscale_extent(r.right)); InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        case WM_COPYDATA: {
            const COPYDATASTRUCT *cds = (const COPYDATASTRUCT *)lp;
            wchar_t *payload;
            const wchar_t **paths;
            size_t chars;
            int count = 0, i;
            wchar_t *p;
            if (cds == NULL || cds->dwData != MYAMP_COPYDATA_TAG ||
                cds->cbData < 2 * sizeof(wchar_t) || cds->cbData > 256 * 1024 ||
                cds->cbData % sizeof(wchar_t) != 0 || cds->lpData == NULL) return FALSE;
            chars = cds->cbData / sizeof(wchar_t);
            payload = (wchar_t *)malloc((chars + 2) * sizeof(wchar_t));
            if (payload == NULL) return FALSE;
            memcpy(payload, cds->lpData, cds->cbData);
            payload[chars] = 0; payload[chars + 1] = 0;
            for (p = payload; *p; p += wcslen(p) + 1) count++;
            if (count == 0) { free(payload); return TRUE; }
            paths = (const wchar_t **)malloc((size_t)count * sizeof(wchar_t *));
            if (paths == NULL) { free(payload); return FALSE; }
            p = payload;
            for (i = 0; i < count; i++) { paths[i] = p; p += wcslen(p) + 1; }
            add_paths(paths, count, TRUE);
            free(paths); free(payload);
            return TRUE;
        }
        case WM_TIMER:
            if (g_soundReady && !g_paused && g_repeatMode != REPEAT_SONG && ma_sound_at_end(&g_sound)) advance_next();
            if (g_soundReady && !g_paused) {
                invalidate_player_rect(g_seekRect);
                invalidate_player_rect(g_timeRect);
            }
            return 0;
        case WM_COMMAND:
            if (HIWORD(wp) == THBN_CLICKED) {
                switch (LOWORD(wp)) {
                    case TASKBAR_PREVIOUS: previous_button(); return 0;
                    case TASKBAR_PLAY_PAUSE: play_pause(); return 0;
                    case TASKBAR_NEXT: next_button(); return 0;
                }
            }
            break;
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), i;
            { RECT r; GetClientRect(hwnd, &r); update_slider_rects(ui_unscale_extent(r.right)); }
            x = ui_unscale_px(x); y = ui_unscale_px(y);
            if (PtInRect(&g_seekRect, (POINT){x,y})) { g_dragSeek = TRUE; SetCapture(hwnd); set_seek_from_x(x); return 0; }
            if (PtInRect(&g_muteRect, (POINT){x,y})) { toggle_mute(); return 0; }
            if (PtInRect(&g_volumeRect, (POINT){x,y})) { g_dragVolume = TRUE; SetCapture(hwnd); set_volume_from_x(x); return 0; }
            for (i = 0; i < BTN_COUNT; i++) if (PtInRect(&g_buttonRects[i], (POINT){x,y})) { click_button(i); return 0; }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_tooltip) { MSG relay; relay.hwnd = hwnd; relay.message = msg; relay.wParam = wp; relay.lParam = lp; relay.time = GetMessageTime(); GetCursorPos(&relay.pt); SendMessageW(g_tooltip, TTM_RELAYEVENT, 0, (LPARAM)&relay); }
            if (g_dragSeek) set_seek_from_x(ui_unscale_px(GET_X_LPARAM(lp)));
            if (g_dragVolume) set_volume_from_x(ui_unscale_px(GET_X_LPARAM(lp)));
            return 0;
        case WM_LBUTTONUP:
            { BOOL wasSeek = g_dragSeek, wasVolume = g_dragVolume;
            if (g_dragSeek) {
                uint32_t duration = current_duration_ms();
                if (g_soundReady && duration > 0) {
                    ma_uint32 rate = 0, channels = 0; ma_format fmt;
                    ma_sound_get_data_format(&g_sound, &fmt, &channels, &rate, NULL, 0);
                    if (rate) ma_sound_seek_to_pcm_frame(&g_sound, (ma_uint64)rate * duration * g_seekPreview / 1000000ULL);
                }
            }
            g_dragSeek = FALSE; g_dragVolume = FALSE; ReleaseCapture();
            if (wasSeek) { invalidate_player_rect(g_seekRect); invalidate_player_rect(g_timeRect); }
            if (wasVolume) { invalidate_player_rect(g_volumeRect); invalidate_player_rect(g_muteRect); }
            return 0; }
        case WM_DROPFILES: add_dropped_paths((HDROP)wp); return 0;
        case WM_MOUSEWHEEL:
            if (g_playlistView && IsWindowVisible(g_playlistWindow)) {
                POINT pt; RECT r;
                GetCursorPos(&pt); ScreenToClient(g_playlistView, &pt); GetClientRect(g_playlistView, &r);
                if (PtInRect(&r, pt)) { SendMessageW(g_playlistView, WM_MOUSEWHEEL, wp, lp); return 0; }
            }
            break;
        case WM_CLOSE:
            stop_sound();
            if (g_engineReady) { ma_engine_uninit(&g_engine); g_engineReady = FALSE; }
            if (IsWindow(g_playlistWindow)) DestroyWindow(g_playlistWindow);
            if (IsWindow(g_playlistOwner)) DestroyWindow(g_playlistOwner);
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, PLAYER_TIMER);
            if (g_playerFont) { DeleteObject(g_playerFont); g_playerFont = NULL; }
            PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int playlist_page(void) {
    RECT r;
    int page;
    if (!g_playlistView) return 1;
    GetClientRect(g_playlistView, &r);
    page = ui_unscale_extent(r.bottom - r.top) / PLAYLIST_ROW_HEIGHT;
    return page > 0 ? page : 1;
}

static int playlist_max_top(void) {
    int maxTop = g_trackCount - playlist_page();
    return maxTop > 0 ? maxTop : 0;
}

static void invalidate_playlist(void) {
    if (g_playlistView) InvalidateRect(g_playlistView, NULL, FALSE);
}

static void scroll_playlist_to(int top) {
    int maxTop = playlist_max_top();
    if (top < 0) top = 0;
    if (top > maxTop) top = maxTop;
    if (g_playlistTop != top) {
        g_playlistTop = top;
        invalidate_playlist();
    }
}

static void ensure_playlist_visible(int index) {
    int page = playlist_page();
    if (index < 0 || index >= g_trackCount) return;
    if (index < g_playlistTop) scroll_playlist_to(index);
    else if (index >= g_playlistTop + page) scroll_playlist_to(index - page + 1);
}

static BOOL playlist_scroll_metrics(RECT *track, RECT *thumb, int *maxTop) {
    RECT client;
    int page, travel, thumbHeight;
    if (!g_playlistView) return FALSE;
    GetClientRect(g_playlistView, &client);
    client.right = ui_unscale_extent(client.right);
    client.bottom = ui_unscale_extent(client.bottom);
    page = (client.bottom - client.top) / PLAYLIST_ROW_HEIGHT;
    if (page < 1) page = 1;
    *maxTop = g_trackCount - page;
    if (*maxTop <= 0) return FALSE;
    track->left = client.right - PLAYLIST_SCROLL_WIDTH;
    track->right = client.right;
    track->top = 2;
    track->bottom = client.bottom - 2;
    travel = track->bottom - track->top;
    if (travel <= 0) return FALSE;
    thumbHeight = (int)((int64_t)travel * page / g_trackCount);
    if (thumbHeight < 24) thumbHeight = 24;
    if (thumbHeight > travel) thumbHeight = travel;
    thumb->left = track->left + 3;
    thumb->right = track->right - 3;
    thumb->top = track->top + (int)((int64_t)(travel - thumbHeight) * g_playlistTop / *maxTop);
    thumb->bottom = thumb->top + thumbHeight;
    return TRUE;
}

static int playlist_index_at_point(POINT point) {
    RECT client;
    int x = ui_unscale_px(point.x), y = ui_unscale_px(point.y);
    int page = playlist_page(), usableWidth, index;
    if (x < 0 || y < 0 || !g_playlistView) return -1;
    GetClientRect(g_playlistView, &client);
    usableWidth = ui_unscale_extent(client.right);
    if (g_trackCount > page) usableWidth -= PLAYLIST_SCROLL_WIDTH;
    if (x >= usableWidth) return -1;
    index = g_playlistTop + y / PLAYLIST_ROW_HEIGHT;
    return index >= 0 && index < g_trackCount ? index : -1;
}

static void show_playlist_context_menu(HWND hwnd, int index, POINT screenPoint) {
    HMENU menu;
    UINT command;
    int i;
    if (index < 0 || index >= g_trackCount) return;
    if (!g_tracks[index].selected) {
        for (i = 0; i < g_trackCount; i++) g_tracks[i].selected = FALSE;
        g_tracks[index].selected = TRUE;
        g_playlistAnchor = index;
    }
    g_playlistFocus = index;
    SetFocus(hwnd);
    invalidate_playlist();
    menu = CreatePopupMenu();
    if (menu == NULL) return;
    AppendMenuW(menu, MF_STRING, IDM_PLAYLIST_REMOVE, L"Remove from playlist");
    AppendMenuW(menu, MF_STRING, IDM_PLAYLIST_LOCATION, L"Go to file location");
    SetForegroundWindow(hwnd);
    command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                              screenPoint.x, screenPoint.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    if (command == IDM_PLAYLIST_REMOVE) remove_selected();
    else if (command == IDM_PLAYLIST_LOCATION) open_track_location(index);
}

static void resize_playlist_view(HWND hwnd) {
    RECT r;
    int width, height, margin = ui_scale_px(9);
    if (!g_playlistView || !hwnd || !IsWindow(hwnd)) return;
    GetClientRect(hwnd, &r);
    width = r.right - margin * 2;
    height = r.bottom - margin * 2;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    MoveWindow(g_playlistView, margin, margin, width, height, TRUE);
    if (g_playlistTip) {
        TOOLINFOW ti;
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd = g_playlistView;
        ti.uId = 1;
        GetClientRect(g_playlistView, &ti.rect);
        SendMessageW(g_playlistTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
    }
    scroll_playlist_to(g_playlistTop);
    invalidate_playlist();
}

static void playlist_select_range(int first, int last, BOOL additive) {
    int i;
    if (first > last) { int temp = first; first = last; last = temp; }
    if (!additive) for (i = 0; i < g_trackCount; i++) g_tracks[i].selected = FALSE;
    for (i = first; i <= last && i < g_trackCount; i++) if (i >= 0) g_tracks[i].selected = TRUE;
}

static void playlist_move_focus(int target, BOOL extend, BOOL additive) {
    if (g_trackCount <= 0) return;
    if (target < 0) target = 0;
    if (target >= g_trackCount) target = g_trackCount - 1;
    if (extend) {
        if (g_playlistAnchor < 0) g_playlistAnchor = g_playlistFocus >= 0 ? g_playlistFocus : target;
        playlist_select_range(g_playlistAnchor, target, additive);
    } else if (!additive) {
        int i;
        for (i = 0; i < g_trackCount; i++) g_tracks[i].selected = FALSE;
        g_tracks[target].selected = TRUE;
        g_playlistAnchor = target;
    }
    g_playlistFocus = target;
    ensure_playlist_visible(target);
    invalidate_playlist();
}

static LRESULT CALLBACK playlist_view_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static int wheelRemainder;
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            RECT client, rowRect, track, thumb;
            HDC target = BeginPaint(hwnd, &ps), buffer;
            HBITMAP bitmap, oldBitmap;
            int pixelWidth, pixelHeight, width, height, page, i, maxTop = 0;
            GetClientRect(hwnd, &client);
            pixelWidth = client.right - client.left;
            pixelHeight = client.bottom - client.top;
            if (pixelWidth <= 0 || pixelHeight <= 0) { EndPaint(hwnd, &ps); return 0; }
            width = ui_unscale_extent(pixelWidth);
            height = ui_unscale_extent(pixelHeight);
            buffer = CreateCompatibleDC(target);
            bitmap = CreateCompatibleBitmap(target, pixelWidth, pixelHeight);
            oldBitmap = (HBITMAP)SelectObject(buffer, bitmap);
            client.right = width; client.bottom = height;
            set_ui_transform(buffer);
            fill_rect(buffer, client, C_BG);
            page = height / PLAYLIST_ROW_HEIGHT;
            if (page < 1) page = 1;
            for (i = g_playlistTop; i < g_trackCount && i < g_playlistTop + page + 1; i++) {
                wchar_t name[1200], time[32], number[24];
                int y = (i - g_playlistTop) * PLAYLIST_ROW_HEIGHT;
                int usableWidth = width;
                int numberWidth = 48, lengthWidth = 62;
                COLORREF background, nameColor, detailColor;
                if (i == g_current) background = C_BUTTON_ON;
                else if (g_tracks[i].selected) background = RGB(39, 63, 50);
                else background = (i & 1) ? C_PANEL : RGB(19, 24, 25);
                if (g_trackCount > page) usableWidth -= PLAYLIST_SCROLL_WIDTH;
                if (usableWidth < 1) usableWidth = 1;
                rowRect.left = 0; rowRect.top = y; rowRect.right = usableWidth; rowRect.bottom = y + PLAYLIST_ROW_HEIGHT;
                fill_rect(buffer, rowRect, background);
                nameColor = C_LED;
                detailColor = i == g_current ? C_TEXT : C_MUTED;
                swprintf(number, 24, L"%d", i + 1);
                format_track_name(&g_tracks[i], name, 1200);
                format_time(g_tracks[i].durationMs, time, 32);
                rowRect.left = 9; rowRect.right = numberWidth - 3;
                draw_text(buffer, number, rowRect, detailColor, DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS, g_playlistFont);
                rowRect.left = numberWidth + 8; rowRect.right = usableWidth - lengthWidth - 8;
                draw_text(buffer, name, rowRect, nameColor, DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS, g_playlistFont);
                rowRect.left = usableWidth - lengthWidth; rowRect.right = usableWidth - 8;
                draw_text(buffer, time, rowRect, detailColor, DT_VCENTER | DT_RIGHT, g_playlistFont);
                if (i == g_playlistFocus && GetFocus() == hwnd) {
                    rowRect.left = 2; rowRect.top = y + 2; rowRect.right = usableWidth - 2; rowRect.bottom = y + PLAYLIST_ROW_HEIGHT - 2;
                    frame_rect(buffer, rowRect, C_ACCENT);
                }
            }
            if (playlist_scroll_metrics(&track, &thumb, &maxTop)) {
                fill_rect(buffer, track, C_PANEL);
                fill_rect(buffer, thumb, C_ACCENT);
                frame_rect(buffer, thumb, C_BUTTON);
                rowRect = thumb; InflateRect(&rowRect, -2, 0);
                rowRect.top = (thumb.top + thumb.bottom) / 2 - 1;
                rowRect.bottom = rowRect.top + 2;
                if (rowRect.right > rowRect.left) fill_rect(buffer, rowRect, C_LED);
            }
            reset_ui_transform(buffer);
            BitBlt(target, 0, 0, pixelWidth, pixelHeight, buffer, 0, 0, SRCCOPY);
            SelectObject(buffer, oldBitmap);
            DeleteObject(bitmap);
            DeleteDC(buffer);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            scroll_playlist_to(g_playlistTop);
            invalidate_playlist();
            return 0;
        case WM_LBUTTONDOWN: {
            int x = ui_unscale_px(GET_X_LPARAM(lp)), y = ui_unscale_px(GET_Y_LPARAM(lp));
            RECT track, thumb; int maxTop = 0;
            SetFocus(hwnd);
            if (playlist_scroll_metrics(&track, &thumb, &maxTop) && x >= track.left) {
                if (y >= thumb.top && y < thumb.bottom) {
                    g_scrollDragging = TRUE;
                    g_scrollDragOffset = y - thumb.top;
                    SetCapture(hwnd);
                } else {
                    int page = playlist_page();
                    scroll_playlist_to(g_playlistTop + (y < thumb.top ? -page : page));
                }
                return 0;
            }
            if (y >= 0) {
                int index = g_playlistTop + y / PLAYLIST_ROW_HEIGHT;
                if (index >= 0 && index < g_trackCount) {
                    BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    if (shift) {
                        if (g_playlistAnchor < 0) g_playlistAnchor = g_playlistFocus >= 0 ? g_playlistFocus : index;
                        playlist_select_range(g_playlistAnchor, index, ctrl);
                        g_playlistFocus = index;
                    } else if (ctrl) {
                        g_tracks[index].selected = !g_tracks[index].selected;
                        g_playlistFocus = index;
                        g_playlistAnchor = index;
                    } else playlist_move_focus(index, FALSE, FALSE);
                    invalidate_playlist();
                }
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int index = g_playlistTop + ui_unscale_px(GET_Y_LPARAM(lp)) / PLAYLIST_ROW_HEIGHT;
            if (index >= 0 && index < g_trackCount) choose_track(index);
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT screenPoint = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            POINT clientPoint;
            int index;
            if (screenPoint.x == -1 && screenPoint.y == -1) {
                index = g_playlistFocus;
                if (index < 0 || index >= g_trackCount) index = 0;
                if (index >= g_trackCount) return 0;
                ensure_playlist_visible(index);
                clientPoint.x = ui_scale_px(32);
                clientPoint.y = ui_scale_px((index - g_playlistTop) * PLAYLIST_ROW_HEIGHT + PLAYLIST_ROW_HEIGHT / 2);
                screenPoint = clientPoint;
                ClientToScreen(hwnd, &screenPoint);
            } else {
                clientPoint = screenPoint;
                ScreenToClient(hwnd, &clientPoint);
                index = playlist_index_at_point(clientPoint);
            }
            if (index >= 0) show_playlist_context_menu(hwnd, index, screenPoint);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (g_scrollDragging) {
                RECT track, thumb; int maxTop = 0;
                if (playlist_scroll_metrics(&track, &thumb, &maxTop)) {
                    int travel = track.bottom - track.top;
                    int thumbHeight = thumb.bottom - thumb.top;
                    int y = ui_unscale_px(GET_Y_LPARAM(lp)) - g_scrollDragOffset - track.top;
                    if (y < 0) y = 0;
                    if (y > travel - thumbHeight) y = travel - thumbHeight;
                    if (travel > thumbHeight) scroll_playlist_to((int)((int64_t)y * maxTop / (travel - thumbHeight)));
                }
            }
            return 0;
        case WM_LBUTTONUP:
            if (g_scrollDragging) { g_scrollDragging = FALSE; ReleaseCapture(); }
            return 0;
        case WM_CAPTURECHANGED:
            g_scrollDragging = FALSE;
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            wheelRemainder += delta;
            if (wheelRemainder >= WHEEL_DELTA || wheelRemainder <= -WHEEL_DELTA) {
                int notches = wheelRemainder / WHEEL_DELTA;
                wheelRemainder %= WHEEL_DELTA;
                scroll_playlist_to(g_playlistTop - notches * 3);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            int focus = g_playlistFocus >= 0 ? g_playlistFocus : 0;
            switch (wp) {
                case VK_DELETE: remove_selected(); return 0;
                case VK_RETURN: if (focus < g_trackCount) choose_track(focus); return 0;
                case VK_UP: playlist_move_focus(focus - 1, shift, ctrl); return 0;
                case VK_DOWN: playlist_move_focus(focus + 1, shift, ctrl); return 0;
                case VK_HOME: playlist_move_focus(0, shift, ctrl); return 0;
                case VK_END: playlist_move_focus(g_trackCount - 1, shift, ctrl); return 0;
                case VK_PRIOR: playlist_move_focus(focus - playlist_page(), shift, ctrl); return 0;
                case VK_NEXT: playlist_move_focus(focus + playlist_page(), shift, ctrl); return 0;
                case 'A':
                    if (ctrl) {
                        int i; for (i = 0; i < g_trackCount; i++) g_tracks[i].selected = TRUE;
                        if (g_trackCount > 0) { g_playlistFocus = 0; g_playlistAnchor = 0; }
                        invalidate_playlist(); return 0;
                    }
                    break;
            }
            return 0;
        }
        case WM_DROPFILES:
            add_dropped_paths((HDROP)wp);
            return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS:
            invalidate_playlist();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK playlist_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ACTIVATE: {
            LRESULT result = DefWindowProcW(hwnd, msg, wp, lp);
            keep_player_above_playlist();
            return result;
        }
        case WM_CREATE: {
            TOOLINFOW ti;
            g_playlistFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_playlistView = CreateWindowExW(0, L"MyAmpPlaylistView", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                               ui_scale_px(9), ui_scale_px(9), ui_scale_px(570), ui_scale_px(420), hwnd, NULL, g_instance, NULL);
            if (!g_playlistView) return -1;
            DragAcceptFiles(hwnd, TRUE);
            DragAcceptFiles(g_playlistView, TRUE);
            g_playlistTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                                           WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                           hwnd, NULL, g_instance, NULL);
            if (g_playlistTip) {
                memset(&ti, 0, sizeof(ti));
                ti.cbSize = sizeof(ti); ti.uFlags = TTF_SUBCLASS;
                ti.hwnd = g_playlistView; ti.uId = 1; ti.lpszText = LPSTR_TEXTCALLBACKW;
                GetClientRect(g_playlistView, &ti.rect);
                SendMessageW(g_playlistTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            }
            return 0;
        }
        case WM_SIZE: resize_playlist_view(hwnd); return 0;
        case WM_ERASEBKGND: {
            RECT r; GetClientRect(hwnd, &r); fill_rect((HDC)wp, r, C_BG); return 1;
        }
        case WM_DROPFILES: add_dropped_paths((HDROP)wp); return 0;
        case WM_MOUSEWHEEL:
            if (g_playlistView) { SendMessageW(g_playlistView, WM_MOUSEWHEEL, wp, lp); return 0; }
            return 0;
        case WM_NOTIFY: {
            NMHDR *nm = (NMHDR *)lp;
            if (nm->hwndFrom == g_playlistTip && nm->code == TTN_GETDISPINFOW) {
                NMTTDISPINFOW *tip = (NMTTDISPINFOW *)lp;
                POINT pt; int index = -1;
                GetCursorPos(&pt); ScreenToClient(g_playlistView, &pt);
                if (pt.y >= 0) index = g_playlistTop + ui_unscale_px(pt.y) / PLAYLIST_ROW_HEIGHT;
                tip->lpszText = (index >= 0 && index < g_trackCount) ? g_tracks[index].path : L"";
                return 0;
            }
            return 0;
        }
        case WM_CLOSE: ShowWindow(hwnd, SW_HIDE); update_player(); return 0;
        case WM_DESTROY:
            if (g_playlistTip) { DestroyWindow(g_playlistTip); g_playlistTip = NULL; }
            if (g_playlistFont) { DeleteObject(g_playlistFont); g_playlistFont = NULL; }
            g_playlistView = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static BOOL register_window_class(const wchar_t *name, WNDPROC proc, HBRUSH brush) {
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(g_instance, MAKEINTRESOURCEW(APP_ICON_ID), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(g_instance, MAKEINTRESOURCEW(APP_ICON_ID), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR | LR_SHARED);
    wc.hbrBackground = brush;
    wc.lpszClassName = name;
    return RegisterClassExW(&wc) != 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR cmd, int show) {
    INITCOMMONCONTROLSEX icc;
    MSG msg;
    ma_result r;
    LPWSTR *argv;
    int argc = 0;
    DWORD mutexError;
    (void)prev; (void)cmd;
    g_instance = instance;
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    g_instanceMutex = CreateMutexW(NULL, TRUE, MYAMP_INSTANCE_MUTEX);
    if (g_instanceMutex == NULL) {
        if (argv) LocalFree(argv);
        MessageBoxW(NULL, L"Could not create the MyAmp instance guard.", APP_NAME, MB_ICONERROR | MB_OK);
        return 1;
    }
    mutexError = GetLastError();
    if (mutexError == ERROR_ALREADY_EXISTS) {
        HWND existing = NULL;
        int attempt;
        for (attempt = 0; attempt < 200 && existing == NULL; attempt++) {
            existing = FindWindowW(L"MyAmpPlayer", APP_TITLE);
            if (existing == NULL) Sleep(50);
        }
        if (existing != NULL) {
            ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOWNORMAL);
            SetForegroundWindow(existing);
            if (argv != NULL && argc > 1 && !send_open_request(existing, argc, argv))
                MessageBoxW(existing, L"The running MyAmp instance could not accept the file.", APP_NAME, MB_ICONERROR | MB_OK);
        } else {
            MessageBoxW(NULL, L"MyAmp is still starting. Please open the file again in a moment.", APP_NAME, MB_ICONINFORMATION | MB_OK);
        }
        if (argv) LocalFree(argv);
        CloseHandle(g_instanceMutex); g_instanceMutex = NULL;
        return existing != NULL ? 0 : 1;
    }
    srand((unsigned)GetTickCount());
    SetProcessDPIAware();
    g_comReady = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
    g_taskbarButtonCreatedMessage = RegisterWindowMessageW(L"TaskbarButtonCreated");
    memset(&icc, 0, sizeof(icc)); icc.dwSize = sizeof(icc); icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);
    if (!register_window_class(L"MyAmpPlayer", player_proc, NULL) ||
        !register_window_class(L"MyAmpPlaylist", playlist_proc, (HBRUSH)(COLOR_WINDOW + 1)) ||
        !register_window_class(L"MyAmpPlaylistView", playlist_view_proc, NULL)) {
        MessageBoxW(NULL, L"Could not register the player windows.", APP_NAME, MB_ICONERROR | MB_OK);
        if (argv) LocalFree(argv);
        CloseHandle(g_instanceMutex); g_instanceMutex = NULL;
        if (g_comReady) CoUninitialize();
        return 1;
    }
    r = ma_engine_init(NULL, &g_engine);
    if (r != MA_SUCCESS) {
        MessageBoxW(NULL, L"Could not initialize audio output.", APP_NAME, MB_ICONERROR | MB_OK);
        if (argv) LocalFree(argv);
        CloseHandle(g_instanceMutex); g_instanceMutex = NULL;
        if (g_comReady) CoUninitialize();
        return 1;
    }
    g_engineReady = TRUE;
    ma_engine_set_volume(&g_engine, g_volume);
    g_playerWindow = CreateWindowExW(WS_EX_APPWINDOW, L"MyAmpPlayer", APP_TITLE,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 570, 327, NULL, NULL, instance, NULL);
    /* A hidden tool-window owner keeps the playlist off the taskbar without
       coupling its Z-order to the player. */
    g_playlistOwner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP,
        0, 0, 0, 0, NULL, NULL, instance, NULL);
    g_playlistWindow = CreateWindowExW(0, L"MyAmpPlaylist", L"MyAmp Playlist",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 465, g_playlistOwner, NULL, instance, NULL);
    if (!g_playerWindow || !g_playlistOwner || !g_playlistWindow) {
        MessageBoxW(NULL, L"Could not create the player windows.", APP_NAME, MB_ICONERROR | MB_OK);
        if (IsWindow(g_playlistWindow)) DestroyWindow(g_playlistWindow);
        if (IsWindow(g_playlistOwner)) DestroyWindow(g_playlistOwner);
        if (IsWindow(g_playerWindow)) DestroyWindow(g_playerWindow);
        if (g_engineReady) ma_engine_uninit(&g_engine);
        if (argv) LocalFree(argv);
        CloseHandle(g_instanceMutex); g_instanceMutex = NULL;
        if (g_comReady) CoUninitialize();
        return 1;
    }
    ShowWindow(g_playerWindow, show);
    ShowWindow(g_playlistWindow, show);
    keep_player_above_playlist();
    UpdateWindow(g_playerWindow);
    UpdateWindow(g_playlistWindow);
    if (argv != NULL && argc > 1) add_paths((const wchar_t **)(argv + 1), argc - 1, TRUE);
    if (argv) LocalFree(argv);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    release_taskbar();
    if (g_instanceMutex) { CloseHandle(g_instanceMutex); g_instanceMutex = NULL; }
    if (g_comReady) CoUninitialize();
    if (g_engineReady) ma_engine_uninit(&g_engine);
    for (int i = 0; i < g_trackCount; i++) free_track(&g_tracks[i]);
    free(g_tracks); free(g_history);
    return (int)msg.wParam;
}
