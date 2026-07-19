package com.zuoqirun.amapesp32forwarder;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public class LrcTimelineTest {
    @Test
    public void parsesMultipleTimestampFormatsAndTranslation() {
        LrcTimeline timeline = LrcTimeline.parse(
                "[00:01.20]第一句\n[00:03.456]第二句\n[00:05]第三句",
                "[00:01.200]First\n[00:03.500]Second");

        assertFalse(timeline.isEmpty());
        LrcTimeline.At before = timeline.at(500L);
        assertEquals("", before.lyric);
        assertEquals("第一句", before.nextLyric);

        LrcTimeline.At current = timeline.at(3_600L);
        assertEquals("第二句", current.lyric);
        assertEquals("Second", current.translatedLyric);
        assertEquals("第三句", current.nextLyric);
        assertEquals(3_456L, current.lineStartMs);
        assertEquals(1_544L, current.lineDurationMs);
    }

    @Test
    public void supportsMultipleTimeTagsOnOneLine() {
        LrcTimeline timeline = LrcTimeline.parse("[00:01.000][00:02.000]重复", "");

        assertEquals("重复", timeline.at(1_500L).lyric);
        assertEquals("重复", timeline.at(2_500L).lyric);
    }

    @Test
    public void parsesYrcWordTimingAndFallsBackToLineTranslation() {
        LrcTimeline timeline = LrcTimeline.parse(
                "[00:14.890]We could leave",
                "[00:14.890]我们可以离开",
                "[14890,720](14890,330,0)We (15220,180,0)could (15400,210,0)leave");

        LrcTimeline.At first = timeline.at(15_300L);
        assertEquals("We could leave", first.lyric);
        assertEquals("我们可以离开", first.translatedLyric);
        assertEquals("We could", first.highlightedLyric);
        assertEquals("could", first.currentWord);
        assertEquals(14_890L, first.lineStartMs);
        assertEquals(5_000L, first.lineDurationMs);
        assertEquals(15_220L, first.wordStartMs);
        assertEquals(180L, first.wordDurationMs);
        assertEquals(444, first.wordProgressPermille);

        LrcTimeline.At last = timeline.at(15_500L);
        assertEquals("We could leave", last.highlightedLyric);
        assertEquals("leave", last.currentWord);
    }
}
