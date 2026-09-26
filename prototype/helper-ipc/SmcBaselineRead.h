#ifndef VENTILATOR_SMC_BASELINE_READ_H
#define VENTILATOR_SMC_BASELINE_READ_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// A fixed, read-only snapshot for the tested Mac15,7 / macOS 27.0 pair.
// No caller-supplied key, target, or write operation exists in this module.
typedef struct {
    uint8_t ftst;
    uint8_t mode[2];
    double target_rpm[2];
    double actual_rpm[2];
    double temperatures_c[3]; // TCMz, Tg0D, TH0a; NAN means unavailable
} SmcBaselineSnapshot;

typedef enum {
    SMC_BASELINE_OK,
    SMC_BASELINE_UNSUPPORTED_ENVIRONMENT,
    SMC_BASELINE_OPEN_FAILED,
    SMC_BASELINE_READ_FAILED,
    SMC_BASELINE_UNEXPECTED_FORMAT,
} SmcBaselineResult;

SmcBaselineResult smc_baseline_read(SmcBaselineSnapshot *snapshot);
bool smc_baseline_is_system(const SmcBaselineSnapshot *snapshot);
const char *smc_baseline_result_name(SmcBaselineResult result);

static inline bool smc_baseline_temperature_valid(double celsius) {
    return isfinite(celsius) && celsius >= 10.0 && celsius <= 115.0;
}

#endif
