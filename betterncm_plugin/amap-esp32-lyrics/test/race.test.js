"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const callbacks = {};
const pendingLyrics = new Map();
const frames = [];
const controls = [];
let controlPending = true;
let currentSong = {
  id: 1,
  name: "Song A",
  artists: [{ name: "Artist A" }],
  album: { name: "Album A", picUrl: "https://example.test/a.jpg" },
  duration: 180000,
};

const context = {
  console,
  setInterval: () => 1,
  clearInterval: () => {},
  setTimeout,
  Date,
  document: {
    querySelector(selector) {
      if (selector !== ".m-player:not(.f-dn)" && selector !== ".m-player") return null;
      return {
        querySelector(buttonSelector) {
          return { click: () => controls.push(buttonSelector) };
        },
      };
    },
  },
  plugin: {
    onLoad(callback) { callback(); },
    onConfig() {},
    getConfig(key, fallback) { return key === "espHost" ? "192.0.2.10" : fallback; },
    setConfig() {},
  },
  betterncm: {
    ncm: { getPlayingSong: () => currentSong },
    app: { exec() {} },
  },
  loadedPlugins: {
    "amap-esp32-lyrics": { pluginPath: "C:\\BetterNCM\\plugins\\amap-esp32-lyrics" },
    liblyric: {
      getLyricData(id) {
        return new Promise(resolve => pendingLyrics.set(String(id), resolve));
      },
      parseLyric(original) {
        if (original === "INTERLUDE") return [
          { time: 0, duration: 1000, originalLyric: "Before", dynamicLyric: [] },
          { time: 1000, duration: 6000, originalLyric: "", isInterlude: true, dynamicLyric: [] },
          { time: 7000, duration: 2000, originalLyric: "After", dynamicLyric: [] },
        ];
        return original ? [{
          time: 0,
          duration: 5000,
          originalLyric: original,
          translatedLyric: "",
          dynamicLyric: [],
        }] : [];
      },
    },
  },
  legacyNativeCmder: {
    appendRegisterCall(event, target, callback) {
      callbacks[`${event}:${target}`] = callback;
    },
  },
  fetch: async (url, options) => {
    if (String(url).startsWith("http://127.0.0.1:")) {
      frames.push(JSON.parse(options.body));
      const response = controlPending ? { controls: ["play_pause"] } : { controls: [] };
      controlPending = false;
      return { ok: true, status: 200, json: async () => response };
    }
    return { ok: true, status: 200, json: async () => ({ songs: [] }) };
  },
};

vm.createContext(context);
const source = fs.readFileSync(path.join(__dirname, "..", "index.js"), "utf8");
vm.runInContext(source, context, { filename: "index.js" });

const flush = () => new Promise(resolve => setTimeout(resolve, 0));

(async () => {
  await flush();
  currentSong = {
    id: 2,
    name: "Song B",
    artists: [{ name: "Artist B" }],
    album: { name: "Album B", picUrl: "https://example.test/b.jpg" },
    duration: 200000,
  };
  callbacks["Load:audioplayer"]();
  await flush();

  pendingLyrics.get("1")({ lrc: { lyric: "Lyric A" } });
  await flush();
  pendingLyrics.get("2")({ lrc: { lyric: "Lyric B" } });
  await flush();
  await flush();

  const songBFrames = frames.filter(frame => frame.music.songId === 2);
  assert(songBFrames.some(frame => frame.music.lyric === "歌词加载中"));
  assert(songBFrames.some(frame => frame.music.lyric === "Lyric B"));
  assert(!songBFrames.some(frame => frame.music.lyric === "Lyric A"));
  assert.equal(songBFrames.at(-1).music.active, true);

  currentSong = {
    id: 4,
    name: "Interlude",
    artists: [{ name: "Artist D" }],
    album: { name: "Album D", picUrl: "https://example.test/d.jpg" },
    duration: 120000,
  };
  callbacks["Load:audioplayer"]();
  await flush();
  pendingLyrics.get("4")({ lrc: { lyric: "INTERLUDE" } });
  await flush();
  callbacks["PlayProgress:audioplayer"](null, 3);
  callbacks["PlayState:audioplayer"]("play");
  await flush();
  const interludeFrames = frames.filter(frame => frame.music.songId === 4);
  assert.equal(interludeFrames.at(-1).music.interlude, true);
  assert.equal(interludeFrames.at(-1).music.lyric, "");
  assert.equal(interludeFrames.at(-1).music.lineStartMs, 1000);
  assert.equal(interludeFrames.at(-1).music.lineDurationMs, 6000);
  assert.equal(interludeFrames.at(-1).music.previousLyric, "Before");
  assert.equal(interludeFrames.at(-1).music.nextLyric, "After");

  currentSong = {
    id: 3,
    name: "Instrumental",
    artists: [{ name: "Artist C" }],
    album: { name: "Album C", picUrl: "https://example.test/c.jpg" },
    duration: 120000,
  };
  callbacks["Load:audioplayer"]();
  await flush();
  pendingLyrics.get("3")({ lrc: { lyric: "" } });
  await flush();
  await flush();
  const noLyricFrames = frames.filter(frame => frame.music.songId === 3);
  assert(noLyricFrames.some(frame => frame.music.lyric === "歌词加载中"));
  assert.equal(noLyricFrames.at(-1).music.lyric, "暂无歌词");
  assert(noLyricFrames.every(frame => frame.music.active));
  assert.deepEqual(controls, [".btnp-pause, .btnp-play"]);
  console.log(`race test passed (${frames.length} frames)`);
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
