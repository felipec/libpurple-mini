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
 *
 * Various SNAC-related dodads...
 *
 * outstanding_snacs is a list of aim_snac_t structs.  A SNAC should be added
 * whenever a new SNAC is sent and it should remain in the list until the
 * response for it has been received.
 *
 * cleansnacs() should be called periodically by the client in order
 * to facilitate the aging out of unreplied-to SNACs. This can and does
 * happen, so it should be handled.
 *
 */

#include "oscar.h"

/*
 * Called from oscar_session_new() to initialize the hash.
 */
void aim_initsnachash(OscarData *od)
{
	int i;

	for (i = 0; i < FAIM_SNAC_HASH_SIZE; i++)
		od->snac_hash[i] = NULL;

	return;
}

aim_snacid_t aim_cachesnac(OscarData *od, const guint16 family, const guint16 type, const guint16 flags, const void *data, const int datalen)
{
	aim_snac_t snac;

	snac.id = od->snacid_next++;
	snac.family = family;
	snac.type = type;
	snac.flags = flags;

	if (datalen)
		snac.data = g_memdup(data, datalen);
	else
		snac.data = NULL;

	return aim_newsnac(od, &snac);
}

/*
 * Clones the passed snac structure and caches it in the
 * list/hash.
 */
aim_snacid_t aim_newsnac(OscarData *od, aim_snac_t *newsnac)
{
	aim_snac_t *snac;
	int index;

	if (!newsnac)
		return 0;

	snac = g_memdup(newsnac, sizeof(aim_snac_t));
	snac->issuetime = time(NULL);

	index = snac->id % FAIM_SNAC_HASH_SIZE;

	snac->next = (aim_snac_t *)od->snac_hash[index];
	od->snac_hash[index] = (void *)snac;

	return snac->id;
}

/*
 * Finds a snac structure with the passed SNAC ID,
 * removes it from the list/hash, and returns a pointer to it.
 *
 * The returned structure must be freed by the caller.
 *
 */
aim_snac_t *aim_remsnac(OscarData *od, aim_snacid_t id)
{
	aim_snac_t *cur, **prev;
	int index;

	index = id % FAIM_SNAC_HASH_SIZE;

	for (prev = (aim_snac_t **)&od->snac_hash[index]; (cur = *prev); ) {
		if (cur->id == id) {
			*prev = cur->next;
			if (cur->flags & AIM_SNACFLAGS_DESTRUCTOR) {
				g_free(cur->data);
				cur->data = NULL;
			}
			return cur;
		} else
			prev = &cur->next;
	}

	return cur;
}

/*
 * This is for cleaning up old SNACs that either don't get replies or
 * a reply was never received for.  Garbage collection. Plain and simple.
 *
 * maxage is the _minimum_ age in seconds to keep SNACs.
 *
 */
void aim_cleansnacs(OscarData *od, int maxage)
{
	int i;

	for (i = 0; i < FAIM_SNAC_HASH_SIZE; i++) {
		aim_snac_t *cur, **prev;
		time_t curtime;

		if (!od->snac_hash[i])
			continue;

		curtime = time(NULL); /* done here in case we waited for the lock */

		for (prev = (aim_snac_t **)&od->snac_hash[i]; (cur = *prev); ) {
			if ((curtime - cur->issuetime) > maxage) {

				*prev = cur->next;

				g_free(cur->data);
				g_free(cur);
			} else
				prev = &cur->next;
		}
	}

	return;
}

int aim_putsnac(ByteStream *bs, guint16 family, guint16 subtype, aim_snacid_t snacid)
{

	byte_stream_put16(bs, family);
	byte_stream_put16(bs, subtype);
	byte_stream_put16(bs, 0x0000);
	byte_stream_put32(bs, snacid);

	return 10;
}
