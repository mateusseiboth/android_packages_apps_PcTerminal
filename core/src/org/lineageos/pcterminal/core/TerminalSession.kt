package org.lineageos.pcterminal.core

import java.io.InputStream
import java.io.OutputStream
import org.lineageos.pcterminal.core.terminal.KeyEncoder
import org.lineageos.pcterminal.core.terminal.KeyModifiers
import org.lineageos.pcterminal.core.terminal.Terminal
import org.lineageos.pcterminal.core.terminal.TerminalKey
import org.lineageos.pcterminal.core.terminal.TerminalListener
import org.lineageos.pcterminal.core.terminal.TerminalSnapshot

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
