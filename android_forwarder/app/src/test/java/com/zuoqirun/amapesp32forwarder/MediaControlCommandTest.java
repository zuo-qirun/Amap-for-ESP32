package com.zuoqirun.amapesp32forwarder;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.media.session.PlaybackState;

import java.nio.charset.StandardCharsets;

import org.junit.Test;

public final class MediaControlCommandTest {
    @Test
    public void parsesJsonAndCompactCommands() {
        assertEquals(MediaControlCommand.PLAY_PAUSE,
                MediaControlCommand.parse(bytes("{\"type\":\"media_control\",\"action\":\"play_pause\"}")));
        assertEquals(MediaControlCommand.NEXT,
                MediaControlCommand.parse(bytes("MC:next")));
        assertEquals(MediaControlCommand.PREVIOUS,
                MediaControlCommand.parse(bytes(" { \"action\" : \"previous\" } ")));
    }

    @Test
    public void rejectsUnknownOrOversizedCommands() {
        assertNull(MediaControlCommand.parse(bytes("{\"action\":\"volume_up\"}")));
        assertNull(MediaControlCommand.parse(bytes("untrusted")));
        assertNull(MediaControlCommand.parse(new byte[257]));
    }

    @Test
    public void playPauseTreatsTransitionalPlaybackAsPlaying() {
        assertTrue(MediaControlCommand.isPlaying(PlaybackState.STATE_PLAYING));
        assertTrue(MediaControlCommand.isPlaying(PlaybackState.STATE_BUFFERING));
        assertTrue(MediaControlCommand.isPlaying(PlaybackState.STATE_CONNECTING));
        assertFalse(MediaControlCommand.isPlaying(PlaybackState.STATE_PAUSED));
        assertFalse(MediaControlCommand.isPlaying(PlaybackState.STATE_STOPPED));
    }

    private static byte[] bytes(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
    }
}
