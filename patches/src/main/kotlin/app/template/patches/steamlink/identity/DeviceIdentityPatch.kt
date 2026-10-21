package app.template.patches.steamlink.identity

import app.template.patches.shared.PatchCategories
import app.morphe.patcher.patch.Compatibility
import app.morphe.patcher.patch.PatchException
import app.morphe.patcher.patch.rawResourcePatch
import app.morphe.patcher.patch.resourcePatch
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_5002363
import app.template.patches.shared.Constants.COMPATIBILITIES_STEAM_LINK_LEGACY
import app.template.patches.shared.Constants.isNativeXrSteamLinkBuild
import app.template.patches.steamlink.androidxr.adaptLegacyHmdConfigForBuild
import app.template.patches.steamlink.androidxr.ensureIdsXml
import app.template.patches.steamlink.androidxr.xrDeviceConfigBaselinePatch

/**
 * The model number SteamVR sees, and therefore the `vrlink_<model>` settings section the VRLink
 * driver reads. It equals this headset's `Build.PRODUCT`, which stock 2.0.23 reports from its own
 * product-name lookup, so a patched legacy base routes to the same section as an unpatched native
 * build and the Galaxy XR Companion only has to seed `vrlink_xrvst2ue`. Never change it to a
 * friendly display name: the PC-side driver derives the section name from this string.
 */
internal const val GALAXY_XR_MODEL_NUMBER = "xrvst2ue"

private val galaxyIdentityEntries = listOf("unknown", "xrvst2ue", "xrvst2")

private fun galaxyIdentityResource(): ByteArray =
    (object {}.javaClass.getResourceAsStream("/steamlink/androidxr/hmd_config.json")
        ?: error("Missing bundled resource: steamlink/androidxr/hmd_config.json"))
        .use { it.readBytes() }

private fun findObjectValueRange(json: String, key: String, start: Int, endExclusive: Int): IntRange? {
    val matches = Regex("\\\"${Regex.escape(key)}\\\"\\s*:")
        .findAll(json, start)
        .takeWhile { it.range.first < endExclusive }
        .toList()
    if (matches.size > 1) {
        throw PatchException("Duplicate HMD identity entry '$key'")
    }
    val match = matches.singleOrNull() ?: return null
    var objectStart = match.range.last + 1
    while (objectStart < endExclusive && json[objectStart].isWhitespace()) objectStart++
    if (objectStart >= endExclusive || json[objectStart] != '{') {
        throw PatchException("HMD identity entry '$key' is not an object")
    }

    var depth = 0
    var inString = false
    var escaped = false
    for (index in objectStart until endExclusive) {
        val character = json[index]
        if (inString) {
            when {
                escaped -> escaped = false
                character == '\\' -> escaped = true
                character == '"' -> inString = false
            }
            continue
        }
        when (character) {
            '"' -> inString = true
            '{' -> depth++
            '}' -> {
                depth--
                if (depth == 0) return objectStart..index
            }
        }
    }
    throw PatchException("Unterminated HMD identity entry '$key'")
}

/**
 * Refuse escaped spellings of the members [patchGalaxyIdentity] upserts.
 *
 * A JSON reader decodes `"\u0078rvst2ue"` to `xrvst2ue`, while a literal scan sees an unrelated
 * member and appends a second, equivalent one: the stale model then stays in force. Only direct
 * members of the `staticProps` object are inspected; plain duplicates remain
 * [findObjectValueRange]'s job so their existing message is unchanged.
 */
private fun rejectEscapedIdentityEntries(json: String, staticProps: IntRange) {
    fun decode(raw: String): String {
        if (raw.indexOf('\\') < 0) return raw
        val decoded = StringBuilder(raw.length)
        var index = 0
        while (index < raw.length) {
            val character = raw[index]
            if (character != '\\' || index == raw.lastIndex) {
                decoded.append(character)
                index++
                continue
            }
            when (val escape = raw[index + 1]) {
                '"', '\\', '/' -> decoded.append(escape)
                'b' -> decoded.append('\u0008')
                'f' -> decoded.append('\u000C')
                'n' -> decoded.append('\n')
                'r' -> decoded.append(Char(13))
                't' -> decoded.append('\t')
                'u' -> {
                    val hex = if (index + 6 <= raw.length) raw.substring(index + 2, index + 6) else null
                    val codePoint = hex?.toIntOrNull(16)
                    if (codePoint == null) {
                        decoded.append(escape)
                    } else {
                        decoded.append(Char(codePoint))
                        index += 4
                    }
                }
                else -> decoded.append(escape)
            }
            index += 2
        }
        return decoded.toString()
    }

    var index = staticProps.first + 1
    var depth = 0
    var inString = false
    var escaped = false
    var nameStart = -1
    while (index < staticProps.last) {
        val character = json[index]
        if (inString) {
            when {
                escaped -> escaped = false
                character == '\\' -> escaped = true
                character == '"' -> {
                    inString = false
                    if (depth == 0) {
                        var after = index + 1
                        while (after < json.length && json[after].isWhitespace()) after++
                        if (after < json.length && json[after] == ':') {
                            val raw = json.substring(nameStart, index)
                            val decoded = decode(raw)
                            if (decoded != raw && decoded in galaxyIdentityEntries) {
                                throw PatchException(
                                    "Escaped or duplicate HMD identity entry '$decoded'",
                                )
                            }
                        }
                    }
                }
            }
            index++
            continue
        }
        when (character) {
            '"' -> {
                inString = true
                if (depth == 0) nameStart = index + 1
            }
            '{', '[' -> depth++
            '}', ']' -> depth--
        }
        index++
    }
}

private fun lineIndentAt(json: String, index: Int): String {
    val lineStart = json.lastIndexOf('\n', index - 1).let { if (it < 0) 0 else it + 1 }
    return json.substring(lineStart, index).takeWhile { it == ' ' || it == '\t' }
}

private fun galaxyIdentityObject(indent: String, newline: String): String {
    val fieldIndent = "$indent  "
    return listOf(
        "{",
        "${fieldIndent}\"sTrackingSystem\": \"SamsungVST\",",
        "${fieldIndent}\"sSerialNumber\": \"VRLINKHMDGALAXYXR\",",
        "${fieldIndent}\"sManufacturerName\": \"Samsung\",",
        "${fieldIndent}\"sModelNumber\": \"$GALAXY_XR_MODEL_NUMBER\",",
        "${fieldIndent}\"sRenderModelName\": \"generic_hmd\",",
        "${fieldIndent}\"sResourceRoot\": \"vrlink\",",
        "${fieldIndent}\"sInputProfilePath\": \"{vrlink}/input/galaxy_xr_hmd_profile.json\",",
        "${fieldIndent}\"flLaserMouseHmdAngleDegrees\": 0,",
        "${fieldIndent}\"sControllerType\": \"galaxy_xr_hmd\",",
        "${fieldIndent}\"sDeviceType\": \"SamsungVST/VRLINKHMDGALAXYXR\"",
        "$indent}",
    ).joinToString(newline)
}

/**
 * Upsert the complete Galaxy XR transport identity into the runtime-selected HMD entries.
 *
 * Valve's native builds select `staticProps` by the exact product name and construct a generic
 * identity on a miss; explicit `xrvst2ue`/`xrvst2` entries keep that lookup on the Galaxy identity
 * and make the reported model deterministic. Only these three entries are replaced; every
 * unrelated byte of hmd_config.json (other products, head offsets, requested extensions, input
 * bindings) remains untouched.
 */
internal fun patchGalaxyIdentity(json: String): String {
    val staticPropsMatch = Regex("\\\"staticProps\\\"\\s*:").find(json)
        ?: throw PatchException("Missing staticProps object")
    val initialStaticProps = findObjectValueRange(
        json,
        "staticProps",
        staticPropsMatch.range.first,
        json.length,
    ) ?: throw PatchException("Missing staticProps object")
    val newline = if (json.contains("\r\n")) "\r\n" else "\n"
    val parentIndent = lineIndentAt(json, staticPropsMatch.range.first)
    val entryIndent = "$parentIndent  "

    // An escaped member name decodes to an identity name while the literal scan below cannot see
    // it, so the upsert would append a second, equivalent entry instead of replacing the stale one.
    rejectEscapedIdentityEntries(json, initialStaticProps)

    // Validate all existing target entries before constructing a result. This keeps malformed or
    // duplicate layouts atomic: either every requested identity is committed, or none is returned.
    galaxyIdentityEntries.forEach { key ->
        findObjectValueRange(json, key, initialStaticProps.first + 1, initialStaticProps.last)
    }

    var patched = json
    galaxyIdentityEntries.forEach { key ->
        val staticProps = findObjectValueRange(patched, "staticProps", 0, patched.length)
            ?: throw PatchException("Missing staticProps object")
        val existing = findObjectValueRange(
            patched,
            key,
            staticProps.first + 1,
            staticProps.last,
        )
        val identityObject = galaxyIdentityObject(entryIndent, newline)
        patched = if (existing != null) {
            patched.replaceRange(existing, identityObject)
        } else {
            val insertion = "$newline$entryIndent\"$key\": $identityObject,"
            patched.replaceRange(staticProps.first + 1, staticProps.first + 1, insertion)
        }
    }
    return patched
}

private val nativeIdentityResourceIdsPatch = resourcePatch {
    execute {
        // Standalone identity still reaches Morphe's resource processor through its legacy
        // dependency graph, although those mutations are skipped on this native-XR build.
        // The stock APK has no ids.xml. Create the required empty document in the decoded
        // resource context, preserving earlier verified builds and existing ID declarations.
        if (packageMetadata.versionName != "2.0.23" || packageMetadata.versionCode != "5002363") {
            return@execute
        }
        ensureIdsXml(get("res/values/ids.xml"))
    }
}

private fun createDeviceIdentityPatch(
    name: String,
    description: String,
    default: Boolean,
    compatibilities: List<Compatibility>,
) = rawResourcePatch(
    name = name,
    description = description,
    default = default,
) {
    category(PatchCategories.APP_AND_DEVICE_IDENTITY)
    compatibleWith(*compatibilities.toTypedArray())
    // Morphe executes dependencies without checking their compatibility. The legacy foundation is
    // therefore build-aware and becomes a mutation no-op on native builds.
    dependsOn(xrDeviceConfigBaselinePatch, nativeIdentityResourceIdsPatch)

    execute {
        val file = get("assets/config/hmd_config.json")
        if (!isNativeXrSteamLinkBuild(packageMetadata.versionName, packageMetadata.versionCode)) {
            // Legacy bases ship per-product identity entries and select them by product name too.
            // Install the same Galaxy identity the native upsert produces. Only 2.0.20/5001712 needs
            // its headset-keyed requestedExtensions block adapted to these three products.
            file.writeBytes(
                adaptLegacyHmdConfigForBuild(
                    galaxyIdentityResource(),
                    packageMetadata.versionName,
                    packageMetadata.versionCode,
                ),
            )
            return@execute
        }

        val original = file.readText()
        val patched = patchGalaxyIdentity(original)
        if (patched != original) file.writeText(patched)
    }
}

// Morphe exposes one global `default` flag per patch object, so recommendation scope is expressed
// with one object per scope over this single shared guarded body. Both scopes install the same
// Galaxy XR identity; there is no headset selector.
@Suppress("unused")
val deviceIdentityPatch = createDeviceIdentityPatch(
    name = "Device identity",
    description = "Sets the Galaxy XR headset identity SteamVR sees (model xrvst2ue) and keeps the " +
        "eye-tracking extension request. Builds: exact 2.0.20/5001712.",
    default = true,
    compatibilities = COMPATIBILITIES_STEAM_LINK_LEGACY,
)

@Suppress("unused")
val deviceIdentityNativeXrPatch = createDeviceIdentityPatch(
    name = "Device identity (2.0.23/5002363)",
    description = "Sets the Galaxy XR headset identity SteamVR sees (model xrvst2ue) on the native " +
        "Android XR build; optional, so it stays out of the recommended set. Builds: exact 2.0.23/5002363.",
    default = false,
    compatibilities = COMPATIBILITIES_STEAM_LINK_5002363,
)
