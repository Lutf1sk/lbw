#include "websock.h"

#include <lt/net.h>
#include <lt/mem.h>
#include <lt/internal.h>

usz ws_write_frame_start(void* out, u8 op, usz len) {
	if (len < 126) {
		u8 frame[6] = {
			op, (len) | 0x80,
			0x00, 0x00, 0x00, 0x00,
		};
		memcpy(out, frame, sizeof(frame));
		return sizeof(frame);
	}
	else if (len < 65536) {
		u8 frame[8] = {
			op, 126 | 0x80,
			len >> 8, len,
			0x00, 0x00, 0x00, 0x00,
		};
		memcpy(out, frame, sizeof(frame));
		return sizeof(frame);
	}
	else {
		u8 frame[14] = {
			op, 127 | 0x80,
			len >> 56, len >> 48, len >> 40, len >> 32, len >> 24, len >> 16, len >> 8, len,
			0x00, 0x00, 0x00, 0x00,
		};
		memcpy(out, frame, sizeof(frame));
		return sizeof(frame);
	}
}

isz ws_send_frame_start(u8 op, usz len, lt_io_callback_t callb, void* usr) {
	u8 frame[WS_FRAME_MAX];
	usz frame_len = ws_write_frame_start(frame, op, len);
	return callb(usr, frame, frame_len);
}

isz ws_send_text(lstr_t data, lt_io_callback_t callb, void* usr) {
	isz res = ws_send_frame_start(WS_FIN | WS_TEXT, data.len, callb, usr);
	if (res < 0)
		return res;
	isz res2 = callb(usr, data.str, data.len);
	if (res2 < 0)
		return res;
	return res + res2;
}

lt_err_t ws_recv(lt_io_callback_t callb, void* usr, u8* out_op, lstr_t* out_payload, lt_alloc_t* alloc) {
	lt_err_t err;

	u8 frame[8];
	isz res = callb(usr, frame, 2);
	if (res < 0)
		return -res;

	u8 op = frame[0] & WS_OP_MASK;

	usz payload_len = frame[1] & 0x7F;
	if (payload_len == 126) {
		res = callb(usr, frame, 2);
		if (res < 0)
			return -res;
		payload_len = frame[1] | (frame[0] << 8);
	}
	else if (payload_len == 127) {
		res = callb(usr, frame, 8);
		if (res < 0)
			return -res;
		payload_len = (u64)frame[7] | ((u64)frame[6] << 8) | ((u64)frame[5] << 16) | ((u64)frame[4] << 24) |
				 ((u64)frame[3] << 32) | ((u64)frame[2] << 40) | ((u64)frame[1] << 48) | ((u64)frame[0] << 56);
	}

	u8* payload = NULL;
	if (payload_len) {
		payload = lt_malloc(alloc, payload_len);
		if (!payload)
			return LT_ERR_OUT_OF_MEMORY;
		if ((res = callb(usr, payload, payload_len)) < 0)
			fail_to(err = -res, err0);
	}

	*out_op = op;
	*out_payload = LSTR(payload, payload_len);
	return LT_SUCCESS;

err0:	if (payload)
			lt_mfree(alloc, payload);
		return err;
}
