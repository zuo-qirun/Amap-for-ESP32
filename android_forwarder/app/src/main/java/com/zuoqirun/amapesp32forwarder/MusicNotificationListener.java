package com.zuoqirun.amapesp32forwarder;

import android.content.ComponentName;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import android.os.Handler;
import android.os.Looper;
import android.service.notification.NotificationListenerService;
import android.service.notification.StatusBarNotification;
import android.util.Log;

import java.util.Collections;
import java.util.List;

public final class MusicNotificationListener extends NotificationListenerService {
    static final String NETEASE_PACKAGE = "com.netease.cloudmusic";
    private static final String TAG = "NetEaseMusic";
    private static volatile MusicNotificationListener activeInstance;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private MediaSessionManager sessionManager;
    private MediaController controller;

    private final MediaSessionManager.OnActiveSessionsChangedListener sessionsChanged =
            this::selectController;
    private final MediaController.Callback controllerCallback = new MediaController.Callback() {
        @Override
        public void onMetadataChanged(android.media.MediaMetadata metadata) {
            publish();
        }

        @Override
        public void onPlaybackStateChanged(PlaybackState state) {
            publish();
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
        if (sbn != null && NETEASE_PACKAGE.equals(sbn.getPackageName()) && controller == null) {
            refreshSessions();
        }
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

    private void applyMediaControl(String action) {
        if (controller == null) {
            refreshSessions();
        }
        MediaController current = controller;
        if (current == null) {
            AppSettings.noteError(this, "未找到网易云音乐的活动播放会话");
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
        selectController(sessions);
    }

    private void selectController(List<MediaController> sessions) {
        MediaController best = null;
        int bestScore = Integer.MIN_VALUE;
        if (sessions != null) {
            for (MediaController candidate : sessions) {
                if (candidate == null || !NETEASE_PACKAGE.equals(candidate.getPackageName())) {
                    continue;
                }
                int score = playbackScore(candidate.getPlaybackState());
                if (score > bestScore) {
                    best = candidate;
                    bestScore = score;
                }
            }
        }
        if (sameSession(controller, best)) {
            publish();
            return;
        }
        if (controller != null) {
            controller.unregisterCallback(controllerCallback);
        }
        controller = best;
        if (controller == null) {
            MusicStateStore.clear();
            return;
        }
        controller.registerCallback(controllerCallback, handler);
        publish();
    }

    private void publish() {
        MediaController current = controller;
        if (current == null) {
            MusicStateStore.clear();
            return;
        }
        MusicStateStore.update(this, current.getMetadata(), current.getPlaybackState());
    }

    private void stopListening() {
        try {
            if (sessionManager != null) {
                sessionManager.removeOnActiveSessionsChangedListener(sessionsChanged);
            }
        } catch (Throwable ignored) {
        }
        if (controller != null) {
            controller.unregisterCallback(controllerCallback);
            controller = null;
        }
        MusicStateStore.clear();
    }

    private static boolean sameSession(MediaController left, MediaController right) {
        return left == right || left != null && right != null
                && left.getSessionToken().equals(right.getSessionToken());
    }

    private static int playbackScore(PlaybackState state) {
        if (state == null) {
            return 0;
        }
        if (state.getState() == PlaybackState.STATE_PLAYING) {
            return 100;
        }
        if (state.getState() == PlaybackState.STATE_BUFFERING) {
            return 80;
        }
        if (state.getState() == PlaybackState.STATE_PAUSED) {
            return 60;
        }
        return 10;
    }
}
