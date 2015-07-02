/* Snownews - A lightweight console RSS newsreader
 * $Id: net-support.h 1159 2005-09-13 19:51:13Z kiza $
 * 
 * Copyright 2003-2004 Oliver Feiler <kiza@kcore.de>
 * http://kiza.kcore.de/software/snownews/
 *
 * net-support.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef NET_SUPPORT_H
#define NET_SUPPORT_H

int NetSupportAuth (struct feed * cur_ptr, const char * authdata, char * url, const char * netbuf);
int checkValidHTTPHeader (const unsigned char * header, int size);
int checkValidHTTPURL (const unsigned char * url);

#endif
