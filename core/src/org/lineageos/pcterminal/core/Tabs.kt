package org.lineageos.pcterminal.core

/** The open terminals of a window and the one in front. */
data class Tabs<T>(val items: List<T> = emptyList(), val selected: Int = -1) {
    val current: T? get() = items.getOrNull(selected)

    fun open(item: T) = Tabs(items + item, items.size)

    fun select(index: Int) = if (index in items.indices) copy(selected = index) else this

    fun selectNext() = if (items.isEmpty()) this else copy(selected = (selected + 1) % items.size)

    fun selectPrevious() = if (items.isEmpty()) this else copy(selected = (selected - 1 + items.size) % items.size)

    fun close(index: Int): Tabs<T> {
        if (index !in items.indices) return this
        val remaining = items.filterIndexed { i, _ -> i != index }
        val next = when {
            index < selected -> selected - 1
            else -> selected.coerceAtMost(remaining.lastIndex)
        }
        return Tabs(remaining, next)
    }
}
