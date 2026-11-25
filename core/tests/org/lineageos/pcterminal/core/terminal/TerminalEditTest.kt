package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Test

class TerminalEditTest {
    private fun filled(rows: Int = 5, cols: Int = 10): Terminal {
        val t = buildTerminal(rows, cols)
        t.feed((0 until rows).joinToString("\r\n") { "${'a' + it}".repeat(cols - 1) })
        return t
    }

    @Test
    fun eraseInDisplay() {
        val t = filled()
        t.feed("${CSI}2;3H${CSI}J")
        assertEquals(listOf("aaaaaaaaa", "bb", "", "", ""), t.readScreen())
        val u = filled()
        u.feed("${CSI}2;3H${CSI}1J")
        assertEquals(listOf("", "   bbbbbb", "ccccccccc"), u.readScreen().take(3))
        val v = filled()
        v.feed("${CSI}2J")
        assertEquals(List(5) { "" }, v.readScreen())
    }

    @Test
    fun eraseInLine() {
        val t = filled()
        t.feed("${CSI}1;4H${CSI}K${CSI}2;4H${CSI}1K${CSI}3;4H${CSI}2K")
        assertEquals(listOf("aaa", "    bbbbb", ""), t.readScreen().take(3))
    }

    @Test
    fun erasedCellsTakeTheCurrentBackground() {
        val t = buildTerminal()
        t.feed("${CSI}44m${CSI}K")
        assertEquals(TermColor.indexed(4), t.snapshot().lines[0].runs[0].style.bg)
    }

    @Test
    fun eraseCharacters() {
        val t = filled()
        t.feed("${CSI}1;2H${CSI}3X")
        assertEquals("a   aaaaa", t.readRow(0))
    }

    @Test
    fun insertAndDeleteCharacters() {
        val t = buildTerminal()
        t.feed("abcdef${CSI}1;2H${CSI}2@")
        assertEquals("a  bcdef", t.readRow(0))
        t.feed("${CSI}3P")
        assertEquals("acdef", t.readRow(0))
    }

    @Test
    fun insertAndDeleteLines() {
        val t = filled()
        t.feed("${CSI}2;5H${CSI}L")
        assertEquals(listOf("aaaaaaaaa", "", "bbbbbbbbb", "ccccccccc", "ddddddddd"), t.readScreen())
        assertEquals(1 to 0, t.readCursor())
        t.feed("${CSI}2M")
        assertEquals(listOf("aaaaaaaaa", "ccccccccc", "ddddddddd", "", ""), t.readScreen())
    }

    @Test
    fun insertLinesStaysInsideTheScrollRegion() {
        val t = filled()
        t.feed("${CSI}2;4r${CSI}2;1H${CSI}L")
        assertEquals(listOf("aaaaaaaaa", "", "bbbbbbbbb", "ccccccccc", "eeeeeeeee"), t.readScreen())
    }

    @Test
    fun scrollUpAndDownInTheRegion() {
        val t = filled()
        t.feed("${CSI}2;4r${CSI}S")
        assertEquals(listOf("aaaaaaaaa", "ccccccccc", "ddddddddd", "", "eeeeeeeee"), t.readScreen())
        t.feed("${CSI}2T")
        assertEquals(listOf("aaaaaaaaa", "", "", "ccccccccc", "eeeeeeeee"), t.readScreen())
    }

    @Test
    fun lineFeedAtTheRegionBottomScrollsOnlyTheRegion() {
        val t = filled()
        t.feed("${CSI}2;4r${CSI}4;1H\nX")
        assertEquals(listOf("aaaaaaaaa", "ccccccccc", "ddddddddd", "X", "eeeeeeeee"), t.readScreen())
    }

    @Test
    fun reverseIndexAtTheRegionTopScrollsDown() {
        val t = filled()
        t.feed("${CSI}2;4r${CSI}2;1H${ESC}M")
        assertEquals(listOf("aaaaaaaaa", "", "bbbbbbbbb", "ccccccccc", "eeeeeeeee"), t.readScreen())
    }

    @Test
    fun indexAndNextLine() {
        val t = buildTerminal()
        t.feed("ab${ESC}Dc${ESC}Ed")
        assertEquals(listOf("ab", "  c", "d"), t.readScreen().take(3))
    }

    @Test
    fun anInvalidScrollRegionIsIgnored() {
        val t = filled()
        t.feed("${CSI}4;2r${CSI}5;1H\n")
        assertEquals("ccccccccc", t.readRow(1))
    }

    @Test
    fun alignmentPatternFillsTheScreen() {
        val t = buildTerminal(rows = 2, cols = 3)
        t.feed("${ESC}#8")
        assertEquals(listOf("EEE", "EEE"), t.readScreen())
    }
}
