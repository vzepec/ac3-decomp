package com.vzepec.ac3game;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * Menu de inicio (etapa 7, paso 7): Jugar, Cambiar disco, Opciones y Acerca de.
 *
 * Solo codigo nuestro y vistas de Android, sin imagenes del juego. Se recorre con la cruceta y A
 * (el sistema convierte A en "pulsar" y B en "atras"). El juego (MainActivity) va en su propio
 * proceso (":game"): al salir de el, ese proceso termina y se vuelve aqui, y cada partida arranca
 * de cero. Las opciones se guardan en las preferencias "ac3game" y MainActivity las lee al
 * arrancar.
 */
public class LauncherActivity extends Activity {
    private static final String TAG = "AC3Game";
    private static final int PICK_DISC = 1;
    static final String PREFS = "ac3game";
    static final String KEY_URI = "disc_uri";
    static final String KEY_STATS = "show_stats";   /* contador y estadisticas de desarrollo en pantalla */

    private static final int BG = 0xFF0B1220, PANEL = 0xFF16213A, ACCENT = 0xFF3FA9F5, TEXT = 0xFFE8EEF8, DIM = 0xFF8C9AB5;

    private TextView discLine;
    private Button play;
    private boolean pickThenPlay;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        buildMenu();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideBars();
        refreshDisc();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideBars();
    }

    /* ------------------------------------------------------------- interfaz */

    private int dp(float v) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private void hideBars() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    private GradientDrawable box(int fill, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(fill);
        d.setCornerRadius(dp(10));
        if (stroke != 0) d.setStroke(dp(3), stroke);
        return d;
    }

    /** Boton grande, con el foco bien visible (se maneja con la cruceta). */
    private Button button(String label, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextColor(TEXT);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        StateListDrawable s = new StateListDrawable();
        s.addState(new int[] {android.R.attr.state_pressed}, box(ACCENT, ACCENT));
        s.addState(new int[] {android.R.attr.state_focused}, box(0xFF22345A, ACCENT));
        s.addState(new int[] {-android.R.attr.state_enabled}, box(0xFF111A2D, 0));
        s.addState(new int[] {}, box(PANEL, 0));
        b.setBackground(s);
        b.setStateListAnimator(null);
        b.setFocusable(true);
        b.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(300), dp(56));
        lp.topMargin = dp(10);
        b.setLayoutParams(lp);
        return b;
    }

    private TextView text(String s, float sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    private String version() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "?";
        }
    }

    private void buildMenu() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(BG);

        TextView title = text("AC3 Recomp", 40, TEXT, true);
        title.setLetterSpacing(0.05f);
        root.addView(title);
        root.addView(text("Ace Combat 3: Electrosphere · nativo en Android · " + version(), 14, DIM, false));
        discLine = text("", 14, ACCENT, false);
        discLine.setPadding(0, dp(8), 0, dp(10));
        root.addView(discLine);

        play = button("Jugar", v -> play());
        root.addView(play);
        root.addView(button("Cambiar disco", v -> { pickThenPlay = false; pickDisc(); }));
        root.addView(button("Opciones", v -> options()));
        root.addView(button("Acerca de", v -> about()));
        root.addView(button("Salir", v -> finishAndRemoveTask()));

        setContentView(root);
        play.requestFocus();
    }

    /* ------------------------------------------------------------- disco */

    private Uri savedDisc() {
        String s = getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_URI, null);
        return s == null ? null : Uri.parse(s);
    }

    /** Nombre del archivo elegido, o null si ya no se puede abrir (borrado, permiso perdido). */
    private String discName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception e) {
            Log.w(TAG, "disco guardado no accesible: " + e);
        }
        return null;
    }

    private void refreshDisc() {
        Uri u = savedDisc();
        String n = u == null ? null : discName(u);
        if (n != null) {
            discLine.setText("Disco: " + n);
            play.setText("Jugar");
        } else {
            discLine.setText(u == null ? "Todavía no has elegido la imagen de tu disco"
                                       : "No se puede abrir el disco elegido: vuelve a elegirlo");
            play.setText("Elegir disco y jugar");
        }
    }

    private void play() {
        Uri u = savedDisc();
        if (u == null || discName(u) == null) {
            pickThenPlay = true;
            pickDisc();
            return;
        }
        startActivity(new Intent(this, MainActivity.class));
    }

    private void pickDisc() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(i, PICK_DISC);
        } catch (Exception e) {
            Log.e(TAG, "no hay selector de archivos: " + e);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_DISC || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            Log.w(TAG, "sin permiso persistente: " + e);
        }
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_URI, uri.toString()).commit();
        refreshDisc();
        if (pickThenPlay) { pickThenPlay = false; play(); }
    }

    /* ------------------------------------------------------------- opciones y acerca de */

    /** Opciones: se guardan al marcarlas y el juego las lee al arrancar. */
    private void options() {
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        boolean[] on = {p.getBoolean(KEY_STATS, true)};
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("Opciones")
                .setMultiChoiceItems(new String[] {"Mostrar contadores y estad\u00edsticas de desarrollo"}, on,
                        (d, which, checked) -> p.edit().putBoolean(KEY_STATS, checked).commit())
                .setPositiveButton("Volver", null)
                .show();
    }

    private void about() {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("AC3 Recomp " + version())
                .setMessage("Ace Combat 3: Electrosphere (PlayStation, Namco, 1999) en Android, a 60 fps y en pantalla ancha.\n\n"
                        + "Esta app no contiene nada del juego: lo lee de la imagen de tu propio disco "
                        + "(europeo SCES-02066 o americano SLUS-00972).\n\n"
                        + "Proyecto no oficial, sin relación con Bandai Namco. Ace Combat es una marca de Bandai Namco Entertainment.\n\n"
                        + "Código y versiones: github.com/vzepec/ac3-decomp")
                .setPositiveButton("Volver", null)
                .show();
    }
}
