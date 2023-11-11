#include "location.h"

#include <lt/debug.h>
#include <lt/json.h>
#include <lt/mem.h>
#include <lt/strstream.h>
#include <lt/darr.h>
#include <lt/ctype.h>
#include <lt/str.h>
#include <lt/io.h>

#define LT_ANSI_SHORTEN_NAMES 1
#include <lt/ansi.h>

#include "main.h"

lstr_t decode_description(lstr_t str) {
	lt_strstream_t ss;
	LT_ASSERT(lt_strstream_create(&ss, lt_libc_heap) == LT_SUCCESS);

	lt_darr(lstr_t) tooltips = lt_darr_create(lstr_t, 8, lt_libc_heap);

	char* it = str.str, *end = it + str.len;
	while (it < end) {
		char c = *it++;

		if (c == '<') {
			char* start = it;
			while (it < end && (lt_is_ident_body(*it) || *it == '-'))
				++it;

			lstr_t tag = lt_lsfrom_range(start, it);
			lstr_t title = NLSTR();
			lstr_t text = NLSTR();

			while (it < end && *it != '>' && *it != '/') {
				while (it < end && lt_is_space(*it))
					++it;

				start = it;
				while (it < end && !lt_is_space(*it) && *it != '/' && *it != '>' && *it != '=')
					++it;
				lstr_t key = lt_lsfrom_range(start, it);

				while (it < end && lt_is_space(*it))
					++it;

				lstr_t val = NLSTR();
				if (it < end && *it == '=') {
					++it;

					if (*it == '"') {
						++it;

						start = it;
						while (it < end && *it != '"')
							++it;
						val = lt_lsfrom_range(start, it);

						++it; // skip "
					}
					else {
						start = it;
						while (it < end && !lt_is_space(*it) && *it != '/' && *it != '>')
							++it;
						val = lt_lsfrom_range(start, it);
					}

					if (lt_lseq(key, CLSTR("title")))
						title = val;
					if (lt_lseq(key, CLSTR("text")))
						text = val;
				}

				while (it < end && lt_is_space(*it))
					++it;
			}

			if (it < end && *it == '/')
				++it;
			if (it < end && *it == '>')
				++it;

			if (lt_lseq(tag, CLSTR("LoreTooltip"))) {
				lt_io_printf((lt_io_callback_t)lt_strstream_write, &ss, BOLD FG_BCYAN BG_BBLACK "%S" RESET, title);
				lt_darr_push(tooltips, title);
				lt_darr_push(tooltips, text);
			}
			else {
				lt_strstream_writels(&ss, CLSTR(BOLD FG_BRED));
				lt_strstream_writels(&ss, tag);
				lt_strstream_writels(&ss, CLSTR(RESET));
			}
		}

		else if (c == '*') {
			char* start = it;
			while (it < end && *it != '*')
				++it;
			lt_strstream_writels(&ss, CLSTR(ITALIC));
			lt_strstream_write(&ss, start, it - start);
			lt_strstream_writels(&ss, CLSTR(RESET));
			++it; // skip *
		}

		else if (c == '%') {
			char* start = it;
			while (it < end && *it != '%')
				++it;
			lstr_t var = lt_lsfrom_range(start, it);

			if (lt_lseq(var, CLSTR("playername"))) {
				lt_strstream_writels(&ss, player_name);
				++it; // skip '%'
			}
			else {
				lt_strstream_writec(&ss, '%');
				lt_strstream_writels(&ss, var);
			}
		}

		else {
			lt_strstream_writec(&ss, c);
		}
	}

	if (lt_darr_count(tooltips))
		lt_strstream_writels(&ss, CLSTR("\n\n\n"));
	for (usz i = 0; i < lt_darr_count(tooltips); i += 2)
		lt_io_printf((lt_io_callback_t)lt_strstream_write, &ss, BOLD FG_BCYAN BG_BBLACK "%S" RESET " - %S", tooltips[i], tooltips[i + 1]);
	lt_darr_destroy(tooltips);

	return ss.str;
}

void location_free(location_t* loc) {
	if (loc->name.str)
		lt_mfree(lt_libc_heap, loc->name.str);
	if (loc->menu_text.str)
		lt_mfree(lt_libc_heap, loc->menu_text.str);
	if (loc->title.str)
		lt_mfree(lt_libc_heap, loc->title.str);
	if (loc->description.str)
		lt_mfree(lt_libc_heap, loc->description.str);
}

void location_load(lt_json_t* location, location_t* out) {
	memset(out, 0, sizeof(location_t));

	lt_json_t* menu_text = lt_json_find_child(location, CLSTR("menuText"));
	if (menu_text && menu_text->stype == LT_JSON_STRING)
		out->menu_text = lt_strdup(lt_libc_heap, menu_text->str_val);

	lt_json_t* name = lt_json_find_child(location, CLSTR("name"));
	if (name && name->stype == LT_JSON_STRING)
		out->name = lt_strdup(lt_libc_heap, name->str_val);

	lt_json_t* description = lt_json_find_child(location, CLSTR("description"));
	if (description && description->stype == LT_JSON_STRING)
		out->description = decode_description(unescape_json_str(description->str_val));
// 		out->description = lt_strdup(lt_libc_heap, description->str_val);

	lt_json_t* title = lt_json_find_child(location, CLSTR("title"));
	if (title && title->stype == LT_JSON_STRING)
		out->title = lt_strdup(lt_libc_heap, title->str_val);

	lt_json_t* type = lt_json_find_child(location, CLSTR("type"));
	if (type && type->stype == LT_JSON_NUMBER)
		out->type = lt_json_int_val(type);

	lt_json_t* job = lt_json_find_child(location, CLSTR("job"));
	if (job && job->stype == LT_JSON_OBJECT) {
		out->has_job = 1;

		lt_json_t* energy_cost = lt_json_find_child(job, CLSTR("energyCost"));
		if (energy_cost && energy_cost->stype == LT_JSON_NUMBER)
			out->job_energy_cost = lt_json_int_val(energy_cost);

		lt_json_t* spirit_cost = lt_json_find_child(job, CLSTR("spiritCost"));
		if (spirit_cost && spirit_cost->stype == LT_JSON_NUMBER)
			out->job_spirit_cost = lt_json_int_val(spirit_cost);

		lt_json_t* earn_min = lt_json_find_child(job, CLSTR("earnMin"));
		if (earn_min && earn_min->stype == LT_JSON_NUMBER)
			out->job_earn_min_credits = lt_json_int_val(earn_min);

		lt_json_t* earn_max = lt_json_find_child(job, CLSTR("earnMax"));
		if (earn_max && earn_max->stype == LT_JSON_NUMBER)
			out->job_earn_max_credits = lt_json_int_val(earn_max);

		lt_json_t* button_text = lt_json_find_child(job, CLSTR("buttonText"));
		if (button_text && button_text->stype == LT_JSON_STRING)
			out->job_text = lt_strdup(lt_libc_heap, button_text->str_val);
	}

	out->workouts = lt_darr_create(workout_t, 4, lt_libc_heap);

	lt_json_t* workouts_json = lt_json_find_child(location, CLSTR("workout"));
	if (workouts_json && workouts_json->stype == LT_JSON_ARRAY) {
		for (lt_json_t* it = workouts_json->child; it; it = it->next) {
			workout_t workout;
			memset(&workout, 0, sizeof(workout));

			lt_json_t* id = lt_json_find_child(it, CLSTR("id"));
			if (id && id->stype == LT_JSON_STRING)
				workout.id = lt_strdup(lt_libc_heap, id->str_val);

			lt_json_t* stat = lt_json_find_child(it, CLSTR("stat"));
			if (stat && stat->stype == LT_JSON_STRING)
				workout.stat = lt_strdup(lt_libc_heap, stat->str_val);

			lt_json_t* energy_cost = lt_json_find_child(it, CLSTR("energyCost"));
			if (energy_cost && energy_cost->stype == LT_JSON_NUMBER)
				workout.energy_cost = lt_json_int_val(energy_cost);

			lt_json_t* spirit_cost = lt_json_find_child(it, CLSTR("spiritCost"));
			if (spirit_cost && spirit_cost->stype == LT_JSON_NUMBER)
				workout.spirit_cost = lt_json_int_val(spirit_cost);

			lt_json_t* earn_min = lt_json_find_child(it, CLSTR("earnMin"));
			if (earn_min && earn_min->stype == LT_JSON_NUMBER)
				workout.earn_min = lt_json_int_val(earn_min);

			lt_json_t* earn_max = lt_json_find_child(it, CLSTR("earnMax"));
			if (earn_max && earn_max->stype == LT_JSON_NUMBER)
				workout.earn_max = lt_json_int_val(earn_max);

			lt_json_t* text = lt_json_find_child(it, CLSTR("buttonText"));
			if (text && text->stype == LT_JSON_STRING)
				workout.text = lt_strdup(lt_libc_heap, text->str_val);

			lt_darr_push(out->workouts, workout);
		}
	}
}
