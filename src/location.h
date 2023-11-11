#ifndef LOCATION_H
#define LOCATION_H 1

#include <lt/lt.h>
#include <lt/fwd.h>

#define LOC_NONE 0
#define LOC_HOME 1
#define LOC_ITEM_SHOP 2
#define LOC_DOCTOR 3
#define LOC_TRAINER 4
#define LOC_JOB 5
#define LOC_COMBAT 6
#define LOC_ARTISAN 7
#define LOC_SKILL_SHOP 8
#define LOC_EXIT 9

typedef
struct workout {
	lstr_t id;
	lstr_t stat;

	isz energy_cost;
	isz spirit_cost;
	isz earn_min;
	isz earn_max;

	lstr_t text;
} workout_t;

typedef
struct location {
	lstr_t name;
	lstr_t menu_text;
	u8 type;

	lstr_t title;
	lstr_t description;

	b8 has_job;
	isz job_spirit_cost;
	isz job_energy_cost;
	isz job_earn_min_credits;
	isz job_earn_max_credits;
	lstr_t job_text;

	lt_darr(workout_t) workouts;
} location_t;

void location_free(location_t* loc);

void location_load(lt_json_t* location, location_t* out);

#endif