package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class TerminalScrollbackTest {
    private fun lines(count: Int) = (1..count).joinToString("\r\n") { "line$it" }

    @Test
    fun linesScrolledOffTheTopGoToScrollback() {
        val t = buildTerminal(rows = 3)
        t.feed(lines(5))
        assertEquals(listOf("line3", "line4", "line5"), t.readScreen())
        assertEquals(2, t.snapshot().scrollbackSize)
        assertEquals("line1", t.readLine(0).text.trimEnd())
        assertEquals("line5", t.readLine(4).text.trimEnd())
        assertEquals(5, t.lineCount)
    }

    @Test
    fun scrollbackKeepsOnlyTheLimit() {
        val t = buildTerminal(rows = 2, scrollback = 3)
        t.feed(lines(10))
        assertEquals(3, t.snapshot().scrollbackSize)
        assertEquals("line6", t.readLine(0).text.trimEnd())
    }

    @Test
    fun aScrollRegionBelowTheTopKeepsNoScrollback() {
        val t = buildTerminal(rows = 3)
        t.feed("${CSI}2;3r${CSI}3;1H\n\n\n")
        assertEquals(0, t.snapshot().scrollbackSize)
    }

    @Test
    fun snapshotScrolledBackShowsOlderLines() {
        val t = buildTerminal(rows = 3)
        t.feed(lines(6))
        val snapshot = t.snapshot(scrollOffset = 2)
        assertEquals(listOf("line2", "line3", "line4"), snapshot.lines.map { it.text.trimEnd() })
        assertFalse(snapshot.isCursorVisible)
        assertEquals(listOf("line1", "line2", "line3"), t.snapshot(scrollOffset = 99).lines.map { it.text.trimEnd() })
    }

    @Test
    fun clearingScrollback() {
        val t = buildTerminal(rows = 2)
        t.feed(lines(5) + "${CSI}3J")
        assertEquals(0, t.snapshot().scrollbackSize)
    }

    @Test
    fun narrowingTruncatesAndKeepsTheCursorInside() {
        val t = buildTerminal(rows = 3, cols = 10)
        t.feed("abcdefghij")
        t.resize(3, 4)
        assertEquals("abcd", t.readRow(0))
        assertEquals(0 to 3, t.readCursor())
        t.resize(3, 8)
        assertEquals("abcd", t.readRow(0))
        t.feed("${CSI}1;8Hz")
        assertEquals("abcd   z", t.readRow(0))
    }

    @Test
    fun shrinkingKeepsTheCursorLineVisible() {
        val t = buildTerminal(rows = 5)
        t.feed(lines(4))
        t.resize(2, 10)
        assertEquals(listOf("line3", "line4"), t.readScreen())
        assertEquals(1 to 5, t.readCursor())
        assertEquals(2, t.snapshot().scrollbackSize)
    }

    @Test
    fun shrinkingDropsBlankLinesBelowTheCursorFirst() {
        val t = buildTerminal(rows = 5)
        t.feed("one\r\ntwo")
        t.resize(3, 10)
        assertEquals(listOf("one", "two", ""), t.readScreen())
        assertEquals(0, t.snapshot().scrollbackSize)
    }

    @Test
    fun growingBringsLinesBackFromScrollback() {
        val t = buildTerminal(rows = 2)
        t.feed(lines(4))
        t.resize(4, 10)
        assertEquals(listOf("line1", "line2", "line3", "line4"), t.readScreen())
        assertEquals(3 to 5, t.readCursor())
        assertEquals(0, t.snapshot().scrollbackSize)
    }

    @Test
    fun resizeResetsTheScrollRegion() {
        val t = buildTerminal(rows = 4)
        t.feed("${CSI}2;3r")
        t.resize(6, 10)
        t.feed("${CSI}1;1Htop${CSI}6;1H\n")
        assertEquals(1, t.snapshot().scrollbackSize)
    }

    @Test
    fun runsSplitOnStyleChanges() {
        val t = buildTerminal(cols = 6)
        t.feed("ab${CSI}1mcd")
        val runs = t.snapshot().lines[0].runs
        assertEquals(listOf("ab", "cd", "  "), runs.map { it.text })
        assertEquals(listOf(0, 2, 4), runs.map { it.column })
    }

    @Test
    fun aZeroLimitKeepsNoScrollback() {
        val t = buildTerminal(rows = 2, scrollback = 0)
        t.feed(lines(5))
        assertEquals(0, t.snapshot().scrollbackSize)
        assertEquals(listOf("line4", "line5"), t.readScreen())
    }
}
