package com.zuoqirun.amapesp32forwarder;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class NetEaseLyricClientTest {
    @Test
    public void extractsNumericSongIdFromMediaId() {
        assertEquals(186016L, NetEaseLyricClient.parseSongId("netease://track/186016"));
        assertEquals(186016L, NetEaseLyricClient.parseSongId("track_186016_bitrate_320000"));
        assertEquals(-1L, NetEaseLyricClient.parseSongId("unknown"));
    }

    @Test
    public void exactTitleArtistAndDurationWins() {
        int exact = NetEaseLyricClient.matchScore(
                "晴天", "周杰伦", 269_000L,
                "晴天", "周杰伦", 269_200L);
        int cover = NetEaseLyricClient.matchScore(
                "晴天", "周杰伦", 269_000L,
                "晴天（深情版）", "其他歌手", 278_000L);

        assertTrue(exact > cover);
        assertTrue(exact >= 100);
    }
}
