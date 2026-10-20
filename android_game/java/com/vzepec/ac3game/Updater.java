package com.vzepec.ac3game;

import android.app.Activity;
import android.content.Intent;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;

/**
 * Actualizador (etapa 7, paso 7): mira en las releases de GitHub si hay una version mas nueva que
 * la instalada, descarga su APK, comprueba tamano y SHA-256 (los que publica GitHub para cada
 * archivo) y se lo pasa al instalador de Android (ApkProvider). Android pide confirmacion y solo
 * acepta un APK firmado con la misma clave que el instalado.
 *
 * Las releases son "pre-release" (alfa), asi que /releases/latest da 404: se pide la lista y se
 * toma la de version mas alta. La version sale de la etiqueta (v1.11-alfa -> 1.11) y se compara
 * numero a numero con la instalada (las de prueba, 1.11.3-alfa, van por delante de la 1.11-alfa).
 */
final class Updater {
    static final String ERR_SIZE = "size", ERR_SHA = "sha256";   /* errores que el menu traduce */

    static final class Release {
        String tag, body, url, sha256;
        long size;
    }

    interface Progress {
        void on(int percent);
    }

    /** "v1.11.3-alfa" -> {1, 11, 3}; null si no empieza por una version. */
    static int[] parseVersion(String v) {
        if (v == null) return null;
        if (v.startsWith("v")) v = v.substring(1);
        int dash = v.indexOf('-');
        if (dash >= 0) v = v.substring(0, dash);
        String[] p = v.split("\\.");
        int[] r = new int[p.length];
        try {
            for (int i = 0; i < p.length; i++) r[i] = Integer.parseInt(p[i]);
        } catch (NumberFormatException e) {
            return null;
        }
        return r;
    }

    static int compare(int[] a, int[] b) {
        for (int i = 0; i < Math.max(a.length, b.length); i++) {
            int x = i < a.length ? a[i] : 0, y = i < b.length ? b[i] : 0;
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
    }

    private static HttpURLConnection open(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(10000);
        c.setReadTimeout(30000);
        c.setRequestProperty("User-Agent", "AC3Recomp");
        c.setRequestProperty("Accept", "application/vnd.github+json");
        if (c.getResponseCode() != 200) throw new Exception("HTTP " + c.getResponseCode() + " en " + url);
        return c;
    }

    /** La release mas nueva que la version instalada, con su APK; null si no hay. */
    static Release check(String installed) throws Exception {
        int[] cur = parseVersion(installed);
        if (cur == null) return null;
        HttpURLConnection c = open(BuildInfo.UPDATE_URL);
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        try (InputStream in = c.getInputStream()) {
            byte[] buf = new byte[16384];
            for (int n; (n = in.read(buf)) > 0; ) b.write(buf, 0, n);
        }
        JSONArray rels = new JSONArray(b.toString("UTF-8"));
        Release best = null;
        int[] bestV = cur;
        for (int i = 0; i < rels.length(); i++) {
            JSONObject r = rels.getJSONObject(i);
            if (r.optBoolean("draft")) continue;
            int[] v = parseVersion(r.optString("tag_name"));
            if (v == null || compare(v, bestV) <= 0) continue;
            JSONArray as = r.optJSONArray("assets");
            for (int k = 0; as != null && k < as.length(); k++) {
                JSONObject a = as.getJSONObject(k);
                String name = a.optString("name");
                if (!name.startsWith("AC3Recomp-") || !name.endsWith(".apk")) continue;
                Release x = new Release();
                x.tag = r.optString("tag_name");
                x.body = r.optString("body");
                x.url = a.optString("browser_download_url");
                x.size = a.optLong("size");
                String d = a.optString("digest");   /* "sha256:..." */
                x.sha256 = d.startsWith("sha256:") ? d.substring(7) : null;
                best = x;
                bestV = v;
                break;
            }
        }
        return best;
    }

    /** Las novedades de la release ("## What's new" en ingles si la hay, si no "## Novedades"),
     *  sin las marcas de Markdown. */
    static String notes(String body, boolean english) {
        if (body == null) return "";
        int a = english ? body.indexOf("## What's new") : -1;
        if (a < 0) a = body.indexOf("## Novedades");
        String s = body;
        if (a >= 0) {
            int nl = body.indexOf('\n', a);
            int b = body.indexOf("\n## ", nl < 0 ? a + 1 : nl);
            s = body.substring(nl < 0 ? a : nl + 1, b < 0 ? body.length() : b);
        }
        s = s.replace("**", "").replace("`", "").replace("\r", "").trim();
        if (s.length() > 1500) s = s.substring(0, 1500) + "…";
        return s;
    }

    private static String hex(byte[] h) {
        StringBuilder sb = new StringBuilder();
        for (byte x : h) sb.append(String.format("%02x", x & 0xff));
        return sb.toString();
    }

    /** Descarga el APK a la cache y comprueba tamano y SHA-256. */
    static File download(Activity a, Release r, Progress p) throws Exception {
        File f = new File(a.getCacheDir(), ApkProvider.FILE);
        HttpURLConnection c = open(r.url);
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        long got = 0;
        int last = -1;
        try (InputStream in = c.getInputStream(); OutputStream out = new FileOutputStream(f)) {
            byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) > 0; ) {
                out.write(buf, 0, n);
                md.update(buf, 0, n);
                got += n;
                int pc = r.size > 0 ? (int) (got * 100 / r.size) : 0;
                if (pc != last) { last = pc; p.on(pc); }
            }
        }
        if (r.size > 0 && got != r.size) throw new Exception(ERR_SIZE);
        if (r.sha256 != null && !hex(md.digest()).equalsIgnoreCase(r.sha256))
            throw new Exception(ERR_SHA);
        return f;
    }

    /** Abre el instalador de Android con el APK descargado. Es el instalador clasico (no una
     *  sesion de PackageInstaller): Android cierra la app para sustituirla y no la deja reabrirse
     *  sola (el registro lo mostro: "Background activity start" bloqueado en Android 13); este
     *  termina en su pantalla "App instalada" con el boton Abrir. */
    @SuppressWarnings("deprecation")
    static void install(Activity a, File apk) {
        Intent i = new Intent(Intent.ACTION_INSTALL_PACKAGE)
                .setDataAndType(ApkProvider.uri(a.getPackageName()), "application/vnd.android.package-archive")
                /* en su propia tarea: en la nuestra, Android la cierra con la app al sustituirla y
                 * el boton Abrir del final no llegaba a abrir nada (medido en la Retroid) */
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        a.startActivity(i);
    }
}
