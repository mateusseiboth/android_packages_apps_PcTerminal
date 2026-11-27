package org.lineageos.pcterminal.ui

import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import org.lineageos.pcterminal.TerminalClipboard
import org.lineageos.pcterminal.core.TerminalSession
import org.lineageos.pcterminal.core.terminal.TextSelection

/**
 * What one terminal view keeps besides the terminal: how far it is scrolled back, the selection and the
 * open menu, with the copy, paste and select all that keys, the mouse and the menu share.
 */
@Stable
class TerminalViewState(val session: TerminalSession, private val clipboard: TerminalClipboard) {
    var scrollOffset by mutableIntStateOf(0)
        private set
    var selection by mutableStateOf<TextSelection?>(null)

    /** Where the right button opened the menu, in pixels of the view; null when it is closed. */
    var menuPosition by mutableStateOf<Offset?>(null)

    val hasSelection: Boolean get() = readSelectedText().isNotEmpty()

    fun hasClipboardText(): Boolean = clipboard.hasText()

    fun scrollBy(lines: Int) {
        scrollOffset = (scrollOffset + lines).coerceIn(0, session.scrollbackSize)
    }

    fun scrollToBottom() {
        scrollOffset = 0
    }

    fun copy() {
        readSelectedText().takeIf { it.isNotEmpty() }?.let(clipboard::copy)
    }

    fun paste() {
        clipboard.readText()?.let(::sendPaste)
    }

    /** The middle button: the last selection, or the clipboard when nothing was selected yet. */
    fun pastePrimary() {
        (clipboard.primary ?: clipboard.readText())?.let(::sendPaste)
    }

    fun selectAll() {
        selection = session.selectAll()
        finishSelection()
    }

    /** At the end of a selection: keeps its text for the middle button, or drops it when it covers nothing. */
    fun finishSelection() {
        val text = readSelectedText()
        if (text.isEmpty()) {
            selection = null
            return
        }
        clipboard.primary = text
    }

    private fun readSelectedText(): String = selection?.let(session::readText).orEmpty()

    private fun sendPaste(text: String) {
        session.paste(text)
        scrollToBottom()
    }
}
