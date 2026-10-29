/* Porpoise UI - the menus in the player's language.
 * Copyright (C) 2026 Ruben (Project Porpoise)
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The translations live in i18n/<language>.json; tools/make-i18n.py turns
 * them into ui_i18n_table.inc. They are first translations, to be read over by
 * native speakers: corrections go in /data/porpoise/lang/<code>.txt (es.txt,
 * es-419.txt, fr.txt, pt.txt, pt-BR.txt, it.txt, de.txt, nl.txt, pl.txt,
 * ru.txt, sv.txt, ja.txt) as "English = translation" lines and win over the table, so a
 * fix needs no new build. */
#include "ui_i18n.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace porpoise::ui
{
namespace
{
/* The English text, then the languages after English in Language's order;
 * nullptr where a language doesn't have it yet. */
struct Entry
{
    const char *en;
    const char *text[kLanguages - 1];
};

const Entry kTable[] = {
#include "ui_i18n_table.inc"
};

/* "System" in each language, in Language's order. */
const char *const kSystemChoice[kLanguages] = {
    "System", "Sistema", "Syst\xC3\xA8me", "Sistema", "Sistema", "\xE6\x9C\xAC\xE4\xBD\x93\xE3\x81\xAE\xE8\xA8\xAD\xE5\xAE\x9A",
    "Sistema", "Sistema", "System", "Systeem", "Systemowy", "\xD0\xA1\xD0\xB8\xD1\x81\xD1\x82\xD0\xB5\xD0\xBC\xD0\xBD\xD1\x8B\xD0\xB9",
    "\xE8\xB7\x9F\xE9\x9A\x8F\xE7\xB3\xBB\xE7\xBB\x9F", "\xE8\xB7\x9F\xE9\x9A\xA8\xE7\xB3\xBB\xE7\xB5\xB1", "\xEC\x8B\x9C\xEC\x8A\xA4\xED\x85\x9C \xEC\x84\xA4\xEC\xA0\x95", "Sistem", "System"};

/* Each language by its own name, in Language's order. */
const char *const kNames[kLanguages] = {
    "English",
    "Espa\xC3\xB1ol (Espa\xC3\xB1" "a)",
    "Fran\xC3\xA7" "ais",
    "Portugu\xC3\xAAs (Portugal)",
    "Italiano",
    "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
    "Espa\xC3\xB1ol (Latinoam\xC3\xA9rica)",
    "Portugu\xC3\xAAs (Brasil)",
    "Deutsch",
    "Nederlands",
    "Polski",
    "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9",
    "\xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87", /* Chinese, Simplified */
    "\xE7\xB9\x81\xE9\xAB\x94\xE4\xB8\xAD\xE6\x96\x87", /* Chinese, Traditional */
    "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4",
    "T\xC3\xBCrk\xC3\xA7" "e",
    "Svenska",
};

/* Codes for the testers' correction files: lang/<code>.txt. */
const char *const kCodes[kLanguages] = {"en", "es", "fr", "pt", "it", "ja", "es-419", "pt-BR", "de", "nl", "pl", "ru", "zh-Hans", "zh-Hant", "ko", "tr", "sv"};

int g_system = 1; /* the PS5's language id */
Language g_lang = Language::English;
std::unordered_map<std::string, std::string> g_map; /* English -> current language */
/* Texts name Porpoise's folder as /data/porpoise; when it is somewhere else
 * (moved to another drive, or the app's own folder in the sandbox) they name
 * where it really is. The rewritten texts are kept here (node-based, so a
 * reference to one stays good). */
const std::string kUsualFolder = "/data/porpoise";
std::string g_folder;
std::unordered_map<std::string, std::string> g_folder_texts;

const std::string &with_folder(const std::string &text)
{
    if (g_folder.empty() || text.find(kUsualFolder) == std::string::npos)
        return text;
    const auto it = g_folder_texts.find(text);
    if (it != g_folder_texts.end())
        return it->second;
    std::string out = text;
    for (std::size_t at = out.find(kUsualFolder); at != std::string::npos; at = out.find(kUsualFolder, at + g_folder.size()))
        out.replace(at, kUsualFolder.size(), g_folder);
    return g_folder_texts.emplace(text, std::move(out)).first->second;
}
std::string g_empty;

const char *pick(const Entry &e, Language l)
{
    const int i = int(l);
    return i > 0 && i < kLanguages ? e.text[i - 1] : nullptr;
}

Language from_ps5(int id)
{
    /* The PS5's language ids (as the PS4's): 0 Japanese, 2/22 French,
     * 3 Spanish, 20 Spanish (Latin America), 4 German, 5 Italian, 6 Dutch,
     * 7 Portuguese, 17 Portuguese (Brazil), 8 Russian, 16 Polish; everything
     * else English. */
    switch (id)
    {
    case 2:
    case 22: return Language::French;
    case 3: return Language::Spanish;
    case 20: return Language::SpanishLatinAmerica;
    case 7: return Language::Portuguese;
    case 17: return Language::PortugueseBrazil;
    case 5: return Language::Italian;
    case 0: return Language::Japanese;
    case 4: return Language::German;
    case 6: return Language::Dutch;
    case 16: return Language::Polish;
    case 8: return Language::Russian;
    case 11: return Language::ChineseSimplified;
    case 10: return Language::ChineseTraditional;
    case 9: return Language::Korean;
    case 19: return Language::Turkish;
    case 13: return Language::Swedish;
    default: return Language::English;
    }
}

const char *code(Language l)
{
    const int i = int(l);
    return i >= 0 && i < kLanguages ? kCodes[i] : "en";
}

/* Which of "one|few|many" a count takes: Polish and Russian. */
int plural_form(long long n)
{
    const long long a = n < 0 ? -n : n, d = a % 10, h = a % 100;
    if (g_lang == Language::Russian)
        return d == 1 && h != 11 ? 0 : (d >= 2 && d <= 4 && (h < 12 || h > 14)) ? 1 : 2;
    if (g_lang == Language::Polish)
        return a == 1 ? 0 : (d >= 2 && d <= 4 && (h < 12 || h > 14)) ? 1 : 2;
    return a == 1 ? 0 : 2;
}

/* The form of a "one|few|many" text that a count takes. */
std::string choose_form(const std::string &s, long long n)
{
    if (s.find('|') == std::string::npos)
        return s;
    std::vector<std::string> forms;
    std::size_t from = 0;
    for (std::size_t bar = s.find('|'); bar != std::string::npos; bar = s.find('|', from))
    {
        forms.push_back(s.substr(from, bar - from));
        from = bar + 1;
    }
    forms.push_back(s.substr(from));
    const std::size_t i = std::size_t(plural_form(n));
    return forms[std::min(i, forms.size() - 1)];
}
} // namespace

void set_system_language(int ps5_language)
{
    g_system = ps5_language;
}

void apply_language(int setting, const std::string &override_dir)
{
    g_lang = setting >= 1 && setting <= kLanguages ? Language(setting - 1) : from_ps5(g_system);
    g_map.clear();
    /* Latin American Spanish and Brazilian Portuguese stand on Spain's and
     * Portugal's: a text one doesn't have yet shows in the other, not English. */
    const Language base = g_lang == Language::SpanishLatinAmerica ? Language::Spanish
                          : g_lang == Language::PortugueseBrazil  ? Language::Portuguese
                                                                  : g_lang;
    if (g_lang != Language::English)
        for (const Entry &e : kTable)
            if (const char *t = pick(e, g_lang) ? pick(e, g_lang) : pick(e, base))
                g_map[e.en] = t;
    /* A tester's corrections: "English = translation" lines. */
    if (!override_dir.empty() && g_lang != Language::English)
        if (std::FILE *f = std::fopen((override_dir + "/" + code(g_lang) + ".txt").c_str(), "r"))
        {
            char line[1024];
            while (std::fgets(line, sizeof line, f))
            {
                std::string s = line;
                while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                    s.pop_back();
                const auto eq = s.find(" = ");
                if (s.empty() || s[0] == '#' || eq == std::string::npos)
                    continue;
                g_map[s.substr(0, eq)] = s.substr(eq + 3);
            }
            std::fclose(f);
        }
}

Language language()
{
    return g_lang;
}

const char *language_choice(int setting)
{
    static std::string system;
    if (setting >= 1 && setting <= kLanguages)
        return kNames[setting - 1];
    system = kSystemChoice[int(g_lang)];
    return system.c_str();
}

const std::vector<int> &language_order()
{
    /* As the PS5 lists them: by their own names. */
    static const std::vector<int> order = {0, 1, 9, 2, 7, 3, 5, 10, 11, 4, 8, 17, 16, 12, 6, 15, 13, 14};
    return order;
}

const char *cjk_font()
{
    switch (g_lang)
    {
    case Language::ChineseSimplified: return "NotoSansSC-Porpoise.ttf";
    case Language::ChineseTraditional: return "NotoSansTC-Porpoise.ttf";
    case Language::Korean: return "NotoSansKR-Porpoise.ttf";
    default: return "NotoSansJP-Porpoise.ttf";
    }
}

std::string language_names()
{
    std::string all;
    for (int i = 0; i < kLanguages; ++i)
        all += std::string(kNames[i]) + kSystemChoice[i];
    return all;
}

void set_folder(const std::string &dir)
{
    std::string d = dir;
    while (d.size() > 1 && d.back() == '/')
        d.pop_back();
    g_folder = d == kUsualFolder ? std::string() : d;
    g_folder_texts.clear();
}

const std::string &tr(const std::string &english)
{
    if (g_lang == Language::English)
        return with_folder(english);
    const auto it = g_map.find(english);
    return with_folder(it == g_map.end() ? english : it->second);
}

const std::string &trc(const char *context, const std::string &english)
{
    if (g_lang == Language::English)
        return with_folder(english);
    const auto it = g_map.find(std::string(context) + "|" + english);
    return it == g_map.end() ? tr(english) : with_folder(it->second);
}

std::string trf(const std::string &english, std::initializer_list<std::pair<const char *, std::string>> values)
{
    std::string s = tr(english);
    /* Polish and Russian count in three forms: "one|few|many". */
    for (const auto &v : values)
        if (std::strcmp(v.first, "n") == 0 && s.find('|') != std::string::npos)
            s = choose_form(s, std::atoll(v.second.c_str()));
    for (const auto &v : values)
    {
        const std::string key = std::string("{") + v.first + "}";
        for (std::size_t at = s.find(key); at != std::string::npos; at = s.find(key, at + v.second.size()))
            s.replace(at, key.size(), v.second);
    }
    return s;
}

std::string plural(long long n, const char *one, const char *many)
{
    if (n == 1)
        return tr(one);
    return trf(many, {{"n", std::to_string(n)}});
}

std::string title_case(const std::string &text)
{
    if (language() != Language::English)
        return text;
    static const char *const kSmall[] = {"a", "an", "the", "and", "or", "of", "to", "in", "on", "at", "for", "by", "with", "from"};
    std::string out = text;
    std::size_t i = 0;
    bool first = true;
    while (i < out.size())
    {
        while (i < out.size() && out[i] == ' ')
            ++i;
        std::size_t j = i;
        while (j < out.size() && out[j] != ' ')
            ++j;
        if (j > i)
        {
            const std::string word = out.substr(i, j - i);
            bool small = false;
            for (const char *k : kSmall)
                small = small || word == k;
            if ((first || !small) && out[i] >= 'a' && out[i] <= 'z')
                out[i] = char(out[i] - 'a' + 'A');
            first = false;
        }
        i = j;
    }
    return out;
}
} // namespace porpoise::ui
