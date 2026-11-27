package org.lineageos.pcterminal.core.terminal

/** Mouse buttons by xterm's button number. */
enum class MouseButton(val code: Int) { LEFT(0), MIDDLE(1), RIGHT(2), WHEEL_UP(64), WHEEL_DOWN(65) }

/** PRESS and RELEASE of a button; DRAG is a move with a button held, MOVE one without. */
enum class MouseAction { PRESS, RELEASE, DRAG, MOVE }

/** One mouse event for the program, at a 0-based screen cell. [button] is null for MOVE. */
data class MouseReport(
    val action: MouseAction,
    val button: MouseButton?,
    val row: Int,
    val col: Int,
    val modifiers: KeyModifiers = KeyModifiers.NONE,
)

/** The bytes xterm sends for mouse events, in the X10 form or the SGR one (DECSET 1006). */
object MouseEncoder {
    private const val CSI = "\u001b["
    private const val MOTION = 32
    private const val NO_BUTTON = 3
    private const val SHIFT = 4
    private const val ALT = 8
    private const val CTRL = 16

    /** X10 puts each number in one byte as 32 + value: columns and rows past 223 cannot be sent. */
    private const val X10_OFFSET = 32
    private const val X10_LIMIT = 255

    /** The events each tracking mode reports (wheel turns are presses). */
    private val REPORTED: Map<MouseTracking, Set<MouseAction>> = mapOf(
        MouseTracking.NONE to emptySet(),
        MouseTracking.CLICKS to setOf(MouseAction.PRESS, MouseAction.RELEASE),
        MouseTracking.DRAG to setOf(MouseAction.PRESS, MouseAction.RELEASE, MouseAction.DRAG),
        MouseTracking.ALL to MouseAction.entries.toSet(),
    )

    /** The button number of each action; X10 cannot say which button was released. */
    private val X10_BUTTONS: Map<MouseAction, (MouseButton?) -> Int> = mapOf(
        MouseAction.PRESS to { b -> b?.code ?: NO_BUTTON },
        MouseAction.RELEASE to { _ -> NO_BUTTON },
        MouseAction.DRAG to { b -> (b?.code ?: NO_BUTTON) + MOTION },
        MouseAction.MOVE to { _ -> NO_BUTTON + MOTION },
    )

    private val SGR_BUTTONS: Map<MouseAction, (MouseButton?) -> Int> = X10_BUTTONS + (MouseAction.RELEASE to { b -> b?.code ?: NO_BUTTON })

    /** What to send for [report], or null when [tracking] does not report it or X10 cannot carry it. */
    fun encode(report: MouseReport, tracking: MouseTracking, isSgr: Boolean): ByteArray? {
        if (report.action !in REPORTED.getValue(tracking)) return null
        return if (isSgr) encodeSgr(report) else encodeX10(report)
    }

    private fun encodeSgr(report: MouseReport): ByteArray {
        val code = SGR_BUTTONS.getValue(report.action)(report.button) + findModifierBits(report.modifiers)
        val final = if (report.action == MouseAction.RELEASE) 'm' else 'M'
        return "$CSI<$code;${report.col + 1};${report.row + 1}$final".toByteArray(Charsets.US_ASCII)
    }

    private fun encodeX10(report: MouseReport): ByteArray? {
        val code = X10_BUTTONS.getValue(report.action)(report.button) + findModifierBits(report.modifiers)
        val values = listOf(code, report.col + 1, report.row + 1).map { it + X10_OFFSET }
        if (values.any { it > X10_LIMIT }) return null
        return "${CSI}M".toByteArray(Charsets.US_ASCII) + values.map { it.toByte() }.toByteArray()
    }

    private fun findModifierBits(m: KeyModifiers): Int =
        (if (m.shift) SHIFT else 0) + (if (m.alt) ALT else 0) + (if (m.ctrl) CTRL else 0)
}
