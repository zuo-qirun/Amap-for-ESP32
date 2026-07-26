package com.zuoqirun.amapesp32forwarder;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class MusicAppRegistryTest {
    @Test
    public void recognizesMainstreamPlayersAndPackageVariants() {
        MusicAppRegistry.App qq = MusicAppRegistry.resolve("com.tencent.qqmusic", "");
        assertEquals("qqmusic", qq.sourceId);
        assertEquals("QQ 音乐", qq.displayName);
        assertTrue(qq.known);

        MusicAppRegistry.App neteaseVariant = MusicAppRegistry.resolve(
                "com.netease.cloudmusic.car", "");
        assertEquals("netease", neteaseVariant.sourceId);
        assertTrue(neteaseVariant.known);
    }

    @Test
    public void acceptsUnknownStandardPlayerWithItsApplicationLabel() {
        MusicAppRegistry.App app = MusicAppRegistry.resolve(
                "com.example.audio", "我的播放器");
        assertEquals("media", app.sourceId);
        assertEquals("我的播放器", app.displayName);
        assertFalse(app.known);
    }

    @Test
    public void playingUnknownPlayerBeatsPausedKnownPlayer() {
        int playingUnknown = MusicAppRegistry.selectionScore(
                10_000, true, true, false, false);
        int pausedKnown = MusicAppRegistry.selectionScore(
                5_000, true, true, true, true);
        assertTrue(playingUnknown > pausedKnown);
    }

    @Test
    public void knownPlayerWinsOtherwiseEquivalentSession() {
        int known = MusicAppRegistry.selectionScore(5_000, true, true, true, false);
        int unknown = MusicAppRegistry.selectionScore(5_000, true, true, false, false);
        assertTrue(known > unknown);
    }
}
