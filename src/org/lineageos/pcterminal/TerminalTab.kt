package org.lineageos.pcterminal

import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import android.system.StructPollfd
import java.io.FileDescriptor
import java.io.FileOutputStream
import java.io.InputStream
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import org.lineageos.pcterminal.core.TerminalSession

/** One open shell: its pty, the terminal fed by a reader thread, and a counter that bumps on every change. */
class TerminalTab(
    val container: String,
    val isRoot: Boolean,
    private val pty: FileDescriptor,
    rows: Int,
    cols: Int,
    onResize: (FileDescriptor, Int, Int) -> Unit,
) {
    val id: Long = NEXT_ID.incrementAndGet()
    val session = TerminalSession(rows, cols, FileOutputStream(pty)) { r, c -> onResize(pty, r, c) }
    private val changes = MutableStateFlow(0L)
    val version: StateFlow<Long> = changes

    @Volatile
    private var isClosing = false

    /** Reads the shell on a thread of its own until it ends or the tab closes, then calls onEnded. */
    fun start(onEnded: (TerminalTab) -> Unit) {
        Thread({
            session.runReader(PtyInput()) { changes.update { it + 1 } }
            runCatching { Os.close(pty) } // the last reference: the shell gets SIGHUP
            onEnded(this)
        }, "pty-$container").start()
    }

    fun close() {
        isClosing = true
    }

    /** Reads with a timeout so a closing tab is noticed; EIO (the shell ended) is the end of the stream. */
    private inner class PtyInput : InputStream() {
        override fun read(): Int {
            val one = ByteArray(1)
            return if (read(one, 0, 1) < 0) -1 else one[0].toInt() and 0xFF
        }

        override fun read(b: ByteArray, off: Int, len: Int): Int {
            val poll = StructPollfd().apply {
                fd = pty
                events = OsConstants.POLLIN.toShort()
            }
            while (!isClosing) {
                val ready = try {
                    Os.poll(arrayOf(poll), POLL_MS)
                } catch (e: ErrnoException) {
                    return -1
                }
                if (ready > 0) return readReady(b, off, len)
            }
            return -1
        }

        private fun readReady(b: ByteArray, off: Int, len: Int): Int = try {
            Os.read(pty, b, off, len).takeIf { it > 0 } ?: -1
        } catch (e: ErrnoException) {
            -1
        }
    }

    private companion object {
        const val POLL_MS = 250
        val NEXT_ID = AtomicLong()
    }
}
