package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class TerminalStyleTest {
    private fun styleOf(sequence: String): CellStyle {
        val t = buildTerminal(cols = 20)
        t.feed("${sequence}x")
        return t.snapshot().lines[0].runs[0].style
    }

    @Test
    fun attributesTurnOnAndOff() {
        val on = styleOf("${CSI}1;2;3;4;5;7;8;9m")
        listOf(on.isBold, on.isDim, on.isItalic, on.isUnderline, on.isBlink, on.isInverse, on.isInvisible, on.isStrikethrough)
            .forEach(::assertTrue)
        val off = styleOf("${CSI}1;2;3;4;5;7;8;9m${CSI}22;23;24;25;27;28;29m")
        assertEquals(CellStyle.DEFAULT, off)
    }

    @Test
    fun resetClearsEverything() {
        assertEquals(CellStyle.DEFAULT, styleOf("${CSI}1;31;42m${CSI}m"))
        assertEquals(CellStyle.DEFAULT, styleOf("${CSI}1;31;42m${CSI}0m"))
    }

    @Test
    fun basicAndBrightColors() {
        val style = styleOf("${CSI}31;102m")
        assertEquals(TermColor.indexed(1), style.fg)
        assertEquals(TermColor.indexed(10), style.bg)
        assertEquals(TermColor.indexed(15), styleOf("${CSI}97m").fg)
        assertEquals(TermColor.indexed(0), styleOf("${CSI}40m").bg)
    }

    @Test
    fun defaultColors() {
        val style = styleOf("${CSI}31;42m${CSI}39;49m")
        assertEquals(TermColor.DEFAULT, style.fg)
        assertEquals(TermColor.DEFAULT, style.bg)
    }

    @Test
    fun palette256WithSemicolonsAndColons() {
        assertEquals(TermColor.indexed(208), styleOf("${CSI}38;5;208m").fg)
        assertEquals(TermColor.indexed(17), styleOf("${CSI}48:5:17m").bg)
    }

    @Test
    fun trueColorWithSemicolonsAndColons() {
        assertEquals(TermColor.rgb(10, 20, 30), styleOf("${CSI}38;2;10;20;30m").fg)
        assertEquals(TermColor.rgb(1, 2, 3), styleOf("${CSI}48:2::1:2:3m").bg)
        assertEquals(TermColor.rgb(4, 5, 6), styleOf("${CSI}38:2:4:5:6m").fg)
    }

    @Test
    fun parametersAfterAnExtendedColorStillApply() {
        val style = styleOf("${CSI}38;5;100;1;48;2;1;2;3;4m")
        assertEquals(TermColor.indexed(100), style.fg)
        assertEquals(TermColor.rgb(1, 2, 3), style.bg)
        assertTrue(style.isBold)
        assertTrue(style.isUnderline)
    }

    @Test
    fun underlineSubParameterZeroTurnsItOff() {
        assertFalse(styleOf("${CSI}4m${CSI}4:0m").isUnderline)
        assertTrue(styleOf("${CSI}4:3m").isUnderline)
    }

    @Test
    fun colorsResolveToArgb() {
        assertEquals(0xFF000000.toInt(), TermColor.toArgb(TermColor.indexed(0), 0))
        assertEquals(0xFFFFFFFF.toInt(), TermColor.toArgb(TermColor.indexed(231), 0))
        assertEquals(0xFF080808.toInt(), TermColor.toArgb(TermColor.indexed(232), 0))
        assertEquals(0xFF5F87AF.toInt(), TermColor.toArgb(TermColor.indexed(67), 0))
        assertEquals(0xFF0A141E.toInt(), TermColor.toArgb(TermColor.rgb(10, 20, 30), 0))
        assertEquals(0x12345678, TermColor.toArgb(TermColor.DEFAULT, 0x12345678))
    }
}
