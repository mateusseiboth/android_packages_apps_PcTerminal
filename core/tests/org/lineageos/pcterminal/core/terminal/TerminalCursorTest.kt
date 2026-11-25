package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Test

class TerminalCursorTest {
    @Test
    fun cursorPositionIsOneBasedAndClamped() {
        val t = buildTerminal()
        t.feed("${CSI}3;4H")
        assertEquals(2 to 3, t.readCursor())
        t.feed("${CSI}99;99f")
        assertEquals(4 to 9, t.readCursor())
        t.feed("${CSI}H")
        assertEquals(0 to 0, t.readCursor())
    }

    @Test
    fun relativeMovesDefaultToOneAndStopAtEdges() {
        val t = buildTerminal()
        t.feed("${CSI}3;3H${CSI}A")
        assertEquals(1 to 2, t.readCursor())
        t.feed("${CSI}2B")
        assertEquals(3 to 2, t.readCursor())
        t.feed("${CSI}0C")
        assertEquals(3 to 3, t.readCursor())
        t.feed("${CSI}9D")
        assertEquals(3 to 0, t.readCursor())
        t.feed("${CSI}9A")
        assertEquals(0 to 0, t.readCursor())
    }

    @Test
    fun nextAndPreviousLineGoToTheFirstColumn() {
        val t = buildTerminal()
        t.feed("${CSI}2;5H${CSI}2E")
        assertEquals(3 to 0, t.readCursor())
        t.feed("${CSI}5G${CSI}F")
        assertEquals(2 to 0, t.readCursor())
    }

    @Test
    fun absoluteColumnAndRow() {
        val t = buildTerminal()
        t.feed("${CSI}4G")
        assertEquals(0 to 3, t.readCursor())
        t.feed("${CSI}3d")
        assertEquals(2 to 3, t.readCursor())
        t.feed("${CSI}7`")
        assertEquals(2 to 6, t.readCursor())
    }

    @Test
    fun saveAndRestoreCursorWithItsStyle() {
        val t = buildTerminal()
        t.feed("${CSI}2;3H${CSI}31m${ESC}7${CSI}H${CSI}0m${ESC}8x")
        assertEquals(1 to 3, t.readCursor())
        assertEquals(TermColor.indexed(1), t.snapshot().lines[1].runs.first { it.text.startsWith("x") }.style.fg)
        t.feed("${CSI}5;5H${CSI}s${CSI}H${CSI}u")
        assertEquals(4 to 4, t.readCursor())
    }

    @Test
    fun restoreWithoutSaveGoesHome() {
        val t = buildTerminal()
        t.feed("${CSI}3;3H${ESC}8")
        assertEquals(0 to 0, t.readCursor())
    }

    @Test
    fun originModeIsRelativeToTheScrollRegion() {
        val t = buildTerminal()
        t.feed("${CSI}2;4r$CSI?6h")
        assertEquals(1 to 0, t.readCursor())
        t.feed("${CSI}9;2H")
        assertEquals(3 to 1, t.readCursor())
        t.feed("$CSI?6l")
        assertEquals(0 to 0, t.readCursor())
    }

    @Test
    fun cursorVisibilityMode() {
        val t = buildTerminal()
        t.feed("$CSI?25l")
        assertEquals(false, t.snapshot().isCursorVisible)
        t.feed("$CSI?25h")
        assertEquals(true, t.snapshot().isCursorVisible)
    }

    @Test
    fun reportsTheCursorPositionAndStatus() {
        val listener = RecordingListener()
        val t = buildTerminal(listener = listener)
        t.feed("${CSI}2;3H${CSI}6n${CSI}5n")
        assertEquals("${CSI}2;3R${CSI}0n", listener.output.toString())
    }

    @Test
    fun answersDeviceAttributes() {
        val listener = RecordingListener()
        val t = buildTerminal(listener = listener)
        t.feed("${CSI}c${CSI}0c")
        assertEquals("$CSI?62;22c$CSI?62;22c", listener.output.toString())
    }

    @Test
    fun answersSecondaryDeviceAttributes() {
        val listener = RecordingListener()
        val t = buildTerminal(listener = listener)
        t.feed("$CSI>c")
        assertEquals("$CSI>0;10;1c", listener.output.toString())
    }
}
