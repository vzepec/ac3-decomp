package com.vzepec.ac3game;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.util.Log;

/**
 * Despues de una actualizacion hecha desde el menu (etapa 7, paso 7). Android cierra la app para
 * sustituir su codigo y al terminar avisa a la version nueva (MY_PACKAGE_REPLACED): aqui se marca
 * para que el menu diga "Actualizada" la proxima vez que se abra (con el boton Abrir del
 * instalador). Reabrir el menu desde aqui no se puede: Android 13 lo bloquea ("Background activity
 * start"; medido en la Retroid). Instalar con adb o con un gestor de archivos no marca nada.
 */
public class UpdatedReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context c, Intent intent) {
        if (!Intent.ACTION_MY_PACKAGE_REPLACED.equals(intent.getAction())) return;
        SharedPreferences p = c.getSharedPreferences(LauncherActivity.PREFS, Context.MODE_PRIVATE);
        if (!p.getBoolean(LauncherActivity.KEY_RELAUNCH, false)) return;
        p.edit().putBoolean(LauncherActivity.KEY_RELAUNCH, false).putBoolean(LauncherActivity.KEY_UPDATED, true).commit();
        Log.i("AC3Game", "actualizada desde el menu");
    }
}
