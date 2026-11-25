package org.lineageos.pcterminal.core.terminal

/** Select Graphic Rendition (CSI ... m): attributes and colors, in `;` and `:` forms. */
internal object Sgr {
    private class Step(val style: CellStyle, val consumed: Int)

    fun apply(style: CellStyle, params: CsiParams): CellStyle {
        if (params.size == 0) return CellStyle.DEFAULT
        var current = style
        var i = 0
        while (i < params.size) {
            val step = applyAt(current, params, i)
            current = step.style
            i += step.consumed
        }
        return current
    }

    private fun applyAt(style: CellStyle, params: CsiParams, i: Int): Step {
        val group = params.readGroup(i)
        val code = group.firstOrNull()?.coerceAtLeast(0) ?: 0
        return when (code) {
            FG_EXTENDED -> applyExtended(style, params, i) { s, c -> s.copy(fg = c) }
            BG_EXTENDED -> applyExtended(style, params, i) { s, c -> s.copy(bg = c) }
            UNDERLINE_COLOR -> applyExtended(style, params, i) { s, _ -> s }
            UNDERLINE -> Step(style.withAttribute(CellStyle.UNDERLINE, group.getOrElse(1) { 1 } != 0), 1)
            else -> Step(SIMPLE[code]?.invoke(style) ?: style, 1)
        }
    }

    private fun applyExtended(style: CellStyle, params: CsiParams, i: Int, setColor: (CellStyle, Int) -> CellStyle): Step {
        val group = params.readGroup(i)
        val parsed = if (group.size > 1) readColon(group) else readSemicolon(params, i)
        val color = parsed.first ?: return Step(style, parsed.second)
        return Step(setColor(style, color), parsed.second)
    }

    /** `38:5:n`, `38:2:r:g:b` or `38:2:<colorspace>:r:g:b`. */
    private fun readColon(group: IntArray): Pair<Int?, Int> {
        val color = when (group[1]) {
            5 -> group.getOrNull(2)?.let { TermColor.indexed(it.coerceAtLeast(0)) }
            2 -> group.takeIf { it.size >= 5 }?.let { readRgb(it[it.size - 3], it[it.size - 2], it[it.size - 1]) }
            else -> null
        }
        return color to 1
    }

    /** `38;5;n` or `38;2;r;g;b`, which take the following parameters. */
    private fun readSemicolon(params: CsiParams, i: Int): Pair<Int?, Int> = when (params.read(i + 1, 0)) {
        5 -> TermColor.indexed(params.read(i + 2, 0)) to 3
        2 -> readRgb(params.read(i + 2, 0), params.read(i + 3, 0), params.read(i + 4, 0)) to 5
        else -> null to 2
    }

    private fun readRgb(r: Int, g: Int, b: Int) = TermColor.rgb(r.coerceAtLeast(0), g.coerceAtLeast(0), b.coerceAtLeast(0))

    private const val FG_EXTENDED = 38
    private const val BG_EXTENDED = 48
    private const val UNDERLINE_COLOR = 58
    private const val UNDERLINE = 4

    private fun on(flag: Int): (CellStyle) -> CellStyle = { it.withAttribute(flag, true) }
    private fun off(flags: Int): (CellStyle) -> CellStyle = { it.copy(attributes = it.attributes and flags.inv()) }

    private val SIMPLE: Map<Int, (CellStyle) -> CellStyle> = buildMap {
        put(0) { CellStyle.DEFAULT }
        put(1, on(CellStyle.BOLD))
        put(2, on(CellStyle.DIM))
        put(3, on(CellStyle.ITALIC))
        put(5, on(CellStyle.BLINK))
        put(6, on(CellStyle.BLINK))
        put(7, on(CellStyle.INVERSE))
        put(8, on(CellStyle.INVISIBLE))
        put(9, on(CellStyle.STRIKETHROUGH))
        put(21, on(CellStyle.UNDERLINE))
        put(22, off(CellStyle.BOLD or CellStyle.DIM))
        put(23, off(CellStyle.ITALIC))
        put(24, off(CellStyle.UNDERLINE))
        put(25, off(CellStyle.BLINK))
        put(27, off(CellStyle.INVERSE))
        put(28, off(CellStyle.INVISIBLE))
        put(29, off(CellStyle.STRIKETHROUGH))
        put(39) { it.copy(fg = TermColor.DEFAULT) }
        put(49) { it.copy(bg = TermColor.DEFAULT) }
        for (n in 0..7) {
            put(30 + n) { it.copy(fg = TermColor.indexed(n)) }
            put(40 + n) { it.copy(bg = TermColor.indexed(n)) }
            put(90 + n) { it.copy(fg = TermColor.indexed(8 + n)) }
            put(100 + n) { it.copy(bg = TermColor.indexed(8 + n)) }
        }
    }
}
