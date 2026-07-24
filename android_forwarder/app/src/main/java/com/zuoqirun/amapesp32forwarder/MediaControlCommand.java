package com.zuoqirun.amapesp32forwarder;

import android.media.session.PlaybackState;

import java.nio.charset.StandardCharsets;

final class MediaControlCommand {
    static final String PREVIOUS = "previous";
    static final String PLAY_PAUSE = "play_pause";
    static final String NEXT = "next";

    private MediaControlCommand() {
    }

    static String parse(byte[] payload) {
        if (payload == null || payload.length == 0 || payload.length > 256) {
            return null;
        }
        String value = new String(payload, StandardCharsets.UTF_8).trim();
        if (value.startsWith("MC:")) {
            return normalize(value.substring(3));
        }
        int actionKey = value.indexOf("\"action\"");
        if (actionKey < 0) {
            return null;
        }
        int colon = value.indexOf(':', actionKey + 8);
        int openingQuote = colon < 0 ? -1 : value.indexOf('"', colon + 1);
        int closingQuote = openingQuote < 0 ? -1 : value.indexOf('"', openingQuote + 1);
        if (openingQuote < 0 || closingQuote < 0) {
            return null;
        }
        return normalize(value.substring(openingQuote + 1, closingQuote));
    }

    static boolean isPlaying(int playbackState) {
        return playbackState == PlaybackState.STATE_PLAYING
                || playbackState == PlaybackState.STATE_BUFFERING
                || playbackState == PlaybackState.STATE_CONNECTING;
    }

    private static String normalize(String action) {
        if (PREVIOUS.equals(action) || PLAY_PAUSE.equals(action) || NEXT.equals(action)) {
            return action;
        }
        return null;
    }
}
