package com.zuoqirun.amapesp32forwarder;

import org.junit.Test;

import static org.junit.Assert.assertEquals;

public final class AppSettingsTest {
    @Test
    public void targetPackageDefaultsWhenMissingOrBlank() {
        assertEquals(AppSettings.DEFAULT_TARGET_PACKAGE, AppSettings.normalizeTargetPackage(null));
        assertEquals(AppSettings.DEFAULT_TARGET_PACKAGE, AppSettings.normalizeTargetPackage("   "));
    }

    @Test
    public void targetPackageIsTrimmedAndPreserved() {
        assertEquals("com.example.amap.coexist",
                AppSettings.normalizeTargetPackage("  com.example.amap.coexist  "));
    }
}
