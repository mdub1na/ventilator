// M2-01 one-shot fan-control trial for Mac15,7 on macOS 27.0.
// This file is intentionally separate from prototype/smc-read: it contains SMC write command 6.
// Protocol layout cross-checked against the MIT-licensed implementation at:
// https://github.com/raminsharifi/MacFanControl/blob/main/src/smc.rs

#include "trial_logic.h"
#include "trial_actions.h"

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    SMC_SELECTOR = 2,
    SMC_READ_BYTES = 5,
    SMC_WRITE_BYTES = 6,
    SMC_READ_KEY_INFO = 9,
};

static const char *const TEMPERATURE_KEYS[TRIAL_TEMPERATURE_COUNT] = {"TCMz", "Tg0D", "TH0a"};

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t build;
    uint8_t reserved;
    uint16_t release;
} SmcVersion;

typedef struct {
    uint16_t version;
    uint16_t length;
    uint32_t cpu;
    uint32_t gpu;
    uint32_t memory;
} SmcPowerLimit;

typedef struct {
    uint32_t size;
    uint32_t type;
    uint8_t attributes;
} SmcKeyInfo;

typedef struct {
    uint32_t key;
    SmcVersion version;
    SmcPowerLimit power_limit;
    SmcKeyInfo key_info;
    uint8_t result;
    uint8_t status;
    uint8_t command;
    uint32_t index;
    uint8_t bytes[32];
} SmcKeyData;

_Static_assert(sizeof(SmcKeyData) == 80, "unexpected AppleSMC call layout");
_Static_assert(offsetof(SmcKeyData, bytes) == 48, "unexpected SMC data offset");

typedef struct {
    uint32_t size;
    char type[5];
    uint8_t bytes[32];
} SmcValue;

typedef struct {
    io_connect_t connection;
    kern_return_t kernel_status;
    uint8_t smc_status;
} Smc;

typedef struct {
    char mode_key[5];
    char target_key[5];
    SmcValue mode;
    SmcValue actual;
    SmcValue target;
    SmcValue minimum;
    SmcValue maximum;
} FanLive;

typedef struct {
    unsigned fan_count;
    SmcValue ftst;
    FanLive fans[TRIAL_FAN_COUNT];
    double temperatures_c[TRIAL_TEMPERATURE_COUNT];
} LiveSnapshot;

typedef struct {
    char key[5];
    double time;
    double value;
    bool ok;
    bool transport_attempted;
    kern_return_t kernel_status;
    uint8_t smc_status;
} WriteEvent;

typedef struct {
    Smc *smc;
    const LiveSnapshot *known_keys;
    const TrialPlan *allowed_plan;
    bool buffer_events;
    WriteEvent writes[64];
    size_t write_count;
    bool has_unlock_observation;
    double unlock_observation_time;
    TrialObservation unlock_observation;
} LiveBackend;

static volatile sig_atomic_t interrupted = 0;

static void on_signal(int signal_number) {
    (void)signal_number;
    interrupted = 1;
}

static uint32_t fourcc(const char *text) {
    return ((uint32_t)(uint8_t)text[0] << 24) |
           ((uint32_t)(uint8_t)text[1] << 16) |
           ((uint32_t)(uint8_t)text[2] << 8) |
           (uint32_t)(uint8_t)text[3];
}

static void fourcc_text(uint32_t value, char output[5]) {
    for (unsigned index = 0; index < 4; ++index) {
        output[index] = (char)(value >> (24 - index * 8));
    }
    output[4] = '\0';
}

static uint16_t big16(const uint8_t *bytes) {
    return ((uint16_t)bytes[0] << 8) | bytes[1];
}

static uint32_t big32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           bytes[3];
}

static uint32_t little32(const uint8_t *bytes) {
    return ((uint32_t)bytes[3] << 24) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[1] << 8) |
           bytes[0];
}

static double monotonic_seconds(void) {
    struct timespec value = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return -1.0;
    return (double)value.tv_sec + (double)value.tv_nsec / 1000000000.0;
}

static bool sleep_milliseconds(unsigned milliseconds) {
    struct timespec remaining = {
        .tv_sec = (time_t)(milliseconds / 1000),
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L,
    };
    while (!interrupted && nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) return false;
    }
    return !interrupted;
}

static bool sleep_milliseconds_uninterruptible(unsigned milliseconds) {
    struct timespec remaining = {
        .tv_sec = (time_t)(milliseconds / 1000),
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L,
    };
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) return false;
    }
    return true;
}

static bool smc_call(Smc *smc, SmcKeyData *request, SmcKeyData *response) {
    memset(response, 0, sizeof(*response));
    size_t output_size = sizeof(*response);
    smc->kernel_status = IOConnectCallStructMethod(
        smc->connection, SMC_SELECTOR, request, sizeof(*request), response, &output_size);
    smc->smc_status = response->result;
    return smc->kernel_status == KERN_SUCCESS &&
           output_size == sizeof(*response) && response->result == 0;
}

static void print_smc_error(const char *action, const char key[5], const Smc *smc) {
    fprintf(stderr, "%s %.4s failed: kernel=0x%08x smc=0x%02x\n",
            action, key, (unsigned)smc->kernel_status, smc->smc_status);
}

static bool read_key_info(Smc *smc, const char name[5], SmcKeyInfo *info, char type[5]) {
    SmcKeyData request = {0};
    SmcKeyData response = {0};
    request.key = fourcc(name);
    request.command = SMC_READ_KEY_INFO;
    if (!smc_call(smc, &request, &response)) return false;
    if (response.key_info.size == 0 || response.key_info.size > 32) return false;
    *info = response.key_info;
    fourcc_text(response.key_info.type, type);
    return true;
}

static bool read_key(Smc *smc, const char name[5], SmcValue *value) {
    SmcKeyInfo info = {0};
    if (!read_key_info(smc, name, &info, value->type)) return false;

    SmcKeyData request = {0};
    SmcKeyData response = {0};
    request.key = fourcc(name);
    request.key_info.size = info.size;
    request.command = SMC_READ_BYTES;
    if (!smc_call(smc, &request, &response)) return false;
    value->size = info.size;
    memset(value->bytes, 0, sizeof(value->bytes));
    memcpy(value->bytes, response.bytes, value->size);
    return true;
}

static bool write_key_exact(
    Smc *smc,
    const char name[5],
    const char expected_type[5],
    uint32_t expected_size,
    const uint8_t *bytes,
    size_t byte_count,
    bool report_errors,
    bool *transport_attempted) {
    if (transport_attempted != NULL) *transport_attempted = false;
    SmcKeyInfo info = {0};
    char actual_type[5] = {0};
    if (!read_key_info(smc, name, &info, actual_type)) {
        if (report_errors) print_smc_error("key-info", name, smc);
        return false;
    }
    if (strncmp(actual_type, expected_type, 4) != 0 ||
        info.size != expected_size || byte_count != expected_size || byte_count > 32) {
        if (report_errors) {
            fprintf(stderr,
                    "write %.4s blocked: expected type %.4s/%u, found %.4s/%u, bytes=%zu\n",
                    name, expected_type, expected_size, actual_type, info.size, byte_count);
        }
        return false;
    }

    SmcKeyData request = {0};
    SmcKeyData response = {0};
    request.key = fourcc(name);
    request.key_info.size = info.size;
    request.command = SMC_WRITE_BYTES;
    memcpy(request.bytes, bytes, byte_count);
    if (transport_attempted != NULL) *transport_attempted = true;
    if (!smc_call(smc, &request, &response)) {
        if (report_errors) print_smc_error("write", name, smc);
        return false;
    }
    return true;
}

static bool read_number(const SmcValue *value, double *number) {
    if (strncmp(value->type, "flt ", 4) == 0 && value->size == 4) {
        uint32_t bits = little32(value->bytes);
        float converted = 0;
        memcpy(&converted, &bits, sizeof(converted));
        *number = converted;
    } else if (strncmp(value->type, "sp78", 4) == 0 && value->size == 2) {
        *number = (int16_t)big16(value->bytes) / 256.0;
    } else if (strncmp(value->type, "fpe2", 4) == 0 && value->size == 2) {
        *number = big16(value->bytes) / 4.0;
    } else if (strncmp(value->type, "ui8 ", 4) == 0 && value->size == 1) {
        *number = value->bytes[0];
    } else if (strncmp(value->type, "ui16", 4) == 0 && value->size == 2) {
        *number = big16(value->bytes);
    } else if (strncmp(value->type, "ui32", 4) == 0 && value->size == 4) {
        *number = big32(value->bytes);
    } else {
        return false;
    }
    return isfinite(*number);
}

static bool read_ui8_exact(const SmcValue *value, uint8_t *number) {
    if (value->size != 1 || strncmp(value->type, "ui8 ", 4) != 0) return false;
    *number = value->bytes[0];
    return true;
}

static bool smc_open(Smc *smc) {
    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (service == IO_OBJECT_NULL) {
        fputs("AppleSMC service not found\n", stderr);
        return false;
    }
    kern_return_t status = IOServiceOpen(service, mach_task_self(), 0, &smc->connection);
    IOObjectRelease(service);
    if (status != KERN_SUCCESS) {
        fprintf(stderr, "AppleSMC open failed: 0x%08x\n", (unsigned)status);
        return false;
    }
    return true;
}

static bool sysctl_string(const char *name, char *output, size_t output_size) {
    size_t length = output_size;
    if (sysctlbyname(name, output, &length, NULL, 0) != 0 || length == 0 || length > output_size) {
        return false;
    }
    output[output_size - 1] = '\0';
    return true;
}

static bool exact_environment(char model[32], char os_version[32]) {
    if (!sysctl_string("hw.model", model, 32) ||
        !sysctl_string("kern.osproductversion", os_version, 32)) {
        fputs("cannot read hw.model or kern.osproductversion\n", stderr);
        return false;
    }
    if (strcmp(model, TRIAL_EXPECTED_MODEL) != 0 ||
        strcmp(os_version, TRIAL_EXPECTED_OS_VERSION) != 0) {
        fprintf(stderr, "unsupported environment: model=%s macOS=%s\n", model, os_version);
        return false;
    }
    return true;
}

static void fan_key(unsigned index, const char suffix[3], char output[5]) {
    output[0] = 'F';
    output[1] = (char)('0' + index);
    output[2] = suffix[0];
    output[3] = suffix[1];
    output[4] = '\0';
}

static bool read_fan(
    Smc *smc, unsigned index, FanLive *fan, bool include_metrics, bool report_errors) {
    char lower_mode[5];
    char upper_mode[5];
    fan_key(index, "md", lower_mode);
    fan_key(index, "Md", upper_mode);
    if (read_key(smc, lower_mode, &fan->mode)) {
        memcpy(fan->mode_key, lower_mode, sizeof(fan->mode_key));
    } else if (read_key(smc, upper_mode, &fan->mode)) {
        memcpy(fan->mode_key, upper_mode, sizeof(fan->mode_key));
    } else {
        if (report_errors) fprintf(stderr, "fan %u has no readable md/Md mode key\n", index);
        return false;
    }

    fan_key(index, "Tg", fan->target_key);
    if (!read_key(smc, fan->target_key, &fan->target)) return false;
    if (include_metrics) {
        char key[5];
        fan_key(index, "Ac", key);
        if (!read_key(smc, key, &fan->actual)) return false;
        fan_key(index, "Mn", key);
        if (!read_key(smc, key, &fan->minimum)) return false;
        fan_key(index, "Mx", key);
        if (!read_key(smc, key, &fan->maximum)) return false;
    }
    return true;
}

static bool read_snapshot(
    Smc *smc, LiveSnapshot *snapshot, bool include_metrics, bool report_errors) {
    memset(snapshot, 0, sizeof(*snapshot));
    SmcValue count = {0};
    uint8_t fan_count = 0;
    if (!read_key(smc, "FNum", &count) || !read_ui8_exact(&count, &fan_count)) {
        if (report_errors) fputs("FNum is unavailable or not ui8\n", stderr);
        return false;
    }
    snapshot->fan_count = fan_count;
    if (fan_count != TRIAL_FAN_COUNT) {
        if (report_errors) fprintf(stderr, "expected two fans, found %u\n", fan_count);
        return false;
    }
    if (!read_key(smc, "Ftst", &snapshot->ftst)) {
        if (report_errors) fputs("Ftst is unavailable\n", stderr);
        return false;
    }
    for (unsigned index = 0; index < TRIAL_FAN_COUNT; ++index) {
        if (!read_fan(smc, index, &snapshot->fans[index], include_metrics,
                      report_errors)) return false;
    }
    if (include_metrics) {
        for (unsigned index = 0; index < TRIAL_TEMPERATURE_COUNT; ++index) {
            SmcValue value = {0};
            if (!read_key(smc, TEMPERATURE_KEYS[index], &value) ||
                !read_number(&value, &snapshot->temperatures_c[index])) {
                if (report_errors) {
                    fprintf(stderr, "temperature %.4s is unavailable\n", TEMPERATURE_KEYS[index]);
                }
                return false;
            }
        }
    }
    return true;
}

static bool copy_fan_state(const FanLive *live, TrialFanState *state) {
    uint8_t mode = 0;
    if (!read_ui8_exact(&live->mode, &mode) ||
        !read_number(&live->actual, &state->actual_rpm) ||
        !read_number(&live->target, &state->target_rpm) ||
        !read_number(&live->minimum, &state->min_rpm) ||
        !read_number(&live->maximum, &state->max_rpm)) return false;
    state->mode = mode;
    memcpy(state->mode_type, live->mode.type, sizeof(state->mode_type));
    state->mode_size = live->mode.size;
    memcpy(state->target_type, live->target.type, sizeof(state->target_type));
    state->target_size = live->target.size;
    return true;
}

static bool snapshot_has_zero_targets(const LiveSnapshot *snapshot) {
    for (unsigned index = 0; index < TRIAL_FAN_COUNT; ++index) {
        double target = 0;
        if (!read_number(&snapshot->fans[index].target, &target) ||
            fabs(target) > 1.0) return false;
    }
    return true;
}

static bool snapshot_is_baseline(const LiveSnapshot *snapshot) {
    uint8_t ftst = 0;
    if (!read_ui8_exact(&snapshot->ftst, &ftst) || ftst != 0) return false;
    for (unsigned index = 0; index < TRIAL_FAN_COUNT; ++index) {
        uint8_t mode = 0;
        if (!read_ui8_exact(&snapshot->fans[index].mode, &mode) || mode != 3) return false;
    }
    return snapshot_has_zero_targets(snapshot);
}

static bool collect_preflight(
    Smc *smc,
    const char *model,
    const char *os_version,
    TrialPreflight *preflight,
    TrialPlan *plan,
    LiveSnapshot *last_snapshot) {
    memset(preflight, 0, sizeof(*preflight));
    preflight->model = model;
    preflight->os_version = os_version;

    for (unsigned sample = 0; sample < TRIAL_PREFLIGHT_SAMPLES; ++sample) {
        LiveSnapshot current = {0};
        if (!read_snapshot(smc, &current, true, true)) return false;
        if (!snapshot_is_baseline(&current)) {
            fputs("preflight blocked: modes 3, Ftst 0 and zero targets required in every sample\n",
                  stderr);
            return false;
        }
        for (unsigned sensor = 0; sensor < TRIAL_TEMPERATURE_COUNT; ++sensor) {
            preflight->temperatures_c[sample][sensor] = current.temperatures_c[sensor];
        }
        *last_snapshot = current;
        printf("{\"event\":\"preflight\",\"sample\":%u,\"TCMz\":%.2f,\"Tg0D\":%.2f,\"TH0a\":%.2f}\n",
               sample + 1,
               current.temperatures_c[0],
               current.temperatures_c[1],
               current.temperatures_c[2]);
        fflush(stdout);
        if (sample + 1 < TRIAL_PREFLIGHT_SAMPLES && !sleep_milliseconds(2500)) return false;
    }

    preflight->fan_count = last_snapshot->fan_count;
    if (!read_ui8_exact(&last_snapshot->ftst, &preflight->ftst)) return false;
    memcpy(preflight->ftst_type, last_snapshot->ftst.type, sizeof(preflight->ftst_type));
    preflight->ftst_size = last_snapshot->ftst.size;
    for (unsigned index = 0; index < TRIAL_FAN_COUNT; ++index) {
        if (!copy_fan_state(&last_snapshot->fans[index], &preflight->fans[index])) return false;
    }

    char error[200];
    if (!trial_make_plan(preflight, plan, error, sizeof(error))) {
        fprintf(stderr, "preflight blocked: %s\n", error);
        return false;
    }
    return true;
}

static void print_plan(
    const char *operation,
    const char *model,
    const char *os_version,
    const LiveSnapshot *snapshot,
    const TrialPlan *plan) {
    uint8_t ftst = 0;
    read_ui8_exact(&snapshot->ftst, &ftst);
    printf("operation=%s model=%s macOS=%s fans=%u Ftst=%u\n",
           operation, model, os_version, snapshot->fan_count, ftst);
    for (unsigned index = 0; index < TRIAL_FAN_COUNT; ++index) {
        TrialFanState state = {0};
        if (copy_fan_state(&snapshot->fans[index], &state)) {
            printf("fan=%u mode_key=%.4s mode=%u actual=%.0f target=%.0f min=%.0f max=%.0f",
                   index,
                   snapshot->fans[index].mode_key,
                   state.mode,
                   state.actual_rpm,
                   state.target_rpm,
                   state.min_rpm,
                   state.max_rpm);
            if (plan != NULL) printf(" planned=%.0f", plan->target_rpm[index]);
            printf(" target_type=%.4s/%u\n", state.target_type, state.target_size);
        } else {
            uint8_t mode = 0;
            double target = 0;
            if (!read_ui8_exact(&snapshot->fans[index].mode, &mode) ||
                !read_number(&snapshot->fans[index].target, &target)) continue;
            printf("fan=%u mode_key=%.4s mode=%u target=%.0f target_type=%.4s/%u\n",
                   index,
                   snapshot->fans[index].mode_key,
                   mode,
                   target,
                   snapshot->fans[index].target.type,
                   snapshot->fans[index].target.size);
        }
    }
}

static bool restore_keys_supported(const LiveSnapshot *snapshot) {
    uint8_t ftst = 0;
    if (!read_ui8_exact(&snapshot->ftst, &ftst)) return false;
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        uint8_t mode = 0;
        uint8_t encoded[4] = {0};
        size_t encoded_size = 0;
        if (!read_ui8_exact(&snapshot->fans[fan].mode, &mode) ||
            !trial_encode_rpm(
                snapshot->fans[fan].target.type,
                snapshot->fans[fan].target.size,
                0.0,
                encoded,
                &encoded_size)) return false;
    }
    return true;
}

static void buffer_write(
    LiveBackend *backend, const char key[5], double value,
    bool ok, bool transport_attempted) {
    if (!backend->buffer_events || backend->write_count >= 64) return;
    WriteEvent *event = &backend->writes[backend->write_count++];
    memcpy(event->key, key, sizeof(event->key));
    event->time = monotonic_seconds();
    event->value = value;
    event->ok = ok;
    event->transport_attempted = transport_attempted;
    event->kernel_status = backend->smc->kernel_status;
    event->smc_status = backend->smc->smc_status;
}

static void print_record(
    FILE *output, unsigned second, double time, const TrialObservation *observation) {
    fprintf(output,
            "{\"event\":\"observe\",\"time\":%.6f,\"second\":%u,"
            "\"mode\":[%u,%u],\"Ftst\":%u,\"actual\":[%.0f,%.0f],"
            "\"target\":[%.0f,%.0f],\"temperatures\":[%.2f,%.2f,%.2f]}\n",
            time, second,
            observation->mode[0], observation->mode[1], observation->ftst,
            observation->actual_rpm[0], observation->actual_rpm[1],
            observation->target_rpm[0], observation->target_rpm[1],
            observation->temperatures_c[0],
            observation->temperatures_c[1],
            observation->temperatures_c[2]);
}

static void print_buffered_events(const LiveBackend *backend, FILE *output) {
    bool observation_printed = false;
    for (size_t index = 0; index < backend->write_count; ++index) {
        const WriteEvent *event = &backend->writes[index];
        if (backend->has_unlock_observation && !observation_printed &&
            backend->unlock_observation_time < event->time) {
            print_record(output, 0, backend->unlock_observation_time,
                         &backend->unlock_observation);
            observation_printed = true;
        }
        fprintf(output,
                "{\"event\":\"write\",\"time\":%.6f,\"key\":\"%.4s\","
                "\"value\":%.0f,\"ok\":%s,\"transport_attempted\":%s",
                event->time, event->key, event->value,
                event->ok ? "true" : "false",
                event->transport_attempted ? "true" : "false");
        if (event->transport_attempted) {
            fprintf(output, ",\"kernel\":%u,\"smc\":%u",
                    (unsigned)event->kernel_status, event->smc_status);
        }
        fputs("}\n", output);
    }
    if (backend->has_unlock_observation && !observation_printed) {
        print_record(output, 0, backend->unlock_observation_time,
                     &backend->unlock_observation);
    }
    fflush(output);
}

static bool backend_write_mode(void *context, unsigned fan, uint8_t mode) {
    LiveBackend *backend = context;
    if (fan >= TRIAL_FAN_COUNT || (mode != 0 && mode != 1)) return false;
    const FanLive *known = &backend->known_keys->fans[fan];
    bool attempted = false;
    bool ok = write_key_exact(backend->smc, known->mode_key, "ui8 ", 1, &mode, 1,
                              !backend->buffer_events, &attempted);
    buffer_write(backend, known->mode_key, mode, ok, attempted);
    return ok;
}

static bool backend_write_target(void *context, unsigned fan, double rpm) {
    LiveBackend *backend = context;
    if (!trial_target_write_allowed(backend->allowed_plan, fan, rpm)) return false;
    const FanLive *known = &backend->known_keys->fans[fan];
    uint8_t encoded[4] = {0};
    size_t encoded_size = 0;
    if (!trial_encode_rpm(
            known->target.type, known->target.size, rpm, encoded, &encoded_size)) return false;
    bool attempted = false;
    bool ok = write_key_exact(
        backend->smc,
        known->target_key,
        known->target.type,
        known->target.size,
        encoded,
        encoded_size,
        !backend->buffer_events,
        &attempted);
    buffer_write(backend, known->target_key, rpm, ok, attempted);
    return ok;
}

static bool backend_write_ftst(void *context, uint8_t value) {
    LiveBackend *backend = context;
    if (value != 0 && value != 1) return false;
    if (strncmp(backend->known_keys->ftst.type, "ui8 ", 4) != 0 ||
        backend->known_keys->ftst.size != 1) return false;
    bool attempted = false;
    bool ok = write_key_exact(backend->smc, "Ftst", "ui8 ", 1, &value, 1,
                              !backend->buffer_events, &attempted);
    buffer_write(backend, "Ftst", value, ok, attempted);
    return ok;
}

static bool backend_read_observation(
    void *context,
    bool include_temperatures,
    TrialObservation *output) {
    LiveBackend *backend = context;
    LiveSnapshot current = {0};
    if (!read_snapshot(backend->smc, &current, include_temperatures,
                       !backend->buffer_events)) return false;
    if (!read_ui8_exact(&current.ftst, &output->ftst)) return false;
    for (unsigned fan = 0; fan < TRIAL_FAN_COUNT; ++fan) {
        const FanLive *known = &backend->known_keys->fans[fan];
        const FanLive *live = &current.fans[fan];
        if (strcmp(known->mode_key, live->mode_key) != 0 ||
            strcmp(known->target_key, live->target_key) != 0 ||
            strncmp(known->mode.type, live->mode.type, 4) != 0 ||
            known->mode.size != live->mode.size ||
            strncmp(known->target.type, live->target.type, 4) != 0 ||
            known->target.size != live->target.size ||
            !read_ui8_exact(&live->mode, &output->mode[fan]) ||
            !read_number(&live->target, &output->target_rpm[fan])) return false;
        if (include_temperatures &&
            !read_number(&live->actual, &output->actual_rpm[fan])) return false;
    }
    if (include_temperatures) {
        memcpy(output->temperatures_c, current.temperatures_c, sizeof(output->temperatures_c));
    }
    return true;
}

static bool backend_wait(void *context, unsigned milliseconds) {
    (void)context;
    return sleep_milliseconds_uninterruptible(milliseconds);
}

static double backend_now(void *context) {
    (void)context;
    return monotonic_seconds();
}

static bool backend_should_stop(void *context) {
    (void)context;
    return interrupted != 0;
}

static void backend_record(
    void *context,
    unsigned second,
    const TrialObservation *observation) {
    LiveBackend *backend = context;
    double time = monotonic_seconds();
    if (backend->buffer_events) {
        backend->has_unlock_observation = true;
        backend->unlock_observation_time = time;
        backend->unlock_observation = *observation;
        return;
    }
    print_record(stdout, second, time, observation);
    fflush(stdout);
}

static TrialBackend trial_backend(LiveBackend *context) {
    return (TrialBackend){
        .context = context,
        .write_mode = backend_write_mode,
        .write_target = backend_write_target,
        .write_ftst = backend_write_ftst,
        .read_observation = backend_read_observation,
        .wait_milliseconds = backend_wait,
        .monotonic_seconds = backend_now,
        .should_stop = backend_should_stop,
        .record_observation = backend_record,
    };
}

static bool prompt_for_direct_trial(const char *program) {
    printf("Prepare this command in a second Terminal before continuing:\n"
           "sudo %s restore --apply --confirm RESTORE-Mac15,7-27.0\n"
           "Type APPLY DIRECT TRIAL to continue: ",
           program);
    fflush(stdout);
    if (!isatty(STDIN_FILENO)) {
        fputs("trial apply requires an interactive TTY\n", stderr);
        return false;
    }
    char line[80];
    if (fgets(line, sizeof(line), stdin) == NULL) return false;
    line[strcspn(line, "\r\n")] = '\0';
    return strcmp(line, "APPLY DIRECT TRIAL") == 0;
}

static bool prompt_for_ftst_check(const char *program) {
    printf("Prepare this command in a second Terminal before continuing:\n"
           "sudo %s restore-unlock --apply --confirm RESTORE-UNLOCK-Mac15,7-27.0\n"
           "Type APPLY FTST CHECK to continue: ", program);
    fflush(stdout);
    if (!isatty(STDIN_FILENO)) {
        fputs("ftst-check apply requires an interactive TTY\n", stderr);
        return false;
    }
    char line[80];
    if (fgets(line, sizeof(line), stdin) == NULL) return false;
    line[strcspn(line, "\r\n")] = '\0';
    return strcmp(line, "APPLY FTST CHECK") == 0;
}

static bool install_signal_handlers(void) {
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGINT, &action, NULL) == 0 &&
           sigaction(SIGTERM, &action, NULL) == 0 &&
           sigaction(SIGQUIT, &action, NULL) == 0;
}

static int run_trial(Smc *smc, const char *program, bool apply) {
    char model[32] = {0};
    char os_version[32] = {0};
    if (!exact_environment(model, os_version)) return EXIT_FAILURE;

    TrialPreflight preflight = {0};
    TrialPlan plan = {0};
    LiveSnapshot baseline = {0};
    if (!collect_preflight(smc, model, os_version, &preflight, &plan, &baseline)) {
        return EXIT_FAILURE;
    }
    print_plan(apply ? "trial-apply" : "trial-dry-run", model, os_version, &baseline, &plan);
    if (!apply) {
        puts("dry-run complete: no SMC writes were attempted");
        return EXIT_SUCCESS;
    }
    if (geteuid() != 0) {
        fputs("trial --apply requires root\n", stderr);
        return EXIT_FAILURE;
    }
    if (!prompt_for_direct_trial(program)) {
        fputs("confirmation did not match; no SMC writes were attempted\n", stderr);
        return EXIT_FAILURE;
    }
    if (!install_signal_handlers()) {
        fputs("cannot install signal handlers; no SMC writes were attempted\n", stderr);
        return EXIT_FAILURE;
    }

    // Confirmation can take arbitrarily long. Recheck the full safety window and
    // calculate targets from its last sample immediately before the first write.
    puts("confirmation accepted; repeating preflight before any SMC write");
    if (!collect_preflight(smc, model, os_version, &preflight, &plan, &baseline)) {
        fputs("post-confirmation preflight failed; no SMC writes were attempted\n", stderr);
        return EXIT_FAILURE;
    }
    print_plan("trial-final", model, os_version, &baseline, &plan);

    double baseline_rpm[TRIAL_FAN_COUNT] = {0};
    for (unsigned index = 0; index < TRIAL_FAN_COUNT; ++index) {
        baseline_rpm[index] = preflight.fans[index].actual_rpm;
    }

    LiveBackend live = {.smc = smc, .known_keys = &baseline, .allowed_plan = &plan};
    TrialBackend backend = trial_backend(&live);
    TrialRunStatus status = trial_execute_direct(&backend, &plan, baseline_rpm);
    if (status == TRIAL_RUN_RESTORE_FAILED) {
        fputs("CRITICAL: automatic restore was not verified; run the prepared restore command\n",
              stderr);
        return EXIT_FAILURE;
    }
    if (status == TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED) {
        fputs("direct trial failed or was interrupted; system mode and zero targets verified\n",
              stderr);
        return EXIT_FAILURE;
    }
    puts("direct trial succeeded and system mode was restored");
    return EXIT_SUCCESS;
}

static int run_restore(Smc *smc, bool apply) {
    char model[32] = {0};
    char os_version[32] = {0};
    if (!exact_environment(model, os_version)) return EXIT_FAILURE;
    LiveSnapshot current = {0};
    if (!read_snapshot(smc, &current, false, true)) return EXIT_FAILURE;

    uint8_t ftst = 0;
    if (!read_ui8_exact(&current.ftst, &ftst)) {
        fputs("restore blocked: Ftst is not ui8\n", stderr);
        return EXIT_FAILURE;
    }
    if (!restore_keys_supported(&current)) {
        fputs("restore blocked: fixed mode/target key types are unsupported\n", stderr);
        return EXIT_FAILURE;
    }
    print_plan(apply ? "restore-apply" : "restore-dry-run", model, os_version, &current, NULL);
    if (!apply) {
        puts("dry-run complete: restore would write mode 0 and target 0 for F0/F1 only");
        return EXIT_SUCCESS;
    }
    if (geteuid() != 0) {
        fputs("restore --apply requires root\n", stderr);
        return EXIT_FAILURE;
    }
    if (ftst != 0) {
        fputs("Ftst is not zero. This direct-only tool did not set it and will not overwrite it.\n",
              stderr);
    }
    LiveBackend live = {.smc = smc, .known_keys = &current, .allowed_plan = NULL};
    TrialBackend backend = trial_backend(&live);
    if (!trial_restore_system(&backend)) {
        fputs("CRITICAL: restore was not verified; reboot and check modes with read-only reader\n",
              stderr);
        return EXIT_FAILURE;
    }
    puts("system mode and zero targets verified");
    return EXIT_SUCCESS;
}

static void print_ftst_readback(
    TrialBackend *backend, const char *event, FILE *output) {
    TrialObservation observation = {0};
    bool read_ok = backend->read_observation(backend->context, false, &observation);
    double time = monotonic_seconds();
    if (read_ok) {
        fprintf(output,
                "{\"event\":\"%s\",\"time\":%.6f,\"read_ok\":true,"
                "\"mode\":[%u,%u],\"Ftst\":%u,\"target\":[%.0f,%.0f]}\n",
                event, time, observation.mode[0], observation.mode[1], observation.ftst,
                observation.target_rpm[0], observation.target_rpm[1]);
    } else {
        fprintf(output, "{\"event\":\"%s\",\"time\":%.6f,\"read_ok\":false}\n",
                event, time);
    }
    fflush(output);
}

static int run_ftst_check(Smc *smc, const char *program, bool apply) {
    char model[32] = {0};
    char os_version[32] = {0};
    if (!exact_environment(model, os_version)) return EXIT_FAILURE;

    TrialPreflight preflight = {0};
    TrialPlan plan = {0};
    LiveSnapshot baseline = {0};
    if (!collect_preflight(smc, model, os_version, &preflight, &plan, &baseline) ||
        !snapshot_has_zero_targets(&baseline)) {
        fputs("ftst-check blocked: preflight or zero-target baseline failed\n", stderr);
        return EXIT_FAILURE;
    }
    print_plan(apply ? "ftst-check-apply" : "ftst-check-dry-run",
               model, os_version, &baseline, NULL);
    if (!apply) {
        puts("dry-run complete: no SMC writes were attempted");
        return EXIT_SUCCESS;
    }
    if (geteuid() != 0) {
        fputs("ftst-check --apply requires root\n", stderr);
        return EXIT_FAILURE;
    }
    if (!prompt_for_ftst_check(program)) {
        fputs("confirmation did not match; no SMC writes were attempted\n", stderr);
        return EXIT_FAILURE;
    }
    if (!install_signal_handlers()) {
        fputs("cannot install signal handlers; no SMC writes were attempted\n", stderr);
        return EXIT_FAILURE;
    }

    puts("confirmation accepted; repeating preflight before any SMC write");
    if (!collect_preflight(smc, model, os_version, &preflight, &plan, &baseline) ||
        !snapshot_has_zero_targets(&baseline)) {
        fputs("post-confirmation preflight failed; no SMC writes were attempted\n", stderr);
        return EXIT_FAILURE;
    }
    print_plan("ftst-check-final", model, os_version, &baseline, NULL);

    LiveBackend live = {.smc = smc, .known_keys = &baseline, .allowed_plan = NULL,
                        .buffer_events = true};
    TrialBackend backend = trial_backend(&live);
    TrialRunStatus status = trial_check_ftst(&backend);
    if (status == TRIAL_RUN_RESTORE_FAILED) {
        fflush(stdout);
        fputs("CRITICAL: Ftst/system baseline was not verified; run prepared restore-unlock command\n",
              stderr);
        fflush(stderr);
        print_buffered_events(&live, stderr);
        print_ftst_readback(&backend, "post_failure", stderr);
        return EXIT_FAILURE;
    }
    print_buffered_events(&live, stdout);
    print_ftst_readback(&backend, "final", stdout);
    if (status == TRIAL_RUN_BASELINE_REJECTED) {
        fputs("ftst-check blocked: baseline not verified before write; no SMC writes were attempted\n",
              stderr);
        return EXIT_FAILURE;
    }
    if (status == TRIAL_RUN_CONTROL_FAILED_SYSTEM_VERIFIED) {
        fputs("ftst-check failed or was interrupted; system baseline verified\n", stderr);
        return EXIT_FAILURE;
    }
    puts("Ftst 0 to 1 to 0 confirmed; fan modes and zero targets stayed at baseline");
    return EXIT_SUCCESS;
}

static int run_restore_unlock(Smc *smc, bool apply) {
    char model[32] = {0};
    char os_version[32] = {0};
    if (!exact_environment(model, os_version)) return EXIT_FAILURE;
    LiveSnapshot current = {0};
    if (!read_snapshot(smc, &current, false, true) || !restore_keys_supported(&current)) {
        fputs("restore-unlock blocked: keys are unreadable or unsupported\n", stderr);
        return EXIT_FAILURE;
    }
    uint8_t ftst = 0;
    if (!read_ui8_exact(&current.ftst, &ftst) || ftst > 1) {
        fputs("restore-unlock blocked: Ftst is not expected ui8 value 0 or 1\n", stderr);
        return EXIT_FAILURE;
    }
    print_plan(apply ? "restore-unlock-apply" : "restore-unlock-dry-run",
               model, os_version, &current, NULL);
    if (!apply) {
        puts("dry-run complete: restore-unlock would release fan modes/targets if needed, then clear Ftst");
        return EXIT_SUCCESS;
    }
    if (geteuid() != 0) {
        fputs("restore-unlock --apply requires root\n", stderr);
        return EXIT_FAILURE;
    }
    LiveBackend live = {.smc = smc, .known_keys = &current, .allowed_plan = NULL,
                        .buffer_events = true};
    TrialBackend backend = trial_backend(&live);
    bool verified = trial_restore_unlock(&backend);
    if (!verified) {
        fflush(stdout);
        fputs("CRITICAL: restore-unlock did not verify system baseline; reboot and read modes\n",
              stderr);
        fflush(stderr);
        print_buffered_events(&live, stderr);
        print_ftst_readback(&backend, "post_failure", stderr);
        return EXIT_FAILURE;
    }
    print_buffered_events(&live, stdout);
    print_ftst_readback(&backend, "final", stdout);
    puts("system mode, zero targets and Ftst=0 verified");
    return EXIT_SUCCESS;
}

static void usage(const char *program) {
    fprintf(stderr,
            "Usage:\n"
            "  %s trial --dry-run\n"
            "  %s restore --dry-run\n"
            "  %s ftst-check --dry-run\n"
            "  %s restore-unlock --dry-run\n"
            "  sudo %s restore --apply --confirm RESTORE-Mac15,7-27.0\n"
            "  sudo %s restore-unlock --apply --confirm RESTORE-UNLOCK-Mac15,7-27.0\n"
            "Hardware trial --apply commands are suspended after the Ftst incident.\n",
            program, program, program, program, program, program);
}

int main(int argc, char **argv) {
    bool trial = argc >= 2 && strcmp(argv[1], "trial") == 0;
    bool restore = argc >= 2 && strcmp(argv[1], "restore") == 0;
    bool ftst_check = argc >= 2 && strcmp(argv[1], "ftst-check") == 0;
    bool restore_unlock = argc >= 2 && strcmp(argv[1], "restore-unlock") == 0;
    bool dry_run = argc == 3 && strcmp(argv[2], "--dry-run") == 0;
    bool apply = argc == 5 && strcmp(argv[2], "--apply") == 0 &&
                 strcmp(argv[3], "--confirm") == 0 &&
                 ((trial && strcmp(argv[4], "TRIAL-Mac15,7-27.0") == 0) ||
                  (restore && strcmp(argv[4], "RESTORE-Mac15,7-27.0") == 0) ||
                  (ftst_check && strcmp(argv[4], "FTST-CHECK-Mac15,7-27.0") == 0) ||
                  (restore_unlock &&
                   strcmp(argv[4], "RESTORE-UNLOCK-Mac15,7-27.0") == 0));
    if ((!trial && !restore && !ftst_check && !restore_unlock) || (!dry_run && !apply)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Ftst=1 was accepted on Mac15,7, but the state changed after this tool
    // reported its original baseline and independent recovery was not verified.
    // Keep read-only checks and emergency restore commands available.
    if (apply && (trial || ftst_check)) {
        fputs("hardware trials suspended after delayed Ftst state change; "
              "no SMC access attempted\n", stderr);
        return EXIT_FAILURE;
    }

    Smc smc = {0};
    if (!smc_open(&smc)) return EXIT_FAILURE;
    char absolute_program[PATH_MAX] = {0};
    const char *program = realpath(argv[0], absolute_program) != NULL ? absolute_program : argv[0];
    int result = trial ? run_trial(&smc, program, apply) :
                 restore ? run_restore(&smc, apply) :
                 ftst_check ? run_ftst_check(&smc, program, apply) :
                 run_restore_unlock(&smc, apply);
    IOServiceClose(smc.connection);
    return result;
}
