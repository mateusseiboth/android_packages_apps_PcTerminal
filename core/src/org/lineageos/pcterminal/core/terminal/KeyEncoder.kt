package org.lineageos.pcterminal.core.terminal

/** Keys that are not characters. */
enum class TerminalKey {
    UP, DOWN, RIGHT, LEFT, HOME, END, PAGE_UP, PAGE_DOWN, INSERT, DELETE,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    TAB, BACKSPACE, ENTER, ESCAPE,
}

data class KeyModifiers(val shift: Boolean = false, val alt: Boolean = false, val ctrl: Boolean = false) {
    val isNone: Boolean get() = !shift && !alt && !ctrl

    /** xterm's modifier parameter: 1 + shift + 2 alt + 4 ctrl. */
    val xtermParameter: Int get() = 1 + (if (shift) 1 else 0) + (if (alt) 2 else 0) + (if (ctrl) 4 else 0)

    companion object {
        val NONE = KeyModifiers()
    }
}

/** The bytes xterm sends for keys, typed characters and pasted text. */
object KeyEncoder {
    private const val ESC = "\u001b"
    private const val CSI = "\u001b["
    private const val SS3 = "\u001bO"

    /** Arrows, Home and End: `CSI x`, `SS3 x` in application mode, `CSI 1;m x` with modifiers. */
    private val CURSOR = mapOf(
        TerminalKey.UP to 'A', TerminalKey.DOWN to 'B', TerminalKey.RIGHT to 'C', TerminalKey.LEFT to 'D',
        TerminalKey.HOME to 'H', TerminalKey.END to 'F',
    )

    /** F1 to F4: `SS3 x`, `CSI 1;m x` with modifiers. */
    private val FUNCTION_SS3 = mapOf(TerminalKey.F1 to 'P', TerminalKey.F2 to 'Q', TerminalKey.F3 to 'R', TerminalKey.F4 to 'S')

    /** `CSI n ~`, `CSI n;m ~` with modifiers. */
    private val TILDE = mapOf(
        TerminalKey.INSERT to 2, TerminalKey.DELETE to 3, TerminalKey.PAGE_UP to 5, TerminalKey.PAGE_DOWN to 6,
        TerminalKey.F5 to 15, TerminalKey.F6 to 17, TerminalKey.F7 to 18, TerminalKey.F8 to 19,
        TerminalKey.F9 to 20, TerminalKey.F10 to 21, TerminalKey.F11 to 23, TerminalKey.F12 to 24,
    )

    private val SIMPLE: Map<TerminalKey, (KeyModifiers) -> String> = mapOf(
        TerminalKey.TAB to { m -> if (m.shift) "${CSI}Z" else withAlt(m, "\t") },
        TerminalKey.BACKSPACE to { m -> withAlt(m, if (m.ctrl) "\b" else "\u007f") },
        TerminalKey.ENTER to { m -> withAlt(m, "\r") },
        TerminalKey.ESCAPE to { m -> withAlt(m, ESC) },
    )

    private val ENCODERS: List<(TerminalKey, KeyModifiers, Boolean) -> String?> = listOf(
        { key, m, app -> CURSOR[key]?.let { encodeCursor(it, m, app) } },
        { key, m, _ -> FUNCTION_SS3[key]?.let { encodeCursor(it, m, true) } },
        { key, m, _ -> TILDE[key]?.let { if (m.isNone) "$CSI$it~" else "$CSI$it;${m.xtermParameter}~" } },
        { key, m, _ -> SIMPLE[key]?.invoke(m) },
    )

    /** CTRL with these characters gives the C0 control that xterm sends. */
    private val CONTROL_SYMBOLS = mapOf(
        ' ' to 0, '@' to 0, '2' to 0, '[' to 0x1B, '3' to 0x1B, '\\' to 0x1C, '4' to 0x1C,
        ']' to 0x1D, '5' to 0x1D, '^' to 0x1E, '6' to 0x1E, '_' to 0x1F, '/' to 0x1F, '7' to 0x1F,
        '?' to 0x7F, '8' to 0x7F,
    ).mapKeys { it.key.code }

    private val PASTE_MARKERS = listOf("${CSI}200~", "${CSI}201~")

    fun encode(key: TerminalKey, modifiers: KeyModifiers, isApplicationCursorKeys: Boolean): ByteArray =
        ENCODERS.firstNotNullOf { it(key, modifiers, isApplicationCursorKeys) }.toByteArray(Charsets.UTF_8)

    /** A typed character: CTRL makes a control character, ALT puts ESC before it. */
    fun encodeChar(codePoint: Int, modifiers: KeyModifiers): ByteArray {
        val base = if (modifiers.ctrl) findControl(codePoint) ?: codePoint else codePoint
        return withAlt(modifiers, String(Character.toChars(base))).toByteArray(Charsets.UTF_8)
    }

    /** Pasted text: newlines become CR, and with bracketed paste it is wrapped in CSI 200~ / 201~. */
    fun encodePaste(text: String, isBracketed: Boolean): ByteArray {
        val clean = PASTE_MARKERS.fold(text) { acc, marker -> acc.replace(marker, "") }
            .replace("\r\n", "\r")
            .replace('\n', '\r')
        val wrapped = if (isBracketed) "${CSI}200~$clean${CSI}201~" else clean
        return wrapped.toByteArray(Charsets.UTF_8)
    }

    private fun encodeCursor(final: Char, m: KeyModifiers, isApplication: Boolean): String = when {
        !m.isNone -> "${CSI}1;${m.xtermParameter}$final"
        isApplication -> "$SS3$final"
        else -> "$CSI$final"
    }

    private fun withAlt(m: KeyModifiers, text: String) = if (m.alt) "$ESC$text" else text

    private fun findControl(codePoint: Int): Int? = when (codePoint) {
        in 'a'.code..'z'.code -> codePoint - 0x60
        in 'A'.code..'Z'.code -> codePoint - 0x40
        else -> CONTROL_SYMBOLS[codePoint]
    }
}
