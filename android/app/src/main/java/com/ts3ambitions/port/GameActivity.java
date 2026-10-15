package com.ts3ambitions.port;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;

import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "openal", "ts3a" };
    }

    /** Result of a text entry started by {@link #showTextInput}. */
    static native void nativeTextInputDone(String text, boolean accepted);

    /** Result of {@link #pickImage}: ARGB pixels, or null if cancelled. */
    static native void nativeImagePicked(int width, int height, int[] argb);

    private static final int PICK_IMAGE = 4242;

    /** UIImagePickerController: let the player choose a picture from the gallery. */
    public static void pickImage() {
        final GameActivity act = (GameActivity) mSingleton;
        if (act == null) return;
        act.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_GET_CONTENT);
            i.setType("image/*");
            i.addCategory(Intent.CATEGORY_OPENABLE);
            try {
                act.startActivityForResult(Intent.createChooser(i, null), PICK_IMAGE);
            } catch (Exception e) {
                nativeImagePicked(0, 0, null);
            }
        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_IMAGE) return;
        final Uri uri = (result == RESULT_OK && data != null) ? data.getData() : null;
        if (uri == null) {
            nativeImagePicked(0, 0, null);
            return;
        }
        new Thread(() -> {
            Bitmap bmp = null;
            try {
                BitmapFactory.Options o = new BitmapFactory.Options();
                o.inJustDecodeBounds = true;
                try (java.io.InputStream in = getContentResolver().openInputStream(uri)) {
                    BitmapFactory.decodeStream(in, null, o);
                }
                int sample = 1;
                while (Math.max(o.outWidth, o.outHeight) / sample > 1024) sample *= 2;
                BitmapFactory.Options o2 = new BitmapFactory.Options();
                o2.inSampleSize = sample;
                o2.inPreferredConfig = Bitmap.Config.ARGB_8888;
                try (java.io.InputStream in = getContentResolver().openInputStream(uri)) {
                    bmp = BitmapFactory.decodeStream(in, null, o2);
                }
            } catch (Exception e) {
                bmp = null;
            }
            if (bmp == null) {
                nativeImagePicked(0, 0, null);
                return;
            }
            int w = bmp.getWidth(), h = bmp.getHeight();
            int[] px = new int[w * h];
            bmp.getPixels(px, 0, w, 0, 0, w, h);
            bmp.recycle();
            nativeImagePicked(w, h, px);
        }, "image-pick").start();
    }

    private AlertDialog mTextDialog;

    /** Android back button (the iOS game has none): offer to quit. */
    public static void confirmExit() {
        final GameActivity act = (GameActivity) mSingleton;
        if (act == null) return;
        act.runOnUiThread(() -> new AlertDialog.Builder(act)
                .setTitle(R.string.exit_title)
                .setMessage(R.string.exit_body)
                .setPositiveButton(android.R.string.ok, (d, w) -> act.finish())
                .setNegativeButton(android.R.string.cancel, null)
                .show());
    }

    /**
     * Called from native code when the game focuses a UITextField. Shows a
     * native dialog so the Android IME works normally (prediction, accents).
     */
    public static void showTextInput(final String text, final String hint, final boolean secure) {
        final GameActivity act = (GameActivity) mSingleton;
        if (act == null) return;
        act.runOnUiThread(() -> act.openTextDialog(text, hint, secure));
    }

    public static void hideTextInput() {
        final GameActivity act = (GameActivity) mSingleton;
        if (act == null) return;
        act.runOnUiThread(() -> {
            if (act.mTextDialog != null) {
                act.mTextDialog.setOnDismissListener(null);
                act.mTextDialog.dismiss();
                act.mTextDialog = null;
            }
        });
    }

    private void openTextDialog(String text, String hint, boolean secure) {
        if (mTextDialog != null) mTextDialog.dismiss();
        final EditText edit = new EditText(this);
        edit.setSingleLine(true);
        edit.setText(text);
        edit.setSelection(edit.getText().length());
        if (hint != null) edit.setHint(hint);
        edit.setInputType(InputType.TYPE_CLASS_TEXT
                | (secure ? InputType.TYPE_TEXT_VARIATION_PASSWORD : InputType.TYPE_TEXT_FLAG_CAP_WORDS));
        edit.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        final boolean[] finished = { false };
        final Runnable accept = () -> {
            if (finished[0]) return;
            finished[0] = true;
            nativeTextInputDone(edit.getText().toString(), true);
            if (mTextDialog != null) mTextDialog.dismiss();
            mTextDialog = null;
        };
        edit.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE
                    || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                accept.run();
                return true;
            }
            return false;
        });
        AlertDialog.Builder b = new AlertDialog.Builder(this);
        if (hint != null && !hint.isEmpty()) b.setTitle(hint);
        b.setView(edit);
        b.setPositiveButton(android.R.string.ok, (d, w) -> accept.run());
        b.setNegativeButton(android.R.string.cancel, (d, w) -> {
            if (!finished[0]) {
                finished[0] = true;
                nativeTextInputDone(null, false);
            }
        });
        mTextDialog = b.create();
        mTextDialog.setOnDismissListener(d -> {
            if (!finished[0]) {
                finished[0] = true;
                nativeTextInputDone(null, false);
            }
            mTextDialog = null;
        });
        mTextDialog.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
        mTextDialog.show();
        edit.requestFocus();
        InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) imm.showSoftInput(edit, InputMethodManager.SHOW_IMPLICIT);
    }
}
