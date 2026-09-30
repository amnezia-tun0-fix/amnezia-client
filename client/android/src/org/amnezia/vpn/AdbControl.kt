package org.amnezia.vpn

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

// Fork builds only (never in a pull request): drive the VPN from adb, for tests and
// measurements. tools/awgctl.sh in the umbrella repo wraps it.
//
//   adb shell setprop debug.awg.ctl "cmd=reconnect mode=include add=com.termux strict=1"
//   adb shell am start -n org.amnezia.vpn.strict/org.amnezia.vpn.AmneziaActivity --ez adb_ctl true
//   adb logcat -d -s AmneziaAdbCtl
//
// The command is read from the property, which only the shell can set: an app that
// sends the same intent can only repeat the shell's last command. A property value
// holds 91 bytes, hence add/del next to apps.
//
//   cmd=connect|disconnect|reconnect|status  connect does nothing while connected;
//                                            reconnect applies changes to a live tunnel
//   mode=off|include|exclude                 appSplitTunnelType
//   apps=a,b  add=a,b  del=a,b               splitTunnelApps: replace, add, remove
//   strict=0|1                               strictSplitTunneling
//
// Changes are applied to the config the service saved last, which is then saved again
// on connect. The app's own settings screen does not see them: connecting from the UI
// sends its own config and overrides them.

const val ADB_CTL = "adb_ctl"
const val ADB_TAG = "AmneziaAdbCtl"

private val adbModes = listOf("off", "include", "exclude")

fun adbCommand(): Map<String, String> {
    val v = try {
        Runtime.getRuntime().exec(arrayOf("/system/bin/getprop", "debug.awg.ctl"))
            .inputStream.bufferedReader().use { it.readText() }.trim()
    } catch (e: Exception) {
        ""
    }
    return v.split(' ').filter { '=' in it }.associate { it.substringBefore('=') to it.substringAfter('=') }
}

fun adbPatch(saved: String, cmd: Map<String, String>): String {
    val c = JSONObject(saved)
    cmd["mode"]?.let {
        val mode = adbModes.indexOf(it)
        require(mode >= 0) { "unknown mode $it" }
        c.put("appSplitTunnelType", mode)
    }
    val apps = LinkedHashSet(adbApps(c))
    cmd["apps"]?.let { apps.clear(); apps += adbList(it) }
    cmd["add"]?.let { apps += adbList(it) }
    cmd["del"]?.let { apps -= adbList(it).toSet() }
    c.put("splitTunnelApps", JSONArray(apps.toList()))
    cmd["strict"]?.let {
        require(it == "0" || it == "1") { "strict must be 0 or 1" }
        c.put("strictSplitTunneling", it == "1")
    }
    return c.toString()
}

fun adbSummary(config: String?): String = try {
    val c = JSONObject(config ?: "")
    "mode=${adbModes.getOrElse(c.optInt("appSplitTunnelType")) { "?" }} " +
        "apps=${adbApps(c).joinToString(",")} " +
        "strict=${if (c.optBoolean("strictSplitTunneling")) 1 else 0}"
} catch (e: JSONException) {
    "no saved config"
}

private fun adbList(v: String) = v.split(',').filter(String::isNotBlank)

private fun adbApps(c: JSONObject): List<String> =
    c.optJSONArray("splitTunnelApps")?.let { a -> (0 until a.length()).map(a::getString) } ?: emptyList()
