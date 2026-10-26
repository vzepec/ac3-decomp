package com.vzepec.ac3game;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.util.Log;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;

/**
 * Menu de inicio (etapa 7, paso 7): Jugar, Cambiar disco, Opciones, Acerca de y Salir, el aviso de
 * version nueva con su boton de actualizar (Updater) y el selector de idioma (ingles por defecto,
 * o espanol; solo el menu: el juego va en el idioma del disco).
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
    static final String KEY_LANG = "lang";          /* idioma del menu: "en" (por defecto) o "es" */
    static final String KEY_SCALE = "scale";        /* resolucion interna de la GPU, 1-4 (etapa 8, paso 4) */
    static final int DEFAULT_SCALE = 4;   /* 60 fps en la Retroid Pocket Flip 2 (medido) */
    static final String KEY_FILTER = "filter";      /* filtro de texturas: 0 el de la PS1, 1 bilineal */
    static final String KEY_TOUCH = "touch";        /* mando tactil: 0 automatico, 1 siempre, 2 nunca (etapa 7, paso 8) */
    static final String KEY_RUMBLE = "rumble";      /* vibracion del DualShock (etapa 7, paso 9) */
    static final String KEY_RUMBLE_FIRE = "rumble_fire";   /* extra, no del original: al disparar y acelerar */
    static final String KEY_HZ = "hz";              /* 60 o 120 imagenes por segundo, si la pantalla llega (etapa 8, paso 12) */
    static final String KEY_RELAUNCH = "relaunch";  /* actualizacion pedida desde el menu (UpdatedReceiver) */
    static final String KEY_UPDATED = "updated";    /* recien actualizada: avisarlo una vez */

    private static final int BG = 0xFF0B1220, PANEL = 0xFF16213A, ACCENT = 0xFF3FA9F5, TEXT = 0xFFE8EEF8,
                             DIM = 0xFF8C9AB5, WARN = 0xFFFFB347;
    private static final int COL = 220, GAP = 10;   /* rejilla de dos columnas (dp) */

    private boolean en = true;
    private TextView discLine, updateLine;
    private Button play, update;
    private String statusMsg = "";
    private boolean pickThenPlay;
    private Updater.Release release;   /* la version nueva, si la hay */
    private File pendingApk;           /* descargado, esperando el permiso de instalar apps */
    private boolean busy;
    private boolean installerShown;    /* el instalador de Android esta (o estuvo) en pantalla */

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        en = !"es".equals(getSharedPreferences(PREFS, MODE_PRIVATE).getString(KEY_LANG, "en"));
        buildMenu();
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        if (p.getBoolean(KEY_UPDATED, false)) {
            p.edit().putBoolean(KEY_UPDATED, false).commit();
            status(t("Updated to ", "Actualizada a la ") + version());
        }
        checkUpdate();
    }

    @Override
    protected void onResume() {
        super.onResume();
        hideBars();
        refreshDisc();
        if (installerShown) {   /* de vuelta del instalador sin actualizar (cancelado o fallido) */
            installerShown = false;
            busy = false;
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_RELAUNCH, false).commit();
            showRelease();
        }
        if (pendingApk != null && getPackageManager().canRequestPackageInstalls()) {
            File f = pendingApk;
            pendingApk = null;
            install(f);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideBars();
    }

    /** El texto en el idioma elegido. */
    private String t(String english, String spanish) {
        return en ? english : spanish;
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

    /** Boton con el foco bien visible (se maneja con la cruceta). */
    private Button button(String label, float sp, int fill, View.OnClickListener l) {
        Button b = new Button(this);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextColor(TEXT);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setPadding(dp(8), 0, dp(8), 0);
        StateListDrawable s = new StateListDrawable();
        s.addState(new int[] {android.R.attr.state_pressed}, box(ACCENT, ACCENT));
        s.addState(new int[] {android.R.attr.state_focused}, box(0xFF22345A, ACCENT));
        s.addState(new int[] {-android.R.attr.state_enabled}, box(0xFF111A2D, 0));
        s.addState(new int[] {}, box(fill, 0));
        b.setBackground(s);
        b.setStateListAnimator(null);
        b.setFocusable(true);
        b.setOnClickListener(l);
        return b;
    }

    private Button gridButton(String label, View.OnClickListener l) {
        Button b = button(label, 18, PANEL, l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(COL), dp(50));
        lp.setMargins(dp(GAP / 2f), dp(GAP / 2f), dp(GAP / 2f), dp(GAP / 2f));
        b.setLayoutParams(lp);
        return b;
    }

    private LinearLayout row(View... v) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER);
        for (View x : v) r.addView(x);
        return r;
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

    /** Selector de idioma pequeno, arriba a la derecha. */
    private View langSelector() {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.HORIZONTAL);
        String[][] opts = {{"en", "EN"}, {"es", "ES"}};
        for (String[] o : opts) {
            boolean sel = o[0].equals(en ? "en" : "es");
            Button b = button(o[1], 13, sel ? 0xFF1F5F99 : PANEL, v -> setLang(o[0]));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(52), dp(34));
            lp.leftMargin = dp(6);
            b.setLayoutParams(lp);
            l.addView(b);
        }
        FrameLayout.LayoutParams fp = new FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.END);
        fp.setMargins(0, dp(12), dp(16), 0);
        l.setLayoutParams(fp);
        return l;
    }

    private void setLang(String lang) {
        boolean e = !"es".equals(lang);
        if (e == en) return;
        en = e;
        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString(KEY_LANG, lang).commit();
        statusMsg = "";
        buildMenu();
        refreshDisc();
        showRelease();
    }

    private void buildMenu() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(BG);

        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER);
        col.setPadding(0, dp(16), 0, dp(16));

        TextView title = text("AC3 Recomp", 38, TEXT, true);
        title.setLetterSpacing(0.05f);
        col.addView(title);
        col.addView(text("Ace Combat 3: Electrosphere · " + t("native on Android", "nativo en Android") + " · "
                         + version(), 14, DIM, false));
        discLine = text("", 14, ACCENT, false);
        discLine.setPadding(0, dp(8), 0, dp(2));
        col.addView(discLine);
        updateLine = text(statusMsg, 14, WARN, true);
        updateLine.setVisibility(statusMsg.isEmpty() ? View.GONE : View.VISIBLE);
        col.addView(updateLine);

        /* fila 1: Jugar (las dos columnas) o Jugar + Actualizar; filas 2 y 3: el resto */
        play = gridButton(t("Play", "Jugar"), v -> play());
        update = gridButton(t("Update", "Actualizar"), v -> askUpdate());
        update.setTextColor(WARN);
        update.setVisibility(View.GONE);
        LinearLayout first = row(play, update);
        first.setPadding(0, dp(6), 0, 0);
        col.addView(first);
        col.addView(row(gridButton(t("Change disc", "Cambiar disco"), v -> { pickThenPlay = false; pickDisc(); }),
                        gridButton(t("Options", "Opciones"), v -> options())));
        col.addView(row(gridButton(t("About", "Acerca de"), v -> about()),
                        gridButton(t("Exit", "Salir"), v -> finishAndRemoveTask())));
        setUpdateVisible(false);

        ScrollView sv = new ScrollView(this);   /* por si la pantalla es baja */
        sv.setFillViewport(true);
        sv.addView(col, new ScrollView.LayoutParams(ScrollView.LayoutParams.MATCH_PARENT, ScrollView.LayoutParams.MATCH_PARENT));
        root.addView(sv, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        root.addView(langSelector());

        setContentView(root);
        play.requestFocus();
    }

    /** Con el boton de actualizar, Jugar ocupa una columna; sin el, las dos. */
    private void setUpdateVisible(boolean on) {
        LinearLayout.LayoutParams lp = (LinearLayout.LayoutParams) play.getLayoutParams();
        lp.width = dp(on ? COL : 2 * COL + GAP);
        play.setLayoutParams(lp);
        update.setVisibility(on ? View.VISIBLE : View.GONE);
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
            discLine.setText(t("Disc: ", "Disco: ") + n);
            play.setText(t("Play", "Jugar"));
        } else {
            discLine.setText(u == null ? t("You have not chosen your disc image yet", "Todavía no has elegido la imagen de tu disco")
                                       : t("The chosen disc cannot be opened: choose it again",
                                           "No se puede abrir el disco elegido: vuelve a elegirlo"));
            play.setText(t("Choose disc and play", "Elegir disco y jugar"));
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

    /* ------------------------------------------------------------- actualizador */

    private void status(String msg) {
        runOnUiThread(() -> {
            statusMsg = msg;
            updateLine.setText(msg);
            updateLine.setVisibility(msg.isEmpty() ? View.GONE : View.VISIBLE);
        });
    }

    private String releaseVersion() {
        return release.tag.startsWith("v") ? release.tag.substring(1) : release.tag;
    }

    /** El aviso y el boton de la version nueva (tambien al cambiar de idioma). */
    private void showRelease() {
        if (release == null) return;
        update.setText(t("Update to ", "Actualizar a la ") + releaseVersion());
        update.setEnabled(!busy);
        setUpdateVisible(true);
        if (!busy) status(t("A new version is available: ", "Hay una versión nueva: ") + releaseVersion());
    }

    /** En segundo plano al abrir el menu; sin conexion no se avisa de nada. */
    private void checkUpdate() {
        if (!"com.vzepec.ac3game".equals(getPackageName())) return;   /* las variantes personales no */
        String cur = version();
        new Thread(() -> {
            try {
                Updater.Release r = Updater.check(cur);
                if (r == null) { Log.i(TAG, "sin version nueva (instalada " + cur + ")"); return; }
                Log.i(TAG, "version nueva: " + r.tag + " " + r.url);
                runOnUiThread(() -> { release = r; showRelease(); });
            } catch (Exception e) {
                Log.w(TAG, "no se pudo comprobar si hay version nueva: " + e);
            }
        }).start();
    }

    private void askUpdate() {
        if (release == null || busy) return;
        String notes = Updater.notes(release.body, en);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("AC3 Recomp " + releaseVersion())
                .setMessage((notes.isEmpty() ? "" : notes + "\n\n")
                        + t("It is downloaded from GitHub and Android will ask you to confirm the installation. "
                            + "Your saved game is kept.",
                            "Se descarga de GitHub y Android te pedirá confirmar la instalación. "
                            + "La partida guardada se conserva."))
                .setPositiveButton(t("Download and install", "Descargar e instalar"), (d, w) -> download())
                .setNegativeButton(t("Not now", "Ahora no"), null)
                .show();
    }

    private String error(Exception e) {
        String m = e.getMessage();
        if (Updater.ERR_SIZE.equals(m)) return t("incomplete download", "descarga incompleta");
        if (Updater.ERR_SHA.equals(m)) return t("the SHA-256 does not match the published one", "el SHA-256 no coincide con el publicado");
        return m != null ? m : e.toString();
    }

    private void download() {
        busy = true;
        update.setEnabled(false);
        Updater.Release r = release;
        new Thread(() -> {
            try {
                File f = Updater.download(this, r, pc -> status(t("Downloading… ", "Descargando… ") + pc + " %"));
                status(t("Download verified (SHA-256)", "Descarga comprobada (SHA-256)"));
                runOnUiThread(() -> install(f));
            } catch (Exception e) {
                Log.w(TAG, "descarga: " + e);
                status(t("Download failed: ", "No se pudo descargar: ") + error(e));
                runOnUiThread(() -> { busy = false; update.setEnabled(true); });
            }
        }).start();
    }

    private void install(File f) {
        if (!getPackageManager().canRequestPackageInstalls()) {
            /* la primera vez: Android exige permitir "instalar apps desconocidas" a esta app */
            pendingApk = f;
            new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle(t("Permission to update", "Permiso para actualizar"))
                    .setMessage(t("Android needs you to allow AC3 Recomp to install apps. "
                                  + "Turn the permission on in the screen that opens and go back to continue.",
                                  "Android necesita que permitas a AC3 Recomp instalar aplicaciones. "
                                  + "Activa el permiso en la pantalla que se abre y vuelve atrás para seguir."))
                    .setPositiveButton(t("Open settings", "Abrir ajustes"), (d, w) -> startActivity(new Intent(
                            Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()))))
                    .setNegativeButton(t("Not now", "Ahora no"), (d, w) -> { pendingApk = null; busy = false; update.setEnabled(true); })
                    .show();
            return;
        }
        try {
            status(t("Installing…", "Instalando…"));
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putBoolean(KEY_RELAUNCH, true).commit();
            Updater.install(this, f);
            installerShown = true;
        } catch (Exception e) {
            Log.w(TAG, "instalacion: " + e);
            status(t("Installation failed: ", "No se pudo instalar: ") + error(e));
            busy = false;
            update.setEnabled(true);
        }
    }

    /* ------------------------------------------------------------- opciones y acerca de */

    /** (etapa 8, paso 12) el modo de la pantalla para 120 imagenes por segundo: el de 120 Hz o, si no lo hay, el mas
     *  alto de 100 Hz o mas (con la resolucion actual); 0 si la pantalla no llega a 100 Hz. */
    static float highRate(Display d) {
        float best = 0f;
        try {
            Display.Mode cur = d.getMode();
            for (Display.Mode m : d.getSupportedModes()) {
                if (m.getPhysicalWidth() != cur.getPhysicalWidth() || m.getPhysicalHeight() != cur.getPhysicalHeight()) continue;
                float r = m.getRefreshRate();
                if (r < 99f) continue;
                if (Math.abs(r - 120f) < 1f) return r;
                if (r > best) best = r;
            }
        } catch (Exception e) {
            Log.w(TAG, "no se pudo mirar la frecuencia de la pantalla: " + e);
        }
        return best;
    }

    /** Una opcion de varias: titulo, explicacion y una fila de botones; el elegido va resaltado y se
     *  guarda en `key` (valores vals). Devuelve el boton elegido (para darle el foco). */
    private Button choice(LinearLayout box, String title, String hint, String key, int def, String[] names, int[] vals, int[] widths) {
        return choice(box, title, hint, key, def, names, vals, widths, null);
    }

    /** Igual, con botones bloqueados (locked[i]: se ven apagados y no se pueden elegir). */
    private Button choice(LinearLayout box, String title, String hint, String key, int def, String[] names, int[] vals, int[] widths,
                          boolean[] locked) {
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        TextView label = text(title, 16, TEXT, true);
        label.setGravity(Gravity.START);
        box.addView(label);
        TextView h = text(hint, 13, DIM, false);
        h.setGravity(Gravity.START);
        box.addView(h);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(6), 0, dp(12));
        int n = names.length, cur = p.getInt(key, def);
        for (int i = 0; i < n; i++) if (locked != null && locked[i] && vals[i] == cur) cur = def;   /* lo guardado, ya no se puede */
        Button[] bs = new Button[n];
        Button sel = null;
        for (int i = 0; i < n; i++) {
            bs[i] = button(names[i], 15, vals[i] == cur ? 0xFF1F5F99 : PANEL, null);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(widths[i]), dp(42));
            lp.rightMargin = dp(8);
            bs[i].setLayoutParams(lp);
            row.addView(bs[i]);
            if (vals[i] == cur) sel = bs[i];
            if (locked != null && locked[i]) { bs[i].setEnabled(false); bs[i].setTextColor(DIM); }
        }
        for (int i = 0; i < n; i++) {
            int val = vals[i];
            bs[i].setOnClickListener(v -> {
                p.edit().putInt(key, val).commit();
                for (int k = 0; k < n; k++) {   /* el resaltado pasa al elegido */
                    StateListDrawable st = new StateListDrawable();
                    st.addState(new int[] {android.R.attr.state_pressed}, box(ACCENT, ACCENT));
                    st.addState(new int[] {android.R.attr.state_focused}, box(0xFF22345A, ACCENT));
                    st.addState(new int[] {-android.R.attr.state_enabled}, box(0xFF111A2D, 0));
                    st.addState(new int[] {}, box(vals[k] == val ? 0xFF1F5F99 : PANEL, 0));
                    bs[k].setBackground(st);
                }
            });
        }
        box.addView(row);
        return sel != null ? sel : bs[0];
    }

    /** Opciones: se guardan al elegirlas y el juego las lee al arrancar. */
    private void options() {
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(20), dp(8), dp(20), dp(4));

        Button first = choice(box, t("Internal resolution", "Resolución interna"),
                              t("Higher looks sharper and needs a faster GPU", "Más alta se ve más nítida y pide más a la GPU"),
                              KEY_SCALE, DEFAULT_SCALE, new String[] {t("Original", "Original"), "2x", "3x", "4x"},
                              new int[] {1, 2, 3, 4}, new int[] {96, 64, 64, 64});
        choice(box, t("Texture filtering", "Filtro de texturas"),
               t("Bilinear smooths the textures of 3D models; menus and HUD stay sharp",
                 "Bilineal suaviza las texturas de los modelos 3D; menús y HUD siguen nítidos"),
               KEY_FILTER, 0, new String[] {t("Original", "Original"), t("Bilinear", "Bilineal")},
               new int[] {0, 1}, new int[] {96, 110});
        choice(box, t("Touch controls", "Mando táctil"),
               t("Auto hides it when you use a physical controller", "Automático lo oculta al usar un mando físico"),
               KEY_TOUCH, 0, new String[] {t("Auto", "Automático"), t("Always", "Siempre"), t("Never", "Nunca")},
               new int[] {0, 1, 2}, new int[] {120, 96, 96});
        boolean hi = highRate(getWindowManager().getDefaultDisplay()) > 0f;
        choice(box, t("Frame rate", "Imágenes por segundo"),
               hi ? t("120 needs a 120 Hz screen and more power; 60 uses less battery",
                      "120 pide pantalla de 120 Hz y más potencia; 60 gasta menos batería")
                  : t("Your screen does not support 120 Hz", "Tu pantalla no admite 120 Hz"),
               KEY_HZ, hi ? 120 : 60, new String[] {"60", "120"}, new int[] {60, 120}, new int[] {80, 80},
               new boolean[] {false, !hi});

        android.widget.CheckBox rumble = new android.widget.CheckBox(this);
        rumble.setText(t("Vibration", "Vibración"));
        rumble.setTextColor(TEXT);
        rumble.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        rumble.setChecked(p.getBoolean(KEY_RUMBLE, true));
        box.addView(rumble);
        android.widget.CheckBox fire = new android.widget.CheckBox(this);
        fire.setText(t("Extra vibration when firing and accelerating (not in the original)",
                       "Vibración extra al disparar y acelerar (no es del original)"));
        fire.setTextColor(TEXT);
        fire.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        fire.setChecked(p.getBoolean(KEY_RUMBLE_FIRE, false));
        fire.setEnabled(rumble.isChecked());
        fire.setOnCheckedChangeListener((v, on) -> p.edit().putBoolean(KEY_RUMBLE_FIRE, on).commit());
        rumble.setOnCheckedChangeListener((v, on) -> { p.edit().putBoolean(KEY_RUMBLE, on).commit(); fire.setEnabled(on); });
        box.addView(fire);

        android.widget.CheckBox stats = new android.widget.CheckBox(this);
        stats.setText(t("Show development counters and statistics", "Mostrar contadores y estadísticas de desarrollo"));
        stats.setTextColor(TEXT);
        stats.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        stats.setChecked(p.getBoolean(KEY_STATS, true));
        stats.setOnCheckedChangeListener((v, on) -> p.edit().putBoolean(KEY_STATS, on).commit());
        box.addView(stats);

        ScrollView sv = new ScrollView(this);   /* por si no cabe en pantallas bajas */
        sv.addView(box);
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle(t("Options", "Opciones"))
                .setView(sv)
                .setPositiveButton(t("Back", "Volver"), null)
                .show();
        first.requestFocus();
    }

    private void about() {
        new AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                .setTitle("AC3 Recomp " + version())
                .setMessage(t("Ace Combat 3: Electrosphere (PlayStation, Namco, 1999) on Android, at 60 fps and in widescreen.\n\n"
                              + "This app contains nothing from the game: it reads it from the image of your own disc "
                              + "(European SCES-02066 or American SLUS-00972).\n\n"
                              + "Unofficial project, not affiliated with Bandai Namco. Ace Combat is a trademark of "
                              + "Bandai Namco Entertainment.\n\n"
                              + "When this menu opens, the app checks GitHub for a new version; "
                              + "it is only installed if you confirm it.\n\n"
                              + "Code and releases: github.com/vzepec/ac3-decomp",
                              "Ace Combat 3: Electrosphere (PlayStation, Namco, 1999) en Android, a 60 fps y en pantalla ancha.\n\n"
                              + "Esta app no contiene nada del juego: lo lee de la imagen de tu propio disco "
                              + "(europeo SCES-02066 o americano SLUS-00972).\n\n"
                              + "Proyecto no oficial, sin relación con Bandai Namco. Ace Combat es una marca de "
                              + "Bandai Namco Entertainment.\n\n"
                              + "Al abrir este menú, la app consulta GitHub para saber si hay una versión nueva; "
                              + "solo se instala si tú lo confirmas.\n\n"
                              + "Código y versiones: github.com/vzepec/ac3-decomp"))
                .setPositiveButton(t("Back", "Volver"), null)
                .show();
    }
}
