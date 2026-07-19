package com.zuoqirun.amapesp32forwarder;

import org.junit.Test;

import static org.junit.Assert.assertEquals;

public final class BleEnableFlowTest {
    @Test
    public void usesStandardEnableRequestWhenAvailable() {
        RecordingLauncher launcher = new RecordingLauncher(true, true, false);

        assertEquals(BleEnableFlow.LaunchResult.ENABLE_REQUESTED,
                BleEnableFlow.launch(launcher));
        assertEquals(1, launcher.enableAttempts);
        assertEquals(0, launcher.settingsAttempts);
    }

    @Test
    public void fallsBackToBluetoothSettingsOnCarHeadUnit() {
        RecordingLauncher launcher = new RecordingLauncher(false, true, false);

        assertEquals(BleEnableFlow.LaunchResult.SETTINGS_OPENED,
                BleEnableFlow.launch(launcher));
        assertEquals(1, launcher.enableAttempts);
        assertEquals(1, launcher.settingsAttempts);
    }

    @Test
    public void fallsBackWhenStandardRequestThrows() {
        RecordingLauncher launcher = new RecordingLauncher(false, true, true);

        assertEquals(BleEnableFlow.LaunchResult.SETTINGS_OPENED,
                BleEnableFlow.launch(launcher));
        assertEquals(1, launcher.settingsAttempts);
    }

    @Test
    public void reportsUnavailableWhenNeitherActivityExists() {
        RecordingLauncher launcher = new RecordingLauncher(false, false, false);

        assertEquals(BleEnableFlow.LaunchResult.UNAVAILABLE,
                BleEnableFlow.launch(launcher));
        assertEquals(1, launcher.enableAttempts);
        assertEquals(1, launcher.settingsAttempts);
    }

    private static final class RecordingLauncher implements BleEnableFlow.Launcher {
        private final boolean enableResult;
        private final boolean settingsResult;
        private final boolean throwFromEnable;
        int enableAttempts;
        int settingsAttempts;

        RecordingLauncher(boolean enableResult, boolean settingsResult,
                          boolean throwFromEnable) {
            this.enableResult = enableResult;
            this.settingsResult = settingsResult;
            this.throwFromEnable = throwFromEnable;
        }

        @Override
        public boolean launchEnableRequest() {
            enableAttempts++;
            if (throwFromEnable) {
                throw new IllegalStateException("missing system activity");
            }
            return enableResult;
        }

        @Override
        public boolean launchBluetoothSettings() {
            settingsAttempts++;
            return settingsResult;
        }
    }
}
