/**
 * @file msnslp.c MSNSLP support
 *
 * purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */
#include "msn.h"
#include "slp.h"
#include "slpcall.h"
#include "slpmsg.h"
#include "msnutils.h"

#include "object.h"
#include "user.h"
#include "switchboard.h"
#include "directconn.h"

#include "smiley.h"

/* seconds to delay between sending buddy icon requests to the server. */
#define BUDDY_ICON_DELAY 20

static void request_user_display(MsnUser *user);

typedef struct {
	MsnSession *session;
	const char *remote_user;
	const char *sha1;
} MsnFetchUserDisplayData;

/**************************************************************************
 * Util
 **************************************************************************/

static char *
get_token(const char *str, const char *start, const char *end)
{
	const char *c, *c2;

	if ((c = strstr(str, start)) == NULL)
		return NULL;

	c += strlen(start);

	if (end != NULL)
	{
		if ((c2 = strstr(c, end)) == NULL)
			return NULL;

		return g_strndup(c, c2 - c);
	}
	else
	{
		/* This has to be changed */
		return g_strdup(c);
	}

}

/**************************************************************************
 * Xfer
 **************************************************************************/

static void
msn_xfer_init(PurpleXfer *xfer)
{
	MsnSlpCall *slpcall;
	/* MsnSlpLink *slplink; */
	char *content;

	purple_debug_info("msn", "xfer_init\n");

	slpcall = xfer->data;

	/* Send Ok */
	content = g_strdup_printf("SessionID: %lu\r\n\r\n",
							  slpcall->session_id);

	msn_slp_send_ok(slpcall, slpcall->branch, "application/x-msnmsgr-sessionreqbody",
			content);

	g_free(content);
	msn_slplink_send_queued_slpmsgs(slpcall->slplink);
}

void
msn_xfer_cancel(PurpleXfer *xfer)
{
	MsnSlpCall *slpcall;
	char *content;

	g_return_if_fail(xfer != NULL);
	g_return_if_fail(xfer->data != NULL);

	slpcall = xfer->data;

	if (purple_xfer_get_status(xfer) == PURPLE_XFER_STATUS_CANCEL_LOCAL)
	{
		if (slpcall->started)
		{
			msn_slpcall_close(slpcall);
		}
		else
		{
			content = g_strdup_printf("SessionID: %lu\r\n\r\n",
									slpcall->session_id);

			msn_slp_send_decline(slpcall, slpcall->branch, "application/x-msnmsgr-sessionreqbody",
						content);

			g_free(content);
			msn_slplink_send_queued_slpmsgs(slpcall->slplink);

			if (purple_xfer_get_type(xfer) == PURPLE_XFER_SEND)
				slpcall->wasted = TRUE;
			else
				msn_slpcall_destroy(slpcall);
		}
	}
}

gssize
msn_xfer_write(const guchar *data, gsize len, PurpleXfer *xfer)
{
	MsnSlpCall *slpcall;

	g_return_val_if_fail(xfer != NULL, -1);
	g_return_val_if_fail(data != NULL, -1);
	g_return_val_if_fail(len > 0, -1);

	g_return_val_if_fail(purple_xfer_get_type(xfer) == PURPLE_XFER_SEND, -1);

	slpcall = xfer->data;
	/* Not sure I trust it'll be there */
	g_return_val_if_fail(slpcall != NULL, -1);

	g_return_val_if_fail(slpcall->xfer_msg != NULL, -1);

	slpcall->u.outgoing.len = len;
	slpcall->u.outgoing.data = data;
	msn_slplink_send_msgpart(slpcall->slplink, slpcall->xfer_msg);
	msn_message_unref(slpcall->xfer_msg->msg);
	return MIN(1202, len);
}

gssize
msn_xfer_read(guchar **data, PurpleXfer *xfer)
{
	MsnSlpCall *slpcall;
	gsize len;

	g_return_val_if_fail(xfer != NULL, -1);
	g_return_val_if_fail(data != NULL, -1);

	g_return_val_if_fail(purple_xfer_get_type(xfer) == PURPLE_XFER_RECEIVE, -1);

	slpcall = xfer->data;
	/* Not sure I trust it'll be there */
	g_return_val_if_fail(slpcall != NULL, -1);

	/* Just pass up the whole GByteArray. We'll make another. */
	*data = slpcall->u.incoming_data->data;
	len = slpcall->u.incoming_data->len;

	g_byte_array_free(slpcall->u.incoming_data, FALSE);
	slpcall->u.incoming_data = g_byte_array_new();

	return len;
}

void
msn_xfer_end_cb(MsnSlpCall *slpcall, MsnSession *session)
{
	if ((purple_xfer_get_status(slpcall->xfer) != PURPLE_XFER_STATUS_DONE) &&
		(purple_xfer_get_status(slpcall->xfer) != PURPLE_XFER_STATUS_CANCEL_REMOTE) &&
		(purple_xfer_get_status(slpcall->xfer) != PURPLE_XFER_STATUS_CANCEL_LOCAL))
	{
		purple_xfer_cancel_remote(slpcall->xfer);
	}
}

void
msn_xfer_completed_cb(MsnSlpCall *slpcall, const guchar *body,
					  gsize size)
{
	PurpleXfer *xfer = slpcall->xfer;

	purple_xfer_set_completed(xfer, TRUE);
	purple_xfer_end(xfer);
}

/**************************************************************************
 * SLP Control
 **************************************************************************/

void
msn_slp_send_ok(MsnSlpCall *slpcall, const char *branch,
		const char *type, const char *content)
{
	MsnSlpLink *slplink;
	MsnSlpMessage *slpmsg;

	slplink = slpcall->slplink;

	/* 200 OK */
	slpmsg = msn_slpmsg_sip_new(slpcall, 1,
								"MSNSLP/1.0 200 OK",
								branch, type, content);

	slpmsg->info = "SLP 200 OK";
	slpmsg->text_body = TRUE;

	msn_slplink_queue_slpmsg(slplink, slpmsg);
}

void
msn_slp_send_decline(MsnSlpCall *slpcall, const char *branch,
			 const char *type, const char *content)
{
	MsnSlpLink *slplink;
	MsnSlpMessage *slpmsg;

	slplink = slpcall->slplink;

	/* 603 Decline */
	slpmsg = msn_slpmsg_sip_new(slpcall, 1,
								"MSNSLP/1.0 603 Decline",
								branch, type, content);

	slpmsg->info = "SLP 603 Decline";
	slpmsg->text_body = TRUE;

	msn_slplink_queue_slpmsg(slplink, slpmsg);
}

/* XXX: this could be improved if we tracked custom smileys
 * per-protocol, per-account, per-session or (ideally) per-conversation
 */
static PurpleStoredImage *
find_valid_emoticon(PurpleAccount *account, const char *path)
{
	GList *smileys;

	if (!purple_account_get_bool(account, "custom_smileys", TRUE))
		return NULL;

	smileys = purple_smileys_get_all();

	for (; smileys; smileys = g_list_delete_link(smileys, smileys)) {
		PurpleSmiley *smiley;
		PurpleStoredImage *img;

		smiley = smileys->data;
		img = purple_smiley_get_stored_image(smiley);

		if (purple_strequal(path, purple_imgstore_get_filename(img))) {
			g_list_free(smileys);
			return img;
		}

		purple_imgstore_unref(img);
	}

	purple_debug_error("msn", "Received illegal request for file %s\n", path);
	return NULL;
}

static char *
parse_dc_nonce(const char *content, MsnDirectConnNonceType *ntype)
{
	char *nonce;

	*ntype = DC_NONCE_UNKNOWN;

	nonce = get_token(content, "Hashed-Nonce: {", "}\r\n");
	if (nonce) {
		*ntype = DC_NONCE_SHA1;
	} else {
		guint32 n1, n6;
		guint16 n2, n3, n4, n5;
		nonce = get_token(content, "Nonce: {", "}\r\n");
		if (nonce
		 && sscanf(nonce, "%08x-%04hx-%04hx-%04hx-%04hx%08x",
		           &n1, &n2, &n3, &n4, &n5, &n6) == 6) {
			*ntype = DC_NONCE_PLAIN;
			g_free(nonce);
			nonce = g_malloc(16);
			*(guint32 *)(nonce +  0) = GUINT32_TO_LE(n1);
			*(guint16 *)(nonce +  4) = GUINT16_TO_LE(n2);
			*(guint16 *)(nonce +  6) = GUINT16_TO_LE(n3);
			*(guint16 *)(nonce +  8) = GUINT16_TO_BE(n4);
			*(guint16 *)(nonce + 10) = GUINT16_TO_BE(n5);
			*(guint32 *)(nonce + 12) = GUINT32_TO_BE(n6);
		} else {
			/* Invalid nonce, so ignore request */
			g_free(nonce);
			nonce = NULL;
		}
	}

	return nonce;
}

static void
msn_slp_process_transresp(MsnSlpCall *slpcall, const char *content)
{
	/* A direct connection negotiation response */
	char *bridge;
	char *nonce;
	char *listening;
	MsnDirectConn *dc = slpcall->slplink->dc;
	MsnDirectConnNonceType ntype;

	purple_debug_info("msn", "process_transresp\n");

	/* Direct connections are disabled. */
	if (!purple_account_get_bool(slpcall->slplink->session->account, "direct_connect", TRUE))
		return;

	g_return_if_fail(dc != NULL);
	g_return_if_fail(dc->state == DC_STATE_CLOSED);

	bridge = get_token(content, "Bridge: ", "\r\n");
	nonce = parse_dc_nonce(content, &ntype);
	listening = get_token(content, "Listening: ", "\r\n");
	if (listening && bridge && !strcmp(bridge, "TCPv1")) {
		/* Ok, the client supports direct TCP connection */

		/* We always need this. */
		if (ntype == DC_NONCE_SHA1) {
			strncpy(dc->remote_nonce, nonce, 36);
			dc->remote_nonce[36] = '\0';
		}

		if (!strcasecmp(listening, "false")) {
			if (dc->listen_data != NULL) {
				/*
				 * We'll listen for incoming connections but
				 * the listening socket isn't ready yet so we cannot
				 * send the INVITE packet now. Put the slpcall into waiting mode
				 * and let the callback send the invite.
				 */
				slpcall->wait_for_socket = TRUE;

			} else if (dc->listenfd != -1) {
				/* The listening socket is ready. Send the INVITE here. */
				msn_dc_send_invite(dc);

			} else {
				/* We weren't able to create a listener either. Use SB. */
				msn_dc_fallback_to_sb(dc);
			}

		} else {
			/*
			 * We should connect to the client so parse
			 * IP/port from response.
			 */
			char *ip, *port_str;
			int port = 0;

			if (ntype == DC_NONCE_PLAIN) {
				/* Only needed for listening side. */
				memcpy(dc->nonce, nonce, 16);
			}

			/* Cancel any listen attempts because we don't need them. */
			if (dc->listenfd_handle != 0) {
				purple_input_remove(dc->listenfd_handle);
				dc->listenfd_handle = 0;
			}
			if (dc->connect_timeout_handle != 0) {
				purple_timeout_remove(dc->connect_timeout_handle);
				dc->connect_timeout_handle = 0;
			}
			if (dc->listenfd != -1) {
				purple_network_remove_port_mapping(dc->listenfd);
				close(dc->listenfd);
				dc->listenfd = -1;
			}
			if (dc->listen_data != NULL) {
				purple_network_listen_cancel(dc->listen_data);
				dc->listen_data = NULL;
			}

			/* Save external IP/port for later use. We'll try local connection first. */
			dc->ext_ip = get_token(content, "IPv4External-Addrs: ", "\r\n");
			port_str = get_token(content, "IPv4External-Port: ", "\r\n");
			if (port_str) {
				dc->ext_port = atoi(port_str);
				g_free(port_str);
			}

			ip = get_token(content, "IPv4Internal-Addrs: ", "\r\n");
			port_str = get_token(content, "IPv4Internal-Port: ", "\r\n");
			if (port_str) {
				port = atoi(port_str);
				g_free(port_str);
			}

			if (ip && port) {
				/* Try internal address first */
				dc->connect_data = purple_proxy_connect(
					NULL,
					slpcall->slplink->session->account,
					ip,
					port,
					msn_dc_connected_to_peer_cb,
					dc
				);

				if (dc->connect_data) {
					/* Add connect timeout handle */
					dc->connect_timeout_handle = purple_timeout_add_seconds(
						DC_OUTGOING_TIMEOUT,
						msn_dc_outgoing_connection_timeout_cb,
						dc
					);
				} else {
					/*
					 * Connection failed
					 * Try external IP/port (if specified)
					 */
					msn_dc_outgoing_connection_timeout_cb(dc);
				}

			} else {
				/*
				 * Omitted or invalid internal IP address / port
				 * Try external IP/port (if specified)
				 */
				msn_dc_outgoing_connection_timeout_cb(dc);
			}

			g_free(ip);
		}

	} else {
		/*
		 * Invalid direct connect invitation or
		 * TCP connection is not supported
		 */
	}

	g_free(listening);
	g_free(nonce);
	g_free(bridge);

	return;
}

static void
got_sessionreq(MsnSlpCall *slpcall, const char *branch,
			   const char *euf_guid, const char *context)
{
	gboolean accepted = FALSE;

	if (!strcmp(euf_guid, MSN_OBJ_GUID))
	{
		/* Emoticon or UserDisplay */
		char *content;
		gsize len;
		MsnSlpLink *slplink;
		MsnSlpMessage *slpmsg;
		MsnObject *obj;
		char *msnobj_data;
		PurpleStoredImage *img = NULL;
		int type;

		/* Send Ok */
		content = g_strdup_printf("SessionID: %lu\r\n\r\n",
								  slpcall->session_id);

		msn_slp_send_ok(slpcall, branch, "application/x-msnmsgr-sessionreqbody",
				content);

		g_free(content);

		slplink = slpcall->slplink;

		msnobj_data = (char *)purple_base64_decode(context, &len);
		obj = msn_object_new_from_string(msnobj_data);
		type = msn_object_get_type(obj);
		g_free(msnobj_data);
		if (type == MSN_OBJECT_EMOTICON) {
			img = find_valid_emoticon(slplink->session->account, obj->location);
		} else if (type == MSN_OBJECT_USERTILE) {
			img = msn_object_get_image(obj);
			if (img)
				purple_imgstore_ref(img);
		}
		msn_object_destroy(obj);

		if (img != NULL) {
			/* DATA PREP */
			slpmsg = msn_slpmsg_new(slplink);
			slpmsg->slpcall = slpcall;
			slpmsg->session_id = slpcall->session_id;
			msn_slpmsg_set_body(slpmsg, NULL, 4);
			slpmsg->info = "SLP DATA PREP";
			msn_slplink_queue_slpmsg(slplink, slpmsg);

			/* DATA */
			slpmsg = msn_slpmsg_new(slplink);
			slpmsg->slpcall = slpcall;
			slpmsg->flags = 0x20;
			slpmsg->info = "SLP DATA";
			msn_slpmsg_set_image(slpmsg, img);
			msn_slplink_queue_slpmsg(slplink, slpmsg);
			purple_imgstore_unref(img);

			accepted = TRUE;

		} else {
			purple_debug_error("msn", "Wrong object.\n");
		}
	}

	else if (!strcmp(euf_guid, MSN_FT_GUID))
	{
		/* File Transfer */
		PurpleAccount *account;
		PurpleXfer *xfer;
		MsnFileContext *header;
		gsize bin_len;
		guint32 file_size;
		char *file_name;

		account = slpcall->slplink->session->account;

		slpcall->end_cb = msn_xfer_end_cb;
		slpcall->branch = g_strdup(branch);

		slpcall->pending = TRUE;

		xfer = purple_xfer_new(account, PURPLE_XFER_RECEIVE,
							 slpcall->slplink->remote_user);

		header = (MsnFileContext *)purple_base64_decode(context, &bin_len);
		if (bin_len >= sizeof(MsnFileContext) - 1 &&
			(header->version == 2 ||
			 (header->version == 3 && header->length == sizeof(MsnFileContext) + 63))) {
			file_size = GUINT64_FROM_LE(header->file_size);

			file_name = g_convert((const gchar *)&header->file_name,
			                      MAX_FILE_NAME_LEN * 2,
			                      "UTF-8", "UTF-16LE",
			                      NULL, NULL, NULL);

			purple_xfer_set_filename(xfer, file_name ? file_name : "");
			g_free(file_name);
			purple_xfer_set_size(xfer, file_size);
			purple_xfer_set_init_fnc(xfer, msn_xfer_init);
			purple_xfer_set_request_denied_fnc(xfer, msn_xfer_cancel);
			purple_xfer_set_cancel_recv_fnc(xfer, msn_xfer_cancel);
			purple_xfer_set_read_fnc(xfer, msn_xfer_read);
			purple_xfer_set_write_fnc(xfer, msn_xfer_write);

			slpcall->u.incoming_data = g_byte_array_new();

			slpcall->xfer = xfer;
			purple_xfer_ref(slpcall->xfer);

			xfer->data = slpcall;

			if (header->type == 0 && bin_len >= sizeof(MsnFileContext)) {
				purple_xfer_set_thumbnail(xfer, &header->preview,
				                          bin_len - sizeof(MsnFileContext),
				    					  "image/png");
			}

			purple_xfer_request(xfer);
		}
		g_free(header);

		accepted = TRUE;

	} else if (!strcmp(euf_guid, MSN_CAM_REQUEST_GUID)) {
		purple_debug_info("msn", "Cam request.\n");
		if (slpcall && slpcall->slplink &&
				slpcall->slplink->session) {
			PurpleConversation *conv;
			gchar *from = slpcall->slplink->remote_user;
			conv = purple_find_conversation_with_account(
					PURPLE_CONV_TYPE_IM, from,
					slpcall->slplink->session->account);
			if (conv) {
				char *buf;
				buf = g_strdup_printf(
						_("%s requests to view your "
						"webcam, but this request is "
						"not yet supported."), from);
				purple_conversation_write(conv, NULL, buf,
						PURPLE_MESSAGE_SYSTEM |
						PURPLE_MESSAGE_NOTIFY,
						time(NULL));
				g_free(buf);
			}
		}

	} else if (!strcmp(euf_guid, MSN_CAM_GUID)) {
		purple_debug_info("msn", "Cam invite.\n");
		if (slpcall && slpcall->slplink &&
				slpcall->slplink->session) {
			PurpleConversation *conv;
			gchar *from = slpcall->slplink->remote_user;
			conv = purple_find_conversation_with_account(
					PURPLE_CONV_TYPE_IM, from,
					slpcall->slplink->session->account);
			if (conv) {
				char *buf;
				buf = g_strdup_printf(
						_("%s invited you to view his/her webcam, but "
						"this is not yet supported."), from);
				purple_conversation_write(conv, NULL, buf,
						PURPLE_MESSAGE_SYSTEM |
						PURPLE_MESSAGE_NOTIFY,
						time(NULL));
				g_free(buf);
			}
		}

	} else
		purple_debug_warning("msn", "SLP SessionReq with unknown EUF-GUID: %s\n", euf_guid);

	if (!accepted) {
		char *content = g_strdup_printf("SessionID: %lu\r\n\r\n",
		                                slpcall->session_id);
		msn_slp_send_decline(slpcall, branch, "application/x-msnmsgr-sessionreqbody", content);
		g_free(content);
	}
}

void
send_bye(MsnSlpCall *slpcall, const char *type)
{
	MsnSlpLink *slplink;
	PurpleAccount *account;
	MsnSlpMessage *slpmsg;
	char *header;

	slplink = slpcall->slplink;

	g_return_if_fail(slplink != NULL);

	account = slplink->session->account;

	header = g_strdup_printf("BYE MSNMSGR:%s MSNSLP/1.0",
							 purple_account_get_username(account));

	slpmsg = msn_slpmsg_sip_new(slpcall, 0, header,
								"A0D624A6-6C0C-4283-A9E0-BC97B4B46D32",
								type,
								"\r\n");
	g_free(header);

	slpmsg->info = "SLP BYE";
	slpmsg->text_body = TRUE;

	msn_slplink_queue_slpmsg(slplink, slpmsg);
}

static void
got_invite(MsnSlpCall *slpcall,
		   const char *branch, const char *type, const char *content)
{
	MsnSlpLink *slplink;

	slplink = slpcall->slplink;

	if (!strcmp(type, "application/x-msnmsgr-sessionreqbody"))
	{
		char *euf_guid, *context;
		char *temp;

		euf_guid = get_token(content, "EUF-GUID: {", "}\r\n");

		temp = get_token(content, "SessionID: ", "\r\n");
		if (temp != NULL)
			slpcall->session_id = atoi(temp);
		g_free(temp);

		temp = get_token(content, "AppID: ", "\r\n");
		if (temp != NULL)
			slpcall->app_id = atoi(temp);
		g_free(temp);

		context = get_token(content, "Context: ", "\r\n");

		if (context != NULL)
			got_sessionreq(slpcall, branch, euf_guid, context);

		g_free(context);
		g_free(euf_guid);
	}
	else if (!strcmp(type, "application/x-msnmsgr-transreqbody"))
	{
		/* A direct connection negotiation request */
		char *bridges;
		char *nonce;
		MsnDirectConnNonceType ntype;

		purple_debug_info("msn", "got_invite: transreqbody received\n");

		/* Direct connections may be disabled. */
		if (!purple_account_get_bool(slplink->session->account, "direct_connect", TRUE)) {
			msn_slp_send_ok(slpcall, branch,
				"application/x-msnmsgr-transrespbody",
				"Bridge: TCPv1\r\n"
				"Listening: false\r\n"
				"Nonce: {00000000-0000-0000-0000-000000000000}\r\n"
				"\r\n");
			msn_slpcall_session_init(slpcall);

			return;
		}

		/* Don't do anything if we already have a direct connection */
		if (slplink->dc != NULL)
			return;

		bridges = get_token(content, "Bridges: ", "\r\n");
		nonce = parse_dc_nonce(content, &ntype);
		if (bridges && strstr(bridges, "TCPv1") != NULL) {
			/*
			 * Ok, the client supports direct TCP connection
			 * Try to create a listening port
			 */
			MsnDirectConn *dc;

			dc = msn_dc_new(slpcall);
			if (ntype == DC_NONCE_PLAIN) {
				/* There is only one nonce for plain auth. */
				dc->nonce_type = ntype;
				memcpy(dc->nonce, nonce, 16);
			} else if (ntype == DC_NONCE_SHA1) {
				/* Each side has a nonce in SHA1 auth. */
				dc->nonce_type = ntype;
				strncpy(dc->remote_nonce, nonce, 36);
				dc->remote_nonce[36] = '\0';
			}

			dc->listen_data = purple_network_listen_range(
				0, 0,
				SOCK_STREAM,
				msn_dc_listen_socket_created_cb,
				dc
			);

			if (dc->listen_data == NULL) {
				/* Listen socket creation failed */

				purple_debug_info("msn", "got_invite: listening failed\n");

				if (dc->nonce_type != DC_NONCE_PLAIN)
					msn_slp_send_ok(slpcall, branch,
						"application/x-msnmsgr-transrespbody",
						"Bridge: TCPv1\r\n"
						"Listening: false\r\n"
						"Hashed-Nonce: {00000000-0000-0000-0000-000000000000}\r\n"
						"\r\n");
				else
					msn_slp_send_ok(slpcall, branch,
						"application/x-msnmsgr-transrespbody",
						"Bridge: TCPv1\r\n"
						"Listening: false\r\n"
						"Nonce: {00000000-0000-0000-0000-000000000000}\r\n"
						"\r\n");

			} else {
				/*
				 * Listen socket created successfully.
				 * Don't send anything here because we don't know the parameters
				 * of the created socket yet. msn_dc_send_ok will be called from
				 * the callback function: dc_listen_socket_created_cb
				 */
				purple_debug_info("msn", "got_invite: listening socket created\n");

				dc->send_connection_info_msg_cb = msn_dc_send_ok;
				slpcall->wait_for_socket = TRUE;
			}

		} else {
			/*
			 * Invalid direct connect invitation or
			 * TCP connection is not supported.
			 */
		}

		g_free(nonce);
		g_free(bridges);
	}
	else if (!strcmp(type, "application/x-msnmsgr-transrespbody"))
	{
		/* A direct connection negotiation response */
		msn_slp_process_transresp(slpcall, content);
	}
}

static void
got_ok(MsnSlpCall *slpcall,
	   const char *type, const char *content)
{
	g_return_if_fail(slpcall != NULL);
	g_return_if_fail(type    != NULL);

	if (!strcmp(type, "application/x-msnmsgr-sessionreqbody"))
	{
		char *content;
		char *header;
		char *nonce = NULL;
		MsnSession *session = slpcall->slplink->session;
		MsnSlpMessage *msg;
		MsnDirectConn *dc;
		MsnUser *user;

		if (!purple_account_get_bool(session->account, "direct_connect", TRUE)) {
			/* Don't attempt a direct connection if disabled. */
			msn_slpcall_session_init(slpcall);
			return;
		}

		if (slpcall->slplink->dc != NULL) {
			/* If we already have an established direct connection
			 * then just start the transfer.
			 */
			msn_slpcall_session_init(slpcall);
			return;
		}

		user = msn_userlist_find_user(session->userlist,
		                              slpcall->slplink->remote_user);
		if (!user || !(user->clientid & 0xF0000000))	{
			/* Just start a normal SB transfer. */
			msn_slpcall_session_init(slpcall);
			return;
		}

		/* Try direct file transfer by sending a second INVITE */
		dc = msn_dc_new(slpcall);
		slpcall->branch = rand_guid();

		dc->listen_data = purple_network_listen_range(
			0, 0,
			SOCK_STREAM,
			msn_dc_listen_socket_created_cb,
			dc
		);

		header = g_strdup_printf(
			"INVITE MSNMSGR:%s MSNSLP/1.0",
			slpcall->slplink->remote_user
		);

		if (dc->nonce_type == DC_NONCE_SHA1)
			nonce = g_strdup_printf("Hashed-Nonce: {%s}\r\n", dc->nonce_hash);

		if (dc->listen_data == NULL) {
			/* Listen socket creation failed */
			purple_debug_info("msn", "got_ok: listening failed\n");

			content = g_strdup_printf(
				"Bridges: TCPv1\r\n"
				"NetID: %u\r\n"
				"Conn-Type: IP-Restrict-NAT\r\n"
				"UPnPNat: false\r\n"
				"ICF: false\r\n"
				"%s"
				"\r\n",

				rand() % G_MAXUINT32,
				nonce ? nonce : ""
			);

		} else {
			/* Listen socket created successfully. */
			purple_debug_info("msn", "got_ok: listening socket created\n");

			content = g_strdup_printf(
				"Bridges: TCPv1\r\n"
				"NetID: 0\r\n"
				"Conn-Type: Direct-Connect\r\n"
				"UPnPNat: false\r\n"
				"ICF: false\r\n"
				"%s"
				"\r\n",

				nonce ? nonce : ""
			);
		}

		msg = msn_slpmsg_sip_new(
			slpcall,
			0,
			header,
			slpcall->branch,
			"application/x-msnmsgr-transreqbody",
			content
		);
		msg->info = "DC INVITE";
		msg->text_body = TRUE;
		g_free(nonce);
		g_free(header);
		g_free(content);

		msn_slplink_queue_slpmsg(slpcall->slplink, msg);
	}
	else if (!strcmp(type, "application/x-msnmsgr-transreqbody"))
	{
		/* Do we get this? */
		purple_debug_info("msn", "OK with transreqbody\n");
	}
	else if (!strcmp(type, "application/x-msnmsgr-transrespbody"))
	{
		msn_slp_process_transresp(slpcall, content);
	}
}

static void
got_error(MsnSlpCall *slpcall,
          const char *error, const char *type, const char *content)
{
	/* It's not valid. Kill this off. */
	purple_debug_error("msn", "Received non-OK result: %s\n",
	                   error ? error : "Unknown");

	if (type && (!strcmp(type, "application/x-msnmsgr-transreqbody")
	          || !strcmp(type, "application/x-msnmsgr-transrespbody"))) {
		MsnDirectConn *dc = slpcall->slplink->dc;
		if (dc) {
			msn_dc_fallback_to_sb(dc);
			return;
		}
	}

	slpcall->wasted = TRUE;
}

MsnSlpCall *
msn_slp_sip_recv(MsnSlpLink *slplink, const char *body)
{
	MsnSlpCall *slpcall;

	if (body == NULL)
	{
		purple_debug_warning("msn", "received bogus message\n");
		return NULL;
	}

	if (!strncmp(body, "INVITE", strlen("INVITE")))
	{
		char *branch;
		char *call_id;
		char *content;
		char *content_type;

		/* From: <msnmsgr:buddy@hotmail.com> */
#if 0
		slpcall->remote_user = get_token(body, "From: <msnmsgr:", ">\r\n");
#endif

		branch = get_token(body, ";branch={", "}");

		call_id = get_token(body, "Call-ID: {", "}");

#if 0
		long content_len = -1;

		temp = get_token(body, "Content-Length: ", "\r\n");
		if (temp != NULL)
			content_len = atoi(temp);
		g_free(temp);
#endif
		content_type = get_token(body, "Content-Type: ", "\r\n");

		content = get_token(body, "\r\n\r\n", NULL);

		slpcall = NULL;
		if (branch && call_id)
		{
			slpcall = msn_slplink_find_slp_call(slplink, call_id);
			if (slpcall)
			{
				g_free(slpcall->branch);
				slpcall->branch = g_strdup(branch);
				got_invite(slpcall, branch, content_type, content);
			}
			else if (content_type && content)
			{
				slpcall = msn_slpcall_new(slplink);
				slpcall->id = g_strdup(call_id);
				got_invite(slpcall, branch, content_type, content);
			}
		}

		g_free(call_id);
		g_free(branch);
		g_free(content_type);
		g_free(content);
	}
	else if (!strncmp(body, "MSNSLP/1.0 ", strlen("MSNSLP/1.0 ")))
	{
		char *content;
		char *content_type;
		/* Make sure this is "OK" */
		const char *status = body + strlen("MSNSLP/1.0 ");
		char *call_id;

		call_id = get_token(body, "Call-ID: {", "}");
		slpcall = msn_slplink_find_slp_call(slplink, call_id);
		g_free(call_id);

		g_return_val_if_fail(slpcall != NULL, NULL);

		content_type = get_token(body, "Content-Type: ", "\r\n");

		content = get_token(body, "\r\n\r\n", NULL);

		if (strncmp(status, "200 OK", 6))
		{
			char *error = NULL;
			const char *c;

			/* Eww */
			if ((c = strchr(status, '\r')) || (c = strchr(status, '\n')) ||
				(c = strchr(status, '\0')))
			{
				size_t len = c - status;
				error = g_strndup(status, len);
			}

			got_error(slpcall, error, content_type, content);
			g_free(error);

		} else {
			/* Everything's just dandy */
			got_ok(slpcall, content_type, content);
		}

		g_free(content_type);
		g_free(content);
	}
	else if (!strncmp(body, "BYE", strlen("BYE")))
	{
		char *call_id;

		call_id = get_token(body, "Call-ID: {", "}");
		slpcall = msn_slplink_find_slp_call(slplink, call_id);
		g_free(call_id);

		if (slpcall != NULL)
			slpcall->wasted = TRUE;

		/* msn_slpcall_destroy(slpcall); */
	}
	else
		slpcall = NULL;

	return slpcall;
}

/**************************************************************************
 * Msg Callbacks
 **************************************************************************/

void
msn_p2p_msg(MsnCmdProc *cmdproc, MsnMessage *msg)
{
	MsnSession *session;
	MsnSlpLink *slplink;
	const char *data;
	gsize len;

	session = cmdproc->servconn->session;
	slplink = msn_session_get_slplink(session, msg->remote_user);

	if (slplink->swboard == NULL)
	{
		/*
		 * We will need swboard in order to change its flags.  If its
		 * NULL, something has probably gone wrong earlier on.  I
		 * didn't want to do this, but MSN 7 is somehow causing us
		 * to crash here, I couldn't reproduce it to debug more,
		 * and people are reporting bugs. Hopefully this doesn't
		 * cause more crashes. Stu.
		 */
		if (cmdproc->data == NULL)
			g_warning("msn_p2p_msg cmdproc->data was NULL\n");
		else {
			slplink->swboard = (MsnSwitchBoard *)cmdproc->data;
			slplink->swboard->slplinks = g_list_prepend(slplink->swboard->slplinks, slplink);
		}
	}

	data = msn_message_get_bin_data(msg, &len);

	msn_slplink_process_msg(slplink, &msg->msnslp_header, data, len);
}

static void
got_emoticon(MsnSlpCall *slpcall,
			 const guchar *data, gsize size)
{
	PurpleConversation *conv;
	MsnSwitchBoard *swboard;

	swboard = slpcall->slplink->swboard;
	conv = swboard->conv;

	if (conv) {
		/* FIXME: it would be better if we wrote the data as we received it
		   instead of all at once, calling write multiple times and
		   close once at the very end
		 */
		purple_conv_custom_smiley_write(conv, slpcall->data_info, data, size);
		purple_conv_custom_smiley_close(conv, slpcall->data_info );
	}
	if (purple_debug_is_verbose())
		purple_debug_info("msn", "Got smiley: %s\n", slpcall->data_info);
}

void
msn_emoticon_msg(MsnCmdProc *cmdproc, MsnMessage *msg)
{
	MsnSession *session;
	MsnSlpLink *slplink;
	MsnSwitchBoard *swboard;
	MsnObject *obj;
	char **tokens;
	char *smile, *body_str;
	const char *body, *who, *sha1;
	guint tok;
	size_t body_len;

	PurpleConversation *conv;

	session = cmdproc->servconn->session;

	if (!purple_account_get_bool(session->account, "custom_smileys", TRUE))
		return;

	swboard = cmdproc->data;
	conv = swboard->conv;

	body = msn_message_get_bin_data(msg, &body_len);
	if (!body || !body_len)
		return;
	body_str = g_strndup(body, body_len);

	/* MSN Messenger 7 may send more than one MSNObject in a single message...
	 * Maybe 10 tokens is a reasonable max value. */
	tokens = g_strsplit(body_str, "\t", 10);

	g_free(body_str);

	for (tok = 0; tok < 9; tok += 2) {
		if (tokens[tok] == NULL || tokens[tok + 1] == NULL) {
			break;
		}

		smile = tokens[tok];
		obj = msn_object_new_from_string(purple_url_decode(tokens[tok + 1]));

		if (obj == NULL)
			break;

		who = msn_object_get_creator(obj);
		sha1 = msn_object_get_sha1(obj);

		slplink = msn_session_get_slplink(session, who);
		if (slplink->swboard != swboard) {
			if (slplink->swboard != NULL)
				/*
				 * Apparently we're using a different switchboard now or
				 * something?  I don't know if this is normal, but it
				 * definitely happens.  So make sure the old switchboard
				 * doesn't still have a reference to us.
				 */
				slplink->swboard->slplinks = g_list_remove(slplink->swboard->slplinks, slplink);
			slplink->swboard = swboard;
			slplink->swboard->slplinks = g_list_prepend(slplink->swboard->slplinks, slplink);
		}

		/* If the conversation doesn't exist then this is a custom smiley
		 * used in the first message in a MSN conversation: we need to create
		 * the conversation now, otherwise the custom smiley won't be shown.
		 * This happens because every GtkIMHtml has its own smiley tree: if
		 * the conversation doesn't exist then we cannot associate the new
		 * smiley with its GtkIMHtml widget. */
		if (!conv) {
			conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, session->account, who);
		}

		if (purple_conv_custom_smiley_add(conv, smile, "sha1", sha1, TRUE)) {
			msn_slplink_request_object(slplink, smile, got_emoticon, NULL, obj);
		}

		msn_object_destroy(obj);
		obj =   NULL;
		who =   NULL;
		sha1 = NULL;
	}
	g_strfreev(tokens);
}

static gboolean
buddy_icon_cached(PurpleConnection *gc, MsnObject *obj)
{
	PurpleAccount *account;
	PurpleBuddy *buddy;
	const char *old;
	const char *new;

	g_return_val_if_fail(obj != NULL, FALSE);

	account = purple_connection_get_account(gc);

	buddy = purple_find_buddy(account, msn_object_get_creator(obj));
	if (buddy == NULL)
		return FALSE;

	old = purple_buddy_icons_get_checksum_for_user(buddy);
	new = msn_object_get_sha1(obj);

	if (new == NULL)
		return FALSE;

	/* If the old and new checksums are the same, and the file actually exists,
	 * then return TRUE */
	if (old != NULL && !strcmp(old, new))
		return TRUE;

	return FALSE;
}

static void
msn_release_buddy_icon_request(MsnUserList *userlist)
{
	MsnUser *user;

	g_return_if_fail(userlist != NULL);

	if (purple_debug_is_verbose())
		purple_debug_info("msn", "Releasing buddy icon request\n");

	if (userlist->buddy_icon_window > 0)
	{
		GQueue *queue;
		PurpleAccount *account;
		const char *username;

		queue = userlist->buddy_icon_requests;

		if (g_queue_is_empty(userlist->buddy_icon_requests))
			return;

		user = g_queue_pop_head(queue);

		account  = userlist->session->account;
		username = user->passport;

		userlist->buddy_icon_window--;
		request_user_display(user);

		if (purple_debug_is_verbose())
			purple_debug_info("msn",
			                  "msn_release_buddy_icon_request(): buddy_icon_window-- yields =%d\n",
			                  userlist->buddy_icon_window);
	}
}

/*
 * Called on a timeout from end_user_display(). Frees a buddy icon window slow and dequeues the next
 * buddy icon request if there is one.
 */
static gboolean
msn_release_buddy_icon_request_timeout(gpointer data)
{
	MsnUserList *userlist = (MsnUserList *)data;

	/* Free one window slot */
	userlist->buddy_icon_window++;

	/* Clear the tag for our former request timer */
	userlist->buddy_icon_request_timer = 0;

	msn_release_buddy_icon_request(userlist);

	return FALSE;
}

void
msn_queue_buddy_icon_request(MsnUser *user)
{
	PurpleAccount *account;
	MsnObject *obj;
	GQueue *queue;

	g_return_if_fail(user != NULL);

	account = user->userlist->session->account;

	obj = msn_user_get_object(user);

	if (obj == NULL)
	{
		purple_buddy_icons_set_for_user(account, user->passport, NULL, 0, NULL);
		return;
	}

	if (!buddy_icon_cached(account->gc, obj))
	{
		MsnUserList *userlist;

		userlist = user->userlist;
		queue = userlist->buddy_icon_requests;

		if (purple_debug_is_verbose())
			purple_debug_info("msn", "Queueing buddy icon request for %s (buddy_icon_window = %i)\n",
			                  user->passport, userlist->buddy_icon_window);

		g_queue_push_tail(queue, user);

		if (userlist->buddy_icon_window > 0)
			msn_release_buddy_icon_request(userlist);
	}
}

static void
got_user_display(MsnSlpCall *slpcall,
				 const guchar *data, gsize size)
{
	MsnUserList *userlist;
	const char *info;
	PurpleAccount *account;

	g_return_if_fail(slpcall != NULL);

	info = slpcall->data_info;
	if (purple_debug_is_verbose())
		purple_debug_info("msn", "Got User Display: %s\n", slpcall->slplink->remote_user);

	userlist = slpcall->slplink->session->userlist;
	account = slpcall->slplink->session->account;

	purple_buddy_icons_set_for_user(account, slpcall->slplink->remote_user,
								  g_memdup(data, size), size, info);

#if 0
	/* Free one window slot */
	userlist->buddy_icon_window++;

	purple_debug_info("msn", "got_user_display(): buddy_icon_window++ yields =%d\n",
					userlist->buddy_icon_window);

	msn_release_buddy_icon_request(userlist);
#endif
}

static void
end_user_display(MsnSlpCall *slpcall, MsnSession *session)
{
	MsnUserList *userlist;

	g_return_if_fail(session != NULL);

	if (purple_debug_is_verbose())
		purple_debug_info("msn", "End User Display\n");

	userlist = session->userlist;

	/* If the session is being destroyed we better stop doing anything. */
	if (session->destroying)
		return;

	/* Delay before freeing a buddy icon window slot and requesting the next icon, if appropriate.
	 * If we don't delay, we'll rapidly hit the MSN equivalent of AIM's rate limiting; the server will
	 * send us an error 800 like so:
	 *
	 * C: NS 000: XFR 21 SB
	 * S: NS 000: 800 21
	 */
	if (userlist->buddy_icon_request_timer) {
		/* Free the window slot used by this previous request */
		userlist->buddy_icon_window++;

		/* Clear our pending timeout */
		purple_timeout_remove(userlist->buddy_icon_request_timer);
	}

	/* Wait BUDDY_ICON_DELAY s before freeing our window slot and requesting the next icon. */
	userlist->buddy_icon_request_timer = purple_timeout_add_seconds(BUDDY_ICON_DELAY,
														  msn_release_buddy_icon_request_timeout, userlist);
}

static void
fetched_user_display(PurpleUtilFetchUrlData *url_data, gpointer user_data,
	const gchar *url_text, gsize len, const gchar *error_message)
{
	MsnFetchUserDisplayData *data = user_data;
	MsnSession *session = data->session;

	session->url_datas = g_slist_remove(session->url_datas, url_data);

	if (url_text) {
		purple_buddy_icons_set_for_user(session->account, data->remote_user,
		                                g_memdup(url_text, len), len,
		                                data->sha1);
	}

	end_user_display(NULL, session);

	g_free(user_data);
}

static void
request_user_display(MsnUser *user)
{
	PurpleAccount *account;
	MsnSession *session;
	MsnSlpLink *slplink;
	MsnObject *obj;
	const char *info;

	session = user->userlist->session;
	account = session->account;

	slplink = msn_session_get_slplink(session, user->passport);

	obj = msn_user_get_object(user);

	info = msn_object_get_sha1(obj);

	if (g_ascii_strcasecmp(user->passport,
						   purple_account_get_username(account)))
	{
		const char *url = msn_object_get_url1(obj);
		if (url) {
			MsnFetchUserDisplayData *data = g_new0(MsnFetchUserDisplayData, 1);
			PurpleUtilFetchUrlData *url_data;
			data->session = session;
			data->remote_user = user->passport;
			data->sha1 = info;
			url_data = purple_util_fetch_url_len(url, TRUE, NULL, TRUE, 200*1024,
			                                     fetched_user_display, data);
			session->url_datas = g_slist_prepend(session->url_datas, url_data);
		} else {
			msn_slplink_request_object(slplink, info, got_user_display,
			                           end_user_display, obj);
		}
	}
	else
	{
		MsnObject *my_obj = NULL;
		gconstpointer data = NULL;
		size_t len = 0;

		if (purple_debug_is_verbose())
			purple_debug_info("msn", "Requesting our own user display\n");

		my_obj = msn_user_get_object(session->user);

		if (my_obj != NULL)
		{
			PurpleStoredImage *img = msn_object_get_image(my_obj);
			data = purple_imgstore_get_data(img);
			len = purple_imgstore_get_size(img);
		}

		purple_buddy_icons_set_for_user(account, user->passport, g_memdup(data, len), len, info);

		/* Free one window slot */
		session->userlist->buddy_icon_window++;

		if (purple_debug_is_verbose())
			purple_debug_info("msn", "request_user_display(): buddy_icon_window++ yields =%d\n",
			                  session->userlist->buddy_icon_window);

		msn_release_buddy_icon_request(session->userlist);
	}
}
