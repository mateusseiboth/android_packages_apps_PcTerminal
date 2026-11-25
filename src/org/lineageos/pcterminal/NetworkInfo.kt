package org.lineageos.pcterminal

import android.content.Context
import android.net.ConnectivityManager

/** The active network's name servers, for the container's resolv.conf (at most three, no scoped addresses). */
fun findDnsServers(context: Context): List<String> {
    val connectivity = context.getSystemService(ConnectivityManager::class.java) ?: return emptyList()
    val properties = connectivity.activeNetwork?.let(connectivity::getLinkProperties) ?: return emptyList()
    return properties.dnsServers.mapNotNull { it.hostAddress }.filterNot { '%' in it }.take(MAX_DNS)
}

private const val MAX_DNS = 3
