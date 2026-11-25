package org.lineageos.pcterminal.core.terminal

/**
 * Colors as plain ints: [DEFAULT] (the theme's foreground or background), 0..255 for the xterm
 * palette (0..15 are the ANSI colors), or 24-bit RGB with bit 24 set.
 */
object TermColor {
    const val DEFAULT = -1
    private const val RGB_FLAG = 0x1000000
    private const val OPAQUE = 0xFF000000.toInt()

    fun indexed(index: Int): Int = index and 0xFF

    fun rgb(red: Int, green: Int, blue: Int): Int =
        RGB_FLAG or (red and 0xFF shl 16) or (green and 0xFF shl 8) or (blue and 0xFF)

    fun isRgb(color: Int): Boolean = color >= RGB_FLAG

    /** ARGB for drawing; [palette] replaces the 256 xterm colors (a theme may change the first 16). */
    fun toArgb(color: Int, defaultArgb: Int, palette: IntArray = PALETTE): Int = when {
        color == DEFAULT -> defaultArgb
        isRgb(color) -> OPAQUE or (color and 0xFFFFFF)
        else -> palette[color]
    }

    private val ANSI = intArrayOf(
        0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
        0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
    )
    private val CUBE_LEVELS = intArrayOf(0, 95, 135, 175, 215, 255)

    /** The xterm 256-color palette as opaque ARGB. */
    val PALETTE: IntArray = IntArray(256) { OPAQUE or buildRgb(it) }

    private fun buildRgb(index: Int): Int = when {
        index < 16 -> ANSI[index]
        index < 232 -> buildCube(index - 16)
        else -> buildGray(index - 232)
    }

    private fun buildCube(n: Int): Int =
        CUBE_LEVELS[n / 36] shl 16 or (CUBE_LEVELS[n / 6 % 6] shl 8) or CUBE_LEVELS[n % 6]

    private fun buildGray(n: Int): Int = (8 + n * 10).let { it shl 16 or (it shl 8) or it }
}
