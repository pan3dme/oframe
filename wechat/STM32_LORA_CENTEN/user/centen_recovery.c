#include "centen_recovery.h"
#include <string.h>

int centen_device_name_valid(const char *name)
{
    size_t i;
    if (!name || !name[0]) return 0;
    for (i = 0; i < 16u; ++i) {
        char c = name[i];
        if (!c) return 1;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return 0;
}

int centen_settings_valid(const CentenSettingsRecord *r)
{
    double lat, lng;
    return r->magic == CENTEN_CONFIG_MAGIC &&
        r->version == CENTEN_SETTINGS_VERSION && r->commit == CENTEN_CONFIG_COMMIT &&
        memchr(r->config, 0, sizeof(r->config)) &&
        memchr(r->center, 0, sizeof(r->center)) &&
        centen_device_name_valid(r->name) && r->power >= 1u && r->power <= 23u &&
        r->crc == centen_crc32(r, offsetof(CentenSettingsRecord, crc)) &&
        centen_config_valid(r->config) &&
        (!r->center[0] || centen_center_parse(r->center, &lat, &lng));
}

void centen_settings_prepare(CentenSettingsRecord *r, const char *config,
    const char *center, const char *name, uint32_t power, uint32_t sequence)
{
    memset(r, 0, sizeof(*r));
    r->magic = CENTEN_CONFIG_MAGIC;
    r->version = CENTEN_SETTINGS_VERSION;
    r->sequence = sequence;
    strncpy(r->config, config, sizeof(r->config) - 1u);
    strncpy(r->center, center, sizeof(r->center) - 1u);
    strncpy(r->name, name, sizeof(r->name) - 1u);
    r->power = power;
    r->crc = centen_crc32(r, offsetof(CentenSettingsRecord, crc));
    r->commit = CENTEN_CONFIG_COMMIT;
}

uint16_t centen_boot_streak(uint16_t previous, int valid, int powerOn,
    int watchdog, uint16_t requestedReason)
{
    uint16_t streak = (!powerOn && valid && previous <= CENTEN_BOOT_STREAK_MAX) ? previous : 0;
    if (!powerOn && (watchdog ||
        (requestedReason >= CENTEN_FAULT_RF && requestedReason <= CENTEN_FAULT_CLOCK))) {
        if (streak < CENTEN_BOOT_STREAK_MAX) ++streak;
    }
    return streak;
}

uint32_t centen_boot_backoff_ms(uint16_t streak)
{
    uint32_t delay = 30000u;
    if (streak < 3u) return 0;
    for (; streak > 3u && delay < 300000u; --streak) delay *= 2u;
    return delay > 300000u ? 300000u : delay;
}

void centen_recovery_failed(CentenRecovery *s, uint32_t now)
{
    s->active = 1;
    s->stable = 0;
    s->lastFailure = now;
    if (s->failures < 65535u) ++s->failures;
}

int centen_recovery_stable(CentenRecovery *s, uint32_t now, int healthy)
{
    if (!healthy) { s->stable = 0; return 0; }
    if (!s->stable) { s->stable = 1; s->stableSince = now; }
    if (now - s->stableSince < CENTEN_RECOVERY_STABLE_MS) return 0;
    s->active = 0;
    s->failures = 0;
    return 1;
}
