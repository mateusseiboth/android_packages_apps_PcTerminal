package org.lineageos.pcterminal.core

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test

class DownloadsTest {
    @Test
    fun copiesCountingAndHashing() {
        val data = ByteArray(200_000) { (it % 251).toByte() }
        val out = ByteArrayOutputStream()
        val seen = mutableListOf<Long>()
        val sum = Downloads.copyWithSha256(ByteArrayInputStream(data), out) { seen += it }
        assertArrayEquals(data, out.toByteArray())
        assertEquals(200_000L, seen.last())
        assertEquals(seen, seen.sorted())
        assertEquals(64, sum.length)
        assertEquals(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            Downloads.copyWithSha256(ByteArrayInputStream(ByteArray(0)), ByteArrayOutputStream()) {},
        )
    }

    @Test
    fun fractionWithAnUnknownTotal() {
        assertEquals(0.5f, Downloads.findFraction(50, 100)!!, 0.0001f)
        assertEquals(null, Downloads.findFraction(50, -1))
        assertEquals(1f, Downloads.findFraction(150, 100)!!, 0.0001f)
    }
}
