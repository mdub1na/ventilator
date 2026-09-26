// Fixed-key AppleSMC reader. Selector 6 (write) is deliberately absent.
// Read protocol layout cross-checked against the MIT-licensed smc.rs at
// https://github.com/raminsharifi/MacFanControl/blob/main/src/smc.rs
#include "SmcBaselineRead.h"

#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
#include <sys/sysctl.h>

enum { SMC_SELECTOR = 2, SMC_READ_BYTES = 5, SMC_READ_KEY_INFO = 9 };

typedef struct {
    uint8_t major, minor, build, reserved;
    uint16_t release;
} SmcVersion;

typedef struct {
    uint16_t version, length;
    uint32_t cpu, gpu, memory;
} SmcPowerLimit;

typedef struct {
    uint32_t size, type;
    uint8_t attributes;
} SmcKeyInfo;

typedef struct {
    uint32_t key;
    SmcVersion version;
    SmcPowerLimit power_limit;
    SmcKeyInfo key_info;
    uint8_t result, status, command;
    uint32_t index;
    uint8_t bytes[32];
} SmcKeyData;

_Static_assert(sizeof(SmcKeyData) == 80, "unexpected AppleSMC call layout");
_Static_assert(offsetof(SmcKeyData, bytes) == 48, "unexpected SMC data offset");

static uint32_t fourcc(const char *text) {
    return ((uint32_t)(uint8_t)text[0] << 24) |
           ((uint32_t)(uint8_t)text[1] << 16) |
           ((uint32_t)(uint8_t)text[2] << 8) |
           (uint32_t)(uint8_t)text[3];
}

static bool expected_environment(void) {
    char model[32] = {0};
    char version[32] = {0};
    size_t model_size = sizeof(model);
    size_t version_size = sizeof(version);
    if (sysctlbyname("hw.model", model, &model_size, NULL, 0) != 0 ||
        sysctlbyname("kern.osproductversion", version, &version_size, NULL, 0) != 0 ||
        model_size == 0 || version_size == 0) return false;
    model[sizeof(model) - 1] = '\0';
    version[sizeof(version) - 1] = '\0';
    return strcmp(model, "Mac15,7") == 0 && strcmp(version, "27.0") == 0;
}

static bool smc_call(io_connect_t connection, SmcKeyData *request, SmcKeyData *response) {
    memset(response, 0, sizeof(*response));
    size_t response_size = sizeof(*response);
    return IOConnectCallStructMethod(connection, SMC_SELECTOR, request, sizeof(*request),
               response, &response_size) == KERN_SUCCESS &&
           response_size == sizeof(*response) && response->result == 0;
}

static SmcBaselineResult read_value(io_connect_t connection, const char *key,
                                    const char *type, uint32_t size, uint8_t bytes[32]) {
    SmcKeyData request = {0};
    SmcKeyData response = {0};
    request.key = fourcc(key);
    request.command = SMC_READ_KEY_INFO;
    if (!smc_call(connection, &request, &response)) return SMC_BASELINE_READ_FAILED;
    if (response.key_info.type != fourcc(type) || response.key_info.size != size)
        return SMC_BASELINE_UNEXPECTED_FORMAT;
    memset(&request, 0, sizeof(request));
    request.key = fourcc(key);
    request.key_info.size = size;
    request.command = SMC_READ_BYTES;
    if (!smc_call(connection, &request, &response)) return SMC_BASELINE_READ_FAILED;
    memcpy(bytes, response.bytes, size);
    return SMC_BASELINE_OK;
}

static SmcBaselineResult read_u8(io_connect_t connection, const char *key, uint8_t *value) {
    uint8_t bytes[32] = {0};
    SmcBaselineResult result = read_value(connection, key, "ui8 ", 1, bytes);
    if (result == SMC_BASELINE_OK) *value = bytes[0];
    return result;
}

static SmcBaselineResult read_float(io_connect_t connection, const char *key, double *value) {
    uint8_t bytes[32] = {0};
    SmcBaselineResult result = read_value(connection, key, "flt ", 4, bytes);
    if (result != SMC_BASELINE_OK) return result;
    uint32_t bits = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                    ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    float converted = 0;
    memcpy(&converted, &bits, sizeof(converted));
    if (!isfinite(converted)) return SMC_BASELINE_UNEXPECTED_FORMAT;
    *value = converted;
    return SMC_BASELINE_OK;
}

SmcBaselineResult smc_baseline_read(SmcBaselineSnapshot *snapshot) {
    if (snapshot == NULL) return SMC_BASELINE_UNEXPECTED_FORMAT;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!expected_environment()) return SMC_BASELINE_UNSUPPORTED_ENVIRONMENT;
    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (service == IO_OBJECT_NULL) return SMC_BASELINE_OPEN_FAILED;
    io_connect_t connection = IO_OBJECT_NULL;
    kern_return_t opened = IOServiceOpen(service, mach_task_self(), 0, &connection);
    IOObjectRelease(service);
    if (opened != KERN_SUCCESS) return SMC_BASELINE_OPEN_FAILED;

    SmcBaselineResult result = SMC_BASELINE_OK;
    uint8_t fan_count = 0;
#define READ_OR_CLOSE(expression) do { result = (expression); if (result != SMC_BASELINE_OK) goto close; } while (0)
    READ_OR_CLOSE(read_u8(connection, "FNum", &fan_count));
    if (fan_count != 2) { result = SMC_BASELINE_UNEXPECTED_FORMAT; goto close; }
    READ_OR_CLOSE(read_u8(connection, "Ftst", &snapshot->ftst));
    READ_OR_CLOSE(read_u8(connection, "F0Md", &snapshot->mode[0]));
    READ_OR_CLOSE(read_u8(connection, "F1Md", &snapshot->mode[1]));
    READ_OR_CLOSE(read_float(connection, "F0Tg", &snapshot->target_rpm[0]));
    READ_OR_CLOSE(read_float(connection, "F1Tg", &snapshot->target_rpm[1]));
    READ_OR_CLOSE(read_float(connection, "F0Ac", &snapshot->actual_rpm[0]));
    READ_OR_CLOSE(read_float(connection, "F1Ac", &snapshot->actual_rpm[1]));
    READ_OR_CLOSE(read_float(connection, "TCMz", &snapshot->temperatures_c[0]));
    READ_OR_CLOSE(read_float(connection, "Tg0D", &snapshot->temperatures_c[1]));
    READ_OR_CLOSE(read_float(connection, "TH0a", &snapshot->temperatures_c[2]));
#undef READ_OR_CLOSE
close:
    IOServiceClose(connection);
    if (result != SMC_BASELINE_OK) memset(snapshot, 0, sizeof(*snapshot));
    return result;
}

bool smc_baseline_is_system(const SmcBaselineSnapshot *snapshot) {
    if (snapshot == NULL || snapshot->ftst != 0) return false;
    for (unsigned fan = 0; fan < 2; ++fan) {
        if (snapshot->mode[fan] != 3 || !isfinite(snapshot->target_rpm[fan]) ||
            fabs(snapshot->target_rpm[fan]) > 1.0) return false;
    }
    return true;
}

const char *smc_baseline_result_name(SmcBaselineResult result) {
    switch (result) {
        case SMC_BASELINE_OK: return "ok";
        case SMC_BASELINE_UNSUPPORTED_ENVIRONMENT: return "unsupported_environment";
        case SMC_BASELINE_OPEN_FAILED: return "open_failed";
        case SMC_BASELINE_READ_FAILED: return "read_failed";
        case SMC_BASELINE_UNEXPECTED_FORMAT: return "unexpected_format";
    }
    return "unknown_error";
}
