package com.zuoqirun.amapesp32forwarder;

import java.util.Locale;

/** Identifies well-known players while keeping a generic MediaSession fallback. */
final class MusicAppRegistry {
    private static final App[] KNOWN_APPS = {
            new App("netease", "网易云音乐", "com.netease.cloudmusic"),
            new App("qqmusic", "QQ 音乐", "com.tencent.qqmusic"),
            new App("kugou", "酷狗音乐", "com.kugou.android"),
            new App("kuwo", "酷我音乐", "cn.kuwo.player"),
            new App("spotify", "Spotify", "com.spotify.music"),
            new App("soda", "汽水音乐", "com.luna.music"),
            new App("migu", "咪咕音乐", "cmccwm.mobilemusic"),
            new App("xiaomi", "小米音乐", "com.miui.player"),
            new App("huawei", "华为音乐", "com.android.mediacenter"),
            new App("apple_music", "Apple Music", "com.apple.android.music"),
            new App("youtube_music", "YouTube Music", "com.google.android.apps.youtube.music"),
            new App("amazon_music", "Amazon Music", "com.amazon.mp3")
    };

    private MusicAppRegistry() {}

    static App resolve(String packageName, String applicationLabel) {
        String normalizedPackage = safe(packageName).toLowerCase(Locale.ROOT);
        for (App app : KNOWN_APPS) {
            if (normalizedPackage.equals(app.packagePrefix)
                    || normalizedPackage.startsWith(app.packagePrefix + ".")) {
                return app;
            }
        }
        String label = safe(applicationLabel).trim();
        if (label.isEmpty()) {
            label = readablePackageName(normalizedPackage);
        }
        return new App("media", label.isEmpty() ? "音乐播放器" : label, normalizedPackage, false);
    }

    static boolean isKnown(String packageName) {
        return resolve(packageName, "").known;
    }

    /**
     * Playback rank must dominate app recognition so an unknown app that is playing
     * always wins over a known app that is merely paused.
     */
    static int selectionScore(int playbackRank, boolean hasMetadata,
                              boolean supportsTransportControls, boolean known,
                              boolean currentSession) {
        int score = playbackRank;
        if (hasMetadata) score += 200;
        if (supportsTransportControls) score += 40;
        if (known) score += 80;
        if (currentSession) score += 10;
        return score;
    }

    private static String readablePackageName(String packageName) {
        int separator = packageName.lastIndexOf('.');
        String tail = separator >= 0 ? packageName.substring(separator + 1) : packageName;
        if (tail.isEmpty() || "player".equals(tail) || "music".equals(tail)) {
            return "音乐播放器";
        }
        return tail;
    }

    private static String safe(String value) {
        return value == null ? "" : value;
    }

    static final class App {
        final String sourceId;
        final String displayName;
        final String packagePrefix;
        final boolean known;

        App(String sourceId, String displayName, String packagePrefix) {
            this(sourceId, displayName, packagePrefix, true);
        }

        private App(String sourceId, String displayName, String packagePrefix, boolean known) {
            this.sourceId = sourceId;
            this.displayName = displayName;
            this.packagePrefix = packagePrefix;
            this.known = known;
        }
    }
}
