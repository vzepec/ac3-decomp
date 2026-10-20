package com.vzepec.ac3game;

import android.app.NativeActivity;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.util.Log;

/**
 * Puente minimo entre Android y el codigo nativo (android_main en game_main.c).
 *
 * Pide la imagen de disco con el selector de archivos del sistema (Storage
 * Access Framework): no hace falta ningun permiso de almacenamiento. El URI
 * elegido se guarda con permiso persistente, asi que solo se pide la primera
 * vez (o cuando el disco no es valido).
 * El descriptor de archivo abierto se pasa al codigo nativo.
 *
 * Desde el paso 7 de la etapa 7 se abre desde el menu de inicio (LauncherActivity), en su propio
 * proceso (":game"): al salir, el proceso termina (android_main hace _exit) y se vuelve al menu.
 * El disco y las opciones llegan por las preferencias compartidas.
 */
public class MainActivity extends NativeActivity {
    private static final String TAG = "AC3Game";
    private static final int PICK_DISC = 1;
    private static final String PREFS = LauncherActivity.PREFS;
    private static final String KEY_URI = LauncherActivity.KEY_URI;

    static {
        System.loadLibrary("ac3game");
    }

    /** Implementado en game_main.c: entrega al codigo nativo el fd de la imagen. */
    private static native void nativeSetDisc(int fd);

    /** Implementado en game_main.c: opciones del menu de inicio (antes de arrancar el juego). */
    private static native void nativeSetOptions(boolean showStats, int scale, int filter, int touch);

    /** Implementado en game_main.c: los graficos del mando virtual (ARGB, celdas de touchpad.h). */
    private static native void nativeSetTouchAtlas(int[] argb, int w, int h);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        nativeSetOptions(p.getBoolean(LauncherActivity.KEY_STATS, true), p.getInt(LauncherActivity.KEY_SCALE, LauncherActivity.DEFAULT_SCALE),
                         p.getInt(LauncherActivity.KEY_FILTER, 0), p.getInt(LauncherActivity.KEY_TOUCH, 0));
        if (p.getInt(LauncherActivity.KEY_TOUCH, 0) != 2) touchAtlas();
        if (savedInstanceState == null && !openSavedDisc()) {
            pickDisc();
        }
    }

    /** Intenta reabrir la imagen elegida en una sesion anterior. */
    private boolean openSavedDisc() {
        String s = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_URI, null);
        return s != null && openDisc(Uri.parse(s));
    }

    private boolean openDisc(Uri uri) {
        try {
            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
            if (pfd == null) return false;
            nativeSetDisc(pfd.detachFd());
            Log.i(TAG, "imagen de disco: " + uri);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "no se puede abrir " + uri + ": " + e);
            return false;
        }
    }

    /** Abre el selector de archivos. Lo llama tambien el codigo nativo (JNI). */
    public void pickDisc() {
        runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            try {
                startActivityForResult(i, PICK_DISC);
            } catch (Exception e) {
                Log.e(TAG, "no hay selector de archivos: " + e);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_DISC) return;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            finish();  /* sin disco no hay nada que mostrar: de vuelta al menu */
            return;
        }
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            Log.w(TAG, "sin permiso persistente: " + e);
        }
        SharedPreferences.Editor ed = getSharedPreferences(PREFS, MODE_PRIVATE).edit();
        ed.putString(KEY_URI, uri.toString()).apply();
        if (!openDisc(uri)) pickDisc();
    }

    /* ------------------------------------------------------------- mando virtual (etapa 7, paso 8) */

    /* celdas de 128 px, 5 por fila, en el orden de touchpad.h */
    private static final int CELL = 128, COLS = 5, NCELL = 17;

    /** Dibuja los graficos del mando virtual (dibujo propio, sin nada del juego) y se los pasa al nativo. */
    private void touchAtlas() {
        int rows = (NCELL + COLS - 1) / COLS;
        Bitmap bm = Bitmap.createBitmap(CELL * COLS, CELL * rows, Bitmap.Config.ARGB_8888);
        Canvas cv = new Canvas(bm);
        Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG), ring = new Paint(Paint.ANTI_ALIAS_FLAG), sym = new Paint(Paint.ANTI_ALIAS_FLAG);
        fill.setColor(0x99202833);
        ring.setStyle(Paint.Style.STROKE);
        ring.setStrokeWidth(6);
        ring.setColor(0xE6E8EEF8);
        sym.setStyle(Paint.Style.STROKE);
        sym.setStrokeWidth(10);
        sym.setStrokeCap(Paint.Cap.ROUND);
        sym.setStrokeJoin(Paint.Join.ROUND);
        Paint txt = new Paint(Paint.ANTI_ALIAS_FLAG);
        txt.setColor(0xFFE8EEF8);
        txt.setTypeface(Typeface.DEFAULT_BOLD);
        txt.setTextAlign(Paint.Align.CENTER);
        for (int i = 0; i < NCELL; i++) {
            float x0 = (i % COLS) * CELL, y0 = (i / COLS) * CELL, cx = x0 + CELL / 2f, cy = y0 + CELL / 2f, r = CELL / 2f - 6;
            switch (i) {
            case 0: case 1: case 2: case 3: {   /* cruz, circulo, cuadrado, triangulo: en sus colores */
                cv.drawCircle(cx, cy, r, fill);
                cv.drawCircle(cx, cy, r, ring);
                int[] col = {0xFF7CB2F0, 0xFFFF6B6B, 0xFFF0A0E6, 0xFF5FD6A0};
                sym.setColor(col[i]);
                float k = r * 0.42f;
                if (i == 0) { cv.drawLine(cx - k, cy - k, cx + k, cy + k, sym); cv.drawLine(cx - k, cy + k, cx + k, cy - k, sym); }
                else if (i == 1) cv.drawCircle(cx, cy, k * 1.05f, sym);
                else if (i == 2) cv.drawRect(cx - k, cy - k, cx + k, cy + k, sym);
                else {
                    Path t = new Path();
                    t.moveTo(cx, cy - k * 1.15f);
                    t.lineTo(cx + k * 1.15f, cy + k * 0.8f);
                    t.lineTo(cx - k * 1.15f, cy + k * 0.8f);
                    t.close();
                    cv.drawPath(t, sym);
                }
                break;
            }
            case 4: case 5: case 6: case 7: {   /* L1, L2, R1, R2: 34 x 22 dp de medio tamano */
                float hh = CELL / 2f * 22 / 34 - 4;
                RectF rr = new RectF(x0 + 4, cy - hh, x0 + CELL - 4, cy + hh);
                cv.drawRoundRect(rr, 22, 22, fill);
                cv.drawRoundRect(rr, 22, 22, ring);
                txt.setTextSize(42);
                cv.drawText(new String[] {"L1", "L2", "R1", "R2"}[i - 4], cx, cy + 15, txt);
                break;
            }
            case 8: case 9: {   /* START, SELECT: 40 x 16 dp */
                float hh = CELL / 2f * 16 / 40 - 3;
                RectF rr = new RectF(x0 + 4, cy - hh, x0 + CELL - 4, cy + hh);
                cv.drawRoundRect(rr, hh, hh, fill);
                cv.drawRoundRect(rr, hh, hh, ring);
                txt.setTextSize(24);
                cv.drawText(i == 8 ? "START" : "SELECT", cx, cy + 9, txt);
                break;
            }
            case 10: case 11: case 12: case 13: {   /* cruceta: arriba, abajo, izquierda, derecha */
                RectF rr = new RectF(x0 + 6, y0 + 6, x0 + CELL - 6, y0 + CELL - 6);
                cv.drawRoundRect(rr, 20, 20, fill);
                cv.drawRoundRect(rr, 20, 20, ring);
                Paint a = new Paint(Paint.ANTI_ALIAS_FLAG);
                a.setColor(0xFFE8EEF8);
                float k = 28;
                Path t = new Path();
                float[][] dir = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
                float dx = dir[i - 10][0], dy = dir[i - 10][1];
                t.moveTo(cx + dx * k, cy + dy * k);
                t.lineTo(cx - dx * k * 0.6f + dy * k, cy - dy * k * 0.6f + dx * k);
                t.lineTo(cx - dx * k * 0.6f - dy * k, cy - dy * k * 0.6f - dx * k);
                t.close();
                cv.drawPath(t, a);
                break;
            }
            case 14: {   /* base del stick */
                Paint b = new Paint(Paint.ANTI_ALIAS_FLAG);
                b.setColor(0x66202833);
                cv.drawCircle(cx, cy, r, b);
                cv.drawCircle(cx, cy, r, ring);
                break;
            }
            case 15: {   /* mando del stick */
                Paint b = new Paint(Paint.ANTI_ALIAS_FLAG);
                b.setColor(0xCC8C9AB5);
                cv.drawCircle(cx, cy, r, b);
                cv.drawCircle(cx, cy, r, ring);
                break;
            }
            case 16: {   /* ocultar / mostrar: un mando pequeno */
                RectF body = new RectF(x0 + 18, cy - 26, x0 + CELL - 18, cy + 26);
                cv.drawRoundRect(body, 26, 26, fill);
                cv.drawRoundRect(body, 26, 26, ring);
                Paint d = new Paint(Paint.ANTI_ALIAS_FLAG);
                d.setColor(0xFFE8EEF8);
                cv.drawCircle(cx - 22, cy, 8, d);
                cv.drawCircle(cx + 22, cy, 8, d);
                break;
            }
            default: break;
            }
        }
        int[] px = new int[bm.getWidth() * bm.getHeight()];
        bm.getPixels(px, 0, bm.getWidth(), 0, 0, bm.getWidth(), bm.getHeight());
        nativeSetTouchAtlas(px, bm.getWidth(), bm.getHeight());
        bm.recycle();
    }
}
