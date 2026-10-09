package org.amnezia.vpn

import android.content.Context
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.amnezia.vpn.util.Log
import org.json.JSONObject

// Fork builds only (never in a pull request): while the VPN runs, the service looks for a
// newer release of the fork once per calendar day, so that people who never open the app
// still learn of it. The app's window does the same at startup (forkUpdateCheck.cpp); both
// keep what they learn in files/forkUpdate.json, and whoever checks first on a day answers
// for the other. The app's switch for the automatic check reaches the service in the VPN
// config ("forkUpdates"); which build this is, the service reads from its own version name.

private const val TAG = "ForkUpdates"
private const val STATE_FILE = "forkUpdate.json"
private const val REPO = "amnezia-tun0-fix/amnezia-client"

object ForkUpdates {

    const val CHECK_PERIOD_MS = 6 * 60 * 60 * 1000L

    private val tagPattern = Regex("""^v(\d+(?:\.\d+)*)-strict\.(\d+)$""")
    private val ownPattern = Regex("""^(\d+(?:\.\d+)*)-strict\.(\d+)$""")

    // This build: 5.0.3.1 and N of its version name 5.0.3.1-strict.N.
    class Settings(val base: String, val release: Int)

    class Release(val tag: String, val name: String, val url: String) {
        val version: String get() = tag.removePrefix("v")
    }

    // Null when the automatic check is off. A config with no "forkUpdates", saved by a build
    // before the check existed, counts as on, as upstream's switch defaults to on. The build's
    // own number comes from its version name, never from a saved config: that may have been
    // sent by the build installed before this one.
    fun settings(context: Context, config: JSONObject?): Settings? {
        if (config?.optJSONObject("forkUpdates")?.optBoolean("autoCheck", true) == false) return null
        val own = ownPattern.matchEntire(ownVersion(context)) ?: return null
        return Settings(own.groupValues[1], own.groupValues[2].toInt())
    }

    // Same key as forkDayKey() in forkUpdateCheck.cpp.
    private fun dayKey(): String = SimpleDateFormat("yyyy-MM-dd", Locale.US).format(Date())

    // "Connected · ⬆️ UPDATE". HyperOS drops bold and colour spans in notification text,
    // while an emoji keeps its colour; the user chose this from the variants on the phone.
    fun statusWithUpdate(status: String, label: String): String = "$status · ⬆️ ${label.uppercase()}"

    // Blocking: call off the main thread. Returns the newer release known after the check.
    fun checkIfDue(context: Context, settings: Settings): Release? {
        val state = readState(context)
        if (state.optString("lastCheckDay") == dayKey()) return knownUpdate(context, settings, state)

        val release = try {
            fetchLatest(settings)
        } catch (e: Exception) {
            Log.w(TAG, "Release check failed: $e")
            return knownUpdate(context, settings, state)
        }
        state.put("lastCheckDay", dayKey())
        state.put("latestTag", release.optString("tag_name"))
        state.put("latestName", release.optString("name"))
        state.put("latestUrl", release.optString("html_url"))
        state.put("publishedAt", release.optString("published_at"))
        writeState(context, state)

        val update = knownUpdate(context, settings, state)
        Log.i(TAG, "Latest release ${state.optString("latestTag")}, this build ${ownVersion(context)}: " +
            if (update != null) "update available" else "up to date")
        return update
    }

    // What an earlier check found, without the network: for a service started before a connect.
    fun known(context: Context, settings: Settings): Release? = knownUpdate(context, settings, readState(context))

    // True once per release: the separate notification is shown for each new version once.
    fun markNotified(context: Context, release: Release): Boolean {
        val state = readState(context)
        if (state.optString("notifiedTag") == release.tag) return false
        state.put("notifiedTag", release.tag)
        writeState(context, state)
        return true
    }

    private fun fetchLatest(settings: Settings): JSONObject {
        val connection = URL("https://api.github.com/repos/$REPO/releases/latest")
            .openConnection() as HttpURLConnection
        try {
            connection.connectTimeout = 7000
            connection.readTimeout = 7000
            connection.setRequestProperty("Accept", "application/vnd.github+json")
            connection.setRequestProperty("User-Agent", "AmneziaVPN-strict.${settings.release}")
            val code = connection.responseCode
            check(code == HttpURLConnection.HTTP_OK) { "HTTP $code" }
            return JSONObject(connection.inputStream.bufferedReader().use { it.readText() })
        } finally {
            connection.disconnect()
        }
    }

    private fun knownUpdate(context: Context, settings: Settings, state: JSONObject): Release? {
        val tag = state.optString("latestTag")
        val match = tagPattern.matchEntire(tag) ?: return null
        val base = compareVersions(match.groupValues[1], settings.base)
        val newer = base > 0 || (base == 0 && match.groupValues[2].toInt() > settings.release)
        return if (newer) Release(tag, state.optString("latestName"), state.optString("latestUrl")) else null
    }

    private fun ownVersion(context: Context): String =
        context.packageManager.getPackageInfo(context.packageName, 0).versionName ?: ""

    private fun compareVersions(a: String, b: String): Int {
        val x = a.split('.').map { it.toIntOrNull() ?: 0 }
        val y = b.split('.').map { it.toIntOrNull() ?: 0 }
        for (i in 0 until maxOf(x.size, y.size)) {
            val d = x.getOrElse(i) { 0 }.compareTo(y.getOrElse(i) { 0 })
            if (d != 0) return d
        }
        return 0
    }

    private fun readState(context: Context): JSONObject = try {
        JSONObject(File(context.filesDir, STATE_FILE).readText())
    } catch (e: Exception) {
        JSONObject()
    }

    private fun writeState(context: Context, state: JSONObject) {
        val tmp = File(context.filesDir, "$STATE_FILE.tmp")
        tmp.writeText(state.toString())
        tmp.renameTo(File(context.filesDir, STATE_FILE))
    }
}
