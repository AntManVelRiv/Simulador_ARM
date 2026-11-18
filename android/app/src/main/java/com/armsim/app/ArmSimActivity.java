package com.armsim.app;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.provider.OpenableColumns;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Actividad de ARM SIM: SDLActivity carga libSDL2.so y libmain.so (el simulador) y le pasa los eventos táctiles.
 *
 * Añade dos cosas que el simulador (C++) pide por JNI (ver src/platform/AndroidBridge.cpp):
 *  - openFilePicker(): abre el administrador de archivos del sistema para elegir un programa.
 *  - getKeyboardInset(): altura del teclado en pantalla, para que no tape el editor.
 */
public class ArmSimActivity extends SDLActivity {

    private static final int REQ_OPEN_FILE = 0x4152;              // "AR"
    private static final long MAX_IMPORT_BYTES = 8L * 1024 * 1024; // un programa en ensamblador pesa unos KB

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    // ------------------------------------------------------------------------------------------------
    // Abrir... → administrador de archivos del sistema (Storage Access Framework)
    // ------------------------------------------------------------------------------------------------

    /** Lo llama el código nativo (hilo de SDL): muestra el selector de archivos del sistema. */
    public void openFilePicker() {
        runOnUiThread(() -> {
            try {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");                                  // los .s no tienen un tipo MIME fiable: se muestran todos
                startActivityForResult(i, REQ_OPEN_FILE);
            } catch (Exception e) {
                toast("No se pudo abrir el administrador de archivos.");
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_OPEN_FILE || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        new Thread(() -> importAndOpen(uri), "armsim-import").start();   // no bloquear el hilo de la interfaz con la copia
    }

    /** Copia el archivo elegido a la carpeta de la app y le pide al simulador que lo abra (evento SDL_DROPFILE). */
    private void importAndOpen(Uri uri) {
        File tmp = null;
        try {
            File base = getExternalFilesDir(null);
            if (base == null) base = getFilesDir();                // misma carpeta que usa main.cpp como HOME
            File dir = new File(base, "importados");
            if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("no se pudo crear " + dir);

            File dest = new File(dir, safeName(displayName(uri)));
            tmp = new File(dir, dest.getName() + ".tmp");
            long total = 0;
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(tmp)) {
                if (in == null) throw new IOException("no se pudo leer el archivo");
                byte[] buf = new byte[16 * 1024];
                int n;
                while ((n = in.read(buf)) > 0) {
                    total += n;
                    if (total > MAX_IMPORT_BYTES) {
                        toast("El archivo es demasiado grande para ser un programa.");
                        return;                                    // el finally borra el temporal
                    }
                    out.write(buf, 0, n);
                }
            }
            if (dest.exists() && !dest.delete()) throw new IOException("no se pudo reemplazar " + dest.getName());
            if (!tmp.renameTo(dest)) throw new IOException("no se pudo guardar " + dest.getName());
            tmp = null;
            SDLActivity.onNativeDropFile(dest.getAbsolutePath());   // main.cpp: SDL_DROPFILE → controller.openFile()
        } catch (Exception e) {
            toast("No se pudo abrir el archivo: " + e.getMessage());
        } finally {
            if (tmp != null) //noinspection ResultOfMethodCallIgnored
                tmp.delete();
        }
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                String n = c.getString(0);
                if (n != null && !n.trim().isEmpty()) return n;
            }
        } catch (Exception ignored) { }
        String last = uri.getLastPathSegment();
        return last != null ? last : "programa.s";
    }

    /** Nombre de archivo seguro: sin rutas, sin caracteres raros y sin empezar por punto. */
    private static String safeName(String raw) {
        String s = raw.replaceAll("[^\\p{L}\\p{N} ._()+-]", "_").trim();
        while (s.startsWith(".")) s = s.substring(1);
        if (s.length() > 100) s = s.substring(s.length() - 100);
        return s.isEmpty() ? "programa.s" : s;
    }

    private void toast(final String msg) {
        runOnUiThread(() -> Toast.makeText(this, msg, Toast.LENGTH_LONG).show());
    }

    // ------------------------------------------------------------------------------------------------
    // Teclado en pantalla: altura que ocupa (Android 11+). El simulador reserva ese espacio abajo.
    // ------------------------------------------------------------------------------------------------

    /** Lo llama el código nativo en cada fotograma mientras hay entrada de texto activa. 0 = teclado oculto o no medible. */
    public int getKeyboardInset() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                View decor = getWindow().getDecorView();
                WindowInsets wi = decor.getRootWindowInsets();
                if (wi != null && wi.isVisible(WindowInsets.Type.ime()))
                    return Math.max(0, wi.getInsets(WindowInsets.Type.ime()).bottom);
            }
        } catch (Throwable ignored) { }
        return 0;
    }
}
