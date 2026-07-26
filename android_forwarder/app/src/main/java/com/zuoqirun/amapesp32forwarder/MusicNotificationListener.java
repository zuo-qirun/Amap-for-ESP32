package com.zuoqirun.amapesp32forwarder;

import android.content.ComponentName;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSession;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import android.os.Handler;
import android.os.Looper;
import android.service.notification.NotificationListenerService;
import android.service.notification.StatusBarNotification;
import android.util.Log;

import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public final class MusicNotificationListener extends NotificationListenerService {
    private static final String TAG = "MusicSession";
    private static volatile MusicNotificationListener activeInstance;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Map<MediaSession.Token, MediaController> observedControllers = new HashMap<>();
    private MediaSessionManager sessionManager;
    private MediaController controller;

    private final MediaSessionManager.OnActiveSessionsChangedListener sessionsChanged =
            this::onSessionsChanged;
    private final MediaController.Callback sessionCallback = new MediaController.Callback() {
        @Override
        public void onMetadataChanged(MediaMetadata metadata) {
            refreshSessions();
        }

        @Override
        public void onPlaybackStateChanged(PlaybackState state) {
            refreshSessions();
        }

        @Override
        public void onSessionDestroyed() {
            refreshSessions();
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        activeInstance = this;
        MusicStateStore.initialize(this);
        sessionManager = (MediaSessionManager) getSystemService(MEDIA_SESSION_SERVICE);
    }

    @Override
    public void onListenerConnected() {
        super.onListenerConnected();
        try {
            if (sessionManager != null) {
                sessionManager.addOnActiveSessionsChangedListener(sessionsChanged,
                        new ComponentName(this, MusicNotificationListener.class), handler);
            }
        } catch (Throwable error) {
            Log.w(TAG, "Unable to subscribe to media sessions", error);
        }
        refreshSessions();
    }

    @Override
    public void onListenerDisconnected() {
        stopListening();
        super.onListenerDisconnected();
    }

    @Override
    public void onNotificationPosted(StatusBarNotification sbn) {
        if (AppSettings.isPhoneEnabled(this) && AppSettings.arePhoneNotificationsEnabled(this)
                && PhoneStateStore.updateNotification(sbn)) {
            ForwarderService.requestPhoneRefresh();
        }
        if (sbn != null && (controller == null || MusicAppRegistry.isKnown(sbn.getPackageName()))) {
            refreshSessions();
        }
    }

    @Override
    public void onNotificationRemoved(StatusBarNotification sbn) {
        if (AppSettings.isPhoneEnabled(this) && PhoneStateStore.clearNotification(sbn)) {
            ForwarderService.requestPhoneRefresh();
        }
        super.onNotificationRemoved(sbn);
    }

    @Override
    public void onDestroy() {
        stopListening();
        if (activeInstance == this) {
            activeInstance = null;
        }
        super.onDestroy();
    }

    static boolean dispatchMediaControl(String action) {
        MusicNotificationListener instance = activeInstance;
        if (instance == null || action == null) {
            return false;
        }
        instance.handler.post(() -> instance.applyMediaControl(action));
        return true;
    }

    static boolean refreshMediaState() {
        MusicNotificationListener instance = activeInstance;
        if (instance == null) {
            return false;
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            instance.refreshSessions();
        } else {
            instance.handler.post(instance::refreshSessions);
        }
        return true;
    }

    private void applyMediaControl(String action) {
        if (controller == null) {
            refreshSessions();
        }
        MediaController current = controller;
        if (current == null) {
            AppSettings.noteError(this, "未找到可控制的活动音乐播放会话");
            return;
        }
        MediaController.TransportControls controls = current.getTransportControls();
        switch (action) {
            case MediaControlCommand.PREVIOUS:
                controls.skipToPrevious();
                break;
            case MediaControlCommand.NEXT:
                controls.skipToNext();
                break;
            case MediaControlCommand.PLAY_PAUSE:
                PlaybackState state = current.getPlaybackState();
                if (MediaControlCommand.isPlaying(
                        state == null ? PlaybackState.STATE_NONE : state.getState())) {
                    controls.pause();
                } else {
                    controls.play();
                }
                break;
            default:
                return;
        }
        Log.i(TAG, "ESP32 media control: " + action);
    }

    private void refreshSessions() {
        List<MediaController> sessions = Collections.emptyList();
        try {
            if (sessionManager != null) {
                sessions = sessionManager.getActiveSessions(
                        new ComponentName(this, MusicNotificationListener.class));
            }
        } catch (Throwable error) {
            Log.w(TAG, "Unable to read active media sessions", error);
        }
        onSessionsChanged(sessions);
    }

    private void onSessionsChanged(List<MediaController> sessions) {
        syncObservedSessions(sessions);
        selectController(sessions);
    }

    private void selectController(List<MediaController> sessions) {
        MediaController best = null;
        int bestScore = Integer.MIN_VALUE;
        if (sessions != null) {
            for (MediaController candidate : sessions) {
                if (candidate == null || getPackageName().equals(candidate.getPackageName())
                        || !isUsableSession(candidate)) {
                    continue;
                }
                MediaMetadata metadata = candidate.getMetadata();
                PlaybackState state = candidate.getPlaybackState();
                MusicAppRegistry.App app = MusicAppRegistry.resolve(
                        candidate.getPackageName(), applicationLabel(candidate.getPackageName()));
                int score = MusicAppRegistry.selectionScore(
                        playbackRank(state), hasMetadata(metadata), supportsControls(state),
                        app.known, sameSession(controller, candidate));
                if (score > bestScore) {
                    best = candidate;
                    bestScore = score;
                }
            }
        }
        controller = best;
        if (controller == null) {
            MusicStateStore.clear();
            return;
        }
        publish();
    }

    private void publish() {
        MediaController current = controller;
        if (current == null) {
            MusicStateStore.clear();
            return;
        }
        String packageName = current.getPackageName();
        MusicAppRegistry.App app = MusicAppRegistry.resolve(
                packageName, applicationLabel(packageName));
        MusicStateStore.update(this, app.sourceId, app.displayName,
                current.getMetadata(), current.getPlaybackState());
    }

    private void stopListening() {
        try {
            if (sessionManager != null) {
                sessionManager.removeOnActiveSessionsChangedListener(sessionsChanged);
            }
        } catch (Throwable ignored) {
        }
        for (MediaController observed : observedControllers.values()) {
            observed.unregisterCallback(sessionCallback);
        }
        observedControllers.clear();
        controller = null;
        MusicStateStore.clear();
    }

    private void syncObservedSessions(List<MediaController> sessions) {
        Map<MediaSession.Token, MediaController> next = new HashMap<>();
        if (sessions != null) {
            for (MediaController candidate : sessions) {
                if (candidate == null || getPackageName().equals(candidate.getPackageName())) {
                    continue;
                }
                MediaSession.Token token = candidate.getSessionToken();
                next.put(token, candidate);
                if (!observedControllers.containsKey(token)) {
                    candidate.registerCallback(sessionCallback, handler);
                }
            }
        }
        for (Map.Entry<MediaSession.Token, MediaController> entry
                : observedControllers.entrySet()) {
            if (!next.containsKey(entry.getKey())) {
                entry.getValue().unregisterCallback(sessionCallback);
            }
        }
        observedControllers.clear();
        observedControllers.putAll(next);
    }

    private static boolean sameSession(MediaController left, MediaController right) {
        return left == right || left != null && right != null
                && left.getSessionToken().equals(right.getSessionToken());
    }

    private static int playbackRank(PlaybackState state) {
        if (state == null) {
            return 0;
        }
        switch (state.getState()) {
            case PlaybackState.STATE_PLAYING:
            case PlaybackState.STATE_FAST_FORWARDING:
            case PlaybackState.STATE_REWINDING:
                return 10_000;
            case PlaybackState.STATE_BUFFERING:
                return 9_000;
            case PlaybackState.STATE_CONNECTING:
                return 8_000;
            case PlaybackState.STATE_PAUSED:
                return 5_000;
            case PlaybackState.STATE_SKIPPING_TO_NEXT:
            case PlaybackState.STATE_SKIPPING_TO_PREVIOUS:
            case PlaybackState.STATE_SKIPPING_TO_QUEUE_ITEM:
                return 7_000;
            default:
                return 0;
        }
    }

    private static boolean isUsableSession(MediaController candidate) {
        PlaybackState state = candidate.getPlaybackState();
        int stateValue = state == null ? PlaybackState.STATE_NONE : state.getState();
        return playbackRank(state) > 0 || hasMetadata(candidate.getMetadata())
                && stateValue != PlaybackState.STATE_STOPPED
                && stateValue != PlaybackState.STATE_ERROR;
    }

    private static boolean hasMetadata(MediaMetadata metadata) {
        return metadata != null && (!empty(metadata.getString(MediaMetadata.METADATA_KEY_TITLE))
                || !empty(metadata.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE)));
    }

    private static boolean supportsControls(PlaybackState state) {
        if (state == null) return false;
        long actions = state.getActions();
        long transportActions = PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE
                | PlaybackState.ACTION_PLAY_PAUSE | PlaybackState.ACTION_SKIP_TO_NEXT
                | PlaybackState.ACTION_SKIP_TO_PREVIOUS;
        return (actions & transportActions) != 0;
    }

    private String applicationLabel(String packageName) {
        try {
            PackageManager manager = getPackageManager();
            ApplicationInfo info = manager.getApplicationInfo(packageName, 0);
            CharSequence label = manager.getApplicationLabel(info);
            return label == null ? "" : label.toString().trim();
        } catch (PackageManager.NameNotFoundException | SecurityException ignored) {
            return "";
        }
    }

    private static boolean empty(String value) {
        return value == null || value.trim().isEmpty();
    }
}
