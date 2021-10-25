// This file is part of Snownews - A lightweight console RSS newsreader
//
// Copyright (c) 2003-2004 Oliver Feiler <kiza@kcore.de>
// Copyright (c) 2021 Mike Sharov <msharov@users.sourceforge.net>
//
// Snownews is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License version 3
// as published by the Free Software Foundation.
//
// Snownews is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Snownews. If not, see http://www.gnu.org/licenses/.

#include "main.h"
#include "about.h"
#include "uiutil.h"
#include <ncurses.h>
#include <sys/time.h>

//----------------------------------------------------------------------

// Santa Hunta
struct shot {
    unsigned x;
    unsigned y;
    bool fired;
};
struct santa {
    int x;
    unsigned y;
    unsigned height;
    unsigned length;
    unsigned speed;
    unsigned anim;
    const char* gfx;
    const char* gfx_line2;
    const char* altgfx;
    const char* altgfx_line2;
};
struct scoreDisplay {
    int score;
    int x;
    int y;
    int rounds;
};

//----------------------------------------------------------------------

// Generate a random number in given range
static unsigned nrand (unsigned r)
{
    return rand() % r;
}

static void mvadd_center_utf8 (unsigned line, const char* text)
{
    mvadd_utf8 (line, (COLS - utf8_length (text)) / 2u, text);
}

//----------------------------------------------------------------------
// Santa Hunta

static void SHDrawGun (unsigned gun_pos)
{
    move (LINES - 3, 0);
    clrtoeol();
    move (LINES - 2, 0);
    clrtoeol();

    attron (WA_BOLD);
    mvaddstr (LINES - 3, gun_pos - 3, "___/\\___");
    mvaddstr (LINES - 2, gun_pos - 3, "|######|");
    attroff (WA_BOLD);
}

static void SHDrawStatus (void)
{
    attron (WA_BOLD);
    char helpstr[128];
    snprintf (helpstr, sizeof (helpstr), _("Move: cursor or %c/%c; shoot: space; quit: %c"), _settings.keybindings.prev, _settings.keybindings.next, _settings.keybindings.quit);
    mvadd_utf8 (LINES - 1, 1, helpstr);
    move (LINES - 1, COLS - 1);
    attroff (WA_BOLD);
}

static void SHDrawScore (int score, int level)
{
    attron (WA_REVERSE);
    mvhline (0, 0, ' ', COLS);
    mvadd_utf8 (0, 1, _("Santa Hunta!"));

    char scorestr[16];
    snprintf (scorestr, sizeof (scorestr), _("Score: %d"), score);
    mvadd_utf8 (0, COLS - utf8_length (scorestr) - 1, scorestr);

    char levelstr[16];
    snprintf (levelstr, sizeof (levelstr), _("Level: %d"), level);
    mvadd_center_utf8 (0, levelstr);

    attroff (WA_REVERSE);
}

static void SHDrawProjectile (struct shot shot)
{
    attron (WA_BOLD);
    mvaddstr (shot.y, shot.x, "|");
    attroff (WA_BOLD);
}

static void SHDrawSanta (struct santa santa)
{
    int len = COLS - santa.x;
    if (santa.anim == 0) {
	if (santa.x < 0)
	    mvaddnstr (santa.y, 0, santa.gfx + abs (santa.x), len);
	else
	    mvaddnstr (santa.y, santa.x, santa.gfx, len);
	if (santa.x < 0)
	    mvaddnstr (santa.y + 1, 0, santa.gfx_line2 + abs (santa.x), len);
	else
	    mvaddnstr (santa.y + 1, santa.x, santa.gfx_line2, len);
    } else {
	if (santa.x < 0)
	    mvaddnstr (santa.y, 0, santa.altgfx + abs (santa.x), len);
	else
	    mvaddnstr (santa.y, santa.x, santa.altgfx, len);
	if (santa.x < 0)
	    mvaddnstr (santa.y + 1, 0, santa.altgfx_line2 + abs (santa.x), len);
	else
	    mvaddnstr (santa.y + 1, santa.x, santa.altgfx_line2, len);
    }
    attron (COLOR_PAIR (10));
    mvaddch (santa.y, santa.x + santa.length - 1, '*');
    attroff (COLOR_PAIR (10));
}

static void newSanta (struct santa* santa, unsigned level)
{
    santa->x = -27;
    santa->y = 1 + nrand (LINES - 7);
    santa->speed = level + nrand (3);
    santa->anim = 0;
    santa->height = 2;
    santa->length = 27;
    santa->gfx = "##___  __-+---+/*__-+---+/*";
    santa->gfx_line2 = "_|__|_)   /\\ /\\     /\\ /\\  ";	// Append 2 spaces!
    santa->altgfx = "##___  __-+---+/*__-+---+/*";
    santa->altgfx_line2 = "_|__|_)   /|  |\\    /|  |\\ ";	// Append 1 space!
}

static bool SHHit (const struct shot shot, const struct santa santa)
{
    return ((int) shot.x >= santa.x && shot.x <= santa.x + santa.length && (shot.y == santa.y || shot.y == santa.y + 1));
}

static unsigned SHAddScore (const struct santa santa)
{
    return 100 / santa.y * santa.speed;
}

static void SHDrawHitScore (struct scoreDisplay score)
{
    attron (WA_BOLD);
    unsigned rand_color = 10 + nrand (6);
    attron (COLOR_PAIR (rand_color));
    mvprintw (score.y, score.x, "%d", score.score);
    attroff (COLOR_PAIR (rand_color));
    attroff (WA_BOLD);
}

static void printFinalScore (unsigned score)
{
    //{{{ numbers ASCII art
    static const char numbers[10][5][8] = {
	{ "_______",
	 "|  _  |",
	 "| | | |",
	 "| |_| |",
	 "|_____|" },	       // 0
	{ "  ____ ",
	 " /   | ",
	 "/_/| | ",
	 "   | | ",
	 "   |_| " },	       // 1
	{ "_______",
	 "|___  |",
	 "| ____|",
	 "| |____",
	 "|_____|" },	       // 2
	{ "_______",
	 "|___  |",
	 " ___  |",
	 "___|  |",
	 "|_____|" },	       // 3
	{ "___ ___",
	 "| |_| |",
	 "|____ |",
	 "    | |",
	 "    |_|" },	       // 4
	{ "_______",
	 "|  ___|",
	 "|___  |",
	 "____| |",
	 "|_____|" },	       // 5
	{ "    __ ",
	 "   / / ",
	 " /    \\",
	 "|  O  /",
	 " \\___/ " },	       // 6
	{ "_______",
	 "|___  |",
	 "    | |",
	 "    | |",
	 "    |_|" },	       // 7
	{ "_______",
	 "|  _  |",
	 "| |_| |",
	 "| |_| |",
	 "|_____|" },	       // 8
	{ "  ___  ",
	 " /   \\ ",
	 "|  O  |",
	 " \\   / ",
	 " /__/ " }	       // 9
    };
    //}}}
    char numtxt[12];
    unsigned ndigits = snprintf (numtxt, 12, "%u", score);
    const int sx = (COLS - ndigits * 8) / 2u, sy = (LINES - 5) / 2u;
    for (unsigned i = 0; i < ndigits; ++i)
	for (unsigned j = 0; j < 5; ++j)
	    mvaddstr (sy + j, sx + i * 8, numbers[numtxt[i] - '0'][j]);
    refresh();
}

static void SHFinalScore (int score)
{
    attron (WA_BOLD);
    mvadd_center_utf8 ((LINES - 5) / 2u - 2, _("Final score:"));
    for (int k; (k = getch()) < ' ' || k > '~';) {
	unsigned rand_color = 10 + nrand (6);
	attron (COLOR_PAIR (rand_color));
	printFinalScore (score);
	attroff (COLOR_PAIR (rand_color));
	refresh();
    }
    attroff (WA_BOLD);
}

static void santaHunta (void)
{
    // Set ncurses halfdelay mode.
    // Max resolution is 1/10sec
    halfdelay (1);

    struct scoreDisplay scoreDisplay = { };
    struct shot shot = { 0, 0, false };
    bool santalives = false;
    struct santa santa = { };

    for (unsigned score = 0, level = 1, gun_pos = COLS / 2u, hitcount = 0, draw_delay = 0;;) {
	struct timeval before, after;
	gettimeofday (&before, NULL);
	int input = getch();
	gettimeofday (&after, NULL);

	if (after.tv_sec > before.tv_sec)
	    after.tv_usec += 1000000;

	if (!santalives) {
	    newSanta (&santa, level);
	    santalives = true;
	}
	// Pad drawing resolution to 1/10sec
	draw_delay += after.tv_usec - before.tv_usec;
	if (draw_delay > 100000) {
	    draw_delay = 0;
	    erase();
	    if (santalives)
		SHDrawSanta (santa);
	    santa.anim = !santa.anim;
	    if (santa.x >= COLS)
		santalives = false;
	    if (shot.fired)
		SHDrawProjectile (shot);
	    if (scoreDisplay.rounds > 0)
		SHDrawHitScore (scoreDisplay);
	    if (SHHit (shot, santa)) {
		if (++hitcount >= 10) {
		    hitcount = 0;
		    ++level;
		}
		santalives = false;
		scoreDisplay.x = shot.x;
		scoreDisplay.y = shot.y;
		scoreDisplay.score = SHAddScore (santa);
		scoreDisplay.rounds = 20;
		score += scoreDisplay.score;
	    }
	    santa.x += santa.speed;
	    --scoreDisplay.rounds;
	    if (!--shot.y)
		shot.fired = false;
	}

	if ((input == KEY_RIGHT) || (input == _settings.keybindings.next)) {
	    if (gun_pos < COLS - 5u)
		++gun_pos;
	} else if ((input == KEY_LEFT) || (input == _settings.keybindings.prev)) {
	    if (gun_pos > 3)
		--gun_pos;
	} else if (input == ' ') {
	    if (!shot.fired) {
		attron (WA_BOLD | COLOR_PAIR (12));
		mvaddstr (LINES - 4, gun_pos - 2, "\\***/");
		attroff (WA_BOLD | COLOR_PAIR (12));

		shot.x = gun_pos;
		shot.y = LINES - 4;
		shot.fired = true;
	    }
	} else if (input == _settings.keybindings.quit) {
	    if (score)
		SHFinalScore (score);
	    break;
	}

	SHDrawGun (gun_pos);
	SHDrawScore (score, level);
	SHDrawStatus();

	refresh();
    }
    // Leave halfdelay mode.
    cbreak();
}

void UIAbout (void)
{
    if (COLS < 80) {
	erase();
	mvadd_utf8 (0, 0, _("Need at least 80 COLS terminal, sorry!"));
	getch();
	return;
    }
    int key = 'S';
    if (!easterEgg()) {
	unsigned xpos = COLS / 2u - 40;

	erase();
	mvaddstr (2, xpos, "  ________ _______     ____ __________ _______     ______ __________   ________");
	mvaddstr (3, xpos, " /  _____/ \\      \\   /    \\\\  |    | \\\\      \\   /  ___/ \\  |    | \\ / ______/");
	mvaddstr (4, xpos, " \\____  \\   \\   |  \\ /  /\\  \\\\   |    / \\   |  \\ /     /   \\   |    / \\____  \\");
	mvaddstr (5, xpos, " /       \\  /   |   \\\\  \\/  / \\  |   /  /   |   \\\\  ___\\    \\  |   /  /       \\");
	mvaddstr (6, xpos, "/______  / / ___|___/ \\____/  /__|__/  / ___|___/ \\____ \\   /__|__/  /______  /");
	mvaddstr (7, xpos, "       \\/  \\/                          \\/              \\/                   \\/");
	char verstrbuf [128];
	snprintf (verstrbuf, sizeof(verstrbuf), _("Version %s"), SNOWNEWS_VERSTRING);
	mvadd_center_utf8 (9, verstrbuf);

	mvadd_center_utf8 (11, _("Brought to you by:"));
	mvadd_center_utf8 (13, _("Main code"));
	mvadd_center_utf8 (14, "Oliver Feiler");
	mvadd_center_utf8 (16, _("Additional code"));
	mvadd_center_utf8 (17, "Mike Sharov, Rene Puls");
	mvadd_center_utf8 (19, _("Translation team"));
	mvadd_center_utf8 (20, "Frank van der Loo, Pascal Varet, Simon Isakovic, Страхиња Радић");
	mvadd_center_utf8 (21, "Fernando J. Pereda, Marco Cova, Cheng-Lung Sung, Dmitry Petukhov");
	mvadd_center_utf8 (22, "Douglas Campos, Ray Iwata, Piotr Ozarowski, Yang Huan");
	mvadd_center_utf8 (23, "Ihar Hrachyshka, Mats Berglund, Johannes Hove-Henriksen");

	key = getch();
    }
    if (key == 'S')
	santaHunta();
}

bool easterEgg (void)
{
    time_t tunix = time (NULL);
    struct tm* t = localtime (&tunix);
    return t->tm_mon == 11 && t->tm_mday >= 24 && t->tm_mday <= 26;
}
