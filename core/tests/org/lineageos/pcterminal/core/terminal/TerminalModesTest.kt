package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TerminalModesTest {
    @Test
    fun alternateScreenSavesAndRestoresTheMainScreen() {
        val t = buildTerminal()
        t.feed("shell\$ vim\r\n")
        t.feed("$CSI?1049h${CSI}H${CSI}2J~\r\n~${CSI}5;1H\"a\" 1L")
        assertTrue(t.snapshot().isAlternateScreen)
        assertEquals(listOf("~", "~", "", "", "\"a\" 1L"), t.readScreen())
        t.feed("$CSI?1049l")
        assertFalse(t.snapshot().isAlternateScreen)
        assertEquals(listOf("shell\$ vim", "", "", "", ""), t.readScreen())
        assertEquals(1 to 0, t.readCursor())
    }

    @Test
    fun theAlternateScreenHasNoScrollback() {
        val t = buildTerminal(rows = 2)
        t.feed("$CSI?1049h1\r\n2\r\n3\r\n4")
        assertEquals(0, t.snapshot().scrollbackSize)
    }

    @Test
    fun mode47KeepsTheAlternateContent() {
        val t = buildTerminal()
        t.feed("$CSI?47hX$CSI?47l$CSI?47h")
        assertEquals("X", t.readRow(0))
    }

    @Test
    fun keyboardAndPasteModes() {
        val t = buildTerminal()
        assertFalse(t.isApplicationCursorKeys)
        assertFalse(t.isBracketedPaste)
        t.feed("$CSI?1h$CSI?2004h")
        assertTrue(t.isApplicationCursorKeys)
        assertTrue(t.isBracketedPaste)
        t.feed("$CSI?1;2004l")
        assertFalse(t.isApplicationCursorKeys)
        assertFalse(t.isBracketedPaste)
    }

    @Test
    fun mouseModesAreRecorded() {
        val t = buildTerminal()
        assertEquals(MouseTracking.NONE, t.mouseTracking)
        t.feed("$CSI?1000h")
        assertEquals(MouseTracking.CLICKS, t.mouseTracking)
        t.feed("$CSI?1002h$CSI?1006h")
        assertEquals(MouseTracking.DRAG, t.mouseTracking)
        assertTrue(t.isSgrMouse)
        t.feed("$CSI?1002l")
        assertEquals(MouseTracking.NONE, t.mouseTracking)
    }

    @Test
    fun unknownModesAreIgnored() {
        val t = buildTerminal()
        t.feed("$CSI?12h$CSI?9999hx")
        assertEquals("x", t.readRow(0))
    }

    @Test
    fun windowTitleFromOsc() {
        val listener = RecordingListener()
        val t = buildTerminal(listener = listener)
        t.feed("$ESC]0;user@host: ~\u0007$ESC]2;vim$ESC\\$ESC]7;file://x\u0007")
        assertEquals(listOf("user@host: ~", "vim"), listener.titles)
        assertEquals("vim", t.snapshot().title)
    }

    @Test
    fun fullResetClearsScreenAndModes() {
        val t = buildTerminal()
        t.feed("${CSI}31mabc$CSI?1h$CSI?25l${CSI}2;3r${ESC}c")
        assertEquals(List(5) { "" }, t.readScreen())
        assertEquals(0 to 0, t.readCursor())
        assertFalse(t.isApplicationCursorKeys)
        assertTrue(t.snapshot().isCursorVisible)
        t.feed("x")
        assertEquals(CellStyle.DEFAULT, t.snapshot().lines[0].runs[0].style)
    }

    @Test
    fun softResetKeepsTheScreen() {
        val t = buildTerminal()
        t.feed("abc$CSI?25l$CSI!p")
        assertEquals("abc", t.readRow(0))
        assertTrue(t.snapshot().isCursorVisible)
    }
}
