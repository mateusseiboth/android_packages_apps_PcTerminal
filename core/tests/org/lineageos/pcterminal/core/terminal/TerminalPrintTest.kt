package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TerminalPrintTest {
    @Test
    fun printsAndMovesTheCursor() {
        val t = buildTerminal()
        t.feed("hello")
        assertEquals("hello", t.readRow(0))
        assertEquals(0 to 5, t.readCursor())
    }

    @Test
    fun carriageReturnAndLineFeed() {
        val t = buildTerminal()
        t.feed("ab\r\ncd\nef")
        assertEquals(listOf("ab", "cd", "  ef", "", ""), t.readScreen())
    }

    @Test
    fun wrapsOnlyWhenTheNextCharacterArrives() {
        val t = buildTerminal(cols = 4)
        t.feed("abcd")
        assertEquals(0 to 3, t.readCursor())
        t.feed("e")
        assertEquals(listOf("abcd", "e"), t.readScreen().take(2))
        assertTrue(t.snapshot().lines[0].isWrapped)
    }

    @Test
    fun aCarriageReturnCancelsThePendingWrap() {
        val t = buildTerminal(cols = 4)
        t.feed("abcd\rX")
        assertEquals("Xbcd", t.readRow(0))
    }

    @Test
    fun withoutAutowrapTheLastColumnIsOverwritten() {
        val t = buildTerminal(cols = 4)
        t.feed("$CSI?7labcdef")
        assertEquals(listOf("abcf", ""), t.readScreen().take(2))
    }

    @Test
    fun backspaceStopsAtTheLeftEdge() {
        val t = buildTerminal()
        t.feed("ab\b\b\bX")
        assertEquals("Xb", t.readRow(0))
    }

    @Test
    fun tabsStopEveryEightColumns() {
        val t = buildTerminal(cols = 20)
        t.feed("a\tb\tc\td")
        assertEquals("a       b       c  d", t.readRow(0))
    }

    @Test
    fun tabStopsCanBeSetAndCleared() {
        val t = buildTerminal(cols = 20)
        t.feed("$CSI${"3g"}  ${ESC}H\rx\ty")
        assertEquals("x y", t.readRow(0))
        t.feed("\r\n  ${CSI}g\r\ty")
        assertEquals(1 to 19, t.readCursor())
    }

    @Test
    fun cursorForwardAndBackwardTabulation() {
        val t = buildTerminal(cols = 30)
        t.feed("${CSI}2Ix$CSI${"2Z"}y")
        assertEquals("        y       x", t.readRow(0))
    }

    @Test
    fun wideCharactersTakeTwoCells() {
        val t = buildTerminal(cols = 6)
        t.feed("a中b")
        assertEquals(0 to 4, t.readCursor())
        assertEquals("a中b", t.readRow(0))
        val runs = t.snapshot().lines[0].runs
        assertEquals(listOf(0, 1, 3), runs.map { it.column })
        assertEquals(2, runs[1].cells)
    }

    @Test
    fun aWideCharacterAtTheLastColumnWrapsFirst() {
        val t = buildTerminal(cols = 4)
        t.feed("abc中")
        assertEquals(listOf("abc", "中"), t.readScreen().take(2))
    }

    @Test
    fun emojiAreWide() {
        val t = buildTerminal()
        t.feed("😀x")
        assertEquals(0 to 3, t.readCursor())
    }

    @Test
    fun overwritingHalfOfAWideCharacterClearsTheOtherHalf() {
        val t = buildTerminal()
        t.feed("中\bx")
        assertEquals(" x", t.readRow(0))
    }

    @Test
    fun combiningMarksAreDropped() {
        val t = buildTerminal()
        t.feed("éx")
        assertEquals("ex", t.readRow(0))
    }

    @Test
    fun insertModeShiftsTheLine() {
        val t = buildTerminal()
        t.feed("abc\r${CSI}4hXY${CSI}4lZ")
        assertEquals("XYZbc", t.readRow(0))
    }

    @Test
    fun bellReachesTheListener() {
        val listener = RecordingListener()
        val t = buildTerminal(listener = listener)
        t.feed("\u0007")
        assertEquals(1, listener.bells)
    }

    @Test
    fun lineDrawingCharset() {
        val t = buildTerminal()
        t.feed("$ESC(0lqk$ESC(Bq")
        assertEquals("┌─┐q", t.readRow(0))
    }

    @Test
    fun shiftOutUsesTheG1Charset() {
        val t = buildTerminal()
        t.feed("$ESC)0\u000eq\u000fq")
        assertEquals("─q", t.readRow(0))
    }

    @Test
    fun aColoredLsLine() {
        val t = buildTerminal(cols = 40)
        t.feed("${CSI}0m${CSI}01;34mDocuments${CSI}0m  ${CSI}01;32mrun.sh${CSI}0m  notes.txt\r\n")
        assertEquals("Documents  run.sh  notes.txt", t.readRow(0))
        val runs = t.snapshot().lines[0].runs
        assertEquals("Documents", runs[0].text)
        assertEquals(TermColor.indexed(4), runs[0].style.fg)
        assertTrue(runs[0].style.isBold)
        val run = runs.first { it.text == "run.sh" }
        assertEquals(TermColor.indexed(2), run.style.fg)
        assertEquals(1 to 0, t.readCursor())
    }

    @Test
    fun repeatsTheLastCharacter() {
        val t = buildTerminal()
        t.feed("a${CSI}3b")
        assertEquals("aaaa", t.readRow(0))
    }

    @Test
    fun aCharacterSplitAcrossWritesIsKept() {
        val t = buildTerminal()
        val bytes = "é".toByteArray(Charsets.UTF_8)
        t.write(bytes, 0, 1)
        t.write(bytes, 1, 1)
        assertEquals("é", t.readRow(0))
    }

    @Test
    fun withoutAutowrapAWideCharacterStaysOnTheLine() {
        val t = buildTerminal(cols = 4)
        t.feed("$CSI?7labc中")
        assertEquals(listOf("ab中", ""), t.readScreen().take(2))
    }
}
