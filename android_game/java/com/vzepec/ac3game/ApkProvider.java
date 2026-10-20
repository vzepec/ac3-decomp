package com.vzepec.ac3game;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Entrega al instalador de Android el APK descargado por el actualizador (etapa 7, paso 7), sin
 * librerias (hace lo minimo de un FileProvider). Solo sirve ese archivo de la cache, de lectura,
 * y solo a quien recibe el permiso en el Intent (no esta exportado).
 * Autoridad: <paquete>.apk (build.sh la cambia con el paquete en las variantes personales).
 */
public class ApkProvider extends ContentProvider {
    static final String FILE = "actualizacion.apk";

    static Uri uri(String pkg) {
        return Uri.parse("content://" + pkg + ".apk/" + FILE);
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!FILE.equals(uri.getLastPathSegment()) || !"r".equals(mode)) throw new FileNotFoundException(uri.toString());
        return ParcelFileDescriptor.open(new File(getContext().getCacheDir(), FILE), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        return "application/vnd.android.package-archive";
    }

    @Override
    public Cursor query(Uri u, String[] p, String s, String[] a, String o) {
        return null;
    }

    @Override
    public Uri insert(Uri u, ContentValues v) {
        return null;
    }

    @Override
    public int delete(Uri u, String s, String[] a) {
        return 0;
    }

    @Override
    public int update(Uri u, ContentValues v, String s, String[] a) {
        return 0;
    }
}
