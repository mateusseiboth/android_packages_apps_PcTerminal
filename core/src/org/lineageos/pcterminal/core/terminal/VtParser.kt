package org.lineageos.pcterminal.core.terminal

/** What the parser found; [Terminal] is the real handler. */
interface VtHandler {
    /** A printable character. */
    fun onPrint(codePoint: Int)

    /** A C0 control (BEL, BS, HT, LF, CR...). */
    fun onExecute(control: Int)

    /** `ESC [ <prefix><params><intermediates><final>`. */
    fun onCsi(params: CsiParams, intermediates: String, final: Char)

    /** `ESC <intermediates><final>`. */
    fun onEsc(intermediates: String, final: Char)

    /** The text of an operating system command, without `ESC ]` and the terminator. */
    fun onOsc(text: String)
}

/**
 * The parameters of a control sequence. Each parameter is a group: its value followed by the sub
 * parameters written after `:`. A missing value is -1.
 */
class CsiParams(private val groups: List<IntArray>, val prefix: Char?) {
    val size: Int get() = groups.size

    fun readGroup(index: Int): IntArray = groups.getOrNull(index) ?: EMPTY

    /** The value of parameter [index], or [default] when it is missing or 0. */
    fun read(index: Int, default: Int): Int {
        val value = groups.getOrNull(index)?.firstOrNull() ?: -1
        return if (value > 0) value else default
    }

    companion object {
        private val EMPTY = IntArray(0)
    }
}

/**
 * A state machine for the escape sequences of VT100-xterm terminals, in the spirit of the DEC
 * ANSI parser: ground, escape, control sequence, operating system command and ignored strings
 * (DCS, SOS, PM, APC). It works on code points, so feed it from a [Utf8Decoder].
 */
class VtParser(private val handler: VtHandler) {
    private enum class State { GROUND, ESCAPE, CSI_ENTRY, CSI_PARAM, CSI_INTERMEDIATE, CSI_IGNORE, OSC, STRING }

    private var state = State.GROUND
    private val intermediates = StringBuilder()
    private val groups = ArrayList<IntArray>()
    private val group = ArrayList<Int>()
    private var value = -1
    private var hasParams = false
    private var prefix: Char? = null
    private val osc = StringBuilder()

    private val stateHandlers: Map<State, (Int) -> Unit> = mapOf(
        State.GROUND to ::onGround,
        State.ESCAPE to ::onEscape,
        State.CSI_ENTRY to ::onCsiEntry,
        State.CSI_PARAM to ::onCsiParam,
        State.CSI_INTERMEDIATE to ::onCsiIntermediate,
        State.CSI_IGNORE to ::onCsiIgnore,
        State.OSC to ::onOscChar,
        State.STRING to { _ -> },
    )

    fun feed(codePoint: Int) {
        when (codePoint) {
            CAN, SUB -> state = State.GROUND
            ESC -> onEscapeStart()
            else -> stateHandlers.getValue(state)(codePoint)
        }
    }

    private fun onEscapeStart() {
        if (state == State.OSC) handler.onOsc(osc.toString())
        intermediates.clear()
        state = State.ESCAPE
    }

    private fun isExecutable(c: Int) = c < 0x20

    private fun onGround(c: Int) {
        when {
            isExecutable(c) -> handler.onExecute(c)
            c == DEL || c in C1 -> Unit
            else -> handler.onPrint(c)
        }
    }

    private fun onEscape(c: Int) {
        when {
            isExecutable(c) -> handler.onExecute(c)
            c in INTERMEDIATE -> intermediates.appendCodePoint(c)
            intermediates.isEmpty() && c in ESCAPE_STARTS -> ESCAPE_STARTS.getValue(c)(this)
            c in ESCAPE_FINAL -> dispatchEscape(c)
            else -> state = State.GROUND
        }
    }

    private fun dispatchEscape(c: Int) {
        state = State.GROUND
        if (intermediates.isEmpty() && c == '\\'.code) return
        handler.onEsc(intermediates.toString(), c.toChar())
    }

    private fun startCsi() {
        groups.clear()
        group.clear()
        value = -1
        hasParams = false
        prefix = null
        intermediates.clear()
        state = State.CSI_ENTRY
    }

    private fun startOsc() {
        osc.clear()
        state = State.OSC
    }

    private fun startString() {
        state = State.STRING
    }

    private fun onCsiEntry(c: Int) {
        if (c in PRIVATE_MARKERS) {
            prefix = c.toChar()
            state = State.CSI_PARAM
            return
        }
        state = State.CSI_PARAM
        onCsiParam(c)
    }

    private fun onCsiParam(c: Int) {
        when {
            isExecutable(c) -> handler.onExecute(c)
            c in DIGITS -> addDigit(c - '0'.code)
            c == ':'.code -> endSubParam()
            c == ';'.code -> endParam()
            c in INTERMEDIATE -> enterCsiIntermediate(c)
            c in CSI_FINAL -> dispatchCsi(c)
            c == DEL -> Unit
            else -> state = State.CSI_IGNORE
        }
    }

    private fun addDigit(digit: Int) {
        value = (maxOf(value, 0) * 10 + digit).coerceAtMost(MAX_VALUE)
        hasParams = true
    }

    private fun endSubParam() {
        group += value
        value = -1
        hasParams = true
    }

    private fun endParam() {
        group += value
        if (groups.size < MAX_PARAMS) groups += group.toIntArray()
        group.clear()
        value = -1
        hasParams = true
    }

    private fun enterCsiIntermediate(c: Int) {
        intermediates.appendCodePoint(c)
        state = State.CSI_INTERMEDIATE
    }

    private fun onCsiIntermediate(c: Int) {
        when {
            isExecutable(c) -> handler.onExecute(c)
            c in INTERMEDIATE -> intermediates.appendCodePoint(c)
            c in CSI_FINAL -> dispatchCsi(c)
            else -> state = State.CSI_IGNORE
        }
    }

    private fun onCsiIgnore(c: Int) {
        when {
            isExecutable(c) -> handler.onExecute(c)
            c in CSI_FINAL -> state = State.GROUND
        }
    }

    private fun dispatchCsi(c: Int) {
        if (hasParams) endParam()
        state = State.GROUND
        handler.onCsi(CsiParams(groups.toList(), prefix), intermediates.toString(), c.toChar())
    }

    private fun onOscChar(c: Int) {
        when {
            c == BEL -> finishOsc()
            isExecutable(c) -> Unit
            osc.length < MAX_OSC -> osc.appendCodePoint(c)
        }
    }

    private fun finishOsc() {
        state = State.GROUND
        handler.onOsc(osc.toString())
    }

    private companion object {
        const val BEL = 0x07
        const val CAN = 0x18
        const val SUB = 0x1A
        const val ESC = 0x1B
        const val DEL = 0x7F
        const val MAX_VALUE = 65535
        const val MAX_PARAMS = 32
        const val MAX_OSC = 4096
        val C1 = 0x80..0x9F
        val DIGITS = '0'.code..'9'.code
        val INTERMEDIATE = 0x20..0x2F
        val ESCAPE_FINAL = 0x30..0x7E
        val CSI_FINAL = 0x40..0x7E
        val PRIVATE_MARKERS = setOf('<'.code, '='.code, '>'.code, '?'.code)
        val ESCAPE_STARTS: Map<Int, (VtParser) -> Unit> = mapOf(
            '['.code to VtParser::startCsi,
            ']'.code to VtParser::startOsc,
            'P'.code to VtParser::startString,
            'X'.code to VtParser::startString,
            '^'.code to VtParser::startString,
            '_'.code to VtParser::startString,
        )
    }
}
