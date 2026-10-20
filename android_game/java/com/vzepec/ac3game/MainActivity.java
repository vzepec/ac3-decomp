package com.vzepec.ac3game;

import android.app.NativeActivity;
import android.content.Intent;
import android.content.SharedPreferences;
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
    private static native void nativeSetOptions(boolean showStats, int scale, int filter);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        nativeSetOptions(p.getBoolean(LauncherActivity.KEY_STATS, true), p.getInt(LauncherActivity.KEY_SCALE, LauncherActivity.DEFAULT_SCALE),
                         p.getInt(LauncherActivity.KEY_FILTER, 0));
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
}
