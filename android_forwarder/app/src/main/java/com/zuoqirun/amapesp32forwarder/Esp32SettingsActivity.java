package com.zuoqirun.amapesp32forwarder;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

public final class Esp32SettingsActivity extends Activity {
    private WebView webView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        boolean dark = (getResources().getConfiguration().uiMode
                & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        int background = dark ? 0xFF000000 : 0xFFF2F2F7;
        int surface = dark ? 0xFF1C1C1E : 0xFFFFFFFF;
        int text = dark ? 0xFFF5F5F7 : 0xFF1C1C1E;
        int secondary = dark ? 0xFFA1A1A6 : 0xFF6E6E73;
        int separator = dark ? 0xFF38383A : 0xFFD1D1D6;
        int accent = dark ? 0xFF0A84FF : 0xFF007AFF;
        getWindow().setStatusBarColor(surface);
        getWindow().setNavigationBarColor(background);
        if (!dark && Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        }

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(background);

        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setPadding(dp(8), dp(6), dp(14), dp(6));
        bar.setBackgroundColor(surface);
        Button back = new Button(this);
        back.setText("‹");
        back.setTextSize(32);
        back.setTextColor(accent);
        back.setContentDescription("返回");
        back.setPadding(0, 0, 0, dp(3));
        back.setBackground(new RippleDrawable(ColorStateList.valueOf(
                dark ? 0x33409CFF : 0x22007AFF), null, rounded(0xFFFFFFFF, 24)));
        back.setOnClickListener(v -> finish());
        bar.addView(back, new LinearLayout.LayoutParams(dp(48), dp(48)));
        LinearLayout titleGroup = new LinearLayout(this);
        titleGroup.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(this);
        title.setText("ESP32 设备");
        title.setTextSize(18);
        title.setTextColor(text);
        title.setTypeface(android.graphics.Typeface.create("sans-serif-medium", 0));
        titleGroup.addView(title);
        TextView subtitle = new TextView(this);
        subtitle.setText("直连控制台");
        subtitle.setTextSize(12);
        subtitle.setTextColor(secondary);
        titleGroup.addView(subtitle);
        bar.addView(titleGroup, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        root.addView(bar);
        View divider = new View(this);
        divider.setBackgroundColor(separator);
        root.addView(divider, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(1)));

        webView = new WebView(this);
        webView.setBackgroundColor(background);
        webView.getSettings().setJavaScriptEnabled(true);
        webView.getSettings().setDomStorageEnabled(true);
        webView.getSettings().setBuiltInZoomControls(false);
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                return false;
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                view.evaluateJavascript("(function(){"
                        + "var p=document.getElementById('developerPreviewPanel');"
                        + "if(p)p.remove();"
                        + "var h=document.querySelector('.sub');"
                        + "if(h)h.textContent='手机端设备控制 · TFT 模拟显示已隐藏';"
                        + "})()", null);
            }

            @Override
            public void onReceivedError(WebView view, int errorCode,
                                        String description, String failingUrl) {
                Toast.makeText(Esp32SettingsActivity.this,
                        "无法打开 ESP32：" + description, Toast.LENGTH_LONG).show();
            }
        });
        root.addView(webView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);

        String address = AppSettings.getEsp32Ip(this);
        if (address == null || address.trim().isEmpty()) {
            address = "192.168.4.1";
        }
        address = address.trim();
        if (!address.startsWith("http://") && !address.startsWith("https://")) {
            address = "http://" + address;
        }
        if (!address.endsWith("/")) {
            address += "/";
        }
        webView.loadUrl(address + "?mobile=1");
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }

    private GradientDrawable rounded(int color, int radiusDp) {
        GradientDrawable drawable = new GradientDrawable();
        drawable.setColor(color);
        drawable.setCornerRadius(dp(radiusDp));
        return drawable;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
