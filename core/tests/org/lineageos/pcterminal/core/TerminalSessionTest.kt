package org.lineageos.pcterminal.core

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.lineageos.pcterminal.core.terminal.KeyModifiers
import org.lineageos.pcterminal.core.terminal.TerminalKey

class TerminalSessionTest {
    private val output = ByteArrayOutputStream()
    private val resizes = mutableListOf<Pair<Int, Int>>()
    private val session = TerminalSession(rows = 4, cols = 20, output = output, onResize = { r, c -> resizes += r to c })

    @Test
    fun readsTheShellUntilItEnds() {
        var updates = 0
        session.runReader(ByteArrayInputStream("hello\r\nworld\u001b]0;vim\u0007".toByteArray())) { updates++ }
        val snapshot = session.snapshot()
        assertEquals("hello", snapshot.lines[0].text.trimEnd())
        assertEquals("world", snapshot.lines[1].text.trimEnd())
        assertEquals("vim", session.title)
        assertTrue(updates > 0)
    }

    @Test
    fun answersTheShellsQuestions() {
        session.runReader(ByteArrayInputStream("\u001b[6n".toByteArray())) {}
        assertEquals("\u001b[1;1R", output.toString())
    }

    @Test
    fun sendsKeysTextAndPastes() {
        session.sendKey(TerminalKey.UP, KeyModifiers())
        session.sendText("é")
        session.sendText("c", KeyModifiers(ctrl = true))
        session.paste("a\nb")
        assertEquals("\u001b[Aé\u0003a\rb", output.toString(Charsets.UTF_8))
    }

    @Test
    fun resizesOnlyWhenTheSizeChanges() {
        session.resize(4, 20)
        session.resize(30, 100)
        session.resize(30, 100)
        assertEquals(listOf(30 to 100), resizes)
        assertEquals(100, session.snapshot().cols)
    }
}
