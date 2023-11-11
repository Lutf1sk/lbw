#ifndef MISSION_H
#define MISSION_H 1

#include <lt/json.h>

#define MREQ_NONE			0
#define MREQ_SELL			2
#define MREQ_DEFEAT			3
#define MREQ_SEND_CHATMSG	4
#define MREQ_LEARN_SKILL	6
#define MREQ_WORK			8
#define MREQ_TRAIN_AT		9
#define MREQ_EQUIP_SKILL	12
#define MREQ_CONSUME		14

typedef
struct mission_requirement {
	u8 type;
	isz count;
	lstr_t name;
	isz quality;

	isz current_count;
} mission_requirement_t;

#define MREW_NONE		0
#define MREW_CREDITS	1
#define MREW_ITEM		2
#define MREW_XP			5

typedef
struct mission_reward {
	u8 type;
	isz count;
	lstr_t name;
	isz quality;
} mission_reward_t;

typedef
struct mission {
	lstr_t name;
	lstr_t description;
	isz min_level;
	b8 daily;
	b8 repeatable;
	b8 complete;

	lt_darr(mission_requirement_t) requirements;
	lt_darr(mission_reward_t) rewards;
} mission_t;

void mission_load(lt_json_t* json, mission_t* mission);
void mission_free(mission_t* mission);

static lstr_t mission_req_verbs[16] = {
	[0] = CLSTR("0??"),
	[1] = CLSTR("1??"),
	[2] = CLSTR("Sell"),
	[3] = CLSTR("Defeat"),
	[4] = CLSTR("Chat"),
	[5] = CLSTR("5??"),
	[6] = CLSTR("Learn"),
	[7] = CLSTR("7??"),
	[8] = CLSTR("Work at"),
	[9] = CLSTR("Train at"),
	[10] = CLSTR("10??"),
	[11] = CLSTR("11??"),
	[12] = CLSTR("Equip"),
	[13] = CLSTR("13??"),
	[14] = CLSTR("Consume"),
	[15] = CLSTR("15??"),
};

#endif