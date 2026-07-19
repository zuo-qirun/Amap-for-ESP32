package com.zuoqirun.amapesp32forwarder;

import org.json.JSONObject;
import org.junit.Test;

import java.nio.charset.StandardCharsets;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class Esp32ProtocolMusicTest {
    @Test
    public void includesMusicInSharedTransportPayload() throws Exception {
        Esp32NavState state = new Esp32NavState();
        state.music.active = true;
        state.music.playing = true;
        state.music.songId = 186016L;
        state.music.title = "晴天";
        state.music.artist = "周杰伦";
        state.music.positionMs = 56_410L;
        state.music.durationMs = 269_000L;
        state.music.lyric = "为你翘课的那一天";
        state.music.highlightedLyric = "为你翘课";
        state.music.currentWord = "课";
        state.music.lineStartMs = 55_000L;
        state.music.lineDurationMs = 2_400L;
        state.music.wordStartMs = 56_300L;
        state.music.wordDurationMs = 220L;
        state.music.wordProgressPermille = 500;
        state.music.coverUrl = "https://p1.music.126.net/example/cover.jpg";

        JSONObject root = new JSONObject(Esp32Protocol.toJson(state, 7L));
        JSONObject music = root.getJSONObject("music");
        assertTrue(music.getBoolean("active"));
        assertEquals(186016L, music.getLong("songId"));
        assertEquals("晴天", music.getString("title"));
        assertEquals(56_410L, music.getLong("positionMs"));
        assertEquals("为你翘课的那一天", music.getString("lyric"));
        assertEquals("为你翘课", music.getString("highlightedLyric"));
        assertEquals("课", music.getString("currentWord"));
        assertEquals(55_000L, music.getLong("lineStartMs"));
        assertEquals(2_400L, music.getLong("lineDurationMs"));
        assertEquals(56_300L, music.getLong("wordStartMs"));
        assertEquals(220L, music.getLong("wordDurationMs"));
        assertEquals(500, music.getInt("wordProgressPermille"));
        assertEquals("https://p1.music.126.net/example/cover.jpg",
                music.getString("coverUrl"));

        JSONObject update = new JSONObject(new String(
                Esp32Protocol.encodeMusicUpdate(state, 8L), StandardCharsets.UTF_8));
        assertEquals("music_update", update.getString("type"));
        assertEquals("为你翘课", update.getJSONObject("music")
                .getString("highlightedLyric"));
        assertEquals("https://p1.music.126.net/example/cover.jpg",
                update.getJSONObject("music").getString("coverUrl"));
    }
}
