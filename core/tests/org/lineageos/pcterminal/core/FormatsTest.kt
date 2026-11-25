package org.lineageos.pcterminal.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FormatsTest {
    @Test
    fun bytesInBinaryUnits() {
        assertEquals("0 B", Formats.formatBytes(0))
        assertEquals("1023 B", Formats.formatBytes(1023))
        assertEquals("1.0 KiB", Formats.formatBytes(1024))
        assertEquals("70.0 MiB", Formats.formatBytes(73400320))
        assertEquals("1.5 GiB", Formats.formatBytes(1610612736))
    }

    @Test
    fun cpuShareBetweenTwoReadings() {
        val first = CpuReading(usageUsec = 1_000_000, atMillis = 10_000)
        val second = CpuReading(usageUsec = 1_500_000, atMillis = 11_000)
        assertEquals(50.0, Formats.findCpuPercent(first, second)!!, 0.001)
        assertNull(Formats.findCpuPercent(second, second))
        assertNull(Formats.findCpuPercent(null, second))
    }
}
