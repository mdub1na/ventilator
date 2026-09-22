// Read-only SMC inventory for macOS. No write command or writable key API exists here.
// Protocol layout and read selectors cross-checked against:
// https://github.com/raminsharifi/MacFanControl/blob/main/src/smc.rs (MIT).

#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <mach/mach.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SMC_SELECTOR = 2,
    SMC_READ_BYTES = 5,
    SMC_READ_INDEX = 8,
    SMC_READ_KEY_INFO = 9,
    MAX_SMC_KEYS = 20000,
};

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

static bool call_read(Smc *smc, SmcKeyData *request, SmcKeyData *response) {
    memset(response, 0, sizeof(*response));
    size_t output_size = sizeof(*response);
    smc->kernel_status = IOConnectCallStructMethod(
        smc->connection, SMC_SELECTOR, request, sizeof(*request), response, &output_size);
    smc->smc_status = response->result;
    return smc->kernel_status == KERN_SUCCESS &&
           output_size == sizeof(*response) && response->result == 0;
}

static bool read_key(Smc *smc, const char name[4], SmcValue *value) {
    SmcKeyData request = {0};
    SmcKeyData response = {0};
    request.key = fourcc(name);
    request.command = SMC_READ_KEY_INFO;
    if (!call_read(smc, &request, &response)) return false;
    if (response.key_info.size == 0 || response.key_info.size > 32) return false;

    value->size = response.key_info.size;
    fourcc_text(response.key_info.type, value->type);
    memset(&request, 0, sizeof(request));
    request.key = fourcc(name);
    request.key_info.size = value->size;
    request.command = SMC_READ_BYTES;
    if (!call_read(smc, &request, &response)) return false;
    memcpy(value->bytes, response.bytes, value->size);
    return true;
}

static bool read_number(const SmcValue *value, double *number) {
    if (strcmp(value->type, "flt ") == 0 && value->size >= 4) {
        uint32_t bits = little32(value->bytes);
        float converted;
        memcpy(&converted, &bits, sizeof(converted));
        *number = converted;
    } else if (strcmp(value->type, "sp78") == 0 && value->size >= 2) {
        *number = (int16_t)big16(value->bytes) / 256.0;
    } else if (strcmp(value->type, "fpe2") == 0 && value->size >= 2) {
        *number = big16(value->bytes) / 4.0;
    } else if (strcmp(value->type, "ui8 ") == 0 && value->size >= 1) {
        *number = value->bytes[0];
    } else if (strcmp(value->type, "ui16") == 0 && value->size >= 2) {
        *number = big16(value->bytes);
    } else if (strcmp(value->type, "ui32") == 0 && value->size >= 4) {
        *number = big32(value->bytes);
    } else {
        return false;
    }
    return isfinite(*number);
}

static void print_key(Smc *smc, const char *name, const char *unit) {
    SmcValue value = {0};
    double number = 0;
    if (!read_key(smc, name, &value)) {
        printf("  %-4s unavailable\n", name);
    } else if (read_number(&value, &number)) {
        printf("  %-4s %-6.0f %-4s (type %.4s)\n", name, number, unit, value.type);
    } else {
        printf("  %-4s present, type %.4s, size %u (decoder unavailable)\n",
               name, value.type, value.size);
    }
}

static void print_fans(Smc *smc) {
    SmcValue value = {0};
    double fan_count = 0;
    puts("Fans:");
    if (!read_key(smc, "FNum", &value) ||
        !read_number(&value, &fan_count) || fan_count < 0 || fan_count > 16) {
        puts("  FNum unavailable or unexpected; fan count cannot be determined");
        return;
    }
    printf("  FNum %.0f (type %.4s)\n", fan_count, value.type);
    for (unsigned fan = 0; fan < (unsigned)fan_count; ++fan) {
        char key[5] = "F0Ac";
        if (fan > 9) break; // Four-character SMC fan keys use one digit.
        key[1] = (char)('0' + fan);
        printf("  Fan %u:\n", fan);
        memcpy(key + 2, "ID", 2);
        if (read_key(smc, key, &value) && value.size >= 16) {
            char name[13] = {0};
            memcpy(name, value.bytes + 4, 12);
            for (unsigned character = 0; character < 12; ++character) {
                if ((unsigned char)name[character] < 32 ||
                    (unsigned char)name[character] > 126) name[character] = '\0';
            }
            if (name[0]) printf("  %-4s %s (unverified label)\n", key, name);
        }
        memcpy(key + 2, "Ac", 2);
        print_key(smc, key, "RPM");
        memcpy(key + 2, "Tg", 2); print_key(smc, key, "RPM");
        memcpy(key + 2, "Mn", 2); print_key(smc, key, "RPM");
        memcpy(key + 2, "Mx", 2); print_key(smc, key, "RPM");
        memcpy(key + 2, "Md", 2);
        if (read_key(smc, key, &value)) print_key(smc, key, "mode");
        else { memcpy(key + 2, "md", 2); print_key(smc, key, "mode"); }
    }
    puts("  Additional state keys (presence does not prove write access):");
    print_key(smc, "Ftst", "flag");
    print_key(smc, "FS! ", "bits");
}

static bool read_finite_number(Smc *smc, const char key[4], double *number) {
    SmcValue value = {0};
    return read_key(smc, key, &value) && read_number(&value, number);
}

static void print_json_number(Smc *smc, const char key[4], bool temperature) {
    double number = 0;
    if (!read_finite_number(smc, key, &number) ||
        (temperature && (number < 10 || number > 115)) ||
        (!temperature && number < 0)) {
        fputs("null", stdout);
    } else {
        printf("%.2f", number);
    }
}

static void print_status_json(Smc *smc) {
    double fan_count = 0;
    bool count_valid = read_finite_number(smc, "FNum", &fan_count) &&
                       fan_count >= 0 && fan_count <= 10 && floor(fan_count) == fan_count;
    fputs("{\"schema\":1,\"fan_count\":", stdout);
    if (count_valid) printf("%.0f", fan_count);
    else fputs("null", stdout);
    fputs(",\"fans\":[", stdout);
    if (count_valid) {
        for (unsigned fan = 0; fan < (unsigned)fan_count; ++fan) {
            char key[5] = "F0Ac";
            key[1] = (char)('0' + fan);
            if (fan) putchar(',');
            printf("{\"index\":%u,\"actual_rpm\":", fan);
            print_json_number(smc, key, false);
            fputs(",\"min_rpm\":", stdout);
            memcpy(key + 2, "Mn", 2);
            print_json_number(smc, key, false);
            fputs(",\"max_rpm\":", stdout);
            memcpy(key + 2, "Mx", 2);
            print_json_number(smc, key, false);
            putchar('}');
        }
    }
    fputs("],\"cpu_key\":\"TCMz\",\"cpu_temp_c\":", stdout);
    print_json_number(smc, "TCMz", true);
    fputs(",\"selected_temperatures\":[", stdout);
    const char *selected[] = {"Tg0D", "TH0a"};
    for (unsigned index = 0; index < sizeof(selected) / sizeof(selected[0]); ++index) {
        if (index) putchar(',');
        printf("{\"key\":\"%s\",\"celsius\":", selected[index]);
        print_json_number(smc, selected[index], true);
        putchar('}');
    }
    putchar(']');
    puts("}");
}

static bool read_key_at(Smc *smc, uint32_t index, char key[5]) {
    SmcKeyData request = {0};
    SmcKeyData response = {0};
    request.command = SMC_READ_INDEX;
    request.index = index;
    if (!call_read(smc, &request, &response)) return false;
    fourcc_text(response.key, key);
    for (unsigned character = 0; character < 4; ++character) {
        if ((unsigned char)key[character] < 32 ||
            (unsigned char)key[character] > 126) return false;
    }
    return true;
}

static void print_json_key(const char key[5]) {
    putchar('"');
    for (unsigned i = 0; i < 4; ++i) {
        if (key[i] == '"' || key[i] == '\\') putchar('\\');
        putchar(key[i]);
    }
    putchar('"');
}

static void print_temperatures_json(Smc *smc) {
    SmcValue value = {0};
    fputs("{\"schema\":1,\"temperatures\":", stdout);
    if (!read_key(smc, "#KEY", &value) || value.size < 4) {
        puts("null}");
        return;
    }
    uint32_t count = big32(value.bytes);
    if (count == 0 || count > MAX_SMC_KEYS) count = little32(value.bytes);
    if (count == 0 || count > MAX_SMC_KEYS) {
        puts("null}");
        return;
    }
    putchar('[');
    bool first = true;
    for (uint32_t index = 0; index < count; ++index) {
        char key[5];
        if (!read_key_at(smc, index, key) || key[0] != 'T') continue;
        if (!first) putchar(',');
        first = false;
        fputs("{\"key\":", stdout);
        print_json_key(key);
        fputs(",\"celsius\":", stdout);
        if (read_key(smc, key, &value) &&
            (strcmp(value.type, "flt ") == 0 || strcmp(value.type, "sp78") == 0)) {
            double celsius = 0;
            if (read_number(&value, &celsius) && celsius >= 10 && celsius <= 115) {
                printf("%.2f", celsius);
            } else {
                fputs("null", stdout);
            }
        } else {
            fputs("null", stdout);
        }
        putchar('}');
    }
    puts("]}");
}

static void print_temperatures(Smc *smc, bool show_all) {
    SmcValue value = {0};
    double count_number = 0;
    puts("Temperatures (raw SMC IDs; component names are unverified):");
    if (!read_key(smc, "#KEY", &value) || value.size < 4) {
        puts("  #KEY unavailable; temperature enumeration unavailable");
        return;
    }
    uint32_t count = big32(value.bytes);
    if (count == 0 || count > MAX_SMC_KEYS) count = little32(value.bytes);
    if (count == 0 || count > MAX_SMC_KEYS) {
        puts("  #KEY returned an implausible count");
        return;
    }
    if (read_number(&value, &count_number) && count_number != count) {
        printf("  #KEY count decoded using byte order probe: %u\n", count);
    }
    unsigned candidates = 0, readable = 0, plausible = 0, shown = 0;
    for (uint32_t index = 0; index < count; ++index) {
        char key[5];
        if (!read_key_at(smc, index, key) || key[0] != 'T') continue;
        ++candidates;
        if (!read_key(smc, key, &value)) continue;
        ++readable;
        double celsius = 0;
        if (strcmp(value.type, "flt ") != 0 && strcmp(value.type, "sp78") != 0) continue;
        if (!read_number(&value, &celsius) || celsius < 10 || celsius > 115) continue;
        ++plausible;
        if (show_all || shown < 24) {
            printf("  %-4s %.2f C (type %.4s)\n", key, celsius, value.type);
            ++shown;
        }
    }
    if (!show_all && plausible > shown) {
        printf("  ... %u more; use --all-temperatures to list all\n", plausible - shown);
    }
    printf("  Summary: %u total SMC keys, %u T-prefixed, %u readable, %u plausible temperatures\n",
           count, candidates, readable, plausible);
}

int main(int argc, char **argv) {
    bool show_all = argc == 2 && strcmp(argv[1], "--all-temperatures") == 0;
    bool status_json = argc == 2 && strcmp(argv[1], "--status-json") == 0;
    bool temperatures_json = argc == 2 && strcmp(argv[1], "--temperatures-json") == 0;
    if (argc > 2 || (argc == 2 && !show_all && !status_json && !temperatures_json)) {
        fprintf(stderr, "Usage: %s [--all-temperatures|--status-json|--temperatures-json]\n", argv[0]);
        return EXIT_FAILURE;
    }
    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (service == IO_OBJECT_NULL) {
        fputs("AppleSMC service not found\n", stderr);
        return EXIT_FAILURE;
    }
    Smc smc = {0};
    kern_return_t status = IOServiceOpen(service, mach_task_self(), 0, &smc.connection);
    IOObjectRelease(service);
    if (status != KERN_SUCCESS) {
        fprintf(stderr, "AppleSMC open failed: 0x%08x\n", (unsigned)status);
        if (status == kIOReturnNotPermitted) {
            fputs("Access denied by this process environment; try a normal Terminal session.\n",
                  stderr);
        }
        return EXIT_FAILURE;
    }
    if (status_json) print_status_json(&smc);
    else if (temperatures_json) print_temperatures_json(&smc);
    else {
        print_fans(&smc);
        print_temperatures(&smc, show_all);
    }
    IOServiceClose(smc.connection);
    return EXIT_SUCCESS;
}
