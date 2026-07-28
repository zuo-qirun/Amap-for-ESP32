package com.zuoqirun.amapesp32forwarder;

import android.content.Context;
import android.media.MediaMetadata;
import android.media.session.PlaybackState;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

final class MusicStateStore {
    private static final String TAG = "MusicSession";
    private static final Object LOCK = new Object();
    private static final ExecutorService LYRIC_EXECUTOR = Executors.newSingleThreadExecutor();

    private static Context appContext;
    private static NetEaseLyricClient lyricClient;
    private static boolean active;
    private static boolean playing;
    private static String source = "media";
    private static String sourceName = "音乐播放器";
    private static String mediaId = "";
    private static String title = "";
    private static String artist = "";
    private static String album = "";
    private static String coverUrl = "";
    private static long durationMs = -1L;
    private static long basePositionMs;
    private static long positionUpdatedAtElapsedMs;
    private static float playbackSpeed;
    private static long songId = -1L;
    private static String trackKey = "";
    private static long loadGeneration;
    private static LrcTimeline timeline = LrcTimeline.EMPTY;
    private static boolean lyricLoadFinished;
    private static boolean trackChangePending;
    private static long trackChangeStartedAtElapsedMs;

    private MusicStateStore() {}

    static void initialize(Context context) {
        synchronized (LOCK) {
            if (appContext == null) {
                appContext = context.getApplicationContext();
                lyricClient = new NetEaseLyricClient(appContext);
            }
        }
    }

    static void update(Context context, String newSource, String newSourceName,
                       MediaMetadata metadata, PlaybackState state) {
        initialize(context);
        if (metadata == null && state == null) {
            clear();
            return;
        }
        String newTitle = firstNonEmpty(metadata,
                MediaMetadata.METADATA_KEY_TITLE, MediaMetadata.METADATA_KEY_DISPLAY_TITLE);
        String newArtist = firstNonEmpty(metadata,
                MediaMetadata.METADATA_KEY_ARTIST, MediaMetadata.METADATA_KEY_ALBUM_ARTIST,
                MediaMetadata.METADATA_KEY_DISPLAY_SUBTITLE);
        String newAlbum = firstNonEmpty(metadata, MediaMetadata.METADATA_KEY_ALBUM);
        String newMediaId = firstNonEmpty(metadata, MediaMetadata.METADATA_KEY_MEDIA_ID);
        long newDuration = metadata == null ? -1L
                : metadata.getLong(MediaMetadata.METADATA_KEY_DURATION);
        int playbackState = state == null ? PlaybackState.STATE_NONE : state.getState();
        boolean newPlaying = playbackState == PlaybackState.STATE_PLAYING
                || playbackState == PlaybackState.STATE_FAST_FORWARDING
                || playbackState == PlaybackState.STATE_REWINDING;
        boolean newActive = !TextUtils.isEmpty(newTitle)
                && playbackState != PlaybackState.STATE_NONE
                && playbackState != PlaybackState.STATE_STOPPED
                && playbackState != PlaybackState.STATE_ERROR;
        long newBasePosition = state == null || state.getPosition() < 0 ? 0L : state.getPosition();
        long newPositionTime = state == null || state.getLastPositionUpdateTime() <= 0
                ? SystemClock.elapsedRealtime() : state.getLastPositionUpdateTime();
        float newSpeed = state == null ? 0f : state.getPlaybackSpeed();
        String normalizedSource = TextUtils.isEmpty(newSource) ? "media" : newSource;
        String normalizedSourceName = TextUtils.isEmpty(newSourceName)
                ? "音乐播放器" : newSourceName;
        String newTrackKey = normalizedSource + "\n" + newTitle + "\n"
                + newArtist + "\n" + newDuration + "\n" + newMediaId;
        long generationToLoad = -1L;
        synchronized (LOCK) {
            boolean trackChanged = !TextUtils.equals(trackKey, newTrackKey);
            if (trackChangePending && (trackChanged
                    || SystemClock.elapsedRealtime() - trackChangeStartedAtElapsedMs >= 1800L)) {
                trackChangePending = false;
            }
            active = newActive;
            playing = newPlaying;
            source = normalizedSource;
            sourceName = normalizedSourceName;
            mediaId = safe(newMediaId);
            title = safe(newTitle);
            artist = safe(newArtist);
            album = safe(newAlbum);
            durationMs = newDuration > 0 ? newDuration : -1L;
            basePositionMs = newBasePosition;
            positionUpdatedAtElapsedMs = newPositionTime;
            playbackSpeed = newSpeed;
            if (trackChanged) {
                trackKey = newTrackKey;
                songId = -1L;
                coverUrl = "";
                timeline = LrcTimeline.EMPTY;
                lyricLoadFinished = false;
                generationToLoad = ++loadGeneration;
            }
        }
        if (generationToLoad >= 0 && !TextUtils.isEmpty(newTitle)) {
            // IDs from other providers are not NetEase song IDs. Those tracks are
            // resolved safely by title, artist and duration instead.
            String lyricMediaId = "netease".equals(normalizedSource) ? newMediaId : "";
            scheduleLyricLoad(generationToLoad, lyricMediaId,
                    newTitle, newArtist, newDuration);
        }
    }

    static void clear() {
        synchronized (LOCK) {
            active = false;
            playing = false;
            source = "media";
            sourceName = "音乐播放器";
            mediaId = "";
            title = "";
            artist = "";
            album = "";
            coverUrl = "";
            durationMs = -1L;
            basePositionMs = 0L;
            playbackSpeed = 0f;
            songId = -1L;
            trackKey = "";
            timeline = LrcTimeline.EMPTY;
            lyricLoadFinished = false;
            trackChangePending = false;
            trackChangeStartedAtElapsedMs = 0L;
            loadGeneration++;
        }
    }

    static void beginMediaControl(String action) {
        synchronized (LOCK) {
            long now = SystemClock.elapsedRealtime();
            if (MediaControlCommand.PLAY_PAUSE.equals(action)) {
                basePositionMs = currentPositionLocked();
                positionUpdatedAtElapsedMs = now;
                playing = !playing;
                playbackSpeed = playing ? 1f : 0f;
                return;
            }
            if (MediaControlCommand.PREVIOUS.equals(action)
                    || MediaControlCommand.NEXT.equals(action)) {
                trackChangePending = true;
                trackChangeStartedAtElapsedMs = now;
                basePositionMs = 0L;
                positionUpdatedAtElapsedMs = now;
                playing = false;
                playbackSpeed = 0f;
                loadGeneration++;
            }
        }
    }

    static void copyInto(Esp32NavState.Music target) {
        synchronized (LOCK) {
            boolean suppressOldTrack = trackChangePending;
            long position = suppressOldTrack ? 0L : currentPositionLocked();
            long lyricPosition = Math.max(0L, position
                    + (appContext == null ? 0 : AppSettings.getLyricOffsetMs(appContext)));
            LrcTimeline.At lyrics = timeline.at(lyricPosition);
            target.active = active;
            target.playing = !suppressOldTrack && playing;
            target.source = source;
            target.sourceName = sourceName;
            target.songId = suppressOldTrack ? -1L : songId;
            target.title = suppressOldTrack ? "" : title;
            target.artist = suppressOldTrack ? "" : artist;
            target.album = suppressOldTrack ? "" : album;
            target.coverUrl = suppressOldTrack ? "" : coverUrl;
            target.durationMs = durationMs;
            target.positionMs = position;
            target.previousLyric = suppressOldTrack ? "" : lyrics.previousLyric;
            target.lyric = suppressOldTrack ? "" : lyrics.lyric;
            target.translatedLyric = suppressOldTrack ? "" : lyrics.translatedLyric;
            target.nextLyric = suppressOldTrack ? "" : lyrics.nextLyric;
            target.interlude = !suppressOldTrack && lyrics.interlude;
            target.highlightedLyric = suppressOldTrack ? "" : lyrics.highlightedLyric;
            target.currentWord = suppressOldTrack ? "" : lyrics.currentWord;
            target.lineStartMs = suppressOldTrack ? -1L : lyrics.lineStartMs;
            target.lineDurationMs = suppressOldTrack ? 0L : lyrics.lineDurationMs;
            target.wordStartMs = suppressOldTrack ? -1L : lyrics.wordStartMs;
            target.wordDurationMs = suppressOldTrack ? 0L : lyrics.wordDurationMs;
            target.wordProgressPermille = suppressOldTrack ? 0 : lyrics.wordProgressPermille;
        }
    }

    static boolean isActive() {
        synchronized (LOCK) {
            return active;
        }
    }

    static String describe() {
        synchronized (LOCK) {
            if (!active) {
                return "未检测到音乐播放";
            }
            return sourceName + " · " + (playing ? "播放中 · " : "已暂停 · ") + title
                    + (artist.isEmpty() ? "" : " / " + artist)
                    + (timeline.isEmpty()
                    ? (lyricLoadFinished ? " · 暂无歌词" : " · 正在获取歌词")
                    : " · 歌词已就绪");
        }
    }

    private static void scheduleLyricLoad(long generation, String requestedMediaId,
                                          String requestedTitle, String requestedArtist,
                                          long requestedDuration) {
        LYRIC_EXECUTOR.execute(() -> {
            try {
                NetEaseLyricClient.Result result = lyricClient.load(requestedMediaId,
                        requestedTitle, requestedArtist, requestedDuration);
                synchronized (LOCK) {
                    if (generation != loadGeneration) {
                        return;
                    }
                    songId = result.songId;
                    coverUrl = result.coverUrl;
                    timeline = result.timeline;
                    lyricLoadFinished = true;
                }
            } catch (Throwable error) {
                Log.w(TAG, "Unable to load lyric for " + requestedTitle, error);
                synchronized (LOCK) {
                    if (generation == loadGeneration) {
                        lyricLoadFinished = true;
                    }
                }
            }
        });
    }

    private static long currentPositionLocked() {
        long position = Math.max(0L, basePositionMs);
        if (active && playing && playbackSpeed != 0f) {
            position += (long) ((SystemClock.elapsedRealtime() - positionUpdatedAtElapsedMs)
                    * playbackSpeed);
        }
        if (durationMs > 0) {
            position = Math.min(position, durationMs);
        }
        return Math.max(0L, position);
    }

    private static String firstNonEmpty(MediaMetadata metadata, String... keys) {
        if (metadata == null) {
            return "";
        }
        for (String key : keys) {
            String value = metadata.getString(key);
            if (!TextUtils.isEmpty(value)) {
                return value.trim();
            }
        }
        return "";
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }
}
