#include <lt/io.h>
#include <lt/mem.h>
#include <lt/net.h>
#include <lt/thread.h>
#include <lt/term.h>
#include <lt/http.h>
#include <lt/ssl.h>
#include <lt/str.h>
#include <lt/json.h>
#include <lt/time.h>
#include <lt/conf.h>
#include <lt/math.h>
#include <lt/darr.h>
#include <lt/strstream.h>
#include <lt/ctype.h>
#include <lt/texted.h>
#include <lt/text.h>

#define LT_ANSI_SHORTEN_NAMES 1
#include <lt/ansi.h>

#include <lt/internal.h>

#include "websock.h"

#include "mission.h"
#include "location.h"
#include "draw.h"

// #define TEST_MODE

lt_ssl_connection_t* ssl = NULL;

lt_socket_t* sock = NULL;
lt_alloc_t* alloc = NULL;

lstr_t host_addr;
u16 host_port;

lstr_t api_key;
lstr_t id_token;
lstr_t refresh_token;
lstr_t auth_token;

b8 done = 0;

lt_mutex_t* state_lock;

volatile b8 login_verified = 0;
lstr_t login_err;

#define VIEW_NONE			0
#define VIEW_LOCATIONS		1
#define VIEW_MISSIONS		2
#define VIEW_MAIL			3
#define VIEW_CHAT			4
#define VIEW_LEADERBOARDS	5

usz view = VIEW_NONE;

lt_darr(mission_t) active_missions;
lt_darr(mission_t) mission_board;

// -----===== chat

typedef
struct chat_msg {
	isz type;
	i64 created_at;
	lstr_t sender_name;
	lstr_t message;
} chat_msg_t;

lt_darr(chat_msg_t) chat_msgs;

// -----===== profile

lstr_t player_name;
lstr_t player_id;

// -----===== area

lstr_t area_name;

lt_darr(location_t) area_locations;

location_t current_location;
volatile usz location_change_count = 0;

// -----===== stats

isz aim;
isz attack;
isz defend;
isz dodge;

isz level;
isz xp;
isz xp_max;

isz hp;
isz hp_max;

isz spirit;
isz spirit_max;

isz energy;
isz energy_max;

isz credits;

b8 is_overdosed;

// -----===== resource ticks

isz energy_tick_at_msec;
isz spirit_tick_at_msec;

isz energy_tick_interval_msec;
isz spirit_tick_interval_msec;

isz energy_increment;
isz spirit_increment;

void try_load_str(lt_json_t* parent, lstr_t key, lstr_t* out) {
	lt_json_t* child = lt_json_find_child(parent, key);
	if (child && child->stype == LT_JSON_STRING)
		*out = lt_strdup(lt_libc_heap, child->str_val);
}

void try_load_int(lt_json_t* parent, lstr_t key, isz* out) {
	lt_json_t* child = lt_json_find_child(parent, key);
	if (child && child->stype == LT_JSON_NUMBER)
		*out = lt_json_int_val(child);
}

void try_load_bool(lt_json_t* parent, lstr_t key, b8* out) {
	lt_json_t* child = lt_json_find_child(parent, key);
	if (child && child->stype == LT_JSON_BOOL)
		*out = lt_json_bool_val(child);
}

lt_json_t* try_find_array(lt_json_t* parent, lstr_t key) {
	lt_json_t* arr = lt_json_find_child(parent, key);
	if (!arr || arr->stype != LT_JSON_ARRAY)
		return NULL;
	return arr;
}

lt_json_t* try_find_object(lt_json_t* parent, lstr_t key) {
	lt_json_t* obj = lt_json_find_child(parent, key);
	if (!obj || obj->stype != LT_JSON_OBJECT)
		return NULL;
	return obj;
}

void read_stats(lt_json_t* parent) {
	try_load_int(parent, CLSTR("aim"), &aim);
	try_load_int(parent, CLSTR("attack"), &attack);
	try_load_int(parent, CLSTR("defend"), &defend);
	try_load_int(parent, CLSTR("dodge"), &dodge);

	try_load_int(parent, CLSTR("level"), &level);
	try_load_int(parent, CLSTR("expCurrent"), &xp);
	try_load_int(parent, CLSTR("expMax"), &xp_max);

	try_load_int(parent, CLSTR("healthCurrent"), &hp);
	try_load_int(parent, CLSTR("healthMax"), &hp_max);

	try_load_int(parent, CLSTR("spiritCurrent"), &spirit);
	try_load_int(parent, CLSTR("spiritMax"), &spirit_max);

	try_load_int(parent, CLSTR("energyCurrent"), &energy);
	try_load_int(parent, CLSTR("energyMax"), &energy_max);

	try_load_int(parent, CLSTR("credits"), &credits);

	try_load_bool(parent, CLSTR("isOverdosed"), &is_overdosed);
}

void read_resource_ticks(lt_json_t* parent) {
	lt_json_t* ticks = try_find_array(parent, CLSTR("resourceTicks"));
	if (ticks && ticks->child_count == 2) {
		if (ticks->child->stype == LT_JSON_NUMBER)
			energy_tick_at_msec = lt_hfreq_time_msec() + lt_json_int_val(ticks->child);
		if (ticks->child->next->stype == LT_JSON_NUMBER)
			spirit_tick_at_msec = lt_hfreq_time_msec() + lt_json_int_val(ticks->child->next);
	}
}

void read_mission_board(lt_json_t* json) {
	for (usz i = 0; i < lt_darr_count(mission_board); ++i)
		mission_free(&mission_board[i]);
	lt_darr_clear(mission_board);

	for (lt_json_t* it = json->child; it; it = it->next) {
		if (it->stype != LT_JSON_OBJECT)
			continue;

		mission_t mission;
		mission_load(it, &mission);
		lt_darr_push(mission_board, mission);
	}
}

void read_active_missions(lt_json_t* json) {
	for (usz i = 0; i < lt_darr_count(active_missions); ++i)
		mission_free(&active_missions[i]);
	lt_darr_clear(active_missions);

	for (lt_json_t* it = json->child; it; it = it->next) {
		if (it->stype != LT_JSON_OBJECT)
			continue;

		lt_json_t* mission_json = try_find_object(it, CLSTR("mission"));
		if (!mission_json)
			continue;

		mission_t mission;
		mission_load(mission_json, &mission);
		try_load_bool(mission_json, CLSTR("complete"), &mission.complete);
		lt_darr_push(active_missions, mission);

		lt_json_t* requirements = try_find_array(it, CLSTR("requirements"));
		if (!requirements)
			continue;
		usz req_i = 0;
		for (lt_json_t* it = requirements->child; it && req_i < lt_darr_count(mission.requirements); it = it->next)
			try_load_int(it, CLSTR("quantity"), &mission.requirements[req_i++].current_count);
	}
}

lstr_t unescape_json_str(lstr_t str) {
	lt_strstream_t ss;
	LT_ASSERT(lt_strstream_create(&ss, lt_libc_heap) == LT_SUCCESS);

	char* it = str.str, *end = it + str.len;
	while (it < end) {
		char c = *it++;

		if (c != '\\' || it >= end) {
			lt_strstream_writec(&ss, c);
			continue;
		}

		switch (*it++) {
		case '\\': lt_strstream_writec(&ss, '\\'); break;
		case 'r': lt_strstream_writec(&ss, '\r'); break;
		case 'n': lt_strstream_writec(&ss, '\n'); break;
		case 't': lt_strstream_writec(&ss, '\t'); break;
		case 'v': lt_strstream_writec(&ss, '\v'); break;
		case '"': lt_strstream_writec(&ss, '\"'); break;
		case '\'': lt_strstream_writec(&ss, '\''); break;
		}
	}

	return ss.str;
}

void read_area(lt_json_t* area) {
	try_load_str(area, CLSTR("name"), &area_name);

	lt_json_t* locations = try_find_array(area, CLSTR("locations"));
	if (!locations)
		return;

	for (usz i = 0; i < lt_darr_count(area_locations); ++i)
		location_free(&area_locations[i]);
	lt_darr_clear(area_locations);

	for (lt_json_t* it = locations->child; it; it = it->next) {
		if (it->stype != LT_JSON_OBJECT)
			continue;
		location_t loc;
		location_load(it, &loc);
		lt_darr_push(area_locations, loc);
	}
}

void load_chat_msg(lt_json_t* json, chat_msg_t* out) {
	memset(out, 0, sizeof(*out));

	try_load_str(json, CLSTR("message"), &out->message);
	try_load_int(json, CLSTR("messageType"), &out->type);

	lt_json_t* user = try_find_object(json, CLSTR("user"));
	if (user)
		try_load_str(user, CLSTR("name"), &out->sender_name);
}

void read_chat_history(lt_json_t* json) {
	for (lt_json_t* it = json->child; it; it = it->next) {
		chat_msg_t msg;
		load_chat_msg(it, &msg);
		lt_darr_insert(chat_msgs, 0, &msg, 1);
	}
}

void on_msg(lt_json_t* json) {
#ifdef TEST_MODE
	lt_json_print(lt_stdout, json);
#endif

	read_stats(json);
	lt_json_t* stats = try_find_object(json, CLSTR("stats"));
	if (stats)
		read_stats(stats);

	lt_json_t* area = try_find_object(json, CLSTR("area"));
	if (area)
		read_area(area);

	lt_json_t* location = try_find_object(json, CLSTR("location"));
	if (location) {
		location_load(location, &current_location);
		++location_change_count;

		lt_json_t* missions = try_find_array(location, CLSTR("missionBoard"));
		if (missions)
			read_mission_board(missions);
	}

	lt_json_t* active_missions_json = try_find_array(json, CLSTR("activeMissions"));
	if (active_missions_json)
		read_active_missions(active_missions_json);

	lt_json_t* add_message = try_find_object(json, CLSTR("addChatMessage"));
	if (add_message) {
		chat_msg_t msg;
		load_chat_msg(add_message, &msg);
		lt_darr_push(chat_msgs, msg);
	}

	read_resource_ticks(json);
}

void on_login_verified(lt_json_t* json) {
	try_load_str(json, CLSTR("error"), &login_err);

	login_verified = 1;

	lt_json_t* login_data = try_find_object(json, CLSTR("data"));
	if (!login_data)
		return;

	try_load_str(json, CLSTR("name"), &player_name);
	try_load_str(json, CLSTR("id"), &player_id);

	read_stats(login_data);
	read_resource_ticks(login_data);

	lt_json_t* area = try_find_object(login_data, CLSTR("area"));
	if (area)
		read_area(area);

	lt_json_t* location = try_find_object(login_data, CLSTR("location"));
	if (location) {
		location_load(location, &current_location);
		lt_json_t* missions = try_find_array(location, CLSTR("missionBoard"));
		if (missions)
			read_mission_board(missions);
	}

	lt_json_t* active_missions_json = try_find_array(login_data, CLSTR("activeMissions"));
	if (active_missions_json)
		read_active_missions(active_missions_json);

	lt_json_t* chat_history = try_find_array(login_data, CLSTR("chatMessageHistory"));
	if (chat_history)
		read_chat_history(chat_history);

	try_load_int(login_data, CLSTR("energyIncrement"), &energy_increment);
	try_load_int(login_data, CLSTR("spiritIncrement"), &spirit_increment);

	lt_json_t* tmp;
	if ((tmp = lt_json_find_child(login_data, CLSTR("energyMinuteInterval"))) && tmp->stype == LT_JSON_NUMBER)
		energy_tick_interval_msec = lt_json_int_val(tmp) * 60 * 1000;
	if ((tmp = lt_json_find_child(login_data, CLSTR("spiritMinuteInterval"))) && tmp->stype == LT_JSON_NUMBER)
		spirit_tick_interval_msec = lt_json_int_val(tmp) * 60 * 1000;
}

void recv_proc(void* usr) {
	lt_err_t err;
	lt_alloc_t* alloc = lt_libc_heap;

	while (!done) {
		lstr_t payload;
		u8 op;
		err = ws_recv((lt_io_callback_t)lt_ssl_recv_fixed, ssl, &op, &payload, lt_libc_heap);
		if (err == LT_ERR_CLOSED)
			lt_ferrf("connection closed by host");
		if (err != LT_SUCCESS)
			lt_ferrf("ws_recv() failed");

		if (op == WS_CLOSE) {
			ws_send_frame_start(WS_FIN | WS_CLOSE, 0, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
			lt_ferrf("connection closed by host");
		}
		else if (op == WS_PING)
			ws_send_frame_start(WS_FIN | WS_PONG, 0, (lt_io_callback_t)lt_ssl_send_fixed, ssl);

		if (op != WS_TEXT || !payload.len)
			continue;

		if (payload.str[0] == '2') {
			ws_send_text(CLSTR("3"), (lt_io_callback_t)lt_ssl_send_fixed, ssl);
			continue;
		}

		lstr_t pfx = CLSTR("42/private,");
		lt_json_t* json = lt_json_parse(lt_libc_heap, payload.str + pfx.len, payload.len - pfx.len); // !! leaked
		if (!json || json->stype != LT_JSON_ARRAY)
			continue;

		lt_mutex_lock(state_lock);

		for (lt_json_t* it = json->child; it; it = it->next) {
			lt_json_t* key = it;

			it = it->next;
			if (key->stype != LT_JSON_STRING || !it)
				break;

			if (lt_lseq(key->str_val, CLSTR("message")))
				on_msg(it);
			if (lt_lseq(key->str_val, CLSTR("loginVerified")))
				on_login_verified(it);
		}
		lt_mutex_release(state_lock);

		lt_mfree(lt_libc_heap, payload.str);
	}
}

void goto_location(lstr_t name) {
	lstr_t msg = lt_lsbuild(lt_libc_heap, "42/private,[\"changeLocation\",\"%S\"]", name);
	isz ws_res = ws_send_text(msg, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
	LT_ASSERT(ws_res >= 0);
	lt_mfree(lt_libc_heap, msg.str);

	usz changes = location_change_count;
	lt_mutex_release(state_lock);
	while (location_change_count == changes)
		lt_sleep_msec(1);
	lt_mutex_lock(state_lock);
}

void accept_mission(lstr_t location_name, lstr_t mission_name) {
	lstr_t msg = lt_lsbuild(lt_libc_heap, "42/private,[\"acceptLocationMission\",[\"%S\",\"%S\"]]", location_name, mission_name);
	isz ws_res = ws_send_text(msg, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
	LT_ASSERT(ws_res >= 0);
	lt_mfree(lt_libc_heap, msg.str);
}

void complete_mission(lstr_t mission_name) {
	static isz req_num = 0;

	lstr_t msg = lt_lsbuild(lt_libc_heap, "42/private,%iz[\"completeMission\",\"%S\"]", req_num++, mission_name);
	isz ws_res = ws_send_text(msg, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
	LT_ASSERT(ws_res >= 0);
	lt_mfree(lt_libc_heap, msg.str);
}

void send_chat(lstr_t msg) {
	lstr_t payload = lt_lsbuild(lt_libc_heap, "42/private,[\"sendChatMessage\",\"%S\"]", msg);
	isz ws_res = ws_send_text(payload, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
	LT_ASSERT(ws_res >= 0);
	lt_mfree(lt_libc_heap, payload.str);
}

void work_job(lstr_t location_name, isz count) {
	lstr_t payload = lt_lsbuild(lt_libc_heap, "42/private,[\"performJob\",[\"%S\",%iz]]", location_name, count);
	isz ws_res = ws_send_text(payload, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
	LT_ASSERT(ws_res >= 0);
	lt_mfree(lt_libc_heap, payload.str);
}

void train_stat(lstr_t location_name, lstr_t stat_id, isz count) {
	lstr_t payload = lt_lsbuild(lt_libc_heap, "42/private,[\"performWorkout\",[\"%S\",\"%S\",%iz]]", location_name, stat_id, count);
	isz ws_res = ws_send_text(payload, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
	LT_ASSERT(ws_res >= 0);
	lt_mfree(lt_libc_heap, payload.str);
}

void send_proc(void* usr) {
	while (!done) {
		lt_sleep_msec(1);

// 		lstr_t msg = CLSTR("42/private,[\"performJob\",[\"The Waking Ship - Kitchens\",1]]");
// 		isz ws_res = ws_send_text(msg, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
// 		LT_ASSERT(ws_res >= 0);
	}
}

#include <lt/darr.h>

lt_err_t upgrade_websock(void) {
	lt_err_t err;

	lstr_t req = lt_lsbuild(alloc,
			"GET /socket.io/?EIO=4&transport=websocket HTTP/1.1\r\n"
			"Connection: Upgrade\r\n"
			"Host: %S\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"Origin: https://test.betweenworlds.net\r\n"
			"Sec-WebSocket-Version: 13\r\n"
			"Upgrade: websocket\r\n"
			"User-Agent: WebSocket++/0.8.2\r\n"
			"\r\n",
			host_addr, WS_KEY);
	if ((err = lt_ssl_send_fixed(ssl, req.str, req.len)) < 0)
		return err;

	lt_http_response_t res;
	if ((err = lt_http_parse_response(&res, (lt_io_callback_t)lt_ssl_recv_fixed, ssl, alloc)))
		return err;
	if (res.status_code != 101)
		return LT_ERR_UNKNOWN;
	return LT_SUCCESS;
}

lt_err_t https_request(char* method, lstr_t host, lstr_t query, char* headers, lstr_t body, lt_http_response_t* out_res) {
	lt_err_t err;

	u16 port = 443;

	lt_sockaddr_t addr;
	if ((err = lt_sockaddr_resolve(host, port, LT_SOCKTYPE_TCP, &addr, alloc)))
		return err;

	lt_socket_t* sock;
	if (!(sock = lt_socket_create(LT_SOCKTYPE_TCP, alloc)))
		return LT_ERR_UNKNOWN;
	if (lt_socket_connect(sock, &addr) != LT_SUCCESS)
		return LT_ERR_UNKNOWN;

	lt_ssl_connection_t* ssl;
	if (!(ssl = lt_ssl_connect(sock, host)))
		return LT_ERR_UNKNOWN;

	lstr_t req = lt_lsbuild(alloc,
			"%s %S HTTP/1.1\r\n"
			"Host: %S\r\n"
			"Connection: close\r\n"
			"Origin: https://test.betweenworlds.net\r\n"
			"%s"
			"Content-Length: %uz\r\n"
			"\r\n"
			"%S",
			method, query, host, headers, body.len, body);
	if (!req.str)
		return LT_ERR_OUT_OF_MEMORY;

	isz res = lt_ssl_send_fixed(ssl, req.str, req.len);
	if (res < 0)
		return -res;

	if ((err = lt_http_parse_response(out_res, (lt_io_callback_t)lt_ssl_recv_fixed, ssl, alloc)))
		return err;

	lt_mfree(alloc, req.str);
	lt_ssl_connection_destroy(ssl);
	lt_socket_destroy(sock, alloc);
	return LT_SUCCESS;
}

lt_err_t get_token(lstr_t key, lstr_t refresh_token, lstr_t* out_token) {
	lt_err_t err;

	lstr_t req_body = lt_lsbuild(alloc, "grant_type=refresh_token&refresh_token=%S", refresh_token);
	if (!req_body.str)
		return LT_ERR_OUT_OF_MEMORY;
	lstr_t req_query = lt_lsbuild(alloc, "/v1/token?key=%S", key);
	if (!req_query.str)
		fail_to(err = LT_ERR_OUT_OF_MEMORY, err0);

	lt_http_response_t res;
	if ((err = https_request("POST", CLSTR("securetoken.googleapis.com"), req_query, "Content-Type: application/x-www-form-urlencoded\r\n", req_body, &res)))
		goto err1;
	if (res.status_code != 200)
		fail_to(err = LT_ERR_UNKNOWN, err2);

	lt_json_t* json = lt_json_parse(alloc, res.body.str, res.body.len);
	if (!json)
		fail_to(err = LT_ERR_INVALID_FORMAT, err2);

	lt_json_t* access_token_json = lt_json_find_child(json, CLSTR("access_token"));
	lt_json_t* token_type_json = lt_json_find_child(json, CLSTR("token_type"));

	if (!access_token_json || !token_type_json)
		fail_to(err = LT_ERR_NOT_FOUND, err3);
	if (access_token_json->stype != LT_JSON_STRING || token_type_json->stype != LT_JSON_STRING || !lt_lseq(token_type_json->str_val, CLSTR("Bearer")))
		fail_to(err = LT_ERR_NOT_FOUND, err3);

	lstr_t token = lt_strdup(alloc, access_token_json->str_val);
	if (!token.str)
		fail_to(err = LT_ERR_OUT_OF_MEMORY, err3);

	*out_token = token;
	return LT_SUCCESS;

err3:	// lt_json_free(json, alloc);
err2:	lt_http_response_destroy(&res, alloc);
err1:	lt_mfree(alloc, req_query.str);
err0:	lt_mfree(alloc, req_body.str);
		return err;
}



lt_err_t authenticate(lstr_t key, lstr_t user, lstr_t pass, lstr_t* out_id_token, lstr_t* out_refresh_token) {
	lt_err_t err;

	lstr_t req_body = lt_lsbuild(alloc, "{\"clientType\":\"CLIENT_TYPE_WEB\",\"email\":\"%S\",\"password\":\"%S\",\"returnSecureToken\":true}", user, pass);
	if (!req_body.str)
		return LT_ERR_OUT_OF_MEMORY;
	lstr_t req_query = lt_lsbuild(alloc, "/v1/accounts:signInWithPassword?key=%S", key);
	if (!req_query.str)
		fail_to(err = LT_ERR_OUT_OF_MEMORY, err0);

	lt_http_response_t res;
	if ((err = https_request("POST", CLSTR("identitytoolkit.googleapis.com"), req_query, "Content-Type: application/json\r\n", req_body, &res)))
		goto err1;

	if (res.status_code != 200)
		fail_to(err = LT_ERR_UNKNOWN, err2);

	lt_json_t* json = lt_json_parse(alloc, res.body.str, res.body.len);
	if (!json)
		fail_to(err = LT_ERR_INVALID_FORMAT, err2);

	lt_json_t* refresh_token_json = lt_json_find_child(json, CLSTR("refreshToken"));
	lt_json_t* id_token_json = lt_json_find_child(json, CLSTR("idToken"));

	if (!refresh_token_json || !id_token_json)
		fail_to(err = LT_ERR_NOT_FOUND, err3);
	if (refresh_token_json->stype != LT_JSON_STRING || id_token_json->stype != LT_JSON_STRING)
		fail_to(err = LT_ERR_NOT_FOUND, err3);

	lstr_t refresh_token = lt_strdup(alloc, refresh_token_json->str_val);
	if (!refresh_token.str)
		fail_to(err = LT_ERR_OUT_OF_MEMORY, err3);
	lstr_t id_token = lt_strdup(alloc, id_token_json->str_val);
	if (!id_token.str)
		fail_to(err = LT_ERR_OUT_OF_MEMORY, err4);

	*out_refresh_token = refresh_token;
	*out_id_token = id_token;
	return LT_SUCCESS;

err4:	lt_mfree(alloc, refresh_token.str);
err3:	// lt_json_free(json, alloc);
err2:	lt_http_response_destroy(&res, alloc);
err1:	lt_mfree(alloc, req_query.str);
err0:	lt_mfree(alloc, req_body.str);
		return err;
}

lt_strstream_t clipboard;

void exec_cmd(lstr_t str) {
	char* it = str.str, *end = it + str.len;

	while (it < end && lt_is_space(*it))
		++it;

	char* cmd_start = it;
	while (it < end && !lt_is_space(*it))
		++it;
	lstr_t cmd = lt_lsfrom_range(cmd_start, it);

	while (it < end && lt_is_space(*it))
		++it;

	lstr_t args[32];
	usz arg_count = 0;
	char* args_start = it;

	while (it < end) {
		char* arg_start = it;
		while (it < end && !lt_is_space(*it))
			++it;
		lstr_t arg = lt_lsfrom_range(arg_start, it);

		while (it < end && lt_is_space(*it))
			++it;

		args[arg_count++] = arg;
	}

	if (lt_lseq(cmd, CLSTR("accept"))) {
		for (usz i = 0; i < arg_count; ++i) {
			isz mission_i;
			if (lt_lstoi(args[i], &mission_i) != LT_SUCCESS)
				continue;
			if (mission_i < lt_darr_count(mission_board))
				accept_mission(current_location.name, mission_board[mission_i].name);
		}
	}
	else if (lt_lseq(cmd, CLSTR("complete"))) {
		for (usz i = 0; i < arg_count; ++i) {
			isz mission_i;
			if (lt_lstoi(args[i], &mission_i) != LT_SUCCESS)
				continue;
			if (mission_i < lt_darr_count(active_missions))
				complete_mission(active_missions[mission_i].name);
		}
	}
	else if (lt_lseq(cmd, CLSTR("enter")) && arg_count == 1) {
		isz loc_i;
		if (lt_lstoi(args[0], &loc_i) == LT_SUCCESS) {
			if (loc_i < lt_darr_count(area_locations))
				goto_location(area_locations[loc_i].name);
		}
	}
	else if (lt_lseq(cmd, CLSTR("say"))) {
		send_chat(lt_lsfrom_range(args_start, it));
	}
	else if (lt_lseq(cmd, CLSTR("work")) && arg_count == 1 && current_location.has_job) {
		i64 count = 0;
		lt_err_t res = lt_lstoi(args[0], &count);

		if (lt_lseq(args[0], CLSTR("max"))) {
			count = LT_I64_MAX;

			if (current_location.job_spirit_cost != 0)
				count = lt_min_isz(count, spirit / current_location.job_spirit_cost);
			if (current_location.job_energy_cost != 0)
				count = lt_min_isz(count, energy / current_location.job_energy_cost);

			res = LT_SUCCESS;
		}

		if (count && res == LT_SUCCESS)
			work_job(current_location.name, count);
	}
	else if (lt_lseq(cmd, CLSTR("train")) && arg_count == 2) {
		isz workout_i = LT_I64_MAX;
		lt_lstoi(args[0], &workout_i);

		if (workout_i < lt_darr_count(current_location.workouts)) {
			i64 count = 0;
			lt_lstoi(args[1], &count);
			workout_t* w = &current_location.workouts[workout_i];

			if (lt_lseq(args[1], CLSTR("max"))) {
				count = LT_I64_MAX;
				if (w->spirit_cost != 0)
					count = lt_min_isz(count, spirit / w->spirit_cost);
				if (w->energy_cost != 0)
					count = lt_min_isz(count, energy / w->energy_cost);
			}

			if (count)
				train_stat(current_location.name, w->id, count);
		}
	}
}

#include <lt/arg.h>


int main(int argc, char** argv) {
	LT_DEBUG_INIT();

	alloc = (lt_alloc_t*)lt_amcreate(NULL, LT_GB(1), 0);

	lstr_t conf_path = CLSTR("bw.conf");

	lt_arg_iterator_t arg_it = lt_arg_iterator_create(argc, argv);
	while (lt_arg_next(&arg_it)) {
		if (lt_arg_flag(&arg_it, 'h', CLSTR("help"))) {
			lt_printf(
				"usage: lbw [OPTIONS]\n"
				"options:\n"
				"  -h, --help         Display this information.\n"
				"  -c, --config=PATH  Use config file at PATH.\n"
			);
			return 0;
		}

		if (lt_arg_lstr(&arg_it, 'c', CLSTR("config"), &conf_path))
			continue;

		lt_printf("unknown argument '%s'\n", *arg_it.it);
	}

	lstr_t conf_data;
	if (lt_freadallp(conf_path, &conf_data, alloc) != LT_SUCCESS)
		lt_ferrf("failed to read config file '%S': %s\n", conf_path, lt_os_err_str());

	lt_conf_err_info_t err_info;
	lt_conf_t conf;
	if (lt_conf_parse(&conf, conf_data.str, conf_data.len, &err_info, alloc) != LT_SUCCESS)
		lt_ferrf("failed to parse %S: %S\n", conf_path, err_info.err_str);

	api_key = lt_conf_str(&conf, CLSTR("google_api_key"));

	host_addr = lt_conf_str(&conf, CLSTR("host.address"));
	host_port = lt_conf_uint(&conf, CLSTR("host.port"));

	lstr_t email = lt_conf_str(&conf, CLSTR("credentials.email"));
	lstr_t password = lt_conf_str(&conf, CLSTR("credentials.password"));

	if (lt_ssl_init())
		lt_ferrf("failed to initialize OpenSSL\n");

	lt_ierrf("sending login requests\n");

	if (authenticate(api_key, email, password, &id_token, &refresh_token) != LT_SUCCESS)
		lt_ferrf("authentication failed\n");

	lt_ierrf("creating access token\n");

	if (get_token(api_key, refresh_token, &auth_token) != LT_SUCCESS)
		lt_ferrf("failed to create auth token\n");

	lt_ierrf("connecting to %S:%uw\n", host_addr, host_port);

	lt_sockaddr_t addr;
	if (lt_sockaddr_resolve(host_addr, host_port, LT_SOCKTYPE_TCP, &addr, alloc))
		lt_ferrf("failed to resolve %S:%uw\n", host_addr, host_port);

	if (!(sock = lt_socket_create(LT_SOCKTYPE_TCP, alloc)))
		lt_ferrf("failed to create socket\n");

	if (lt_socket_connect(sock, &addr) != LT_SUCCESS)
		lt_ferrf("failed to connect to %S:%uw\n", host_addr, host_port);

	if (!(ssl = lt_ssl_connect(sock, host_addr)))
		lt_ferrf("ssl handshake failed\n");

	if (upgrade_websock() != LT_SUCCESS)
		lt_ferrf("failed to establish websocket connection\n");

	lstr_t auth_msg = lt_lsbuild(alloc, "40/private,{\"token\":\"%S\"}", auth_token);
	isz ws_res = ws_send_text(auth_msg, (lt_io_callback_t)lt_ssl_send_fixed, ssl);
	LT_ASSERT(ws_res >= 0);

	state_lock = lt_mutex_create(alloc);

	chat_msgs = lt_darr_create(chat_msg_t, 16, lt_libc_heap);
	area_locations = lt_darr_create(location_t, 16, lt_libc_heap);
	active_missions = lt_darr_create(mission_t, 16, lt_libc_heap);
	mission_board = lt_darr_create(mission_t, 16, lt_libc_heap);

	lt_thread_t *recv_thread, *send_thread;
	if (!(recv_thread = lt_thread_create(recv_proc, NULL, alloc)))
		lt_ferrf("failed to create thread\n");
	if (!(send_thread = lt_thread_create(send_proc, NULL, alloc)))
		lt_ferrf("failed to create thread\n");

	while (!login_verified)
		lt_sleep_msec(1);

	if (login_err.str)
		lt_ferrf("login failed: %S\n", login_err);

	draw_init(alloc);
	lt_term_init(LT_TERM_ALTBUF);

	lt_texted_t ed;
	LT_ASSERT(lt_texted_create(&ed, alloc) == LT_SUCCESS);

	LT_ASSERT(lt_strstream_create(&clipboard, alloc) == LT_SUCCESS);

#ifdef TEST_MODE

	for (;;) {
		int key = 0;
		if (lt_term_key_available())
			key = lt_term_getkey();
		else
			lt_sleep_msec(10);

		draw_writef(CLEFT(%iz), (isz)lt_term_width);
		draw_writef("%r ", lt_term_width);
		draw_writef(CLEFT(%iz), (isz)lt_term_width);
		draw_texted(&ed, BG_BWHITE FG_BLACK, RESET);
		draw_writef(RESET CRESTORE RESET);

		draw_flush();

		if (key == (LT_TERM_MOD_CTRL|'D')) {
			lt_term_restore();
			return 0;
		}
		else if (key == '\n') {
			draw_writef(CLEFT(%iz), (isz)lt_term_width);
			draw_writef("%r ", lt_term_width);
			draw_writef(CLEFT(%iz), (isz)lt_term_width);
			draw_flush();
			exec_cmd(lt_texted_line_str(&ed, 0));
			lt_texted_clear(&ed);
		}
		else if (key != 0) {
			lt_texted_input_term_key(&ed, &clipboard, key);
		}
	}

#endif

	int key = 0;
	do {
		lt_mutex_lock(state_lock);

		u64 time_msec = lt_hfreq_time_msec();
		while (time_msec >= energy_tick_at_msec) {
			energy_tick_at_msec += energy_tick_interval_msec;
			energy = lt_min_isz(energy + energy_increment, energy_max);
		}
		while (time_msec >= spirit_tick_at_msec) {
			spirit_tick_at_msec += spirit_tick_interval_msec;
			spirit = lt_min_isz(spirit + spirit_increment, spirit_max);
		}

		draw_writef(CLS CSET(1,1));

		char* start = draw_buf_it;
		draw_writef(
				FG_BLACK BG_BWHITE " %S " BG_WHITE " %S LV.%iz " BG_BBLACK FG_BWHITE FG_BRED "  HP %iz/%iz " FG_BYELLOW "XP %iz/%iz " FG_BCYAN "SP %iz/%iz " FG_BGREEN "EN %iz/%iz " FG_BWHITE "CRD %iz ",
				area_name, player_name, level, hp, hp_max, xp, xp_max, spirit, spirit_max, energy, energy_max, credits);
		usz header_width = str_width(lt_lsfrom_range(start, draw_buf_it)) % lt_term_width;
		usz fill_chars = 0;
		if (header_width)
			fill_chars = lt_term_width - header_width;
		draw_writef("%r "RESET, fill_chars);

		draw_writef("\n");

		if (key == (LT_TERM_MOD_CTRL | 'Q'))
			view = VIEW_NONE;
		else if (key == (LT_TERM_MOD_CTRL | 'W'))
			view = VIEW_LOCATIONS;
		else if (key == (LT_TERM_MOD_CTRL | 'A'))
			view = VIEW_MISSIONS;
		else if (key == (LT_TERM_MOD_CTRL | 'S'))
			view = VIEW_CHAT;
		else if (key == (LT_TERM_MOD_CTRL | 'D'))
			view = VIEW_MAIL;
		else {
			if (key == '\n') {
				exec_cmd(lt_texted_line_str(&ed, 0));
				lt_texted_clear(&ed);
			}
			else if (key != 0) {
				lt_texted_input_term_key(&ed, &clipboard, key);
			}
		}

		if (view == VIEW_NONE) {
			if (lt_lseq(current_location.menu_text, current_location.title))
				draw_writef(BOLD "\n %S" RESET "\n", current_location.title);
			else
				draw_writef(BOLD "\n %S - %S" RESET "\n", current_location.menu_text, current_location.title);
			draw_wrapped_text(current_location.description, lt_min_isz(lt_term_width - 1, 100), CLSTR("  "));

			if (current_location.has_job) {
				draw_writef("\n  " BG_BWHITE FG_BLACK "[work <n/max>]" RESET BOLD " %S -" RESET FG_BCYAN " %iz SP" RESET FG_BGREEN " %iz EN\n" RESET,
						current_location.job_text, current_location.job_spirit_cost, current_location.job_energy_cost);
			}

			if (lt_darr_count(current_location.workouts)) {
				draw_writef("\n");
				for (usz i = 0; i < lt_darr_count(current_location.workouts); ++i) {
					workout_t* w = &current_location.workouts[i];
					draw_writef("  " FG_BLACK BG_BWHITE "[train %iz <n/max>]" RESET BOLD " %S -" RESET " cost" FG_BCYAN " %iz SP" RESET FG_BGREEN " %iz EN" RESET,
						i, w->text, w->spirit_cost, w->energy_cost);
					draw_writef(" - earn " FG_BYELLOW "%iz" RESET " to " FG_BYELLOW "%iz" RESET " "BOLD"%S tokens\n"RESET, w->earn_min, w->earn_max, w->stat);
				}
			}

			if (lt_darr_count(mission_board)) {
				draw_writef("\n");
				for (usz i = 0; i < lt_darr_count(mission_board); ++i) {
					mission_t* mission = &mission_board[i];
					char* daily = mission->daily ? FG_BGREEN "*" RESET : "";
					draw_writef("  " FG_BLACK BG_BWHITE "[accept %iz]" RESET BOLD " %S%s\n" RESET, i, mission->name, daily);
				}
			}

		}
		else if (view == VIEW_LOCATIONS) {
			draw_writef("\n");

			LT_ASSERT(area_locations);
			for (usz i = 0; i < lt_darr_count(area_locations); ++i)
				draw_writef(" " BG_BWHITE FG_BLACK "[enter %uz]" RESET BOLD " %S" RESET "\n", i, area_locations[i].menu_text);
		}
		else if (view == VIEW_MISSIONS) {
			draw_writef("\n");

			LT_ASSERT(active_missions);
			for (usz i = 0; i < lt_darr_count(active_missions); ++i) {
				mission_t* mission = &active_missions[i];

				char* daily = mission->daily ? FG_BGREEN "*" RESET : "";
				draw_writef(" " FG_BLACK BG_BWHITE "[complete %iz]" RESET BOLD " %S%s\n" RESET, i, mission->name, daily);

				for (usz i = 0; i < lt_darr_count(mission->requirements); ++i) {
					mission_requirement_t* req = &mission->requirements[i];
					lstr_t name = req->name;
					if (!name.len || lt_lseq(name, CLSTR("none")))
						name = CLSTR("\b");

					char* count_clr = FG_BYELLOW;
					if (req->current_count >= req->count)
						count_clr = FG_BGREEN;
					draw_writef("               - %S " BOLD "%S" RESET "%s (%iz/%iz)\n" RESET, mission_req_verbs[req->type], name, count_clr, req->current_count, req->count);
				}
			}
		}
		else if (view == VIEW_CHAT) {
			draw_writef("\n");

			for (isz i = 0; i < lt_darr_count(chat_msgs); ++i)
				draw_writef(BOLD"%S: "RESET"%S\n", chat_msgs[i].sender_name, chat_msgs[i].message);
		}
		else if (view == VIEW_MAIL) {
			draw_writef("\n" FG_BRED " NOT IMPLEMENTED\n" RESET);
		}
		else if (view == VIEW_LEADERBOARDS) {
			draw_writef("\n" FG_BRED " NOT IMPLEMENTED\n" RESET);
		}

		lt_mutex_release(state_lock);

		draw_writef(CSET(%iz,1) " C-q Home | C-w Locations | C-a Missions | C-s Chat | C-e Exit", lt_term_height - 1);
		draw_writef("\n  ");
		draw_texted(&ed, BG_BWHITE FG_BLACK, RESET);
		draw_writef(RESET CRESTORE RESET);

		draw_flush();

		if (lt_term_key_available())
			key = lt_term_getkey();
		else {
			lt_sleep_msec(42); // 1000 / 24
			lt_update_term_dimensions();
			key = 0;
		}
	} while (key != (LT_TERM_MOD_CTRL | 'E'));
	done = 1;
	lt_term_restore();

	lt_thread_terminate(recv_thread);
	lt_thread_terminate(send_thread);

	lt_thread_join(recv_thread, alloc);
	lt_thread_join(send_thread, alloc);

	lt_ssl_connection_destroy(ssl);
	lt_socket_destroy(sock, alloc);

	lt_amdestroy((lt_arena_t*)alloc);
	return 0;
}
