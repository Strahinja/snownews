// This file is part of Snownews - A lightweight console RSS newsreader
//
// Copyright (c) 2003-2009 Oliver Feiler <kiza@kcore.de>
// Copyright (c) 2003-2009 Rene Puls <rpuls@gmx.net>
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
#include "conv.h"
#include "uiutil.h"
#include <ctype.h>
#include <libxml/HTMLparser.h>
#include <langinfo.h>
#include <openssl/evp.h>
#include <openssl/md5.h>

//----------------------------------------------------------------------

static int calcAgeInDays (const struct tm* t);

//----------------------------------------------------------------------

// This is a replacement for strsep which is not portable (missing on Solaris).
//
// http://www.winehq.com/hypermail/wine-patches/2001/11/0024.html
//
// The following function was written by Francois Gouget.
#ifdef SUN
char* strsep (char** str, const char* delims)
{
    if (!*str)		       // No more tokens
	return NULL;
    char* token = *str;
    while (**str) {
	if (strchr (delims, **str)) {
	    *(*str)++ = '\0';
	    return token;
	}
	++(*str);
    }
    // There is no other token
    *str = NULL;
    return token;
}

// timegm() is not available on Solaris
static time_t timegm (struct tm* t)
{
    time_t tl = mktime (t);
    if (tl == -1) {
	--t->tm_hour;
	tl = mktime (t);
	if (tl == -1)
	    return -1;	       // can't deal with output from strptime
	tl += 3600;
    }
    struct tm* tg = gmtime (&tl);
    tg->tm_isdst = 0;
    time_t tb = mktime (tg);
    if (tb == -1) {
	--tg->tm_hour;
	tb = mktime (tg);
	if (tb == -1)
	    return -1;	       // can't deal with output from gmtime
	tb += 3600;
    }
    return tl - (tb - tl);
}
#endif

// strcasestr stolen from: http://www.unixpapa.com/incnote/string.html
const char* s_strcasestr (const char* a, const char* b)
{
    const size_t lena = strlen (a), lenb = strlen (b);
    char f[3];
    snprintf (f, sizeof (f), "%c%c", tolower (*b), toupper (*b));
    for (size_t l = strcspn (a, f); l != lena; l += strcspn (a + l + 1, f) + 1)
	if (strncasecmp (a + l, b, lenb) == 0)
	    return a + l;
    return NULL;
}

//----------------------------------------------------------------------

char* text_from_html (const char* html)
{
    char* text = strdup (html);
    char* o = text;

    unsigned nnl = 0;
    bool intag = false;

    for (const char* i = html; *i; ++i) {
	if (*i == '>')
	    intag = false;
	else if (*i == '<') {
	    intag = true;
	    if (i[1] == '!') {
		// Check for uninterpreted content
		if (0 == strncmp (i, "!--", strlen("!--"))) // Comment
		    i = strstr (i+strlen("!--"), "-->");
		else if (0 == strncmp (i, "![CDATA[", strlen("![CDATA["))) // CDATA
		    i = strstr (i+strlen("![CDATA["), "]]>");
		if (!i)
		    break; // unclosed tag, error
		i += 2;
	    } else if (i[1] == 'p' || (i[1] == 'b' && i[2] == 'r')) {
		// Paragraph markers and hard newlines
		const char* tage = strchr (i, '>');
		if (tage && tage[1] != '\n' && (!nnl || (i[1] == 'p' && nnl < 2))) {
		    ++nnl;
		    *o++ = '\n';
		}
	    }
	} else if (!intag) {
	    if (*i == '&') {
		const char* ampe = strchr (i, ';');
		if (ampe) {
		    unsigned enamelen = ampe-(i+1);
		    wchar_t ec = 0;
		    if (i[1] == '#') {
			if (i[2] == 'x')
			    ec = strtoul (&i[3], NULL, 16);
			else
			    ec = strtoul (&i[2], NULL, 10);
		    } else {
			xmlChar ebuf[8] = {};
			const htmlEntityDesc* ep = NULL;
			if (enamelen < sizeof(ebuf)) {
			    strncpy ((char*) ebuf, &i[1], enamelen);
			    ep = htmlEntityLookup (ebuf);
			}
			if (ep)
			    ec = ep->value;
		    }
		    // Replace the entity with the unicode char if it is not a terminal control char
		    if (ec >= ' ' && (ec <= '~' || ec >= 0xa0) && utf8_osize(ec) <= enamelen+strlen("&;")) {
			o = utf8_write (ec, o);
			i = ampe;
			continue;
		    }
		}
	    }
	    if (*i != '\n')
		nnl = 0;
	    else if (++nnl > 2)
		continue; // avoid too many newlines
	    *o++ = *i;
	}
    }
    *o = 0;
    CleanupString (text, false);
    return text;
}

// 5th try at a wrap text functions.
// My first version was broken, my second one sucked, my third try was
// so overcomplicated I didn't understand it anymore... Kianga tried
// the 4th version which corrupted some random memory unfortunately...
// but this one works. Heureka!
char* WrapText (const char* text, unsigned width)
{
    char* textblob = strdup (text);	// Working copy of text.
    char* start = textblob;

    char* line = malloc (1);	// One line of text with max width.
    line[0] = '\0';

    char* newtext = malloc (1);
    memset (newtext, 0, 1);

    while (1) {
	// First, cut at \n.
	char* chapter = strsep (&textblob, "\n");
	if (chapter == NULL)
	    break;
	while (1) {
	    char* savepos = chapter;	// Saved position pointer so we can go back in the string.
	    const char* chunk = strsep (&chapter, " ");

	    // Last chunk.
	    if (chunk == NULL) {
		if (line != NULL) {
		    newtext = realloc (newtext, strlen (newtext) + strlen (line) + 2);
		    strcat (newtext, line);
		    strcat (newtext, "\n");
		    line[0] = '\0';
		}
		break;
	    }

	    if (utf8_length (chunk) > width) {
		// First copy remaining stuff in line to newtext.
		newtext = realloc (newtext, strlen (newtext) + strlen (line) + 2);
		strcat (newtext, line);
		strcat (newtext, "\n");

		free (line);
		line = malloc (1);
		line[0] = '\0';

		// Then copy chunk with max length of line to newtext.
		line = realloc (line, width + 1);
		strncat (line, chunk, width - 5);
		strcat (line, "...");
		newtext = realloc (newtext, strlen (newtext) + width + 2);
		strcat (newtext, line);
		strcat (newtext, "\n");
		free (line);
		line = malloc (1);
		line[0] = '\0';
		continue;
	    }

	    if (utf8_length (line) + utf8_length (chunk) <= width) {
		line = realloc (line, strlen (line) + strlen (chunk) + 2);
		strcat (line, chunk);
		strcat (line, " ");
	    } else {
		// Why can chapter be NULL here anyway?
		if (chapter != NULL) {
		    --chapter;
		    chapter[0] = ' ';
		}
		chapter = savepos;
		newtext = realloc (newtext, strlen (newtext) + strlen (line) + 2);
		strcat (newtext, line);
		strcat (newtext, "\n");
		free (line);
		line = malloc (1);
		line[0] = '\0';
	    }
	}
    }
    free (line);
    free (start);
    return newtext;
}

// Remove leading whitspaces, newlines, tabs.
// This function should be safe for working on UTF-8 strings.
// fullclean:	false = only suck chars from beginning of string
//		true = remove newlines inside the string
void CleanupString (char* s, bool fullclean)
{
    // If we are passed a NULL pointer, leave it alone and return.
    if (!s)
	return;

    // Remove leading spaces
    size_t len = strlen (s), leadspace = 0;
    while (leadspace < len && isspace (s[leadspace]))
	++leadspace;
    if (leadspace) {
	memmove (s, s + leadspace, (len + 1) - leadspace);
	len -= leadspace;
    }

    // Eat newlines and tabs along the whole s.
    for (size_t i = 0; i < len; ++i) {
	if (s[i] == '\t')
	    s[i] = ' ';
	if (fullclean && s[i] == '\n')
	    s[i] = ' ';
    }

    // Remove embedded CDATA tags
    #define CDATA_START	"<![CDATA["
    for (char* si = s; (si = strstr (si, CDATA_START));)
	memmove (si, si + strlen(CDATA_START), (len -= strlen(CDATA_START)) - (si - s) + 1);
    #define CDATA_END	"]]>"
    for (char* si = s; (si = strstr (si, CDATA_END));)
	memmove (si, si + strlen(CDATA_END), (len -= strlen(CDATA_END)) - (si - s) + 1);
    #define CDATA_BROK	"]]"
    for (char* si = s; (si = strstr (si, CDATA_BROK));)
	memmove (si, si + strlen(CDATA_BROK), (len -= strlen(CDATA_BROK)) - (si - s) + 1);

    // Remove trailing spaces.
    while (len > 1 && isspace (s[len-1]))
	s[--len] = 0;
}

// http://foo.bar/address.rdf -> http:__foo.bar_address.rdf
char* Hashify (const char* url)
{
    char* hashed_url = strdup (url);
    size_t len = strlen (hashed_url);

    // Don't allow filenames > 128 chars for teeny weeny operating systems.
    if (len > 128) {
	len = 128;
	hashed_url[128] = '\0';
    }

    for (size_t i = 0; i < len; ++i) {
	if (((hashed_url[i] < 32) || (hashed_url[i] > 38)) && ((hashed_url[i] < 43) || (hashed_url[i] > 46)) && ((hashed_url[i] < 48) || (hashed_url[i] > 90)) && ((hashed_url[i] < 97) || (hashed_url[i] > 122)) && (hashed_url[i] != 0))
	    hashed_url[i] = '_';

	// Cygwin doesn't seem to like anything besides a-z0-9 in filenames. Zap'em!
#ifdef __CYGWIN__
	if (((hashed_url[i] < 48) || (hashed_url[i] > 57)) && ((hashed_url[i] < 65) || (hashed_url[i] > 90)) && ((hashed_url[i] < 97) || (hashed_url[i] > 122)) && (hashed_url[i] != 0))
	    hashed_url[i] = '_';
#endif
    }

    return hashed_url;
}

char* genItemHash (const char* const* hashitems, unsigned items)
{
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    EVP_DigestInit (mdctx, EVP_md5());

    for (unsigned i = 0; i < items; ++i)
	if (hashitems[i])
	    EVP_DigestUpdate (mdctx, hashitems[i], strlen (hashitems[i]));

    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned md_len = 0;
    EVP_DigestFinal_ex (mdctx, md_value, &md_len);
    EVP_MD_CTX_free (mdctx);

    char hashtext [MD5_DIGEST_LENGTH*2 + 1];
    for (unsigned i = 0; i < md_len; ++i)
	sprintf (&hashtext[2*i], "%02hhx", md_value[i]);
    return strdup (hashtext);
}

// Date conversion
// 2004-11-20T19:45:00+00:00
time_t ISODateToUnix (const char* ISODate)
{
    // Do not crash with an empty tag
    if (!ISODate)
	return 0;
    struct tm t = { };
    // OpenBSD does not know %F == %Y-%m-%d
    if (!strptime (ISODate, "%Y-%m-%dT%T", &t) && !strptime (ISODate, "%Y-%m-%d", &t))
	return 0;
#ifdef __CYGWIN__
    return mktime (&t);
#else
    return timegm (&t);
#endif
}

// Sat, 20 Nov 2004 21:45:40 +0000
time_t pubDateToUnix (const char* pubDate)
{
    // Do not crash with an empty Tag
    if (!pubDate)
	return 0;
#ifdef LOCALEPATH
    // Cruft!
    // Save old locale so we can parse the stupid pubDate format.
    // However strftime is not really more intelligent since there is no
    // format string for abbr. month name NOT in the current locale. Grr.
    //
    // This is also not thread safe!
    char* oldlocale = setlocale (LC_TIME, NULL);
    if (oldlocale)
	setlocale (LC_TIME, "C");
#endif
    struct tm t = { };
    char* r = strptime (pubDate + strlen ("Sat, "), "%d %b %Y %T", &t);
#ifdef LOCALEPATH
    if (oldlocale)
	setlocale (LC_TIME, oldlocale);
#endif
    if (!r)
	return 0;
#ifdef __CYGWIN__
    return mktime (&t);
#else
    return timegm (&t);
#endif
}

char* unixToPostDateString (time_t unixDate)
{
    int len = 64;
    char* time_str = malloc (len);

    int strfstr_len = 32;
    char* time_strfstr = malloc (strfstr_len);

    struct tm t;
    gmtime_r (&unixDate, &t);

    strftime (time_strfstr, strfstr_len, _(", %H:%M"), &t);
    strcpy (time_str, _("Posted "));
    len -= strlen (_("Posted "));
    if (len <= 0)
	return NULL;

    int age = calcAgeInDays (&t);
    if (age == 0) {
	strncat (time_str, _("today"), len - 1);
	len -= strlen (_("today"));
	if (len <= 0)
	    return NULL;
	if (!(!t.tm_hour && !t.tm_min && !t.tm_sec))
	    strncat (time_str, time_strfstr, len - 1);
    } else if (age == 1) {
	strncat (time_str, _("yesterday"), len - 1);
	len -= strlen (_("yesterday"));
	if (len <= 0)
	    return NULL;
	if (!(!t.tm_hour && !t.tm_min && !t.tm_sec))
	    strncat (time_str, time_strfstr, len - 1);
    } else if ((age > 1) && (age < 7)) {
	char tmpstr[32];
	snprintf (tmpstr, sizeof (tmpstr), _("%d days ago"), age);
	strncat (time_str, tmpstr, len - 1);
	len -= strlen (tmpstr);
	if (len <= 0)
	    return NULL;
	if (!(!t.tm_hour && !t.tm_min && !t.tm_sec))
	    strncat (time_str, time_strfstr, len - 1);
    } else if (age == 7) {
	strncat (time_str, _("a week ago"), len - 1);
	len -= strlen (_("a week ago"));
	if (len <= 0)
	    return NULL;
	if (!(!t.tm_hour && !t.tm_min && !t.tm_sec))
	    strncat (time_str, time_strfstr, len - 1);
    } else if (age < 0) {
	strcpy (time_str, _("Not yet posted: "));
	len = 64 - strlen (_("Not yet posted: "));
	if (!t.tm_hour && !t.tm_min && !t.tm_sec)
	    strftime (time_strfstr, strfstr_len, _("%x"), &t);
	else
	    strftime (time_strfstr, strfstr_len, _("%x, %H:%M"), &t);
	strncat (time_str, time_strfstr, len - 1);
    } else {
	if (!t.tm_hour && !t.tm_min && !t.tm_sec)
	    strftime (time_strfstr, strfstr_len, _("%x"), &t);
	else
	    strftime (time_strfstr, strfstr_len, _("%x, %H:%M"), &t);
	strncat (time_str, time_strfstr, len - 1);
    }
    free (time_strfstr);

    return time_str;
}

static int calcAgeInDays (const struct tm* t)
{
    time_t unix_t = time (NULL);
    struct tm current_t;
    gmtime_r (&unix_t, &current_t);

    // (((current year - passed year) * 365) + current year day) - passed year day
    return (((current_t.tm_year - t->tm_year) * 365) + current_t.tm_yday) - t->tm_yday;
}
