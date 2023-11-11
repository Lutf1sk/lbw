#ifndef WEBSOCK_H
#define WEBSOCK_H 1

#define WS_KEY "dGhlIHNhbXBsZSBub25jZQ=="
#define WS_ACC "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="

#define WS_CONTINUE	0x0
#define WS_TEXT		0x1
#define WS_BIN		0x2
#define WS_CLOSE	0x8
#define WS_PING		0x9
#define WS_PONG		0xA

#define WS_OP_MASK 0x0F

#define WS_FIN 0x80

#define WS_FRAME_MAX 0x20

#include <lt/fwd.h>
#include <lt/err.h>

usz ws_write_frame_start(void* out, u8 op, usz len);
isz ws_send_frame_start(u8 op, usz len, lt_io_callback_t callb, void* usr);
isz ws_send_text(lstr_t data, lt_io_callback_t callb, void* usr);

lt_err_t ws_recv(lt_io_callback_t callb, void* usr, u8* out_op, lstr_t* out_payload, lt_alloc_t* alloc);

#endif
