#include <obs-module.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <sstream>

#include "WebsocketEndpoint.hpp"

// TODO: there are memory leaks

// TODO: consider using buttons for "start" and "stop"

// TODO: turn this into a class/object - the aja plugin is a nice reference
struct deepgram_source_data {
	obs_source_t *context;
    // the websocket object, connection id, and auth info used to connect to deepgram
	WebsocketEndpoint *endpoint;
	int endpoint_id;
	std::string api_key;
	// the audio source (and name) to stream to deepgram (via an audio capture callback)
	obs_source_t *audio_source;
	std::string audio_source_name;
	// the text source and transcript to display in it
	obs_source_t *text_source;
	std::string transcript;
};

static const char *deepgram_source_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Deepgram Source");
}

int16_t f32_to_i16(float f) {
	f = f * 32768;
	if (f > 32767) {
		return 32767;
	}
	if (f < -32768) {
		return -32768;
	}
	return (int16_t) f;
}

static void audio_capture(void *param, obs_source_t *source, const struct audio_data *audio_data, bool muted)
{
	struct deepgram_source_data *dgsd = (deepgram_source_data *) param;
	UNUSED_PARAMETER(source);

    if (audio_data != NULL) {
		if (dgsd->endpoint != NULL) {
			// TODO: double check audio format
			// TODO: mix multichannel audio together
			uint16_t *i16_audio = (uint16_t *) malloc(audio_data->frames * sizeof(float) / 2);
			for (int i = 0; i < audio_data->frames; i++) {
				float sample_float;
				memcpy(&sample_float, &audio_data->data[0][0 + i * sizeof(float)], sizeof(float));
				i16_audio[i] = f32_to_i16(sample_float);
			}
			dgsd->endpoint->send_binary(dgsd->endpoint_id, i16_audio, audio_data->frames * sizeof(float) / 2);
			free(i16_audio);
		}
    }
}

static void deepgram_source_update(void *data, obs_data_t *settings)
{
	blog(LOG_INFO, "Running deepgram_source_update.");

    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;
	const char *audio_source_name = obs_data_get_string(settings, "audio_source_name");
	const char *api_key = obs_data_get_string(settings, "api_key");

	// if the update included a change to the selected audio source
    if (dgsd->audio_source_name != audio_source_name) {
		// if there is an audio source, we will remove its capture callback
		if (dgsd->audio_source != NULL) {
			obs_source_remove_audio_capture_callback(dgsd->audio_source, audio_capture, dgsd);
		}
		// if there is a websocket object present, we will remove it
        if (dgsd->endpoint != NULL) {
            delete dgsd->endpoint;
        }
		// if the new selected audio source is not "none"
		// and if we have an api key,
		// we will set up a new websocket object and audio capture callback
        dgsd->audio_source_name = audio_source_name;
		dgsd->api_key = api_key;
        if (audio_source_name != "none" && api_key != "") {
			blog(LOG_INFO, "The audio source changed and we have an API Key, so we will try to connect to Deepgram and setup an audio callback.");

			// set up the websocket object connecting to deepgram
			WebsocketEndpoint *endpoint = new WebsocketEndpoint();
			// TODO: edit the url/query parameters based on the actual audio format
			int endpoint_id = endpoint->connect("wss://api.deepgram.com/v1/listen?encoding=linear16&sample_rate=44100&channels=1", api_key);
			dgsd->endpoint = endpoint;
			dgsd->endpoint_id = endpoint_id;

			// set up the audio capture callback
            obs_source_t *audio_source = obs_get_source_by_name(audio_source_name);
            dgsd->audio_source = audio_source;
            obs_source_add_audio_capture_callback(audio_source, audio_capture, dgsd);
        }
    }
}

static void *deepgram_source_create(obs_data_t *settings, obs_source_t *source)
{
	blog(LOG_INFO, "Running deepgram_source_create.");

	struct deepgram_source_data *dgsd = (deepgram_source_data *) bzalloc(sizeof(struct deepgram_source_data));
	dgsd->context = source;
    dgsd->audio_source_name = "none";
	dgsd->api_key = "";
	dgsd->endpoint = NULL;

	// also grab and store info about the audio format
    //audio_output_get_info(obs_get_audio()).format;
    //audio_output_get_sample_rate(obs_get_audio());
    //audio_output_get_channels(obs_get_audio());

	// set up a text source as a child of our deepgram source
	const char *text_source_id = "text_ft2_source";
	dgsd->text_source = obs_source_create_private(text_source_id, text_source_id, settings);
	obs_source_add_active_child(dgsd->context, dgsd->text_source);

	// set up some defaults for this new text source
	obs_data_t *text_source_settings = obs_source_get_settings(dgsd->text_source);
	obs_data_set_string(text_source_settings, "text", "transcript...");
	obs_data_t *font_obj = obs_data_get_obj(text_source_settings, "font");
	obs_data_set_int(font_obj, "size", 256);
	obs_source_update(dgsd->text_source, text_source_settings);

	deepgram_source_update(dgsd, settings);

    return(dgsd);
}

static void deepgram_source_destroy(void *data)
{
	blog(LOG_INFO, "Running deepgram_source_destroy.");

    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;

	// if there is an audio source, we will remove its capture callback
	if (dgsd->audio_source != NULL) {
		obs_source_remove_audio_capture_callback(dgsd->audio_source, audio_capture, dgsd);
	}

	// if there is a websocket object present, we will remove it
    if (dgsd->endpoint != NULL) {
        delete dgsd->endpoint;
    }

	obs_source_remove(dgsd->text_source);
	obs_source_release(dgsd->text_source);
	dgsd->text_source = NULL;

	bfree(dgsd);
}

static void deepgram_source_render(void *data, gs_effect_t *effect)
{
    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;	
	obs_source_video_render(dgsd->text_source);
}

static uint32_t deepgram_source_width(void *data)
{
    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;	
	return obs_source_get_width(dgsd->text_source);
}

static uint32_t deepgram_source_height(void *data)
{
    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;	
	return obs_source_get_height(dgsd->text_source);
}

struct sources_and_parent {
	obs_property_t *sources;
	obs_source_t *parent;
};

static bool add_sources(void *data, obs_source_t *source)
{
	struct sources_and_parent *info = (sources_and_parent *) data;
	uint32_t capabilities = obs_source_get_output_flags(source);

	if (source == info->parent)
		return true;

	// if this source does not have audio, return and don't allow it to be selectable
	if ((capabilities & OBS_SOURCE_AUDIO) == 0)
		return true;

	// we will add the source name to our "list of sources" property
	// we will use the name later to grab the source itself
	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(info->sources, name, name);

	return true;
}

static obs_properties_t *deepgram_source_properties(void *data)
{
	blog(LOG_INFO, "Running deepgram_source_properties.");

    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;
	obs_properties_t *properties = obs_properties_create();
	// instead of starting with these text properties, try adding them later with "obs_properties_add_group"
	//obs_properties_t *properties = obs_source_properties(dgsd->text_source);

	// TODO: add a drop-down for language

	// TODO: add a text property for API Key
	obs_properties_add_text(properties, "api_key", obs_module_text("Deepgram API Key"), OBS_TEXT_PASSWORD);

	// add a drop-down to select the audio source
	// TODO: consider what "parent" is and if I actually need it
	obs_source_t *parent = NULL;
	obs_property_t *sources = obs_properties_add_list(
		properties, "audio_source_name", obs_module_text("Audio Source"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(sources, obs_module_text("None"), "none");
	struct sources_and_parent info = {sources, parent};
	obs_enum_sources(add_sources, &info);

	return properties;
}

static void deepgram_source_tick(void *data, float seconds)
{
    struct deepgram_source_data *dgsd = (deepgram_source_data *) data;

	if (dgsd->endpoint != NULL) {
		// TODO: double-check JSON
		std::vector<std::string> messages = dgsd->endpoint->get_messages(dgsd->endpoint_id);
		for (auto message : messages) {
			auto json_message = nlohmann::json::parse(message);
			std::string transcript = json_message["channel"]["alternatives"][0]["transcript"];
			if (json_message["is_final"]) {
				if (dgsd->transcript != "" && transcript != "") {
					dgsd->transcript += " ";
				}
				if (transcript != "") {
					dgsd->transcript += transcript;
				}
			}
		}

		obs_data_t *text_source_settings = obs_source_get_settings(dgsd->text_source);
		obs_data_set_string(text_source_settings, "text", dgsd->transcript.c_str());
		obs_source_update(dgsd->text_source, text_source_settings);
	}
}

static void deepgram_source_defaults(obs_data_t *settings)
{
	blog(LOG_INFO, "Running deepgram_defaults.");
	obs_data_set_default_string(settings, "audio_source_name", "none");
	obs_data_set_default_string(settings, "api_key", "");
}

void enum_active_sources(void *data, obs_source_enum_proc_t enum_callback, void *param)
{
	struct deepgram_source_data *dgsd = (deepgram_source_data *) data;

	enum_callback(dgsd->context, dgsd->text_source, param);
}

struct obs_source_info deepgram_source = {
	.id = "deepgram_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = deepgram_source_name,
	.create = deepgram_source_create,
	.destroy = deepgram_source_destroy,
	.update = deepgram_source_update,
	.video_tick = deepgram_source_tick,
    .video_render = deepgram_source_render,
    .get_width = deepgram_source_width,
    .get_height = deepgram_source_height,
	.get_defaults = deepgram_source_defaults,
	.get_properties = deepgram_source_properties,
	.enum_active_sources = enum_active_sources
};
