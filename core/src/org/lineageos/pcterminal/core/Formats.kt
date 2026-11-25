package org.lineageos.pcterminal.core

import java.util.Locale

data class CpuReading(val usageUsec: Long, val atMillis: Long)

object Formats {
    private val UNITS = listOf("KiB", "MiB", "GiB", "TiB")
    private const val STEP = 1024.0

    fun formatBytes(bytes: Long): String {
        if (bytes < STEP) return "$bytes B"
        var value = bytes / STEP
        var unit = 0
        while (value >= STEP && unit < UNITS.lastIndex) {
            value /= STEP
            unit++
        }
        return String.format(Locale.ROOT, "%.1f %s", value, UNITS[unit])
    }

    /** CPU time over wall time between two readings, in percent of one core; null without a span. */
    fun findCpuPercent(before: CpuReading?, after: CpuReading): Double? {
        val wallUsec = before?.let { (after.atMillis - it.atMillis) * 1000.0 } ?: return null
        if (wallUsec <= 0) return null
        return (after.usageUsec - before.usageUsec) * 100.0 / wallUsec
    }
}
