package com.zuoqirun.amapesp32forwarder;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;

final class AMapBroadcastReceiver extends BroadcastReceiver {
    interface Listener {
        void onAmapBroadcast(Intent intent);
    }

    private final Listener listener;

    AMapBroadcastReceiver(Listener listener) {
        this.listener = listener;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (listener != null) {
            listener.onAmapBroadcast(intent);
        }
    }

    static IntentFilter createFilter() {
        IntentFilter filter = new IntentFilter();
        for (String action : AMapConstants.AMAP_ACTIONS) {
            filter.addAction(action);
        }
        return filter;
    }
}
