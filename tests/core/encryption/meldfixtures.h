// Fixtures made by Meld Encrypt itself (obsidian-encrypt 3.1.1, commit cb17ad9, MIT): its own
// CryptoHelperObsolete, CryptoHelper, CryptoHelper2304(16, 16, 210000), CryptoHelperV3 and
// FileDataHelper / JsonFileEncoding run in Node (bundled with esbuild from the plugin's sources), each
// checked by the plugin's own readers before it was copied here. `Legacy*` are the plugin's own
// smoke-test fixtures (tests/fixtures/smoke: a released v2 note and its beta inline marker).
#pragma once

#include <QStringView>

namespace meldfixtures {

/// The plaintext and password of every fixture below but fileV3Compact and the legacy ones.
inline constexpr QStringView Text = u"Zażółć gęślą jaźń 🥑\nDruga linia — [[Tajny projekt]] #sekret";
inline constexpr QStringView Password = u"hasło-żółw 🔑";
/// v0 (obsolete) payload
inline constexpr QStringView InlineV0 = u"+bFtH6Gldeu9TzwBrdYYND5+L1oY+xGJ2cVFPkO2x/VvKbfozgEP6uXbfh2sRxEnIxKDx7ExK/Rw/"
                                        u"1CmI9P9Nwj5lYU7Uiec4IpZ0fF+H7qRlqk7PN59OPg=";
/// v1 (α) payload
inline constexpr QStringView InlineV1 =
        u"J9Lef77CgY4RsRRHp4vPyJDz/2Oz8i1r1/"
        u"J3DGwqzRHTlK5GEohgn68p5n0wYA8Dn9ssH8XHiknCPqkwItcRCGX85MCbmFDpag8RxRZxynopi5n6acR8z+K+gFjSmSvuWjzS4mT7hk6N";
/// v2 (β) payload
inline constexpr QStringView InlineV2 =
        u"RlnsQriAmH0U5/O9fiS5eb6h7YXZQH+S7KOYIBsuyi6wYRW4tDzgVE1hpbf9gvcLbUrqDLf5BRwiOyRsETstoTwyIH4Wp+p1/"
        u"lQIjQqEM2rS4jtorZ1t23yv5un/V0pJc2uKDZXddWBiuzoTFfvUx/Wtd8hPwKbwRA==";
/// v3 (γ) payload, 220 000 rounds
inline constexpr QStringView InlineV3 =
        u"AQNbYAWeS1Nspelbi7JOIG57XRSQf2Bct+v9VEmfhtn+Buq5WLJoArGo6rHtagI04U4wokv4RMgLg3kQ+B4yeweYu7mW7ntkhqP+"
        u"yjYe1otitOmpU6U8NCBxhlZ+xTYWrVB6D7BBeRppNBFA43qY3p/77I+Rnrg5Jg==";
/// v3 payload of an empty text (48 bytes)
inline constexpr QStringView InlineV3Empty = u"AQNbYPmJlfOgHnf3fMsKWhAR6WjLyM892di0Tv12fqhhGhZXt6hZShOdHuIiJyIf";
/// v3 payload with 300 000 rounds
inline constexpr QStringView InlineV3Iter300k =
        u"AQST4DzxoF4CgOSzPch51zHIj8MCIqGxp5uAB41UsVn9zn3yT93UoTCO/q6deHWT2HGuivXuwYBB6+HPAfxH5CYoRjxqxNB51Zl1wHv9Dh4/"
        u"xyPtbD1U1U7XNOJdUn+tof116TzWM0M09lwXl5F8OpiRrAv2bp9trg==";
/// A v3 note as compact JSON with the keys in another order: "Krótki" under "p", hint "podpowiedź".
inline constexpr QStringView FileV3Compact =
        u"{\"version\":\"3.0\",\"hint\":\"podpowiedź\",\"kdf\":{\"name\":\"PBKDF2\",\"hash\":\"SHA-512\","
        u"\"iterations\":220000,\"salt\":\"X8sw+aqHfZpVwhNwb3FCeA==\"},\"cipher\":{\"name\":\"AES-GCM\",\"keyLength\":"
        u"256,\"iv\":\"sESN/ySkew+rsvTC\",\"tagLength\":128},\"encodedData\":\"J5XIj76RoepgyZUtFV8QbGxOcVsS4sY=\"}";
/// A v3 note as the plugin writes it (JSON.stringify(…, null, 2)), hint "Podpowiedź: zwierzę".
inline constexpr QStringView FileV3 =
        u"{\n  \"version\": \"3.0\",\n  \"hint\": \"Podpowiedź: zwierzę\",\n  \"encodedData\": "
        u"\"p0wpuW/0QcTXi8hsOjrSzOpy0DytLlbc8PLiNvtALLnm1b1nvQLvTkWicCP2xWvCdJbC/"
        u"Q6obAw4Hl9iAsHsAbEmpTVdHp0DMYjaMoWHosXytPnYI35gS0Q=\",\n  \"kdf\": {\n    \"name\": \"PBKDF2\",\n    "
        u"\"hash\": \"SHA-512\",\n    \"iterations\": 220000,\n    \"salt\": \"40ShtIjS+ZA7VMyYOnQgdw==\"\n  },\n  "
        u"\"cipher\": {\n    \"name\": \"AES-GCM\",\n    \"keyLength\": 256,\n    \"iv\": \"y4JctzsdfbNkPWIr\",\n    "
        u"\"tagLength\": 128\n  }\n}";
/// A v2 note (hint "stara") and a v1 note (no hint), as older versions wrote them.
inline constexpr QStringView FileV2 = u"{\"version\":\"2.0\",\"hint\":\"stara\",\"encodedData\":\"yGbGtR/Go7PBQBOR/"
                                      u"W69RLIPWpWy6TBQkShP+g7NT9wC3T1BAiEwSSbXs9M5WXp3Y7gZSX64SeVD0WsYAAPhjixP/"
                                      u"S0eJYS3S3us+idQKxgrRsD3I5keVImUXikf9qyjdSuRycsm8F6OlF6TcIAj90uQKJqeffjotg==\"}";
inline constexpr QStringView FileV1 =
        u"{\"version\":\"1.0\",\"hint\":\"\",\"encodedData\":\"XDo5v6pEtpKur15ekESfUyV7syo2SRv2+"
        u"OJ1EN1WBcPvEDZuLQQQxTz76RMNmjKumy26EJ8cE3sAcOf2fnV+2nA6ZLj5dAI5FykcLEBZaTF/"
        u"zf7zRcwdk9Wx82mrT2tPghmqYtn434t7\"}";

/// The plugin's smoke fixtures: both decrypt to LegacyText with LegacyPassword.
inline constexpr QStringView LegacyPassword = u"legacy-release-password";
inline constexpr QStringView LegacyText = u"Legacy release secret café 🥑";
inline constexpr QStringView LegacyInline =
        u"%%🔐β "
        u"i/0nBeBRTz1b9fMGY8qBjeXw78V8ze0a+mHhHoY8Vr97RqHRmU0PjC4O3Y9tRndeh5HHbSGYj1n5mBnx8giGe+GwZwpt5rQdP+UPGv5n+TY= "
        u"🔐%%";
inline constexpr QStringView LegacyWhole =
        u"{\"version\":\"2.0\",\"hint\":\"legacy "
        u"test\",\"encodedData\":\"i/"
        u"0nBeBRTz1b9fMGY8qBjeXw78V8ze0a+mHhHoY8Vr97RqHRmU0PjC4O3Y9tRndeh5HHbSGYj1n5mBnx8giGe+GwZwpt5rQdP+UPGv5n+TY="
        u"\"}";

} // namespace meldfixtures

namespace meldfixtures {

/// The plugin's CryptoHelperV3 / FileDataHelper with crypto.getRandomValues giving 0, 1, 2, … (salt
/// 0…15, IV 16…27), Text under Password: what Brulion must write byte for byte with such a source.
inline constexpr QStringView DeterministicInline =
        u"AQNbYAABAgMEBQYHCAkKCwwNDg8QERITFBUWFxgZGhuoJdLDsG3VGFT4/g5IALWDrDvn/rzwelFX/"
        u"loPgWKibk9lDfvpwLwp4HuVbUHZN58ETtxWkuJzB3wB77RiUygAHrKQbVTbiI/tGbxqyflDYetGjnzuFV01sg==";
/// The same as a file, hint `Podpowiedź "z" cudzysłowem` and a tab (JSON escapes).
inline constexpr QStringView DeterministicHint = u"Podpowiedź \"z\" cudzysłowem\t";
inline constexpr QStringView DeterministicFile =
        u"{\n  \"version\": \"3.0\",\n  \"hint\": \"Podpowiedź \\\"z\\\" cudzysłowem\\t\",\n  \"encodedData\": "
        u"\"qCXSw7Bt1RhU+P4OSAC1g6w75/688HpRV/"
        u"5aD4Fiom5PZQ376cC8KeB7lW1B2TefBE7cVpLicwd8Ae+0YlMoAB6ykG1U24iP7Rm8asn5Q2HrRo"
        u"587hVdNbI=\",\n  \"kdf\": {\n    \"name\": \"PBKDF2\",\n    \"hash\": \"SHA-512\",\n    \"iterations\": "
        u"220000,\n "
        u"   \"salt\": \"AAECAwQFBgcICQoLDA0ODw==\"\n  },\n  \"cipher\": {\n    \"name\": \"AES-GCM\",\n    "
        u"\"keyLength\": "
        u"256,\n    \"iv\": \"EBESExQVFhcYGRob\",\n    \"tagLength\": 128\n  }\n}";

} // namespace meldfixtures
