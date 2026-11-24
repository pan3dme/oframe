#ifndef CENTEN_SUPPORT_H
#define CENTEN_SUPPORT_H

#include <stdint.h>
#include <stddef.h>

#define CENTEN_RX_RING_SIZE 1024u
#define CENTEN_FRAME_SIZE 512u
#define CENTEN_COMMAND_FRAME_SIZE 100u
#define CENTEN_CONFIG_VERSION 1u
#define CENTEN_CONFIG_MAGIC 0x43454E54u
#define CENTEN_CONFIG_COMMIT 0x434F4D54u

typedef struct {
    uint8_t data[CENTEN_RX_RING_SIZE];
    volatile uint16_t head, tail;
    volatile uint32_t dropped;
} CentenRxRing;

typedef struct {
    char data[CENTEN_FRAME_SIZE];
    uint16_t len, depth;
    uint8_t quoted, escaped, dropping;
    uint32_t rejected;
} CentenFramer;

typedef struct {
    char cmd[32], deviceId[32], value[32];
    uint32_t timeMin;
} CentenCommand;

typedef struct {
    uint32_t magic, version, sequence;
    char config[32], center[32];
    uint32_t crc, commit;
} CentenConfigRecord;

void centen_ring_push(CentenRxRing *ring, uint8_t byte);
int centen_ring_pop(CentenRxRing *ring, uint8_t *byte);
/* A completed frame remains in data until the next feed/idle call. */
int centen_frame_feed(CentenFramer *frame, uint8_t byte);
int centen_frame_idle(CentenFramer *frame);
int centen_text_payload_size(const uint8_t *payload, size_t wireSize, size_t capacity);
int centen_parse_command(const char *json, CentenCommand *command);
int centen_json_escape(const char *input, char *output, size_t size);
int centen_command_frame(char *output, size_t size, const char *device,
                         const char *cmd, const char *value);
int centen_parse_uint(const char *str, uint32_t min, uint32_t max,
                      uint32_t *value);
int centen_config_valid(const char *config);
int centen_center_parse(const char *str, double *lat, double *lng);
int centen_datetime(int year, int month, int day, int hour, int minute,
                     int second, uint32_t *timestamp);
uint32_t centen_crc32(const void *data, size_t size);
int centen_record_valid(const CentenConfigRecord *record);
void centen_record_prepare(CentenConfigRecord *record, const char *config,
                           const char *center, uint32_t sequence);
int centen_due(uint32_t now, uint32_t deadline);

#endif
