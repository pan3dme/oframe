#ifndef CENTEN_RECOVERY_H
#define CENTEN_RECOVERY_H

#include "centen_support.h"

/* All BKP values, including the cookie, must fit a 16-bit register. */
#define CENTEN_BOOT_COOKIE 0xCE13u
#define CENTEN_BOOT_STREAK_MAX 15u
#define CENTEN_SETTINGS_VERSION 2u
#define CENTEN_RECOVERY_STABLE_MS 60000u
#define CENTEN_RF_RECOVERY_LIMIT 20u

enum {
    CENTEN_FAULT_NONE = 0,
    CENTEN_FAULT_RF = 1,
    CENTEN_FAULT_UART = 2,
    CENTEN_FAULT_DTU = 3,
    CENTEN_FAULT_CLOCK = 4,
    CENTEN_FAULT_IWDG = 5
};

typedef struct {
    uint32_t magic, version, sequence;
    char config[32], center[32], name[16];
    uint32_t power, crc, commit;
} CentenSettingsRecord;

typedef struct {
    uint32_t lastFailure, stableSince;
    uint16_t failures;
    uint8_t active, stable;
} CentenRecovery;

int centen_device_name_valid(const char *name);
int centen_settings_valid(const CentenSettingsRecord *record);
void centen_settings_prepare(CentenSettingsRecord *record, const char *config,
    const char *center, const char *name, uint32_t power, uint32_t sequence);
uint16_t centen_boot_streak(uint16_t previous, int valid, int powerOn,
    int watchdog, uint16_t requestedReason);
uint32_t centen_boot_backoff_ms(uint16_t streak);
void centen_recovery_failed(CentenRecovery *state, uint32_t now);
/* A successful init alone does not erase the history of a failing device. */
int centen_recovery_stable(CentenRecovery *state, uint32_t now, int healthy);

#endif
