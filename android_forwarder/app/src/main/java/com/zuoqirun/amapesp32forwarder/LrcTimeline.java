package com.zuoqirun.amapesp32forwarder;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class LrcTimeline {
    private static final Pattern TIME_TAG = Pattern.compile(
            "\\[(\\d{1,3}):(\\d{2})(?:[.:](\\d{1,3}))?]");
    private static final Pattern YRC_LINE = Pattern.compile(
            "^\\[(\\d+),(\\d+)](.*)$");
    private static final Pattern YRC_WORD = Pattern.compile(
            "\\((\\d+),(\\d+),\\d+\\)");
    static final LrcTimeline EMPTY = new LrcTimeline(Collections.emptyList());

    private final List<Line> lines;

    private LrcTimeline(List<Line> lines) {
        this.lines = lines;
    }

    static LrcTimeline parse(String original, String translated) {
        return parse(original, translated, "");
    }

    static LrcTimeline parse(String original, String translated, String wordByWord) {
        TreeMap<Long, String> translations = parseTimedLines(translated);
        List<Line> enhanced = parseYrcLines(wordByWord, translations);
        if (!enhanced.isEmpty()) {
            return new LrcTimeline(Collections.unmodifiableList(enhanced));
        }
        TreeMap<Long, String> originals = parseTimedLines(original);
        if (originals.isEmpty()) {
            return EMPTY;
        }
        List<Line> result = new ArrayList<>(originals.size());
        for (Map.Entry<Long, String> entry : originals.entrySet()) {
            result.add(new Line(entry.getKey(), entry.getValue(),
                    closestTranslation(translations, entry.getKey()), Collections.emptyList()));
        }
        return new LrcTimeline(Collections.unmodifiableList(result));
    }

    At at(long positionMs) {
        if (lines.isEmpty()) {
            return At.EMPTY;
        }
        int low = 0;
        int high = lines.size();
        while (low < high) {
            int mid = (low + high) >>> 1;
            if (lines.get(mid).timeMs <= positionMs) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        int currentIndex = low - 1;
        Line current = currentIndex >= 0 ? lines.get(currentIndex) : null;
        Line previous = currentIndex > 0 ? lines.get(currentIndex - 1) : null;
        Line next = low < lines.size() ? lines.get(low) : null;
        long lineStartMs = current == null ? -1L : current.timeMs;
        long lineDurationMs = current == null ? 0L
                : Math.max(1_000L, next == null ? 5_000L : next.timeMs - current.timeMs);
        String highlight = "";
        String currentWord = "";
        long wordStartMs = -1L;
        long wordDurationMs = 0L;
        int wordProgressPermille = 0;
        if (current != null && !current.words.isEmpty()) {
            StringBuilder highlighted = new StringBuilder();
            for (Word word : current.words) {
                if (positionMs < word.startMs) {
                    break;
                }
                highlighted.append(word.text);
                if (word.durationMs <= 0 || positionMs >= word.startMs + word.durationMs) {
                    wordProgressPermille = 1000;
                } else {
                    currentWord = word.text.trim();
                    wordStartMs = word.startMs;
                    wordDurationMs = word.durationMs;
                    wordProgressPermille = (int) Math.max(0L, Math.min(1000L,
                            (positionMs - word.startMs) * 1000L / word.durationMs));
                    break;
                }
            }
            highlight = highlighted.toString().trim();
        }
        return new At(previous == null ? "" : previous.text,
                current == null ? "" : current.text,
                current == null ? "" : current.translated,
                next == null ? "" : next.text, highlight, currentWord,
                lineStartMs, lineDurationMs, wordStartMs, wordDurationMs,
                wordProgressPermille);
    }

    boolean isEmpty() {
        return lines.isEmpty();
    }

    private static TreeMap<Long, String> parseTimedLines(String value) {
        TreeMap<Long, String> result = new TreeMap<>();
        if (value == null || value.isEmpty()) {
            return result;
        }
        for (String rawLine : value.split("\\r?\\n")) {
            Matcher matcher = TIME_TAG.matcher(rawLine);
            List<Long> timestamps = new ArrayList<>();
            int textStart = -1;
            while (matcher.find()) {
                timestamps.add(toMilliseconds(matcher.group(1), matcher.group(2), matcher.group(3)));
                textStart = matcher.end();
            }
            if (timestamps.isEmpty() || textStart < 0) {
                continue;
            }
            String text = rawLine.substring(textStart).trim();
            if (text.isEmpty()) {
                continue;
            }
            for (Long timestamp : timestamps) {
                result.put(timestamp, text);
            }
        }
        return result;
    }

    private static List<Line> parseYrcLines(String value,
                                            TreeMap<Long, String> translations) {
        List<Line> result = new ArrayList<>();
        if (value == null || value.isEmpty()) {
            return result;
        }
        for (String rawLine : value.split("\\r?\\n")) {
            Matcher lineMatcher = YRC_LINE.matcher(rawLine);
            if (!lineMatcher.matches()) {
                continue;
            }
            long lineStart = Long.parseLong(lineMatcher.group(1));
            String content = lineMatcher.group(3);
            Matcher wordMatcher = YRC_WORD.matcher(content);
            List<Word> words = new ArrayList<>();
            long previousStart = -1L;
            long previousDuration = 0L;
            int textStart = -1;
            while (wordMatcher.find()) {
                if (previousStart >= 0 && textStart >= 0) {
                    words.add(new Word(previousStart, previousDuration,
                            content.substring(textStart, wordMatcher.start())));
                }
                previousStart = Long.parseLong(wordMatcher.group(1));
                previousDuration = Long.parseLong(wordMatcher.group(2));
                textStart = wordMatcher.end();
            }
            if (previousStart >= 0 && textStart >= 0) {
                words.add(new Word(previousStart, previousDuration,
                        content.substring(textStart)));
            }
            StringBuilder text = new StringBuilder();
            for (Word word : words) {
                text.append(word.text);
            }
            String lineText = text.toString().trim();
            if (!lineText.isEmpty()) {
                result.add(new Line(lineStart, lineText,
                        closestTranslation(translations, lineStart),
                        Collections.unmodifiableList(words)));
            }
        }
        return result;
    }

    private static long toMilliseconds(String minutes, String seconds, String fraction) {
        long result = Long.parseLong(minutes) * 60_000L + Long.parseLong(seconds) * 1_000L;
        if (fraction == null || fraction.isEmpty()) {
            return result;
        }
        long value = Long.parseLong(fraction);
        if (fraction.length() == 1) {
            value *= 100L;
        } else if (fraction.length() == 2) {
            value *= 10L;
        } else if (fraction.length() > 3) {
            value /= (long) Math.pow(10, fraction.length() - 3);
        }
        return result + value;
    }

    private static String closestTranslation(TreeMap<Long, String> translations, long timestamp) {
        if (translations.isEmpty()) {
            return "";
        }
        Map.Entry<Long, String> floor = translations.floorEntry(timestamp);
        Map.Entry<Long, String> ceil = translations.ceilingEntry(timestamp);
        Map.Entry<Long, String> best = floor;
        if (best == null || ceil != null
                && Math.abs(ceil.getKey() - timestamp) < Math.abs(best.getKey() - timestamp)) {
            best = ceil;
        }
        return best != null && Math.abs(best.getKey() - timestamp) <= 500L
                ? best.getValue() : "";
    }

    private static final class Line {
        final long timeMs;
        final String text;
        final String translated;
        final List<Word> words;

        Line(long timeMs, String text, String translated, List<Word> words) {
            this.timeMs = timeMs;
            this.text = text;
            this.translated = translated;
            this.words = words;
        }
    }

    private static final class Word {
        final long startMs;
        final long durationMs;
        final String text;

        Word(long startMs, long durationMs, String text) {
            this.startMs = startMs;
            this.durationMs = durationMs;
            this.text = text;
        }
    }

    static final class At {
        static final At EMPTY = new At("", "", "", "", "", "",
                -1L, 0L, -1L, 0L, 0);
        final String previousLyric;
        final String lyric;
        final String translatedLyric;
        final String nextLyric;
        final String highlightedLyric;
        final String currentWord;
        final long lineStartMs;
        final long lineDurationMs;
        final long wordStartMs;
        final long wordDurationMs;
        final int wordProgressPermille;

        At(String previousLyric, String lyric, String translatedLyric, String nextLyric,
           String highlightedLyric, String currentWord, long lineStartMs,
           long lineDurationMs, long wordStartMs, long wordDurationMs,
           int wordProgressPermille) {
            this.previousLyric = previousLyric;
            this.lyric = lyric;
            this.translatedLyric = translatedLyric;
            this.nextLyric = nextLyric;
            this.highlightedLyric = highlightedLyric;
            this.currentWord = currentWord;
            this.lineStartMs = lineStartMs;
            this.lineDurationMs = lineDurationMs;
            this.wordStartMs = wordStartMs;
            this.wordDurationMs = wordDurationMs;
            this.wordProgressPermille = wordProgressPermille;
        }
    }
}
