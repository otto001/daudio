//
// Created by ludwig on 10/21/21.
//



#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>

#include "util.h"


typedef struct PulseSink {
    uint32_t index;
    char name[128];
    char description[128];
    pa_volume_t volume;
    pa_volume_t base_volume;
    uint8_t mute;
    uint8_t channels;
} PulseSink;

pthread_mutex_t lock;

int setup_pulse();

int free_pulse();

void set_volume(const PulseSink *sink, pa_volume_t volume);
void set_mute(const PulseSink *sink, uint8_t mute);
void set_default_sink(const PulseSink* sink);

const PulseSink *get_sinks();
int get_sinks_count();
const PulseSink *get_default_sink();

void pulse_lock();
void pulse_unlock();


const int get_updates();
void updated();
