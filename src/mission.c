#include "mission.h"

#include <lt/mem.h>
#include <lt/json.h>
#include <lt/darr.h>

void mission_load(lt_json_t* json, mission_t* out) {
	memset(out, 0, sizeof(mission_t));

	lt_json_t* name = lt_json_find_child(json, CLSTR("name"));
	if (name && name->stype == LT_JSON_STRING)
		out->name = lt_strdup(lt_libc_heap, name->str_val);

	lt_json_t* description = lt_json_find_child(json, CLSTR("description"));
	if (description && description->stype == LT_JSON_STRING)
		out->description = lt_strdup(lt_libc_heap, description->str_val);

	lt_json_t* min_level = lt_json_find_child(json, CLSTR("minLevel"));
	if (min_level && min_level->stype == LT_JSON_NUMBER)
		out->min_level = lt_json_int_val(min_level);

	lt_json_t* daily = lt_json_find_child(json, CLSTR("daily"));
	if (daily && daily->stype == LT_JSON_BOOL)
		out->daily = lt_json_bool_val(daily);

	lt_json_t* repeatable = lt_json_find_child(json, CLSTR("repeatable"));
	if (repeatable && repeatable->stype == LT_JSON_BOOL)
		out->repeatable = lt_json_bool_val(repeatable);

	out->requirements = lt_darr_create(mission_requirement_t, 4, lt_libc_heap);
	lt_json_t* requirements = lt_json_find_child(json, CLSTR("requirements"));
	if (requirements && requirements->stype == LT_JSON_ARRAY) {
		lt_json_t* it = requirements->child;
		for (; it; it = it->next) {
			if (it->stype != LT_JSON_OBJECT)
				continue;

			mission_requirement_t req;
			memset(&req, 0, sizeof(req));

			lt_json_t* name = lt_json_find_child(it, CLSTR("name"));
			if (name && name->stype == LT_JSON_STRING)
				req.name = lt_strdup(lt_libc_heap, name->str_val);

			lt_json_t* type = lt_json_find_child(it, CLSTR("type"));
			if (type && type->stype == LT_JSON_NUMBER)
				req.type = lt_json_int_val(type);

			lt_json_t* count = lt_json_find_child(it, CLSTR("quantity"));
			if (count && count->stype == LT_JSON_NUMBER)
				req.count = lt_json_int_val(count);

			lt_darr_push(out->requirements, req);
		}
	}

	out->rewards = lt_darr_create(mission_reward_t, 4, lt_libc_heap);
	lt_json_t* rewards = lt_json_find_child(json, CLSTR("rewards"));
	if (rewards && rewards->stype == LT_JSON_ARRAY) {
		lt_json_t* it = rewards->child;
		for (; it; it = it->next) {
			if (it->stype != LT_JSON_OBJECT)
				continue;

			mission_reward_t rew;
			memset(&rew, 0, sizeof(rew));

			lt_json_t* name = lt_json_find_child(it, CLSTR("name"));
			if (name && name->stype == LT_JSON_STRING)
				rew.name = lt_strdup(lt_libc_heap, name->str_val);

			lt_json_t* type = lt_json_find_child(it, CLSTR("type"));
			if (type && type->stype == LT_JSON_NUMBER)
				rew.type = lt_json_int_val(type);

			lt_json_t* count = lt_json_find_child(it, CLSTR("quantity"));
			if (count && count->stype == LT_JSON_NUMBER)
				rew.count = lt_json_int_val(count);

			lt_darr_push(out->rewards, rew);
		}
	}
}

void mission_free(mission_t* mission) {
	if (mission->name.str)
		lt_mfree(lt_libc_heap, mission->name.str);
	if (mission->description.str)
		lt_mfree(lt_libc_heap, mission->description.str);

	if (mission->requirements)
		lt_darr_destroy(mission->requirements);
	if (mission->rewards)
		lt_darr_destroy(mission->rewards);
}
