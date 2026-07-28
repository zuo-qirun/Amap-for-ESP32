"use strict";

// This mirrors Esp32Protocol.encodeMusicUpdate() in android_forwarder. BetterNCM
// has no UDP API, so the bundled loopback-only helper owns the UDP socket.
plugin.onLoad(() => {
  const DEFAULT_ESP_HOST = "";
  const DEFAULT_ESP_PORT = 4210;
  const HELPER_PORT = 25835;
  const HEARTBEAT_MS = 200; // Android Forwarder music heartbeat interval.
  const HELPER_RETRY_MS = 3000;
  const state = {
    lines: [], song: null, positionMs: 0, playing: false, sequence: 1,
    sending: false, pending: false, lastSentAt: 0, lastError: "Waiting for playback",
    helperStartedAt: 0, helperStartFailed: false, coverUrl: "", heartbeat: null,
    loadingSongId: "", loadGeneration: 0, lyricStatus: "暂无歌词",
  };

  const config = (key, fallback) => plugin.getConfig(key, fallback);
  const targetHost = () => String(config("espHost", DEFAULT_ESP_HOST)).trim();
  const targetPort = () => Math.min(65535, Math.max(1, Number(config("espPort", DEFAULT_ESP_PORT)) || DEFAULT_ESP_PORT));
  const helperUrl = () => {
    const host = targetHost();
    return host ? `http://127.0.0.1:${HELPER_PORT}/v1/send?host=${encodeURIComponent(host)}&port=${targetPort()}` : "";
  };
  const clip = (value, length) => String(value || "").slice(0, length);
  const artists = song => Array.isArray(song.artists)
    ? song.artists.map(item => item && item.name).filter(Boolean).join(" / ")
    : String(song.artists || song.artist || "");
  const duration = song => Number(song.duration || song.dt || song.durationMs || -1);
  const embeddedCoverUrl = song => {
    const album = song.album || song.al || {};
    return clip(album.picUrl || album.picUrl160 || album.pic || song.picUrl || song.coverUrl, 320);
  };

  function runMediaControl(action) {
    const player = document.querySelector(".m-player:not(.f-dn)") || document.querySelector(".m-player");
    const selectors = {
      previous: ".btnc-prv",
      play_pause: ".btnp-pause, .btnp-play",
      next: ".btnc-nxt",
    };
    const button = player && player.querySelector(selectors[action] || "");
    if (!button) {
      state.lastError = `ESP32 control unavailable: ${action}`;
      console.warn("AMap ESP32 Lyrics could not find NCM control:", action);
      return;
    }
    button.click();
    state.lastError = `ESP32 control received: ${action}`;
  }

  async function resolveCoverUrl(song, generation) {
    const embedded = embeddedCoverUrl(song);
    if (embedded) return embedded;
    const id = Number(song && song.id);
    if (!Number.isFinite(id) || id <= 0) return "";
    try {
      const response = await fetch(`https://music.163.com/api/song/detail/?ids=[${id}]`, { cache: "no-store" });
      const data = await response.json();
      const detail = data && data.songs && data.songs[0];
      const url = clip(detail && detail.album && detail.album.picUrl, 320);
      if (generation === state.loadGeneration && url) {
        state.coverUrl = url;
        publish(true);
      }
      return url;
    } catch (error) {
      console.warn("AMap ESP32 Lyrics cover lookup failed:", error);
      return "";
    }
  }

  function getCurrentSong() {
    const playing = betterncm.ncm.getPlayingSong && betterncm.ncm.getPlayingSong();
    return playing && (playing.data || playing.song || playing);
  }

  function lineAt(positionMs) {
    if (!state.lines.length) return { current: null, previous: null, next: null };
    let nextIndex = state.lines.findIndex(line => Number(line.time) > positionMs);
    if (nextIndex < 0) nextIndex = state.lines.length;
    const index = Math.max(0, nextIndex - 1);
    return { current: state.lines[index] || null, previous: state.lines[index - 1] || null, next: state.lines[nextIndex] || null };
  }

  // Refined's lyric processor keeps an empty timeline row only when it spans
  // at least five seconds. LibLyric normally supplies those rows itself; this
  // fallback also reconstructs one when a provider returns only non-empty
  // lines but exposes trustworthy line durations.
  function withRefinedInterludes(lines) {
    const source = Array.isArray(lines) ? lines : [];
    const result = [];
    const first = source[0];
    const firstText = String(first && first.originalLyric || "").trim();
    const firstStart = Number(first && first.time);
    if (firstText && Number.isFinite(firstStart) && firstStart > 5000) {
      result.push({ time: 500, duration: firstStart - 500,
                    originalLyric: "", isInterlude: true });
    }
    for (let index = 0; index < source.length; index++) {
      const line = source[index];
      const text = String(line && line.originalLyric || "").trim();
      const start = Number(line && line.time);
      const durationMs = Math.max(0, Number(line && line.duration) || 0);
      if (text || (Number.isFinite(start) && durationMs >= 5000)) result.push(line);

      const next = source[index + 1];
      if (!text || !next || durationMs <= 0 || !Number.isFinite(start)) continue;
      const nextText = String(next.originalLyric || "").trim();
      const nextStart = Number(next.dynamicLyricTime || next.time);
      const end = Number(line.dynamicLyricTime || start) + durationMs;
      if (nextText && Number.isFinite(nextStart) && nextStart - end >= 5000) {
        result.push({ time: end, duration: nextStart - end, originalLyric: "", isInterlude: true });
      }
    }
    return result;
  }

  // LibLyric parses NetEase YRC into `dynamicLyric`: every item has its own
  // absolute time, duration, and word. This is the direct counterpart of the
  // Android forwarder's LrcTimeline word loop.
  function wordAt(line, positionMs) {
    const words = line && Array.isArray(line.dynamicLyric) ? line.dynamicLyric : [];
    let highlighted = "";
    let currentWord = "";
    let wordStartMs = -1;
    let wordDurationMs = 0;
    let wordProgressPermille = 0;
    for (const item of words) {
      const start = Number(item.time);
      const durationMs = Math.max(0, Number(item.duration) || 0);
      if (!Number.isFinite(start) || positionMs < start) break;
      const word = String(item.word || "");
      highlighted += word;
      if (durationMs <= 0 || positionMs >= start + durationMs) {
        wordProgressPermille = 1000;
        continue;
      }
      currentWord = word.trim();
      wordStartMs = start;
      wordDurationMs = durationMs;
      wordProgressPermille = Math.max(0, Math.min(1000, Math.round((positionMs - start) * 1000 / durationMs)));
      break;
    }
    return { highlightedLyric: clip(highlighted.trim(), 80), currentWord: clip(currentWord, 24), wordStartMs, wordDurationMs, wordProgressPermille };
  }

  // Field names and caps intentionally match android_forwarder/Esp32Protocol.java.
  function frame() {
    const song = state.song || {};
    const selected = lineAt(state.positionMs);
    const current = selected.current || {};
    const interlude = Boolean(current.isInterlude ||
      (!String(current.originalLyric || "").trim() && Number(current.duration || 0) >= 5000));
    const album = song.album || song.al || {};
    const word = wordAt(selected.current, state.positionMs);
    return {
      proto: 1,
      type: "music_update",
      seq: state.sequence++,
      ts: Date.now(),
      active: false,
      mode: "standby",
      music: {
        active: Boolean(state.song),
        playing: state.playing,
        source: "netease",
        sourceName: "Netease Cloud Music",
        songId: Number(song.id || song.mid || -1),
        title: clip(song.name, 48),
        artist: clip(artists(song), 48),
        album: clip(album.name || album, 48),
        coverUrl: state.coverUrl || embeddedCoverUrl(song),
        positionMs: Math.max(0, Math.round(state.positionMs)),
        durationMs: duration(song),
        previousLyric: clip(selected.previous && selected.previous.originalLyric, 80),
        lyric: interlude ? "" : clip(current.originalLyric || state.lyricStatus || "暂无歌词", 80),
        translatedLyric: clip(current.translatedLyric, 80),
        nextLyric: clip(selected.next && selected.next.originalLyric, 80),
        interlude,
        highlightedLyric: word.highlightedLyric,
        currentWord: word.currentWord,
        lineStartMs: Number.isFinite(Number(current.time)) ? Number(current.time) : -1,
        lineDurationMs: Number(current.duration || 0),
        wordStartMs: word.wordStartMs,
        wordDurationMs: word.wordDurationMs,
        wordProgressPermille: word.wordProgressPermille,
      },
    };
  }

  function startUdpHelper(force) {
    const now = Date.now();
    if (!force && now - state.helperStartedAt < HELPER_RETRY_MS) return;
    state.helperStartedAt = now;
    const info = loadedPlugins["amap-esp32-lyrics"];
    const helperPath = info && info.pluginPath;
    if (!helperPath || !betterncm.app || !betterncm.app.exec) {
      state.helperStartFailed = true;
      state.lastError = "Unable to start UDP helper";
      return;
    }
    const executable = `${helperPath.replace(/\//g, "\\")}\\amap-esp32-lyrics-udp.exe`;
    // BetterNCM's exec expects an executable command, not a shell builtin such
    // as `start`. This is the same cmd /S /C invocation used by its bundled
    // Taskbar-Lyrics plugin. The doubled quotes preserve plugin paths with spaces.
    betterncm.app.exec(`cmd /S /C ""${executable}" --listen ${HELPER_PORT}"`, false, false);
    state.helperStartFailed = false;
  }

  async function publish(force) {
    if (!state.song) return;
    const now = Date.now();
    if (!force && now - state.lastSentAt < HEARTBEAT_MS) return;
    if (state.sending) { state.pending = true; return; }
    const endpoint = helperUrl();
    if (!endpoint) { state.lastError = "Set the ESP32 IPv4 address in plugin settings first"; return; }
    startUdpHelper(false);
    state.sending = true;
    state.pending = false;
    state.lastSentAt = now;
    try {
      const response = await fetch(endpoint, {
        method: "POST",
        headers: { "Content-Type": "application/json;charset=utf-8" },
        body: JSON.stringify(frame()),
        cache: "no-store",
      });
      if (!response.ok) throw new Error("UDP helper returned " + response.status);
      const result = await response.json();
      if (result && Array.isArray(result.controls)) result.controls.forEach(runMediaControl);
      state.lastError = `UDP v1 active: ${targetHost()}:${targetPort()}`;
    } catch (error) {
      // If an old helper was closed or crashed, the next heartbeat starts it again.
      state.helperStartedAt = 0;
      startUdpHelper(true);
      state.lastError = error && error.message ? error.message : String(error);
      console.error("AMap ESP32 Lyrics:", error);
    } finally {
      state.sending = false;
      if (state.pending) publish(true);
    }
  }

  async function loadSong() {
    const song = getCurrentSong();
    if (!song || !song.id) return;
    const songId = String(song.id);
    if (state.song && String(state.song.id) === songId) return;
    if (state.loadingSongId === songId) return;
    const generation = ++state.loadGeneration;
    state.loadingSongId = songId;
    state.song = song;
    state.playing = true;
    state.lines = [];
    state.lyricStatus = "歌词加载中";
    state.coverUrl = embeddedCoverUrl(song);
    resolveCoverUrl(song, generation);
    // Commit the new song once and keep the same playing page visible while
    // LibLyric resolves. A transient empty result must never look like a lost
    // player session to the ESP32.
    await publish(true);
    if (generation !== state.loadGeneration ||
        !state.song || String(state.song.id) !== songId) return;
    const lyricApi = loadedPlugins.liblyric;
    if (!lyricApi) {
      state.lastError = "LibLyric is required";
      state.lyricStatus = "暂无歌词";
      state.loadingSongId = "";
      await publish(true); // Still send title/cover/state like Android's lyric fallback.
      return;
    }
    let parsedLines = [];
    let lyricStatus = "暂无歌词";
    try {
      const data = await lyricApi.getLyricData(song.id);
      parsedLines = lyricApi.parseLyric(
        (data && data.lrc && data.lrc.lyric) || "",
        (data && data.tlyric && data.tlyric.lyric) || "",
        (data && data.romalrc && data.romalrc.lyric) || "",
        (data && data.yrc && data.yrc.lyric) || ""
      );
      parsedLines = withRefinedInterludes(parsedLines);
      lyricStatus = parsedLines.length ? "" : "暂无歌词";
    } catch (error) {
      state.lastError = "Lyric loading failed";
      lyricStatus = "歌词加载失败";
      console.error("AMap ESP32 Lyrics lyric load:", error);
    }
    // A slow response for the previous song must not overwrite the new song's
    // lyric timeline. This race was the source of the visible page oscillation.
    if (generation !== state.loadGeneration ||
        !state.song || String(state.song.id) !== songId) return;
    state.lines = parsedLines;
    state.lyricStatus = lyricStatus;
    state.loadingSongId = "";
    await publish(true);
  }

  function onProgress(_ignored, seconds) {
    const value = Number(seconds);
    if (!Number.isFinite(value)) return;
    state.positionMs = Math.max(0, Math.round(value * 1000 + Number(config("offsetMs", 0))));
    publish(false);
  }
  function onPlayState(...args) {
    const description = args.map(value => String(value || "")).join("|").toLowerCase();
    if (description.includes("pause")) state.playing = false;
    if (description.includes("resume") || description.includes("play")) state.playing = true;
    publish(true);
  }

  legacyNativeCmder.appendRegisterCall("Load", "audioplayer", loadSong);
  legacyNativeCmder.appendRegisterCall("PlayProgress", "audioplayer", onProgress);
  legacyNativeCmder.appendRegisterCall("PlayState", "audioplayer", onPlayState);
  startUdpHelper(true);
  state.heartbeat = setInterval(() => { loadSong(); publish(false); }, HEARTBEAT_MS);
  loadSong();

  plugin.onConfig(() => {
    const root = document.createElement("div");
    root.innerHTML = `<div style="max-width:520px;line-height:1.6"><h2>AMap ESP32 Lyrics</h2><p>UDP v1 is kept alive every 200 ms, matching the Android Forwarder protocol.</p><label>ESP32 IP address (required)</label><input id="amap-esp-host" required placeholder="Example: 192.168.1.53" style="display:block;width:100%;margin:6px 0 12px;box-sizing:border-box" value="${String(config("espHost", DEFAULT_ESP_HOST)).replace(/&/g, "&amp;").replace(/\"/g, "&quot;")}"><label>ESP32 UDP port</label><input id="amap-esp-port" type="number" min="1" max="65535" style="display:block;width:160px;margin:6px 0 12px" value="${targetPort()}"><label>Lyric offset (ms)</label><input id="amap-esp-offset" type="number" style="display:block;width:160px;margin:6px 0 12px" value="${Number(config("offsetMs", 0))}"><button id="amap-esp-save">Save and send now</button><p id="amap-esp-status" style="opacity:.72;margin-top:12px">${state.lastError}</p><p style="opacity:.62">The plugin starts and restarts its local UDP helper automatically. Your ESP32 address is stored only on this computer.</p></div>`;
    root.querySelector("#amap-esp-save").addEventListener("click", () => {
      const host = root.querySelector("#amap-esp-host").value.trim();
      const status = root.querySelector("#amap-esp-status");
      if (!/^\d{1,3}(?:\.\d{1,3}){3}$/.test(host)) { status.textContent = "Enter an IPv4 address, for example 192.168.1.53"; return; }
      plugin.setConfig("espHost", host);
      plugin.setConfig("espPort", Number(root.querySelector("#amap-esp-port").value) || DEFAULT_ESP_PORT);
      plugin.setConfig("offsetMs", Number(root.querySelector("#amap-esp-offset").value) || 0);
      startUdpHelper(true);
      publish(true);
      status.textContent = "Saved. Sending UDP v1 heartbeat…";
    });
    return root;
  });
});
