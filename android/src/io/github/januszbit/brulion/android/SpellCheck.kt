// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.textservice.SentenceSuggestionsInfo
import android.view.textservice.SpellCheckerSession
import android.view.textservice.SuggestionsInfo
import android.view.textservice.TextInfo
import android.view.textservice.TextServicesManager
import org.json.JSONArray
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executor
import java.util.concurrent.TimeUnit

/**
 * The system's spell checker (the keyboard's dictionaries: [TextServicesManager],
 * [SpellCheckerSession]) behind the `spell*` functions of [BrulionAndroid]
 * (`src/platform/spell_android.cpp`; docs/UI.md "Spell checking").
 *
 * C++ calls from its spell-checking worker thread and waits for the answer: each request is posted
 * to a [HandlerThread] of its own — a session delivers its callbacks on the looper it was made on
 * (an [java.util.concurrent.Executor] of it on Android 12+) — and the caller blocks on a latch, at
 * most [TIMEOUT_MS]. A session that did not answer in time is closed (a late answer must not be
 * taken for the next request's) and opened again by the next request. One session per language
 * (`Locale(language)`, not the system's language settings: Brulion chooses the languages); a
 * word is wrong only when the checker says it looks like a typo — a checker that knows nothing
 * about a language marks nothing.
 */
internal object SpellCheck {
  private const val TAG = "BrulionSpellCheck"

  /** How long C++ waits for one answer of the checker (it runs in another process). */
  private const val TIMEOUT_MS = 3000L

  private val thread: HandlerThread by lazy { HandlerThread("BrulionSpellCheck").apply { start() } }
  private val handler: Handler by lazy { Handler(thread.looper) }

  /** One open session and the answer it is waiting for. */
  private class Session : SpellCheckerSession.SpellCheckerSessionListener {
    var session: SpellCheckerSession? = null
    @Volatile var latch: CountDownLatch? = null
    @Volatile var answer: Array<SentenceSuggestionsInfo?>? = null

    override fun onGetSuggestions(results: Array<SuggestionsInfo?>?) {}

    override fun onGetSentenceSuggestions(results: Array<SentenceSuggestionsInfo?>?) {
      answer = results
      latch?.countDown()
    }
  }

  /** Open sessions by language (guarded by itself). */
  private val sessions = HashMap<String, Session>()

  private fun manager(context: Context): TextServicesManager? =
    context.getSystemService(Context.TEXT_SERVICES_MANAGER_SERVICE) as? TextServicesManager

  /** Runs [block] on the spell checker's thread and waits for it (at most [TIMEOUT_MS]). */
  private fun <T> onThread(block: () -> T?): T? {
    val done = CountDownLatch(1)
    var result: T? = null
    handler.post {
      try {
        result = block()
      } catch (e: Exception) {
        Log.w(TAG, "spell checker: $e")
      } finally {
        done.countDown()
      }
    }
    return if (done.await(TIMEOUT_MS, TimeUnit.MILLISECONDS)) result else null
  }

  @Suppress("DEPRECATION")
  private fun open(context: Context, language: String): Session? =
    onThread {
      val tsm = manager(context) ?: return@onThread null
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !tsm.isSpellCheckerEnabled) return@onThread null
      val holder = Session()
      val locale = Locale.forLanguageTag(language)
      holder.session =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
          val params =
            SpellCheckerSession.SpellCheckerSessionParams.Builder()
              .setLocale(locale)
              .setShouldReferToSpellCheckerLanguageSettings(false)
              .setSupportedAttributes(
                SuggestionsInfo.RESULT_ATTR_LOOKS_LIKE_TYPO or SuggestionsInfo.RESULT_ATTR_HAS_RECOMMENDED_SUGGESTIONS,
              )
              .build()
          tsm.newSpellCheckerSession(params, Executor { handler.post(it) }, holder)
        } else {
          tsm.newSpellCheckerSession(null, locale, holder, false)
        }
      if (holder.session == null) null else holder
    }

  private fun session(context: Context, language: String): Session? =
    synchronized(sessions) {
      sessions[language] ?: open(context, language)?.also { sessions[language] = it }
    }

  private fun drop(language: String, holder: Session) {
    synchronized(sessions) {
      if (sessions[language] === holder) sessions.remove(language)
    }
    handler.post { holder.session?.close() }
  }

  /** The checker's answer for [words] (one sentence each), null when it did not answer. */
  private fun ask(context: Context, language: String, words: List<String>, suggestions: Int): Array<SentenceSuggestionsInfo?>? {
    val holder = session(context, language) ?: return null
    synchronized(holder) {
      val latch = CountDownLatch(1)
      holder.answer = null
      holder.latch = latch
      val infos = Array(words.size) { TextInfo(words[it], it, it) }
      handler.post {
        try {
          holder.session?.getSentenceSuggestions(infos, suggestions)
        } catch (e: Exception) {
          Log.w(TAG, "getSentenceSuggestions: $e")
          latch.countDown()
        }
      }
      if (!latch.await(TIMEOUT_MS, TimeUnit.MILLISECONDS)) {
        Log.w(TAG, "the spell checker did not answer for $language in $TIMEOUT_MS ms")
        drop(language, holder)
        return null
      }
      holder.latch = null
      return holder.answer
    }
  }

  private fun looksLikeTypo(info: SuggestionsInfo): Boolean =
    info.suggestionsAttributes and SuggestionsInfo.RESULT_ATTR_LOOKS_LIKE_TYPO != 0

  /**
   * The languages (primary subtags, JSON array) the system's checker offers: its subtypes on
   * Android 12+, else — and when it lists none — the device's languages and English; `[]` when
   * spell checking is off in the system's settings.
   */
  fun languages(context: Context): String {
    val out = LinkedHashSet<String>()
    try {
      val tsm = manager(context) ?: return "[]"
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        if (!tsm.isSpellCheckerEnabled) return "[]"
        val info = tsm.currentSpellCheckerInfo ?: return "[]"
        for (i in 0 until info.subtypeCount) {
          val tag = info.getSubtypeAt(i).languageTag
          val primary = Locale.forLanguageTag(tag).language
          if (primary.isNotEmpty()) out.add(primary)
        }
      }
      if (out.isEmpty()) {
        val locales = context.resources.configuration.locales
        for (i in 0 until locales.size()) out.add(locales[i].language)
        out.add("en")
      }
    } catch (e: Exception) {
      Log.w(TAG, "spell checker languages: $e")
      return "[]"
    }
    return JSONArray(out.filter { it.isNotEmpty() }).toString()
  }

  /** A session for [language] could be opened (the system has a spell checker on). */
  fun available(context: Context, language: String): Boolean = session(context, language) != null

  /** JSON array of booleans (true: right) for the JSON array [wordsJson]; null when it did not answer. */
  fun check(context: Context, language: String, wordsJson: String): String? {
    val array = JSONArray(wordsJson)
    val words = List(array.length()) { array.getString(it) }
    if (words.isEmpty()) return "[]"
    val answer = ask(context, language, words, 0) ?: return null
    val out = JSONArray()
    for (i in words.indices) {
      val sentence = answer.getOrNull(i)
      var right = true
      if (sentence != null) {
        for (k in 0 until sentence.suggestionsCount) {
          if (looksLikeTypo(sentence.getSuggestionsInfoAt(k))) right = false
        }
      }
      out.put(right)
    }
    return out.toString()
  }

  /** Up to [max] corrections of [word] (JSON array; empty when there are none or no answer). */
  fun suggest(context: Context, language: String, word: String, max: Int): String {
    val answer = ask(context, language, listOf(word), max) ?: return "[]"
    val out = LinkedHashSet<String>()
    for (sentence in answer) {
      if (sentence == null) continue
      for (k in 0 until sentence.suggestionsCount) {
        val info = sentence.getSuggestionsInfoAt(k)
        for (j in 0 until info.suggestionsCount) {
          val s = info.getSuggestionAt(j)
          if (s.isNotEmpty() && s != word) out.add(s)
        }
      }
    }
    return JSONArray(out.take(max)).toString()
  }
}
