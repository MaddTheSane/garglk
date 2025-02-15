/******************************************************************************
 *                                                                            *
 * Copyright (C) 2006-2009 by Tor Andersson.                                  *
 * Copyright (C) 2010 by Ben Cressey.                                         *
 *                                                                            *
 * This file is part of Gargoyle.                                             *
 *                                                                            *
 * Gargoyle is free software; you can redistribute it and/or modify           *
 * it under the terms of the GNU General Public License as published by       *
 * the Free Software Foundation; either version 2 of the License, or          *
 * (at your option) any later version.                                        *
 *                                                                            *
 * Gargoyle is distributed in the hope that it will be useful,                *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with Gargoyle; if not, write to the Free Software                    *
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA *
 *                                                                            *
 *****************************************************************************/

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "glk.h"
#include "glkstart.h"
#include "garglk.h"

bool gli_utf8input = false;
bool gli_utf8output = false;

struct gli_font_files gli_conf_prop, gli_conf_mono, gli_conf_prop_override, gli_conf_mono_override;

std::string gli_conf_monofont = "Gargoyle Mono";
std::string gli_conf_propfont = "Gargoyle Serif";
float gli_conf_monosize = 12.6;	/* good size for Gargoyle Mono */
float gli_conf_propsize = 14.7;	/* good size for Gargoyle Serif */

style_t gli_tstyles[style_NUMSTYLES] =
{
    {PROPR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Normal */
    {PROPI, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Emphasized */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Preformatted */
    {PROPB, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Header */
    {PROPB, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Subheader */
    {PROPZ, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Alert */
    {PROPR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Note */
    {PROPR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* BlockQuote */
    {PROPB, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Input */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* User1 */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* User2 */
};

style_t gli_gstyles[style_NUMSTYLES] =
{
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Normal */
    {MONOI, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Emphasized */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Preformatted */
    {MONOB, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Header */
    {MONOB, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Subheader */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Alert */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Note */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* BlockQuote */
    {MONOR, {0xff,0xff,0xff}, {0x00,0x00,0x00}, 0}, /* Input */
    {MONOR, {0x60,0x60,0x60}, {0xff,0xff,0xff}, 0}, /* User1 */
    {MONOR, {0x60,0x60,0x60}, {0xff,0xff,0xff}, 0}, /* User2 */
};

style_t gli_tstyles_def[style_NUMSTYLES];
style_t gli_gstyles_def[style_NUMSTYLES];

static int font2idx(const std::string &font)
{
    if (font == "monor") return MONOR;
    if (font == "monob") return MONOB;
    if (font == "monoi") return MONOI;
    if (font == "monoz") return MONOZ;
    if (font == "propr") return PROPR;
    if (font == "propb") return PROPB;
    if (font == "propi") return PROPI;
    if (font == "propz") return PROPZ;
    return MONOR;
}

float gli_conf_gamma = 1.0;

unsigned char gli_window_color[3] = { 0xff, 0xff, 0xff };
unsigned char gli_caret_color[3] = { 0x00, 0x00, 0x00 };
unsigned char gli_border_color[3] = { 0x00, 0x00, 0x00 };
unsigned char gli_more_color[3] = { 0x00, 0x00, 0x00 };
unsigned char gli_link_color[3] = { 0x00, 0x00, 0x60 };

unsigned char gli_window_save[3] = { 0xff, 0xff, 0xff };
unsigned char gli_caret_save[3] = { 0x00, 0x00, 0x00 };
unsigned char gli_border_save[3] = { 0x00, 0x00, 0x00 };
unsigned char gli_more_save[3] = { 0x00, 0x00, 0x00 };
unsigned char gli_link_save[3] = { 0x00, 0x00, 0x60 };

bool gli_override_fg_set = false;
int gli_override_fg_val = 0;
bool gli_override_bg_set = false;
int gli_override_bg_val = 0;
bool gli_override_reverse = false;

static std::string base_more_prompt = "— more —";
glui32 *gli_more_prompt;
glui32 gli_more_prompt_len;
int gli_more_align = 0;
int gli_more_font = PROPB;

unsigned char gli_scroll_bg[3] = { 0xb0, 0xb0, 0xb0 };
unsigned char gli_scroll_fg[3] = { 0x80, 0x80, 0x80 };
int gli_scroll_width = 0;

int gli_caret_shape = 2;
int gli_link_style = 1;

bool gli_conf_lcd = true;
unsigned char gli_conf_lcd_weights[5] = {28, 56, 85, 56, 28};

int gli_wmarginx = 15;
int gli_wmarginy = 15;
int gli_wpaddingx = 0;
int gli_wpaddingy = 0;
int gli_wborderx = 1;
int gli_wbordery = 1;
int gli_tmarginx = 7;
int gli_tmarginy = 7;

int gli_wmarginx_save = 15;
int gli_wmarginy_save = 15;

int gli_cols = 60;
int gli_rows = 25;

bool gli_conf_lockcols = false;
bool gli_conf_lockrows = false;

float gli_conf_propaspect = 1.0;
float gli_conf_monoaspect = 1.0;

int gli_baseline = 15;
int gli_leading = 20;

bool gli_conf_justify = false;
int gli_conf_quotes = 1;
int gli_conf_dashes = 1;
int gli_conf_spaces = 0;
bool gli_conf_caps = false;

bool gli_conf_graphics = true;
bool gli_conf_sound = true;
bool gli_conf_speak = false;
bool gli_conf_speak_input = false;
std::string gli_conf_speak_language;

bool gli_conf_fullscreen = false;

bool gli_conf_stylehint = true;
bool gli_conf_safeclicks = false;

std::string garglk::downcase(const std::string &string)
{
    std::string lowered;

    for (const auto &c : string)
        lowered.push_back(std::tolower(static_cast<unsigned char>(c)));

    return lowered;
}

static void parsecolor(const std::string &str, unsigned char *rgb)
{
    std::string r, g, b;

    if (str.size() != 6)
        return;

    r = str.substr(0, 2);
    g = str.substr(2, 2);
    b = str.substr(4, 2);

    rgb[0] = std::stoi(r, nullptr, 16);
    rgb[1] = std::stoi(g, nullptr, 16);
    rgb[2] = std::stoi(b, nullptr, 16);
}

// Return a vector of all possible config files. This is in order of
// highest priority to lowest (i.e. earlier entries should take
// precedence over later entries).
//
// The following is the list of locations tried:
//
// 1. Name of game file with extension replaced by .ini (e.g. zork1.z3
//    becomes zork1.ini)
// 2. <directory containing game file>/garglk.ini
// 3. <current directory>/garglk.ini
// 4. $XDG_CONFIG_HOME/garglk.ini or $HOME/.config/garglk.ini
// 5. $HOME/.garglkrc
// 6. $HOME/garglk.ini
// 7. $GARGLK_INI/.garglk
// 8. $GARGLK_INI/garglk.ini
// 9. /etc/garglk.ini (or other location set at build time)
// 10. <directory containing gargoye executable>/garglk.ini
//
// exedir is the directory containing the gargoyle executable
// gamepath is the path to the game file being run
std::vector<std::string> garglk::configs(const std::string &exedir, const std::string &gamepath)
{
    std::vector<std::string> configs;
    if (!gamepath.empty())
    {
        std::string config;

        // game file .ini
        config = gamepath;
        auto dot = config.rfind('.');
        if (dot != std::string::npos)
            config.replace(dot, std::string::npos, ".ini");
        else
            config += ".ini";

        configs.push_back(config);

        // game directory .ini
        config = gamepath;
        auto slash = config.find_last_of("/\\");
        if (slash != std::string::npos)
            config.replace(slash + 1, std::string::npos, "garglk.ini");
        else
            config = "garglk.ini";

        configs.push_back(config);
    }

    // current directory .ini
    configs.push_back("garglk.ini");

    // XDG Base Directory Specification
    std::string xdg_path;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr)
    {
        xdg_path = xdg;
    }
    else
    {
        const char *home = getenv("HOME");
        if (home != nullptr)
            xdg_path = std::string(home) + "/.config";
    }

    if (!xdg_path.empty())
        configs.push_back(xdg_path + "/garglk.ini");

    // Various environment directories
    //
    // $GARGLK_INI may be set to a platform-specific path by the
    // launcher. At the moment it's only used on macOS to point inside
    // the bundle to a directory containing the a default garglk.ini.
    std::vector<const char *> env_vars = {"HOME", "GARGLK_INI"};
    for (const auto &var : env_vars)
    {
        const char *dir = std::getenv(var);
        if (dir != nullptr)
        {
            configs.push_back(std::string(dir) + "/.garglkrc");
            configs.push_back(std::string(dir) + "/garglk.ini");
        }
    }

    // system directory
    configs.push_back(GARGLKINI);

    // install directory
    if (!exedir.empty())
        configs.push_back(exedir + "/garglk.ini");

    return configs;
}

void garglk::config_entries(const std::string &fname, bool accept_bare, const std::vector<std::string> &matches, std::function<void(const std::string &cmd, const std::string &arg)> callback)
{
    std::string line;
    bool accept = accept_bare;

    std::ifstream f(fname);
    if (!f.is_open())
        return;

    while (std::getline(f >> std::ws, line))
    {
        auto comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);
        line.erase(line.find_last_not_of(" \t\r") + 1);

        if (line.empty())
            continue;

        if (line[0] == '[' && line.back() == ']')
        {
            accept = std::any_of(matches.begin(), matches.end(),[&line](const std::string match) {
                return garglk::downcase(line).find(garglk::downcase(match)) != std::string::npos;
            });
            continue;
        }

        if (!accept)
            continue;

        std::string cmd;
        std::stringstream linestream(line);

        if (linestream >> cmd)
        {
            std::string arg;
            std::set<std::string> singlearg = {
                "tcolor", "gcolor",
                "tfont", "gfont",
                "monofont", "propfont",
                "monor", "monob", "monoi", "monoz",
                "propr", "propb", "propi", "propz",
                "lcdweights",
                "moreprompt",
                "terp",
            };

            if (std::any_of(singlearg.begin(), singlearg.end(), [&cmd](const std::string &key) { return key == cmd; }))
                std::getline(linestream >> std::ws, arg);
            else
                linestream >> arg;

            if (linestream)
                callback(cmd, arg);
        }
    }
}

static void readoneconfig(const std::string &fname, const std::string &argv0, const std::string &gamefile)
{
    std::vector<std::string> matches = {argv0, gamefile};

    garglk::config_entries(fname, true, matches, [](const std::string &cmd, const std::string &arg) {
        if (cmd == "moreprompt") {
            base_more_prompt = arg;
        } else if (cmd == "morecolor") {
            parsecolor(arg, gli_more_color);
            parsecolor(arg, gli_more_color);
        } else if (cmd == "morefont") {
            gli_more_font = font2idx(arg);
        } else if (cmd == "morealign") {
            gli_more_align = std::stoi(arg);
        } else if (cmd == "monoaspect") {
            gli_conf_monoaspect = std::stof(arg);
        } else if (cmd == "propaspect") {
            gli_conf_propaspect = std::stof(arg);
        } else if (cmd == "monosize") {
            gli_conf_monosize = std::stof(arg);
        } else if (cmd == "monor") {
            gli_conf_mono_override.r = arg;
        } else if (cmd == "monob") {
            gli_conf_mono_override.b = arg;
        } else if (cmd == "monoi") {
            gli_conf_mono_override.i = arg;
        } else if (cmd == "monoz") {
            gli_conf_mono_override.z = arg;
        } else if (cmd == "monofont") {
            gli_conf_monofont = arg;
        } else if (cmd == "propsize") {
            gli_conf_propsize = std::stof(arg);
        } else if (cmd == "propr") {
            gli_conf_prop_override.r = arg;
        } else if (cmd == "propb") {
            gli_conf_prop_override.b = arg;
        } else if (cmd == "propi") {
            gli_conf_prop_override.i = arg;
        } else if (cmd == "propz") {
            gli_conf_prop_override.z = arg;
        } else if (cmd == "propfont") {
            gli_conf_propfont = arg;
        } else if (cmd == "leading") {
            gli_leading = std::stof(arg) + 0.5;
        } else if (cmd == "baseline") {
            gli_baseline = std::stof(arg) + 0.5;
        } else if (cmd == "rows") {
            gli_rows = std::stoi(arg);
        } else if (cmd == "cols") {
            gli_cols = std::stoi(arg);
        } else if (cmd == "minrows") {
            int r = std::stoi(arg);
            if (gli_rows < r)
                gli_rows = r;
        } else if (cmd ==  "maxrows") {
            int r = std::stoi(arg);
            if (gli_rows > r)
                gli_rows = r;
        } else if (cmd == "mincols") {
            int r = std::stoi(arg);
            if (gli_cols < r)
                gli_cols = r;
        } else if (cmd ==  "maxcols") {
            int r = std::stoi(arg);
            if (gli_cols > r)
                gli_cols = r;
        } else if (cmd == "lockrows") {
            gli_conf_lockrows = std::stoi(arg);
        } else if (cmd == "lockcols") {
            gli_conf_lockcols = std::stoi(arg);
        } else if (cmd == "wmarginx") {
            gli_wmarginx = std::stoi(arg);
            gli_wmarginx_save = gli_wmarginx;
        } else if (cmd == "wmarginy") {
            gli_wmarginy = std::stoi(arg);
            gli_wmarginy_save = gli_wmarginy;
        } else if (cmd == "wpaddingx") {
            gli_wpaddingx = std::stoi(arg);
        } else if (cmd == "wpaddingy") {
            gli_wpaddingy = std::stoi(arg);
        } else if (cmd == "wborderx") {
            gli_wborderx = std::stoi(arg);
        } else if (cmd == "wbordery") {
            gli_wbordery = std::stoi(arg);
        } else if (cmd == "tmarginx") {
            gli_tmarginx = std::stoi(arg);
        } else if (cmd == "tmarginy") {
            gli_tmarginy = std::stoi(arg);
        } else if (cmd == "gamma") {
            gli_conf_gamma = std::stof(arg);
        } else if (cmd == "caretcolor") {
            parsecolor(arg, gli_caret_color);
            parsecolor(arg, gli_caret_save);
        } else if (cmd == "linkcolor") {
            parsecolor(arg, gli_link_color);
            parsecolor(arg, gli_link_save);
        } else if (cmd == "bordercolor") {
            parsecolor(arg, gli_border_color);
            parsecolor(arg, gli_border_save);
        } else if (cmd == "windowcolor") {
            parsecolor(arg, gli_window_color);
            parsecolor(arg, gli_window_save);
        } else if (cmd == "lcd") {
            gli_conf_lcd = std::stoi(arg);
        } else if (cmd == "lcdfilter") {
            garglk::set_lcdfilter(arg);
        } else if (cmd == "lcdweights") {
            std::stringstream argstream(arg);
            int weight;
            std::vector<unsigned char> weights;

            while (argstream >> weight)
                weights.push_back(weight);

            if (weights.size() == 5)
                std::memcpy(gli_conf_lcd_weights, &weights[0], sizeof gli_conf_lcd_weights);
        } else if (cmd == "caretshape") {
            gli_caret_shape = std::stoi(arg);
        } else if (cmd == "linkstyle") {
            gli_link_style = !!std::stoi(arg);
        } else if (cmd == "scrollwidth") {
            gli_scroll_width = std::stoi(arg);
        } else if (cmd == "scrollbg") {
            parsecolor(arg, gli_scroll_bg);
        } else if (cmd == "scrollfg") {
            parsecolor(arg, gli_scroll_fg);
        } else if (cmd == "justify") {
            gli_conf_justify = std::stoi(arg);
        } else if (cmd == "quotes") {
            gli_conf_quotes = std::stoi(arg);
        } else if (cmd == "dashes") {
            gli_conf_dashes = std::stoi(arg);
        } else if (cmd == "spaces") {
            gli_conf_spaces = std::stoi(arg);
        } else if (cmd == "caps") {
            gli_conf_caps = std::stoi(arg);
        } else if (cmd == "graphics") {
            gli_conf_graphics = std::stoi(arg);
        } else if (cmd == "sound") {
            gli_conf_sound = std::stoi(arg);
        } else if (cmd == "fullscreen") {
            gli_conf_fullscreen = std::stoi(arg);
        } else if (cmd == "speak") {
            gli_conf_speak = std::stoi(arg);
        } else if (cmd == "speak_input") {
            gli_conf_speak_input = std::stoi(arg);
        } else if (cmd == "speak_language") {
            gli_conf_speak_language = arg;
        } else if (cmd == "stylehint") {
            gli_conf_stylehint = std::stoi(arg);
        } else if (cmd == "safeclicks") {
            gli_conf_safeclicks = std::stoi(arg);
        } else if (cmd == "tcolor" || cmd == "gcolor") {
            std::stringstream argstream(arg);
            std::string style, fg, bg;

            if (argstream >> style >> fg >> bg)
            {
                style_t *styles = cmd[0] == 't' ? gli_tstyles : gli_gstyles;

                if (style == "*")
                {
                    for (int i = 0; i < style_NUMSTYLES; i++)
                    {
                        parsecolor(fg, styles[i].fg);
                        parsecolor(bg, styles[i].bg);
                    }
                }
                else
                {
                    int i = std::stoi(style);

                    if (i >= 0 && i < style_NUMSTYLES)
                    {
                        parsecolor(fg, styles[i].fg);
                        parsecolor(bg, styles[i].bg);
                    }
                }
            }
        } else if (cmd == "tfont" || cmd == "gfont") {
            std::stringstream argstream(arg);
            std::string style, font;

            if (argstream >> style >> font)
            {
                int i = std::stoi(style);

                if (i >= 0 && i < style_NUMSTYLES)
                {
                    if (cmd[0] == 't')
                        gli_tstyles[i].font = font2idx(font);
                    else
                        gli_gstyles[i].font = font2idx(font);
                }
            }
        }
    });
}

static void gli_read_config(int argc, char **argv)
{
    auto basename = [](std::string path) {
        auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos)
            path.erase(0, slash + 1);

        return path;
    };

    /* load argv0 with name of executable without suffix */
    std::string argv0 = basename(argv[0]);
    auto dot = argv0.rfind('.');
    if (dot != std::string::npos)
        argv0.erase(dot);

    /* load gamefile with basename of last argument */
    std::string gamefile = basename(argv[argc - 1]);

    /* load exefile with directory containing main executable */
    std::string exedir = argv[0];
    exedir = exedir.substr(0, exedir.find_last_of("/\\"));

    /* load gamepath with the path to the story file itself */
    std::string gamepath;
    if (argc > 1)
        gamepath = argv[argc - 1];

    /* load from all config files */
    auto configs = garglk::configs(exedir, gamepath);
    std::reverse(configs.begin(), configs.end());

    for (const auto &config : configs)
        readoneconfig(config, argv0, gamefile);
}

strid_t glkunix_stream_open_pathname(char *pathname, glui32 textmode, glui32 rock)
{
    return gli_stream_open_pathname(pathname, (textmode != 0), rock);
}

void gli_startup(int argc, char *argv[])
{
    wininit(&argc, argv);

    if (argc > 1)
        glkunix_set_base_file(argv[argc-1]);

    gli_read_config(argc, argv);

    gli_more_prompt = new glui32[1 + base_more_prompt.size()];
    gli_more_prompt_len = gli_parse_utf8(reinterpret_cast<const unsigned char *>(base_more_prompt.data()), base_more_prompt.size(), gli_more_prompt, base_more_prompt.size());

    std::memcpy(gli_tstyles_def, gli_tstyles, sizeof(gli_tstyles_def));
    std::memcpy(gli_gstyles_def, gli_gstyles, sizeof(gli_gstyles_def));

    if (!gli_baseline)
        gli_baseline = gli_conf_propsize + 0.5;

    gli_initialize_tts();
    if (gli_conf_speak)
        gli_conf_quotes = 0;

    gli_initialize_misc();
    gli_initialize_fonts();
    gli_initialize_windows();
    gli_initialize_sound();

    winopen();
    gli_initialize_babel();
}
