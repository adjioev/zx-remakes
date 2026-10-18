package com.zxremakes.app;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

// HTTPS for the native code (android.cpp): a GET, redirects followed (also between http and https,
// which HttpURLConnection does not do by itself); null on any failure.
public final class Net {
    public static byte[] get(String url, int timeoutMs) {
        try {
            for (int hops = 0; hops < 6; hops++) {
                HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
                c.setConnectTimeout(timeoutMs);
                c.setReadTimeout(timeoutMs);
                c.setInstanceFollowRedirects(false);
                c.setRequestProperty("User-Agent", "ZXRemakes");
                int code = c.getResponseCode();
                if (code >= 300 && code < 400) {
                    String to = c.getHeaderField("Location");
                    c.disconnect();
                    if (to == null) return null;
                    url = new URL(new URL(url), to).toString();
                    continue;
                }
                if (code != 200) {
                    c.disconnect();
                    return null;
                }
                try (InputStream in = c.getInputStream()) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    byte[] buf = new byte[16384];
                    for (int n; (n = in.read(buf)) > 0;) out.write(buf, 0, n);
                    return out.toByteArray();
                } finally {
                    c.disconnect();
                }
            }
        } catch (Exception e) {
            return null;
        }
        return null;
    }
}
