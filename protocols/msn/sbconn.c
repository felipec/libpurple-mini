#include "internal.h"
#include "debug.h"

#include "msg.h"
#include "sbconn.h"

void msn_sbconn_send_part(MsnSlpLink *slplink, MsnSlpMessagePart *part)
{
	MsnMessage *msg;
	const char *passport;
	char *data;
	size_t size;

	msg = msn_message_new_msnslp();

	passport = purple_normalize(slplink->session->account, slplink->remote_user);
	msn_message_set_header(msg, "P2P-Dest", passport);

	msg->part = msn_slpmsgpart_ref(part);
	data = msn_slpmsgpart_serialize(part, &size);
	msn_message_set_bin_data(msg, data, size);
	g_free(data);

	if (slplink->swboard == NULL)
	{
		slplink->swboard = msn_session_get_swboard(slplink->session,
				slplink->remote_user, MSN_SB_FLAG_FT);

		g_return_if_fail(slplink->swboard != NULL);

		/* If swboard is destroyed we will be too */
		slplink->swboard->slplinks = g_list_prepend(slplink->swboard->slplinks, slplink);
	}

	msn_switchboard_send_msg(slplink->swboard, msg, TRUE);
	msn_message_destroy(msg);
}

/** Called when a message times out. */
static void
msg_timeout(MsnCmdProc *cmdproc, MsnTransaction *trans)
{
	MsnMessage *msg;

	msg = trans->data;

	msg_error_helper(cmdproc, msg, MSN_MSG_ERROR_TIMEOUT);
}

static void
release_msg(MsnSwitchBoard *swboard, MsnMessage *msg)
{
	MsnCmdProc *cmdproc;
	MsnTransaction *trans;
	char *payload;
	gsize payload_len;
	char flag;

	g_return_if_fail(swboard != NULL);
	g_return_if_fail(msg     != NULL);

	cmdproc = swboard->cmdproc;

	payload = msn_message_gen_payload(msg, &payload_len);

	if (purple_debug_is_verbose()) {
		purple_debug_info("msn", "SB length:{%" G_GSIZE_FORMAT "}\n", payload_len);
		msn_message_show_readable(msg, "SB SEND", FALSE);
	}

	flag = msn_message_get_flag(msg);
	trans = msn_transaction_new(cmdproc, "MSG", "%c %" G_GSIZE_FORMAT,
								flag, payload_len);

	/* Data for callbacks */
	msn_transaction_set_data(trans, msg);

	if (flag != 'U') {
		if (msg->type == MSN_MSG_TEXT)
		{
			msg->ack_ref = TRUE;
			msn_message_ref(msg);
			swboard->ack_list = g_list_append(swboard->ack_list, msg);
			msn_transaction_set_timeout_cb(trans, msg_timeout);
		}
		else if (msg->type == MSN_MSG_SLP)
		{
			msg->ack_ref = TRUE;
			msn_message_ref(msg);
			swboard->ack_list = g_list_append(swboard->ack_list, msg);
			msn_transaction_set_timeout_cb(trans, msg_timeout);
#if 0
			if (msg->ack_cb != NULL)
			{
				msn_transaction_add_cb(trans, "ACK", msg_ack);
				msn_transaction_add_cb(trans, "NAK", msg_nak);
			}
#endif
		}
	}

	trans->payload = payload;
	trans->payload_len = payload_len;

	msg->trans = trans;

	msn_cmdproc_send_trans(cmdproc, trans);
}

static void
queue_msg(MsnSwitchBoard *swboard, MsnMessage *msg)
{
	g_return_if_fail(swboard != NULL);
	g_return_if_fail(msg     != NULL);

	purple_debug_info("msn", "Appending message to queue.\n");

	g_queue_push_tail(swboard->msg_queue, msg);

	msn_message_ref(msg);
}

void
msn_sbconn_process_queue(MsnSwitchBoard *swboard)
{
	MsnMessage *msg;

	g_return_if_fail(swboard != NULL);

	purple_debug_info("msn", "Processing queue\n");

	while ((msg = g_queue_pop_head(swboard->msg_queue)) != NULL)
	{
		purple_debug_info("msn", "Sending message\n");
		release_msg(swboard, msg);
		msn_message_unref(msg);
	}
}

void
msn_switchboard_send_msg(MsnSwitchBoard *swboard, MsnMessage *msg,
						 gboolean queue)
{
	g_return_if_fail(swboard != NULL);
	g_return_if_fail(msg     != NULL);

	purple_debug_info("msn", "switchboard send msg..\n");
	if (msn_switchboard_can_send(swboard))
		release_msg(swboard, msg);
	else if (queue)
		queue_msg(swboard, msg);
}
