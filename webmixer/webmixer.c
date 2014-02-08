/*******************************************************************************
 * BEGIN COPYRIGHT NOTICE
 * 
 * This file is part of program "I-Trigue 2.1 3300 Digital Control"
 * Copyright 2013-2014  R. Lemos
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * END COPYRIGHT NOTICE
 ******************************************************************************/
#include <math.h>
#include <alsa/asoundlib.h>
#include <jansson.h>

static void
error(const char *fmt,...)
{
	va_list va;

	va_start(va, fmt);
	fprintf(stderr, "webmixer: ");
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");
	va_end(va);
}

static struct snd_mixer_selem_regopt
smixer_options = {
	.ver = 1,
	.abstract = SND_MIXER_SABSTRACT_NONE,
};

struct volume_ops {
	int (*get_range)(snd_mixer_elem_t *elem, long *min, long *max);
	int (*get)(snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t c,
		   long *value);
};
	
enum { VOL_RAW, VOL_DB };

struct volume_ops_set {
	int (*has_volume)(snd_mixer_elem_t *elem);
	struct volume_ops v[2];
};

static const struct volume_ops_set
vol_ops[2] = {
	{
		.has_volume = snd_mixer_selem_has_playback_volume,
		.v = {{ snd_mixer_selem_get_playback_volume_range,
			snd_mixer_selem_get_playback_volume },
		      { snd_mixer_selem_get_playback_dB_range,
			snd_mixer_selem_get_playback_dB },
		},
	},
	{
		.has_volume = snd_mixer_selem_has_capture_volume,
		.v = {{ snd_mixer_selem_get_capture_volume_range,
			snd_mixer_selem_get_capture_volume },
		      { snd_mixer_selem_get_capture_dB_range,
			snd_mixer_selem_get_capture_dB },
		},
	},
};

static int
convert_prange(long val, long min, long max)
{
	long range = max - min;
	int tmp;

	if (range == 0)
		return 0;
	val -= min;
	tmp = rint((double)val/(double)range * 100);
	return tmp;
}

static json_t*
get_selem_volume(snd_mixer_elem_t *elem, 
		  snd_mixer_selem_channel_id_t chn, int dir,
		  long min, long max)
{
	json_t *volume = json_object();

	long raw, val;
	vol_ops[dir].v[VOL_RAW].get(elem, chn, &raw);
	val = convert_prange(raw, min, max);

	json_object_set_new(volume, "raw", json_integer(raw));
	json_object_set_new(volume, "perc", json_integer(val));

	if (!vol_ops[dir].v[VOL_DB].get(elem, chn, &val)) {
		json_object_set_new(volume, "dB", json_integer(val));
	}

	return volume;
}

static json_t* 
get_selem(snd_mixer_t *handle, snd_mixer_selem_id_t *id, const char *space, const char *name)
{
	snd_mixer_selem_channel_id_t chn;
	long pmin, pmax;
	long cmin, cmax;
	int pmono, cmono;
	snd_mixer_elem_t *elem;

	json_t *selem = json_object();
	json_t *value = json_array();

	json_object_set_new(selem, "name", json_string(snd_mixer_selem_id_get_name(id)));
	json_object_set_new(selem, "index", json_integer(snd_mixer_selem_id_get_index(id)));



	
	elem = snd_mixer_find_selem(handle, id);
	if (!elem) {
		error("Mixer %s simple element not found", name);
		return NULL;
	}

	{
		json_t *capabilities = json_array();

		json_object_set_new(selem, "capabilities", capabilities);

		if (snd_mixer_selem_has_common_volume(elem)) {
			json_array_append_new(capabilities, json_string("volume"));
			if (snd_mixer_selem_has_playback_volume_joined(elem))
				json_array_append_new(capabilities, json_string("volume-joined"));
		} else {
			if (snd_mixer_selem_has_playback_volume(elem)) {
				json_array_append_new(capabilities, json_string("pvolume"));
				if (snd_mixer_selem_has_playback_volume_joined(elem))
					json_array_append_new(capabilities, json_string("pvolume-joined"));
			}
			if (snd_mixer_selem_has_capture_volume(elem)) {
				json_array_append_new(capabilities, json_string("cvolume"));
				if (snd_mixer_selem_has_capture_volume_joined(elem))
					json_array_append_new(capabilities, json_string("cvolume-joined"));
			}
		}

		if (snd_mixer_selem_has_common_switch(elem)) {
			json_array_append_new(capabilities, json_string("switch"));
			if (snd_mixer_selem_has_playback_switch_joined(elem))
				json_array_append_new(capabilities, json_string("switch-joined"));
		} else {
			if (snd_mixer_selem_has_playback_switch(elem)) {
				json_array_append_new(capabilities, json_string("pswitch"));
				if (snd_mixer_selem_has_playback_switch_joined(elem))
					json_array_append_new(capabilities, json_string("pswitch-joined"));
			}
			if (snd_mixer_selem_has_capture_switch(elem)) {
				json_array_append_new(capabilities, json_string("cswitch"));
				if (snd_mixer_selem_has_capture_switch_joined(elem))
					json_array_append_new(capabilities, json_string("cswitch-joined"));
				if (snd_mixer_selem_has_capture_switch_exclusive(elem))
					json_array_append_new(capabilities, json_string("cswitch-exclusive"));
			}
		}
		if (snd_mixer_selem_is_enum_playback(elem)) {
			json_array_append_new(capabilities, json_string("penum"));
		} else if (snd_mixer_selem_is_enum_capture(elem)) {
			json_array_append_new(capabilities, json_string("cenum"));
		} else if (snd_mixer_selem_is_enumerated(elem)) {
			json_array_append_new(capabilities, json_string("enum"));
		}

	}

	if (snd_mixer_selem_is_enumerated(elem)) {
		int i, altcount;
		unsigned int idx;
		char altname[40];

		json_t *alternatives = json_array();

		json_object_set_new(selem, "alternatives", alternatives);

		altcount = snd_mixer_selem_get_enum_items(elem);
		for (i = 0; i < altcount; i++) {
			snd_mixer_selem_get_enum_item_name(elem, i, sizeof(altname) - 1, altname);
			json_array_append_new(alternatives, json_string(altname));
		}


		for (i = 0; !snd_mixer_selem_get_enum_item(elem, i, &idx); i++) {
			snd_mixer_selem_get_enum_item_name(elem, idx, sizeof(altname) - 1, altname);
			json_array_append_new(value, json_string(altname));
		}

		json_object_set_new(selem, "value", value);

		return selem; /* no more thing to do */
	}

	if (snd_mixer_selem_has_capture_switch_exclusive(elem)) {
		json_object_set_new(selem, "captureExclusiveGroup", json_integer(snd_mixer_selem_get_capture_group(elem)));
	}

	if (snd_mixer_selem_has_playback_volume(elem) ||
	    snd_mixer_selem_has_playback_switch(elem)) {
		json_t *playbackChannels = json_array();
		json_object_set_new(selem, "playbackChannels", playbackChannels);

		if (snd_mixer_selem_is_playback_mono(elem)) {
			json_array_append_new(playbackChannels, json_string("Mono"));
		} else {
			for (chn = 0; chn <= SND_MIXER_SCHN_LAST; chn++){
				if (!snd_mixer_selem_has_playback_channel(elem, chn))
					continue;
				json_array_append_new(playbackChannels, json_string(snd_mixer_selem_channel_name(chn)));			}
		}
	}

	if (snd_mixer_selem_has_capture_volume(elem) ||
	    snd_mixer_selem_has_capture_switch(elem)) {
		json_t *captureChannels = json_array();
		json_object_set_new(selem, "captureChannels", captureChannels);

		if (snd_mixer_selem_is_capture_mono(elem)) {
			json_array_append_new(captureChannels, json_string("Mono"));
		} else {
			for (chn = 0; chn <= SND_MIXER_SCHN_LAST; chn++){
				if (!snd_mixer_selem_has_capture_channel(elem, chn))
					continue;
				json_array_append_new(captureChannels, json_string(snd_mixer_selem_channel_name(chn)));
			}
		}
	}

	if (snd_mixer_selem_has_playback_volume(elem) ||
	    snd_mixer_selem_has_capture_volume(elem)) {
		json_t *limits = json_object();
		json_object_set_new(selem, "limits", limits);

		if (snd_mixer_selem_has_common_volume(elem)) {
			json_t *common = json_object();
			json_object_set_new(limits, "common", common);
			
			snd_mixer_selem_get_playback_volume_range(elem, &pmin, &pmax);
			snd_mixer_selem_get_capture_volume_range(elem, &cmin, &cmax);

			json_object_set_new(common, "min", json_integer(pmin));
			json_object_set_new(common, "max", json_integer(pmax));
		} else {
			if (snd_mixer_selem_has_playback_volume(elem)) {
				json_t *playback = json_object();
				json_object_set_new(limits, "playback", playback);

				snd_mixer_selem_get_playback_volume_range(elem, &pmin, &pmax);

				json_object_set_new(playback, "min", json_integer(pmin));
				json_object_set_new(playback, "max", json_integer(pmax));
			}
			if (snd_mixer_selem_has_capture_volume(elem)) {
				json_t *capture = json_object();
				json_object_set_new(limits, "capture", capture);

				snd_mixer_selem_get_capture_volume_range(elem, &cmin, &cmax);

				json_object_set_new(capture, "min", json_integer(cmin));
				json_object_set_new(capture, "max", json_integer(cmax));
			}
		}
	}

	pmono = snd_mixer_selem_has_playback_channel(elem, SND_MIXER_SCHN_MONO) &&
	        (snd_mixer_selem_is_playback_mono(elem) || 
		 (!snd_mixer_selem_has_playback_volume(elem) &&
		  !snd_mixer_selem_has_playback_switch(elem)));
	cmono = snd_mixer_selem_has_capture_channel(elem, SND_MIXER_SCHN_MONO) &&
	        (snd_mixer_selem_is_capture_mono(elem) || 
		 (!snd_mixer_selem_has_capture_volume(elem) &&
		  !snd_mixer_selem_has_capture_switch(elem)));
#if 0
	printf("pmono = %i, cmono = %i (%i, %i, %i, %i)\n", pmono, cmono,
			snd_mixer_selem_has_capture_channel(elem, SND_MIXER_SCHN_MONO),
			snd_mixer_selem_is_capture_mono(elem),
			snd_mixer_selem_has_capture_volume(elem),
			snd_mixer_selem_has_capture_switch(elem));
#endif

	json_object_set_new(selem, "value", value);

	
	if (pmono || cmono) {
		json_t *mono = json_object();
		json_array_append_new(value, mono);
		
		json_object_set_new(mono, "channel", json_string("Mono"));

		if (snd_mixer_selem_has_common_volume(elem)) {
			json_object_set_new(mono, "volume", get_selem_volume(elem, SND_MIXER_SCHN_MONO, 0, pmin, pmax));
		}
		if (snd_mixer_selem_has_common_switch(elem)) {
			int sw;

			snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &sw);
			json_object_set_new(mono, "switch", json_string(sw ? "on" : "off"));
		}

		if (pmono) {
			json_t *playback = NULL;

			if (!snd_mixer_selem_has_common_volume(elem)) {
				if (snd_mixer_selem_has_playback_volume(elem)) {
					if (!playback) {
						playback = json_object();
						json_object_set_new(mono, "playback", playback);
					}
				
					json_object_set_new(playback, "volume", get_selem_volume(elem, SND_MIXER_SCHN_MONO, 0, pmin, pmax));
				}
			}
			if (!snd_mixer_selem_has_common_switch(elem)) {
				if (snd_mixer_selem_has_playback_switch(elem)) {
					int sw;

					if (!playback) {
						playback = json_object();
						json_object_set_new(mono, "playback", playback);
					}

					snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &sw);
					json_object_set_new(playback, "switch", json_string(sw ? "on" : "off"));
				}
			}
		}

		if (cmono) {
			json_t *capture = NULL;

			if (!snd_mixer_selem_has_common_volume(elem)) {
				if (snd_mixer_selem_has_capture_volume(elem)) {
					if (!capture) {
						capture = json_object();
						json_object_set_new(mono, "capture", capture);
					}

					json_object_set_new(capture, "volume", get_selem_volume(elem, SND_MIXER_SCHN_MONO, 1, cmin, cmax));
				}
			}
			if (!snd_mixer_selem_has_common_switch(elem)) {
				if (snd_mixer_selem_has_capture_switch(elem)) {
					int sw;

					if (!capture) {
						capture = json_object();
						json_object_set_new(mono, "capture", capture);
					}

					snd_mixer_selem_get_capture_switch(elem, SND_MIXER_SCHN_MONO, &sw);
					json_object_set_new(capture, "switch", json_string(sw ? "on" : "off"));
				}
			}
		}
	}

	if (!pmono || !cmono) {
		for (chn = 0; chn <= SND_MIXER_SCHN_LAST; chn++) {
			json_t *channel;

			if ((pmono || !snd_mixer_selem_has_playback_channel(elem, chn)) &&
			    (cmono || !snd_mixer_selem_has_capture_channel(elem, chn)))
				continue;

			channel = json_object();
			json_object_set_new(channel, "channel", json_string(snd_mixer_selem_channel_name(chn)));
			json_array_append_new(value, channel);

			if (!pmono && !cmono && snd_mixer_selem_has_common_volume(elem)) {
				json_object_set_new(channel, "volume", get_selem_volume(elem, chn, 0, pmin, pmax));
			}
			if (!pmono && !cmono && snd_mixer_selem_has_common_switch(elem)) {
				int sw;

				snd_mixer_selem_get_playback_switch(elem, chn, &sw);
				json_object_set_new(channel, "switch", json_string(sw ? "on" : "off"));
			}
			if (!pmono && snd_mixer_selem_has_playback_channel(elem, chn)) {
				json_t *playback = NULL;

				if (!snd_mixer_selem_has_common_volume(elem)) {
					if (snd_mixer_selem_has_playback_volume(elem)) {
						if (!playback) {
							playback = json_object();
							json_object_set_new(channel, "playback", playback);
						}

						json_object_set_new(playback, "volume", get_selem_volume(elem, chn, 0, pmin, pmax));
					}
				}
				if (!snd_mixer_selem_has_common_switch(elem)) {
					if (snd_mixer_selem_has_playback_switch(elem)) {
						int sw;

						if (!playback) {
							playback = json_object();
							json_object_set_new(channel, "playback", playback);
						}

						snd_mixer_selem_get_playback_switch(elem, chn, &sw);
						json_object_set_new(playback, "switch", json_string(sw ? "on" : "off"));
					}
				}
			}
			if (!cmono && snd_mixer_selem_has_capture_channel(elem, chn)) {
				json_t *capture = NULL;

				if (!snd_mixer_selem_has_common_volume(elem)) {
					if (snd_mixer_selem_has_capture_volume(elem)) {
						if (!capture) {
							capture = json_object();
							json_object_set_new(channel, "capture", capture);
						}

						json_object_set_new(capture, "volume", get_selem_volume(elem, chn, 1, cmin, cmax));
					}
				}
				if (!snd_mixer_selem_has_common_switch(elem)) {
					if (snd_mixer_selem_has_capture_switch(elem)) {
						int sw;

						if (!capture) {
							capture = json_object();
							json_object_set_new(channel, "capture", capture);
						}

						snd_mixer_selem_get_capture_switch(elem, chn, &sw);
						json_object_set_new(capture, "switch", json_string(sw ? "on" : "off"));
					}
				}
			}
		}
	}

	return selem;
}

static json_t*
get_card_mixer (const char *name)
{
	int err;
	snd_mixer_t *handle;
	snd_mixer_selem_id_t *sid;
	snd_mixer_elem_t *elem;

	json_t *mixer = json_array();

	snd_mixer_selem_id_alloca(&sid);
	
	if ((err = snd_mixer_open(&handle, 0)) < 0) {
		error("Mixer %s open error: %s", name, snd_strerror(err));
		return NULL;
	}

	smixer_options.device = name;

	if ((err = snd_mixer_selem_register(handle, &smixer_options, NULL)) < 0) {
		error("Mixer register error: %s", snd_strerror(err));
		snd_mixer_close(handle);
		return NULL;
	}
	err = snd_mixer_load(handle);
	if (err < 0) {
		error("Mixer %s load error: %s", name, snd_strerror(err));
		snd_mixer_close(handle);
		return NULL;
	}

	for (elem = snd_mixer_first_elem(handle); elem; elem = snd_mixer_elem_next(elem)) {
		json_t *selem;

		snd_mixer_selem_get_id(elem, sid);

		selem = get_selem(handle, sid, "  ", name);
		if (!snd_mixer_selem_is_active(elem))
			json_object_set_new(selem, "inactive", json_true());

		json_array_append_new(mixer, selem);
	}
	snd_mixer_close(handle);

	return mixer;
}

static json_t*
get_card (const char *name)
{
	int err;

	snd_ctl_t *handle;
	snd_ctl_card_info_t *info;

	json_t *card = json_object();

	snd_ctl_card_info_alloca(&info);


	if ((err = snd_ctl_open(&handle, name, 0)) < 0) {
		error("control open (%s): %s", name, snd_strerror(err));
		return NULL;
	}
	if ((err = snd_ctl_card_info(handle, info)) < 0) {
		error("control hardware info (%s): %s", name, snd_strerror(err));
		snd_ctl_close(handle);
		return NULL;
	}

	json_object_set_new(card, "id", json_string(snd_ctl_card_info_get_id(info)));	
	json_object_set_new(card, "driver", json_string(snd_ctl_card_info_get_driver(info)));
	json_object_set_new(card, "name", json_string(snd_ctl_card_info_get_name(info)));
	json_object_set_new(card, "longname", json_string(snd_ctl_card_info_get_longname(info)));
	json_object_set_new(card, "mixername", json_string(snd_ctl_card_info_get_mixername(info)));
	json_object_set_new(card, "components", json_string(snd_ctl_card_info_get_components(info)));

	snd_ctl_close(handle);

	json_object_set_new(card, "mixer", get_card_mixer(name));

	return card;
}

static json_t* 
get_cards(void)
{
	int err;
	int card;

	json_t *cards = json_array();

	card = -1;
	if ((err = snd_card_next(&card)) < 0 || card < 0) {
		error("no soundcards found...");
		return NULL;
	}

	while (card >= 0) {
		char name[32];
		sprintf(name, "hw:%d", card);

		json_array_append_new(cards, get_card(name));

		if ((err = snd_card_next(&card)) < 0) {
			error("snd_card_next");
			return NULL;
		}
	}

	return cards;
}

int
main (int argc, char *argv[])
{
	json_t *cards = get_cards();
	
	json_t *card;
	size_t i;

	json_array_foreach(cards, i, card) {
		json_t *selems = json_object_get(card, "mixer");

		json_t *selem;
		size_t j;

		printf("card\n");
		printf("get_id: %s\n", json_string_value(json_object_get(card, "id")));
		printf("get_driver: %s\n", json_string_value(json_object_get(card, "driver")));
		printf("get_name: %s\n", json_string_value(json_object_get(card, "name")));
		printf("get_longname: %s\n", json_string_value(json_object_get(card, "longname")));
		printf("get_mixername: %s\n", json_string_value(json_object_get(card, "mixername")));
		printf("get_components: %s\n", json_string_value(json_object_get(card, "components")));

		printf("card %s: %s [%s]\n",
			"**LOST**", json_string_value(json_object_get(card, "id")), json_string_value(json_object_get(card, "name")));
		
		json_array_foreach(selems, j, selem) {
			json_t *inactive = json_object_get(selem, "inactive");
			json_t *values = json_object_get(selem, "value");

			int is_enum = 0;

			if (inactive == json_true())
				printf("[INACTIVE] ");

			printf("Simple mixer control '%s',%i\n", json_string_value(json_object_get(selem, "name")), (int)json_integer_value(json_object_get(selem, "index")));

			{
				json_t *capabilities = json_object_get(selem, "capabilities");

				json_t *capability;
				size_t k;

				printf("  Capabilities:");
				json_array_foreach(capabilities, k, capability) {
					const char *cap = json_string_value(capability);
					is_enum |= !strcmp("enum", cap) || !strcmp("penum", cap) || !strcmp("cenum", cap);
					printf(" %s", cap);
				}
				printf("\n");
			}

			if (is_enum) {
				json_t *alternatives = json_object_get(selem, "alternatives");

				json_t *value;
				size_t k;
				
				printf("  Items:");
				json_array_foreach(alternatives, k, value) {
					printf(" '%s'", json_string_value(value));
				}
				printf("\n");

				json_array_foreach(values, k, value) {
					printf("  Item%zu: '%s'\n", k, json_string_value(value));
				}
			} else {
				json_t *captureExclusiveGroup = json_object_get(selem, "captureExclusiveGroup");
				json_t *playbackChannels = json_object_get(selem, "playbackChannels");
				json_t *captureChannels = json_object_get(selem, "captureChannels");
				json_t *limits = json_object_get(selem, "limits");

				json_t *value;
				size_t k;

				if (captureExclusiveGroup)
					printf("  Capture exclusive group: %" JSON_INTEGER_FORMAT "\n", json_integer_value(captureExclusiveGroup));

				if (playbackChannels) {
					char *sep = "";
					json_t *playbackChannel;

					printf("  Playback channels:");
					json_array_foreach(playbackChannels, k, playbackChannel) {
						printf("%s %s", sep, json_string_value(playbackChannel));
						sep = " -";
					}
					printf("\n");
				}
			
				if (captureChannels) {
					char *sep = "";
					json_t *captureChannel;

					printf("  Capture channels:");
					json_array_foreach(captureChannels, k, captureChannel) {
						printf("%s %s", sep, json_string_value(captureChannel));
						sep = " -";
					}
					printf("\n");
				}

				if (limits) {
					json_t *common = json_object_get(limits, "common");

					printf("  Limits:");
					if (common) {
						printf(" %" JSON_INTEGER_FORMAT " - %" JSON_INTEGER_FORMAT, json_integer_value(json_object_get(common, "min")), json_integer_value(json_object_get(common, "max")));
					} else {
						json_t *playback = json_object_get(limits, "playback");
						json_t *capture = json_object_get(limits, "capture");

						if (playback)
							printf(" Playback %" JSON_INTEGER_FORMAT " - %" JSON_INTEGER_FORMAT, json_integer_value(json_object_get(playback, "min")), json_integer_value(json_object_get(playback, "max")));
						if (capture)
							printf(" Capture %" JSON_INTEGER_FORMAT " - %" JSON_INTEGER_FORMAT, json_integer_value(json_object_get(capture, "min")), json_integer_value(json_object_get(capture, "max")));
					}
					printf("\n");
				}


				json_array_foreach(values, k, value) {
					json_t *volume = json_object_get(value, "volume");
					json_t *playback = json_object_get(value, "playback");
					json_t *capture = json_object_get(value, "capture");

					printf("  %s:", json_string_value(json_object_get(value, "channel")));


					if (volume) {
						json_t *db = json_object_get(volume, "dB");

						printf(" %" JSON_INTEGER_FORMAT " [%" JSON_INTEGER_FORMAT "%%]", json_integer_value(json_object_get(volume, "raw")), json_integer_value(json_object_get(volume, "perc")));
						if (db) {
							json_int_t dbv = json_integer_value(db);
							if (dbv < 0) 
								printf(" [-%" JSON_INTEGER_FORMAT ".%02" JSON_INTEGER_FORMAT "dB]", -dbv/100, -dbv % 100);
							else
								printf(" [%" JSON_INTEGER_FORMAT ".%02" JSON_INTEGER_FORMAT "dB]", dbv/100, dbv % 100);
						}
					} 

					if (playback) {
						json_t *volume = json_object_get(playback, "volume");
						json_t *sw = json_object_get(playback, "switch");

						if (volume || sw)
							printf(" Playback");
	
						if (volume) {
							json_t *db = json_object_get(volume, "dB");

							printf(" %" JSON_INTEGER_FORMAT " [%" JSON_INTEGER_FORMAT "%%]", json_integer_value(json_object_get(volume, "raw")), json_integer_value(json_object_get(volume, "perc")));
							if (db) {
								json_int_t dbv = json_integer_value(db);
								if (dbv < 0) 
									printf(" [-%" JSON_INTEGER_FORMAT ".%02" JSON_INTEGER_FORMAT "dB]", -dbv/100, -dbv % 100);
								else
									printf(" [%" JSON_INTEGER_FORMAT ".%02" JSON_INTEGER_FORMAT "dB]", dbv/100, dbv % 100);
							}
						}

						if (sw) 
							printf(" [%s]", json_string_value(sw));
					}

					if (capture) {
						json_t *volume = json_object_get(capture, "volume");
						json_t *sw = json_object_get(capture, "switch");

						if (volume || sw)
							printf(" Capture");
	
						if (volume) {
							json_t *db = json_object_get(volume, "dB");

							printf(" %" JSON_INTEGER_FORMAT " [%" JSON_INTEGER_FORMAT "%%]", json_integer_value(json_object_get(volume, "raw")), json_integer_value(json_object_get(volume, "perc")));
							if (db) {
								json_int_t dbv = json_integer_value(db);
								if (dbv < 0) 
									printf(" [-%" JSON_INTEGER_FORMAT ".%02" JSON_INTEGER_FORMAT "dB]", -dbv/100, -dbv % 100);
								else
									printf(" [%" JSON_INTEGER_FORMAT ".%02" JSON_INTEGER_FORMAT "dB]", dbv/100, dbv % 100);
							}
						}

						if (sw) 
							printf(" [%s]", json_string_value(sw));
					}
					printf("\n");
				}
			}
		}
	}
	json_decref(cards);

	return 0;
}