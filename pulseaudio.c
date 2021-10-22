//
// Created by ludwig on 10/21/21.
//

#ifndef DAUDIO_PULSEAUDIO_C
#define DAUDIO_PULSEAUDIO_C

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <pulse/pulseaudio.h>

#include "pulseaudio.h"

static pa_context *context = NULL;
static pa_threaded_mainloop *threaded_mainloop = NULL;
static pa_mainloop_api *api = NULL;

static PulseSink *sinks = NULL;
static int sink_count = 0;

static char default_sink_name[sizeof(sinks->name)];
static PulseSink *default_sink = NULL;

static int updates = 0;


void context_state_callback(pa_context *c, void *userdata);

int setup_pulse() {

    if (context)
        return -1;

    if (pthread_mutex_init(&lock, NULL) != 0) {
        die("pthread_mutex_init() has failed");
    }

    threaded_mainloop = pa_threaded_mainloop_new();

    api = pa_threaded_mainloop_get_api(threaded_mainloop);

    pa_proplist *proplist = pa_proplist_new();
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "daudio");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "daudio");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "audio-card");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_VERSION, STRINGIZE(VERSION));

    context = pa_context_new_with_proplist(api, NULL, proplist);

    pa_proplist_free(proplist);

    pa_context_set_state_callback(context, context_state_callback, NULL);

    if (pa_context_connect(context, NULL, PA_CONTEXT_NOFAIL, NULL) < 0) {
        if (pa_context_errno(context) == PA_ERR_INVALID) {
            die("can't connect to pulseaudio, PA_ERR_INVALID");
        }
        else {
            die("can't connect to pulseaudio");
        }
    }

    pa_threaded_mainloop_start(threaded_mainloop);
    return 0;
}

int free_pulse() {
    pa_threaded_mainloop_stop(threaded_mainloop);
    pa_threaded_mainloop_free(threaded_mainloop);
    pthread_mutex_destroy(&lock);
    free(sinks);
    return 0;
}

void updated_default_sink() {
    for (int i = 0; i < sink_count; ++i) {
        if (strcmp(sinks[i].name, default_sink_name) == 0) {
            default_sink = &sinks[i];
            break;
        }
    }
}

PulseSink *get_or_add_sink(uint32_t index){
    for (PulseSink *sink = sinks; sink < sinks+sink_count; ++sink) {
        if (sink->index == index) {
            return sink;
        }
    }
    sink_count++;
    sinks = realloc(sinks,  sizeof(*sinks) * sink_count);
    return &sinks[sink_count - 1];
}

void remove_sink(uint32_t index) {
    for (int i = 0; i < sink_count; ++i) {
        if (sinks[i].index == index) {
            sink_count--;
            memmove(&sinks[i], &sinks[i+1], sink_count - i);
            sinks = realloc(sinks,  sizeof(*sinks) * sink_count);
            return;
        }
    }
}

void server_info_cb(pa_context *c, const pa_server_info *server_info, void *userdata) {
    pulse_lock();

    updates++;

    if (!server_info) {
        fprintf(stderr, "Server info callback failure");
        pulse_unlock();
        return;
    }

    strlcpy(default_sink_name, server_info->default_sink_name ? server_info->default_sink_name : "",
            sizeof(default_sink_name));

    updated_default_sink();
    pulse_unlock();
}

void sink_info_cb(pa_context *c, const pa_sink_info *sink_info, int eol, void *userdata) {
    pulse_lock();

    updates++;

    if (eol != 0) {
        if (eol == 1) {
            updated_default_sink();
        }
        pulse_unlock();
        return;
    };
    PulseSink *sink = get_or_add_sink(sink_info->index);
    strlcpy(sink->name, sink_info->name, sizeof(sink->name));
    strlcpy(sink->description, sink_info->description, sizeof(sink->description));
    sink->index = sink_info->index;
    sink->volume = sink_info->volume.values[0];
    sink->base_volume = sink_info->base_volume;
    sink->mute = sink_info->mute;
    sink->channels = sink_info->volume.channels;
    pulse_unlock();
}

void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t index, void *userdata) {

    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            if ((t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE)
                remove_sink(index);
            else {
                pa_operation *o;
                if (!(o = pa_context_get_sink_info_by_index(c, index, sink_info_cb, NULL))) {
                    fprintf(stderr, "pa_context_get_sink_info_list() failed");
                    return;
                }
                pa_operation_unref(o);
            }
            break;
        case PA_SUBSCRIPTION_EVENT_SERVER: {
            pa_operation *o;
            if (!(o = pa_context_get_server_info(c, server_info_cb, NULL))) {
                fprintf(stderr, "pa_context_get_server_info() failed");
                return;
            }
            pa_operation_unref(o);
            }
            break;
    }
}

void context_state_callback(pa_context *c, void *userdata) {

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;

        case PA_CONTEXT_READY: {
            pa_operation *o;

            pa_context_set_subscribe_callback(c, subscribe_cb, NULL);

            if (!(o = pa_context_subscribe(c, (pa_subscription_mask_t)
                    (PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER), NULL, NULL))) {
                fprintf(stderr, "pa_context_subscribe() failed");
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_server_info(c, server_info_cb, NULL))) {
                fprintf(stderr, "pa_context_get_server_info() failed");
                return;
            }
            pa_operation_unref(o);

            if (!(o = pa_context_get_sink_info_list(c, sink_info_cb, NULL))) {
                fprintf(stderr, "pa_context_get_sink_info_list() failed");
                return;
            }
            pa_operation_unref(o);

            break;
        }

        case PA_CONTEXT_FAILED:
            die("PA_CONTEXT_FAILED");
            return;

        case PA_CONTEXT_TERMINATED:
            die("PA_CONTEXT_TERMINATED");
            return;
        default:
            return;
    }
}

void set_volume(const PulseSink *sink, pa_volume_t volume) {
    pa_cvolume cvolume;
    pa_cvolume_set(&cvolume, sink->channels, volume);
    pa_context_set_sink_volume_by_index(context, sink->index, &cvolume, NULL, NULL);
}

void set_mute(const PulseSink *sink, uint8_t mute) {
    pa_context_set_sink_mute_by_index(context, sink->index, mute, NULL, NULL);
}

void set_default_sink(const PulseSink* sink) {
    pa_context_set_default_sink(context, sink->name, NULL, NULL);
}

const PulseSink *get_sinks() {
    return sinks;
}

int get_sinks_count() {
    return sink_count;
}

const PulseSink *get_default_sink()  {
    return default_sink;
}

const int get_updates() {
    return updates;
}

void updated() {
    pulse_lock();
    updates = 0;
    pulse_unlock();
}

void pulse_lock() {
    pthread_mutex_lock(&lock);
}
void pulse_unlock() {
    pthread_mutex_unlock(&lock);
}


#endif //DAUDIO_PULSEAUDIO_C
