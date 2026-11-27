package org.lineageos.pcterminal

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context

/**
 * The system clipboard (the person's own: the app runs as their user's system uid), plus [primary]: the
 * last text selected with the mouse, which the middle button pastes as X11's primary selection does.
 * Android has one clipboard, so selecting never replaces what was copied.
 */
class TerminalClipboard(private val context: Context) {
    private val manager: ClipboardManager? = context.getSystemService(ClipboardManager::class.java)

    var primary: String? = null

    fun copy(text: String) {
        manager?.setPrimaryClip(ClipData.newPlainText(context.getString(R.string.app_name), text))
    }

    /** True when the clipboard holds something that reads as text. */
    fun hasText(): Boolean = manager?.primaryClipDescription?.hasMimeType(TEXT_TYPES) == true

    fun readText(): String? =
        manager?.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(context)?.toString()?.takeIf { it.isNotEmpty() }

    private companion object {
        const val TEXT_TYPES = "text/*"
    }
}
