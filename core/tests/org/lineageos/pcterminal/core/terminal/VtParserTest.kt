package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Test

class VtParserTest {
    private class Recorder : VtHandler {
        val events = mutableListOf<String>()

        override fun onPrint(codePoint: Int) {
            events += "print ${String(Character.toChars(codePoint))}"
        }

        override fun onExecute(control: Int) {
            events += "exec $control"
        }

        override fun onCsi(params: CsiParams, intermediates: String, final: Char) {
            events += "csi ${params.prefix ?: ""}${params.describe()}$intermediates$final"
        }

        override fun onEsc(intermediates: String, final: Char) {
            events += "esc $intermediates$final"
        }

        override fun onOsc(text: String) {
            events += "osc $text"
        }
    }

    private fun parse(text: String): List<String> {
        val recorder = Recorder()
        val parser = VtParser(recorder)
        text.codePoints().forEach(parser::feed)
        return recorder.events
    }

    @Test
    fun printsTextAndExecutesControls() {
        assertEquals(listOf("print a", "exec 13", "exec 10", "print é"), parse("a\r\né"))
    }

    @Test
    fun parsesCsiWithParametersAndDefaults() {
        assertEquals(listOf("csi 1;2H"), parse("${CSI}1;2H"))
        assertEquals(listOf("csi H"), parse("${CSI}H"))
        assertEquals(listOf("csi -;5H"), parse("$CSI;5H"))
    }

    @Test
    fun parsesPrivateMarkersAndIntermediates() {
        assertEquals(listOf("csi ?1049h"), parse("$CSI?1049h"))
        assertEquals(listOf("csi 2 q"), parse("${CSI}2 q"))
        assertEquals(listOf("csi >c"), parse("$CSI>c"))
    }

    @Test
    fun parsesColonSubParameters() {
        assertEquals(listOf("csi 38:2:-:10:20:30m"), parse("${CSI}38:2::10:20:30m"))
    }

    @Test
    fun executesControlsInsideASequence() {
        assertEquals(listOf("exec 8", "csi 2A"), parse("${CSI}2\bA"))
    }

    @Test
    fun parsesEscapeSequences() {
        assertEquals(listOf("esc 7", "esc (0", "esc #8"), parse("${ESC}7$ESC(0$ESC#8"))
    }

    @Test
    fun parsesOscEndedByBelOrStringTerminator() {
        assertEquals(listOf("osc 0;hello"), parse("$ESC]0;hello\u0007"))
        assertEquals(listOf("osc 2;wórld", "print x"), parse("$ESC]2;wórld$ESC\\x"))
    }

    @Test
    fun ignoresDcsStrings() {
        assertEquals(listOf("print a"), parse("${ESC}P1\$r0m$ESC\\a"))
    }

    @Test
    fun cancelReturnsToGround() {
        assertEquals(listOf("print x"), parse("${CSI}12\u0018x"))
    }

    @Test
    fun escapeRestartsASequence() {
        assertEquals(listOf("csi 3B"), parse("${CSI}1$CSI${"3B"}"))
    }

    @Test
    fun ignoresDeleteInGround() {
        assertEquals(listOf("print a"), parse("a\u007f"))
    }

    @Test
    fun malformedCsiIsIgnoredUpToItsFinalByte() {
        assertEquals(listOf("print z"), parse("${CSI}1?2hz"))
    }
}

/** "1;2" with "-" for a missing value and ":" between sub parameters. */
fun CsiParams.describe(): String = (0 until size).joinToString(";") { i ->
    readGroup(i).joinToString(":") { if (it < 0) "-" else it.toString() }
}
