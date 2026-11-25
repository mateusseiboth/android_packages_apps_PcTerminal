package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Test

class KeyEncoderTest {
    private fun key(key: TerminalKey, mods: KeyModifiers = KeyModifiers.NONE, app: Boolean = false) =
        String(KeyEncoder.encode(key, mods, app), Charsets.UTF_8)

    private fun char(c: Char, mods: KeyModifiers) = String(KeyEncoder.encodeChar(c.code, mods), Charsets.UTF_8)

    @Test
    fun arrowsInNormalAndApplicationMode() {
        assertEquals("${CSI}A", key(TerminalKey.UP))
        assertEquals("${ESC}OB", key(TerminalKey.DOWN, app = true))
        assertEquals("${CSI}C", key(TerminalKey.RIGHT))
        assertEquals("${ESC}OD", key(TerminalKey.LEFT, app = true))
    }

    @Test
    fun modifiersUseTheXtermParameter() {
        assertEquals("${CSI}1;5A", key(TerminalKey.UP, KeyModifiers(ctrl = true), app = true))
        assertEquals("${CSI}1;2D", key(TerminalKey.LEFT, KeyModifiers(shift = true)))
        assertEquals("${CSI}1;8H", key(TerminalKey.HOME, KeyModifiers(shift = true, alt = true, ctrl = true)))
        assertEquals("${CSI}3;3~", key(TerminalKey.DELETE, KeyModifiers(alt = true)))
        assertEquals("${CSI}1;5P", key(TerminalKey.F1, KeyModifiers(ctrl = true)))
        assertEquals("${CSI}15;2~", key(TerminalKey.F5, KeyModifiers(shift = true)))
    }

    @Test
    fun editingAndPageKeys() {
        assertEquals("${CSI}H", key(TerminalKey.HOME))
        assertEquals("${ESC}OF", key(TerminalKey.END, app = true))
        assertEquals("${CSI}2~", key(TerminalKey.INSERT))
        assertEquals("${CSI}3~", key(TerminalKey.DELETE))
        assertEquals("${CSI}5~", key(TerminalKey.PAGE_UP))
        assertEquals("${CSI}6~", key(TerminalKey.PAGE_DOWN))
    }

    @Test
    fun functionKeys() {
        assertEquals("${ESC}OP", key(TerminalKey.F1))
        assertEquals("${ESC}OS", key(TerminalKey.F4))
        assertEquals("${CSI}17~", key(TerminalKey.F6))
        assertEquals("${CSI}21~", key(TerminalKey.F10))
        assertEquals("${CSI}24~", key(TerminalKey.F12))
    }

    @Test
    fun simpleKeys() {
        assertEquals("\t", key(TerminalKey.TAB))
        assertEquals("${CSI}Z", key(TerminalKey.TAB, KeyModifiers(shift = true)))
        assertEquals("\u007f", key(TerminalKey.BACKSPACE))
        assertEquals("\b", key(TerminalKey.BACKSPACE, KeyModifiers(ctrl = true)))
        assertEquals("$ESC\u007f", key(TerminalKey.BACKSPACE, KeyModifiers(alt = true)))
        assertEquals("\r", key(TerminalKey.ENTER))
        assertEquals("$ESC\r", key(TerminalKey.ENTER, KeyModifiers(alt = true)))
        assertEquals(ESC, key(TerminalKey.ESCAPE))
    }

    @Test
    fun controlCharacters() {
        assertEquals("\u0003", char('c', KeyModifiers(ctrl = true)))
        assertEquals("\u0003", char('C', KeyModifiers(ctrl = true)))
        assertEquals("\u0000", char(' ', KeyModifiers(ctrl = true)))
        assertEquals("\u001b", char('[', KeyModifiers(ctrl = true)))
        assertEquals("\u001f", char('_', KeyModifiers(ctrl = true)))
        assertEquals("\u007f", char('?', KeyModifiers(ctrl = true)))
    }

    @Test
    fun altPrefixesEscape() {
        assertEquals("${ESC}x", char('x', KeyModifiers(alt = true)))
        assertEquals("$ESC\u0018", char('x', KeyModifiers(alt = true, ctrl = true)))
        assertEquals("é", char('é', KeyModifiers.NONE))
    }

    @Test
    fun pasteIsBracketedWhenAsked() {
        assertEquals("a\rb", String(KeyEncoder.encodePaste("a\r\nb", false), Charsets.UTF_8))
        assertEquals("${CSI}200~x\ry${CSI}201~", String(KeyEncoder.encodePaste("x\ny", true), Charsets.UTF_8))
    }

    @Test
    fun pasteCannotEndTheBracketEarly() {
        assertEquals("${CSI}200~ab${CSI}201~", String(KeyEncoder.encodePaste("a$ESC[201~b", true), Charsets.UTF_8))
    }
}
