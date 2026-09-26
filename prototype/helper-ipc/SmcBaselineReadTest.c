#include "SmcBaselineRead.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    SmcBaselineSnapshot snapshot = {
        .ftst = 0,
        .mode = {3, 3},
        .target_rpm = {0, 0},
    };
    assert(smc_baseline_is_system(&snapshot));
    snapshot.target_rpm[1] = 1458;
    assert(!smc_baseline_is_system(&snapshot));
    snapshot.target_rpm[1] = 0;
    snapshot.ftst = 1;
    assert(!smc_baseline_is_system(&snapshot));
    snapshot.ftst = 0;
    snapshot.mode[0] = 0;
    assert(!smc_baseline_is_system(&snapshot));
    assert(strcmp(smc_baseline_result_name(SMC_BASELINE_READ_FAILED), "read_failed") == 0);
    if (argc == 1) {
        puts("baseline classifier tests passed");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "--live") != 0) return 2;
    SmcBaselineResult result = smc_baseline_read(&snapshot);
    if (result != SMC_BASELINE_OK) {
        fprintf(stderr, "read-only baseline failed: %s\n", smc_baseline_result_name(result));
        return 1;
    }
    printf("model=Mac15,7 macOS=27.0 Ftst=%u mode=[%u,%u] target=[%.0f,%.0f] "
           "actual=[%.0f,%.0f] temperatures=[%.2f,%.2f,%.2f] baseline=%s\n",
           snapshot.ftst, snapshot.mode[0], snapshot.mode[1],
           snapshot.target_rpm[0], snapshot.target_rpm[1],
           snapshot.actual_rpm[0], snapshot.actual_rpm[1],
           snapshot.temperatures_c[0], snapshot.temperatures_c[1],
           snapshot.temperatures_c[2],
           smc_baseline_is_system(&snapshot) ? "true" : "false");
    return 0;
}
