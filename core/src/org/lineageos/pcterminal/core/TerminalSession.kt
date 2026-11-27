package org.lineageos.pcterminal.core

import java.io.InputStream
import java.io.OutputStream
import org.lineageos.pcterminal.core.terminal.KeyEncoder
import org.lineageos.pcterminal.core.terminal.KeyModifiers
import org.lineageos.pcterminal.core.terminal.MouseEncoder
import org.lineageos.pcterminal.core.terminal.MouseReport
import org.lineageos.pcterminal.core.terminal.MouseTracking
import org.lineageos.pcterminal.core.terminal.SelectedRange
import org.lineageos.pcterminal.core.terminal.Terminal
import org.lineageos.pcterminal.core.terminal.TerminalKey
import org.lineageos.pcterminal.core.terminal.TerminalListener
import org.lineageos.pcterminal.core.terminal.TerminalSnapshot
import org.lineageos.pcterminal.core.terminal.TextSelection
import org.lineageos.pcterminal.core.terminal.TextSelector

/**
 * A terminal bound to a shell's pty: [runReader] feeds it the shell's output, keys and pastes go to
 * [output], and a new window size goes to [onResize] (the daemon's resize). Safe to use from the reader
 * thread and the UI thread.
 */
class TerminalSession(
    rows: Int,
    cols: Int,
    private val output: OutputStream,
    private val onResize: (Int, Int) -> Unit,
) {
    private val lock = Any()
    private val terminal = Terminal(rows, cols, listener = object : TerminalListener {
        override fun onOutput(bytes: ByteArray) = write(bytes)
    })

    val title: String get() = synchronized(lock) { terminal.title }
    val rows: Int get() = synchronized(lock) { terminal.rows }
    val cols: Int get() = synchronized(lock) { terminal.cols }
    val scrollbackSize: Int get() = synchronized(lock) { terminal.scrollbackSize }
    val isAlternateScreen: Boolean get() = synchronized(lock) { terminal.isAlternateScreen }

    /** The mouse reporting the program asked for: when not NONE, clicks go to the program. */
    val mouseTracking: MouseTracking get() = synchronized(lock) { terminal.mouseTracking }

    /** Reads the shell until it ends (end of stream or an error); onUpdate after each chunk. */
    fun runReader(input: InputStream, onUpdate: () -> Unit) {
        val buffer = ByteArray(BUFFER)
        while (true) {
            val n = runCatching { input.read(buffer) }.getOrDefault(-1)
            if (n < 0) return
            synchronized(lock) { terminal.write(buffer, 0, n) }
            onUpdate()
        }
    }

    fun snapshot(scrollOffset: Int = 0): TerminalSnapshot = synchronized(lock) { terminal.snapshot(scrollOffset) }

    fun sendKey(key: TerminalKey, modifiers: KeyModifiers) =
        write(KeyEncoder.encode(key, modifiers, synchronized(lock) { terminal.isApplicationCursorKeys }))

    fun sendText(text: String, modifiers: KeyModifiers = KeyModifiers.NONE) =
        text.codePoints().forEach { write(KeyEncoder.encodeChar(it, modifiers)) }

    fun paste(text: String) = write(KeyEncoder.encodePaste(text, synchronized(lock) { terminal.isBracketedPaste }))

    /** Sends [report] to the program when its mouse tracking takes it; true when sent. */
    fun sendMouse(report: MouseReport): Boolean {
        val bytes = synchronized(lock) { MouseEncoder.encode(report, terminal.mouseTracking, terminal.isSgrMouse) } ?: return false
        write(bytes)
        return true
    }

    /** The number of the line on screen [row] when the view is [scrollOffset] lines up (see [TextSelection]). */
    fun findLineNumber(row: Int, scrollOffset: Int): Long = synchronized(lock) { terminal.findLineNumber(row, scrollOffset) }

    /** The cells [selection] covers now, or null. */
    fun resolveSelection(selection: TextSelection): SelectedRange? = synchronized(lock) { TextSelector.resolve(terminal, selection) }

    /** The text [selection] covers, empty when it covers nothing. */
    fun readText(selection: TextSelection): String = synchronized(lock) {
        TextSelector.resolve(terminal, selection)?.let { TextSelector.readText(terminal, it) }.orEmpty()
    }

    fun selectAll(): TextSelection? = synchronized(lock) { TextSelector.selectAll(terminal) }

    fun resize(rows: Int, cols: Int) {
        val changed = synchronized(lock) {
            val isNew = rows != terminal.rows || cols != terminal.cols
            if (isNew) terminal.resize(rows, cols)
            isNew
        }
        if (changed) onResize(rows, cols)
    }

    private fun write(bytes: ByteArray) {
        runCatching {
            output.write(bytes)
            output.flush()
        }
    }

    private companion object {
        const val BUFFER = 16 * 1024
    }
}
