/*
 * Purple's oscar protocol plugin
 * This file is the legal property of its developers.
 * Please see the AUTHORS file distributed alongside this file.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
*/

/*
 * Cookie Caching stuff. Adam wrote this, apparently just some
 * derivatives of n's SNAC work. I cleaned it up, added comments.
 *
 */

/*
 * I'm assuming that cookies are type-specific. that is, we can have
 * "1234578" for type 1 and type 2 concurrently. if i'm wrong, then we
 * lose some error checking. if we assume cookies are not type-specific and are
 * wrong, we get quirky behavior when cookies step on each others' toes.
 */

#include "oscar.h"

/**
 * aim_cachecookie - appends a cookie to the cookie list
 *
 * if cookie->cookie for type cookie->type is found, updates the
 * ->addtime of the found structure; otherwise adds the given cookie
 * to the cache
 *
 * @param od session to add to
 * @param cookie pointer to struct to append
 * @return returns -1 on error, 0 on append, 1 on update.  the cookie you pass
 *         in may be free'd, so don't count on its value after calling this!
 */
int aim_cachecookie(OscarData *od, IcbmCookie *cookie)
{
	IcbmCookie *newcook;

	if (!od || !cookie)
		return -EINVAL;

	newcook = aim_checkcookie(od, cookie->cookie, cookie->type);

	if (newcook == cookie) {
		newcook->addtime = time(NULL);
		return 1;
	} else if (newcook)
		aim_cookie_free(od, newcook);

	cookie->addtime = time(NULL);

	cookie->next = od->msgcookies;
	od->msgcookies = cookie;

	return 0;
}

/**
 * aim_uncachecookie - grabs a cookie from the cookie cache (removes it from the list)
 *
 * takes a cookie string and a cookie type and finds the cookie struct associated with that duple, removing it from the cookie list ikn the process.
 *
 * @param od session to grab cookie from
 * @param cookie cookie string to look for
 * @param type cookie type to look for
 * @return if found, returns the struct; if none found (or on error), returns NULL:
 */
IcbmCookie *aim_uncachecookie(OscarData *od, guint8 *cookie, int type)
{
	IcbmCookie *cur, **prev;

	if (!cookie || !od->msgcookies)
		return NULL;

	for (prev = &od->msgcookies; (cur = *prev); ) {
		if ((cur->type == type) &&
				(memcmp(cur->cookie, cookie, 8) == 0)) {
			*prev = cur->next;
			return cur;
		}
		prev = &cur->next;
	}

	return NULL;
}

/**
 * aim_mkcookie - generate an IcbmCookie *struct from a cookie string, a type, and a data pointer.
 *
 * @param c pointer to the cookie string array
 * @param type cookie type to use
 * @param data data to be cached with the cookie
 * @return returns NULL on error, a pointer to the newly-allocated
 *         cookie on success.
 */
IcbmCookie *aim_mkcookie(guint8 *c, int type, void *data)
{
	IcbmCookie *cookie;

	if (!c)
		return NULL;

	cookie = g_new0(IcbmCookie, 1);

	cookie->data = data;
	cookie->type = type;
	memcpy(cookie->cookie, c, 8);

	return cookie;
}

/**
 * aim_checkcookie - check to see if a cookietuple has been cached
 *
 * @param od session to check for the cookie in
 * @param cookie pointer to the cookie string array
 * @param type type of the cookie to look for
 * @return returns a pointer to the cookie struct (still in the list)
 *         on success; returns NULL on error/not found
 */

IcbmCookie *aim_checkcookie(OscarData *od, const guint8 *cookie, const int type)
{
	IcbmCookie *cur;

	for (cur = od->msgcookies; cur; cur = cur->next) {
		if ((cur->type == type) &&
				(memcmp(cur->cookie, cookie, 8) == 0))
			return cur;
	}

	return NULL;
}

/**
 * aim_cookie_free - free an IcbmCookie struct
 *
 * this function removes the cookie *cookie from the list of cookies
 * in od, and then frees all memory associated with it. including
 * its data! if you want to use the private data after calling this,
 * make sure you copy it first.
 *
 * @param od session to remove the cookie from
 * @param cookie the address of a pointer to the cookie struct to remove
 * @return returns -1 on error, 0 on success.
 *
 */
int aim_cookie_free(OscarData *od, IcbmCookie *cookie)
{
	IcbmCookie *cur, **prev;

	if (!od || !cookie)
		return -EINVAL;

	for (prev = &od->msgcookies; (cur = *prev); ) {
		if (cur == cookie)
			*prev = cur->next;
		else
			prev = &cur->next;
	}

	g_free(cookie->data);
	g_free(cookie);

	return 0;
}
