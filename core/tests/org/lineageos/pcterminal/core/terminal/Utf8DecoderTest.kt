package org.lineageos.pcterminal.core.terminal

import org.junit.Assert.assertEquals
import org.junit.Test

class Utf8DecoderTest {
    private fun decode(decoder: Utf8Decoder, vararg bytes: Int): List<Int> {
        val out = mutableListOf<Int>()
        val array = ByteArray(bytes.size) { bytes[it].toByte() }
        decoder.decode(array, 0, array.size) { out += it }
        return out
    }

    @Test
    fun decodesAsciiAndMultiByteSequences() {
        val text = "aé€😀"
        val out = mutableListOf<Int>()
        val bytes = text.toByteArray(Charsets.UTF_8)
        Utf8Decoder().decode(bytes, 0, bytes.size) { out += it }
        assertEquals(text.codePoints().toArray().toList(), out)
    }

    @Test
    fun keepsASequenceSplitAcrossChunks() {
        val decoder = Utf8Decoder()
        assertEquals(emptyList<Int>(), decode(decoder, 0xF0, 0x9F))
        assertEquals(emptyList<Int>(), decode(decoder, 0x98))
        assertEquals(listOf(0x1F600), decode(decoder, 0x80))
    }

    @Test
    fun replacesInvalidBytes() {
        assertEquals(listOf(0xFFFD, 'a'.code), decode(Utf8Decoder(), 0xFF, 'a'.code))
        assertEquals(listOf(0xFFFD, 'b'.code), decode(Utf8Decoder(), 0xC3, 'b'.code))
        assertEquals(listOf(0xFFFD), decode(Utf8Decoder(), 0x80))
    }

    @Test
    fun replacesOverlongAndSurrogateEncodings() {
        assertEquals(0xFFFD, decode(Utf8Decoder(), 0xC0, 0x80).first())
        assertEquals(0xFFFD, decode(Utf8Decoder(), 0xED, 0xA0, 0x80).first())
    }

    @Test
    fun honorsOffsetAndLength() {
        val out = mutableListOf<Int>()
        Utf8Decoder().decode("xaby".toByteArray(), 1, 2) { out += it }
        assertEquals(listOf('a'.code, 'b'.code), out)
    }
}
