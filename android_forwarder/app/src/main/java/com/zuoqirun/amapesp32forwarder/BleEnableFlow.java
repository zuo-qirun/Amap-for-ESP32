package com.zuoqirun.amapesp32forwarder;

final class BleEnableFlow {
    enum LaunchResult {
        ENABLE_REQUESTED,
        SETTINGS_OPENED,
        UNAVAILABLE
    }

    interface Launcher {
        boolean launchEnableRequest();

        boolean launchBluetoothSettings();
    }

    private BleEnableFlow() {}

    static LaunchResult launch(Launcher launcher) {
        try {
            if (launcher.launchEnableRequest()) {
                return LaunchResult.ENABLE_REQUESTED;
            }
        } catch (RuntimeException ignored) {
        }

        try {
            if (launcher.launchBluetoothSettings()) {
                return LaunchResult.SETTINGS_OPENED;
            }
        } catch (RuntimeException ignored) {
        }
        return LaunchResult.UNAVAILABLE;
    }
}
