/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <cutils/properties.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "audio_extn.h"
#include "platform.h"
#include "platform_api.h"

struct audio_extn_module {
    bool hpx_enabled;
    bool hifi_audio_enabled;
};

static struct audio_extn_module aextnmod = {
    .hpx_enabled = 0,
    .hifi_audio_enabled = 0,
};

#define AUDIO_PARAMETER_CUSTOM_STEREO  "stereo_as_dual_mono"
/* Query offload playback instances count */
#define AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE "offload_num_active"
#define AUDIO_PARAMETER_HPX            "HPX"

#ifndef CUSTOM_STEREO_ENABLED
#define audio_extn_customstereo_set_parameters(adev, parms)         (0)
#else
void audio_extn_customstereo_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    bool custom_stereo_state = false;
    const char *mixer_ctl_name = "Set Custom Stereo OnOff";
    struct mixer_ctl *ctl;

    ALOGV("%s", __func__);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_CUSTOM_STEREO, value,
                            sizeof(value));
    if (ret >= 0) {
        if (!strncmp("true", value, sizeof("true")) || atoi(value))
            custom_stereo_state = true;

        if (custom_stereo_state == aextnmod.custom_stereo_enabled)
            return;

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return;
        }
        if (mixer_ctl_set_value(ctl, 0, custom_stereo_state) < 0) {
            ALOGE("%s: Could not set custom stereo state %d",
                  __func__, custom_stereo_state);
            return;
        }
        aextnmod.custom_stereo_enabled = custom_stereo_state;
        ALOGV("%s: Setting custom stereo state success", __func__);
    }
}
#endif /* CUSTOM_STEREO_ENABLED */

#ifndef DTS_EAGLE
#define audio_extn_hpx_set_parameters(adev, parms)         (0)
#define audio_extn_hpx_get_parameters(query, reply)  (0)
#define audio_extn_check_and_set_dts_hpx_state(adev)       (0)
#else
void audio_extn_hpx_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    char prop[PROPERTY_VALUE_MAX] = "false";
    bool hpx_state = false;
    const char *mixer_ctl_name = "Set HPX OnOff";
    struct mixer_ctl *ctl = NULL;
    ALOGV("%s", __func__);

    property_get("use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (!strncmp("ON", value, sizeof("ON")))
            hpx_state = true;

        if (hpx_state == aextnmod.hpx_enabled)
            return;

        aextnmod.hpx_enabled = hpx_state;
        /* set HPX state on stream pp */
        if (adev->offload_effects_set_hpx_state != NULL)
            adev->offload_effects_set_hpx_state(hpx_state);

        audio_extn_dts_eagle_fade(adev, aextnmod.hpx_enabled, NULL);
        /* set HPX state on device pp */
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (ctl)
            mixer_ctl_set_value(ctl, 0, aextnmod.hpx_enabled);
    }
}

static int audio_extn_hpx_get_parameters(struct str_parms *query,
                                       struct str_parms *reply)
{
    int ret;
    char value[32]={0};

    ALOGV("%s: hpx %d", __func__, aextnmod.hpx_enabled);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (aextnmod.hpx_enabled)
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "ON");
        else
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "OFF");
    }
    return ret;
}

void audio_extn_check_and_set_dts_hpx_state(const struct audio_device *adev)
{
    char prop[PROPERTY_VALUE_MAX];
    property_get("use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;
    if (adev->offload_effects_set_hpx_state)
        adev->offload_effects_set_hpx_state(aextnmod.hpx_enabled);
}
#endif

#ifdef HIFI_AUDIO_ENABLED
bool audio_extn_is_hifi_audio_enabled(void)
{
    ALOGV("%s: status: %d", __func__, aextnmod.hifi_audio_enabled);
    return (aextnmod.hifi_audio_enabled ? true: false);
}

bool audio_extn_is_hifi_audio_supported(void)
{
    /*
     * for internal codec, check for hifiaudio property to enable hifi audio
     */
    if (property_get_bool("persist.audio.hifi.int_codec", false))
    {
        ALOGD("%s: hifi audio supported on internal codec", __func__);
        aextnmod.hifi_audio_enabled = 1;
    }

    return (aextnmod.hifi_audio_enabled ? true: false);
}
#endif

struct snd_card_split cur_snd_card_split = {
    .device = {0},
    .snd_card = {0},
    .form_factor = {0},
};

struct snd_card_split *audio_extn_get_snd_card_split()
{
    return &cur_snd_card_split;
}

void audio_extn_set_snd_card_split(const char* in_snd_card_name)
{
    /* sound card name follows below mentioned convention
       <target name>-<sound card name>-<form factor>-snd-card
       parse target name, sound card name and form factor
    */
    char *snd_card_name = strdup(in_snd_card_name);
    char *tmp = NULL;
    char *device = NULL;
    char *snd_card = NULL;
    char *form_factor = NULL;

    if (in_snd_card_name == NULL) {
        ALOGE("%s: snd_card_name passed is NULL", __func__);
        goto on_error;
    }

    device = strtok_r(snd_card_name, "-", &tmp);
    if (device == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.device, device, HW_INFO_ARRAY_MAX_SIZE);

    snd_card = strtok_r(NULL, "-", &tmp);
    if (snd_card == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.snd_card, snd_card, HW_INFO_ARRAY_MAX_SIZE);

    form_factor = strtok_r(NULL, "-", &tmp);
    if (form_factor == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.form_factor, form_factor, HW_INFO_ARRAY_MAX_SIZE);

    ALOGI("%s: snd_card_name(%s) device(%s) snd_card(%s) form_factor(%s)",
               __func__, in_snd_card_name, device, snd_card, form_factor);

on_error:
    if (snd_card_name)
        free(snd_card_name);
}

#ifdef KPI_OPTIMIZE_ENABLED
typedef int (*perf_lock_acquire_t)(int, int, int*, int);
typedef int (*perf_lock_release_t)(int);

static void *qcopt_handle;
static perf_lock_acquire_t perf_lock_acq;
static perf_lock_release_t perf_lock_rel;

static int perf_lock_handle;
char opt_lib_path[PROPERTY_VALUE_MAX] = {0};

int perf_lock_opts[] = {0x101, 0x20E, 0x30E};

int audio_extn_perf_lock_init(void)
{
    int ret = 0;
    if (qcopt_handle == NULL) {
        if (property_get("ro.vendor.extension_library",
                         opt_lib_path, NULL) <= 0) {
            ALOGE("%s: Failed getting perf property", __func__);
            ret = -EINVAL;
            goto err;
        }
        if ((qcopt_handle = dlopen(opt_lib_path, RTLD_NOW)) == NULL) {
            ALOGE("%s: Failed to open perf handle", __func__);
            ret = -EINVAL;
            goto err;
        } else {
            perf_lock_acq = (perf_lock_acquire_t)dlsym(qcopt_handle,
                                                       "perf_lock_acq");
            if (perf_lock_acq == NULL) {
                ALOGE("%s: Perf lock Acquire NULL", __func__);
                dlclose(qcopt_handle);
                qcopt_handle = NULL;
                ret = -EINVAL;
                goto err;
            }
            perf_lock_rel = (perf_lock_release_t)dlsym(qcopt_handle,
                                                       "perf_lock_rel");
            if (perf_lock_rel == NULL) {
                ALOGE("%s: Perf lock Release NULL", __func__);
                dlclose(qcopt_handle);
                qcopt_handle = NULL;
                ret = -EINVAL;
                goto err;
            }
            ALOGD("%s: Perf lock handles Success", __func__);
        }
    }
err:
    return ret;
}

void audio_extn_perf_lock_acquire(void)
{
    if (perf_lock_acq) {
        perf_lock_handle = perf_lock_acq(perf_lock_handle, 0, perf_lock_opts, 3);
        ALOGV("%s: Perf lock acquired", __func__);
    } else {
        ALOGE("%s: Perf lock acquire error", __func__);
    }
}

void audio_extn_perf_lock_release(void)
{
    if (perf_lock_rel && perf_lock_handle) {
        perf_lock_rel(perf_lock_handle);
        perf_lock_handle = 0;
        ALOGV("%s: Perf lock released", __func__);
    } else {
        ALOGE("%s: Perf lock release error", __func__);
    }
}
#endif /* KPI_OPTIMIZE_ENABLED */
