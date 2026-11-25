package org.lineageos.pcterminal.core.terminal

/** How a cell is drawn: colors from [TermColor] and attribute bits. */
data class CellStyle(val fg: Int = TermColor.DEFAULT, val bg: Int = TermColor.DEFAULT, val attributes: Int = 0) {
    val isBold get() = has(BOLD)
    val isDim get() = has(DIM)
    val isItalic get() = has(ITALIC)
    val isUnderline get() = has(UNDERLINE)
    val isBlink get() = has(BLINK)
    val isInverse get() = has(INVERSE)
    val isInvisible get() = has(INVISIBLE)
    val isStrikethrough get() = has(STRIKETHROUGH)

    private fun has(flag: Int) = attributes and flag != 0

    fun withAttribute(flag: Int, on: Boolean): CellStyle =
        copy(attributes = if (on) attributes or flag else attributes and flag.inv())

    companion object {
        const val BOLD = 1
        const val DIM = 2
        const val ITALIC = 4
        const val UNDERLINE = 8
        const val BLINK = 16
        const val INVERSE = 32
        const val INVISIBLE = 64
        const val STRIKETHROUGH = 128
        val DEFAULT = CellStyle()
    }
}
