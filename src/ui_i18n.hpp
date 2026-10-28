/* Porpoise UI - the menus in the player's language.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every piece of menu text is written in English in the code and passed
 * through tr(), which finds it in the table for the chosen language (and in
 * /data/porpoise/lang/<code>.txt, where testers can correct a translation
 * without a new build). Text with a number or a name in it uses {placeholders}
 * through trf(). Game names and GameTDB's details are never translated. */
#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace porpoise::ui
{
/* In the Language setting's numbering, less one (the setting's 0 follows the
 * console): new languages go at the end, so saved settings keep theirs. */
enum class Language
{
    English,
    Spanish, /* Spain */
    French,
    Portuguese, /* Portugal */
    Italian,
    Japanese,
    SpanishLatinAmerica,
    PortugueseBrazil,
    German,
    Dutch,
    Polish,
    Russian,
    ChineseSimplified,
    ChineseTraditional,
    Korean,
    Turkish,
    Swedish,
};
constexpr int kLanguages = 17;

/* The PS5's language (sceSystemServiceParamGetInt 1), told once at start. */
void set_system_language(int ps5_language);
/* setting: 0 follows the console, else Language + 1 (1 English, 2 Spanish,
 * 3 French, 4 Portuguese, 5 Italian, 6 Japanese, 7 Latin American Spanish,
 * 8 Brazilian Portuguese, 9 German, 10 Dutch, 11 Polish, 12 Russian, 13 Simplified Chinese, 14 Traditional
 * Chinese, 15 Korean, 16 Turkish, 17 Swedish). */
void apply_language(int setting, const std::string &override_dir = "");
Language language();
/* The choices for the Language setting, each in its own language. */
const char *language_choice(int setting);
/* The settings' numbers in the order the Language setting lists them. */
const std::vector<int> &language_order();
/* The font (in assets/fonts) for characters beyond Nunito's: the current
 * language's when it is Japanese, Chinese or Korean, else the Japanese one. */
const char *cjk_font();
/* Every language's name and "System" in it, as the Language setting shows
 * them: the characters every CJK font must find somewhere. */
std::string language_names();

/* Where Porpoise's folder is: texts that name /data/porpoise name this
 * instead when it's elsewhere. */
void set_folder(const std::string &dir);
/* The text in the current language (the English itself when there is none). */
const std::string &tr(const std::string &english);
/* tr for a word that means different things in different places: looks up
 * "context|English" first. */
const std::string &trc(const char *context, const std::string &english);
/* tr, then each {name} replaced: trf("{n} games", {{"n", "9"}}). */
std::string trf(const std::string &english, std::initializer_list<std::pair<const char *, std::string>> values);
/* "1 game" / "{n} games" chosen by n, translated. A Polish or Russian
 * translation may hold "one|few|many" forms; trf picks by {n}. */
std::string plural(long long n, const char *one, const char *many);
/* A label as a button or prompt shows it: in English, Title Case ("Back Up",
 * "GameCube Cards", "Copy to B"); other languages keep their own. */
std::string title_case(const std::string &text);
} // namespace porpoise::ui
