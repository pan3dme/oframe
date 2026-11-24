#include "centen_support.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if defined(__CC_ARM)
#define CENTEN_BARRIER() __schedule_barrier()
#elif defined(__GNUC__)
#define CENTEN_BARRIER() __asm__ volatile ("" ::: "memory")
#else
#define CENTEN_BARRIER() ((void)0)
#endif

void centen_ring_push(CentenRxRing *ring, uint8_t byte)
{
    uint16_t next = (uint16_t)((ring->head + 1u) % CENTEN_RX_RING_SIZE);
    if (next == ring->tail) { ring->dropped++; return; }
    ring->data[ring->head] = byte;
    CENTEN_BARRIER();
    ring->head = next;
}

int centen_ring_pop(CentenRxRing *ring, uint8_t *byte)
{
    if (ring->tail == ring->head) return 0;
    CENTEN_BARRIER();
    *byte = ring->data[ring->tail];
    CENTEN_BARRIER();
    ring->tail = (uint16_t)((ring->tail + 1u) % CENTEN_RX_RING_SIZE);
    return 1;
}

static void frame_reset(CentenFramer *frame)
{
    frame->len = frame->depth = 0;
    frame->quoted = frame->escaped = frame->dropping = 0;
}

int centen_frame_feed(CentenFramer *frame, uint8_t byte)
{
    /* depth == 0xffff means the preceding call returned a complete frame. */
    if (frame->depth == 0xffffu) frame_reset(frame);
    if (!frame->len && !frame->dropping &&
        (byte == ' ' || byte == '\r' || byte == '\n' || byte == '\t')) return 0;
    if (byte == 0) {
        frame->rejected++;
        frame_reset(frame);
        frame->dropping = 1;
        return 0;
    }
    if (!frame->depth && (byte == '\r' || byte == '\n')) {
        if (frame->dropping) { frame_reset(frame); return 0; }
        if (!frame->len) return 0;
        frame->data[frame->len] = 0;
        frame->depth = 0xffffu;
        return 1;
    }
    if (!frame->dropping) {
        if (frame->len + 1u >= sizeof(frame->data)) {
            frame->dropping = 1;
            frame->rejected++;
        } else frame->data[frame->len++] = (char)byte;
    }
    /* JSON may be concatenated without CR/LF; braces inside strings are data. */
    if (frame->depth || (frame->len == 1u && byte == '{')) {
        if (frame->quoted) {
            if (frame->escaped) frame->escaped = 0;
            else if (byte == '\\') frame->escaped = 1;
            else if (byte == '"') frame->quoted = 0;
        } else if (byte == '"') frame->quoted = 1;
        else if (byte == '{') frame->depth++;
        else if (byte == '}' && frame->depth) {
            frame->depth--;
            if (!frame->depth) {
                if (frame->dropping) { frame_reset(frame); return 0; }
                frame->data[frame->len] = 0;
                frame->depth = 0xffffu;
                return 1;
            }
        }
    }
    return 0;
}

int centen_frame_idle(CentenFramer *frame)
{
    if (frame->depth == 0xffffu) { frame_reset(frame); return 0; }
    if (frame->dropping || frame->depth || frame->quoted) {
        if (!frame->dropping) frame->rejected++;
        frame_reset(frame);
        return 0;
    }
    if (!frame->len) return 0;
    frame->data[frame->len] = 0;
    frame->depth = 0xffffu;
    return 1;
}

int centen_text_payload_size(const uint8_t *payload, size_t wireSize, size_t capacity)
{
    if (!wireSize) return -1;
    /* ONLY_SEND includes strlen(msg)+1 on the air. Other peers may omit NUL. */
    if (payload[wireSize - 1u] == 0) wireSize--;
    if (!wireSize || wireSize > capacity || memchr(payload, 0, wireSize)) return -1;
    return (int)wireSize;
}

/* Bounded JSON reader. Unknown values are validated and skipped, not searched
 * as text. Only the outer cominfo search descends into nested objects. */
typedef struct {
    const char *p, *wanted;
    char *out;
    size_t size;
    int recursive, found, type;
} JsonReader;

static void json_space(JsonReader *r)
{
    while (*r->p == ' ' || *r->p == '\t' || *r->p == '\r' || *r->p == '\n') r->p++;
}

static int json_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int json_string(JsonReader *r, char *out, size_t size)
{
    size_t n = 0;
    if (*r->p++ != '"') return 0;
    while (*r->p && *r->p != '"') {
        unsigned char c = (unsigned char)*r->p++;
        if (c < 0x20u) return 0;
        if (c == '\\') {
            c = (unsigned char)*r->p++;
            switch (c) {
            case '"': case '\\': case '/': break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                unsigned int v = 0;
                int i, h;
                for (i = 0; i < 4; i++) {
                    h = json_hex(*r->p);
                    if (h < 0) return 0;
                    v = (v << 4) | (unsigned int)h;
                    r->p++;
                }
                /* Protocol identifiers/values are ASCII. Reject NUL and
                 * unsupported Unicode rather than silently truncating. */
                if (out && (v == 0 || v > 127u)) return 0;
                c = (unsigned char)v;
                break;
            }
            default: return 0;
            }
        }
        if (out) {
            if (n + 1u >= size) return 0;
            out[n] = (char)c;
        }
        n++;
    }
    if (*r->p != '"') return 0;
    r->p++;
    if (out) out[n] = 0;
    return 1;
}

static int json_value(JsonReader *r, unsigned int depth, int capture);

static int json_object(JsonReader *r, unsigned int depth)
{
    char key[32];
    r->p++;
    json_space(r);
    if (*r->p == '}') { r->p++; return 1; }
    do {
        int match;
        /* Oversized unknown keys are rejected consistently with other fields. */
        if (!json_string(r, key, sizeof(key))) return 0;
        json_space(r);
        if (*r->p++ != ':') return 0;
        json_space(r);
        match = (depth == 1u || r->recursive) && strcmp(key, r->wanted) == 0;
        if (match && r->found) return 0;
        if (match) r->found = 1;
        if (!json_value(r, depth, match)) return 0;
        json_space(r);
        if (*r->p == '}') { r->p++; return 1; }
        if (*r->p++ != ',') return 0;
        json_space(r);
    } while (*r->p);
    return 0;
}

static int json_value(JsonReader *r, unsigned int depth, int capture)
{
    const char *start;
    size_t n;
    if (depth > 8u) return 0;
    if (*r->p == '"') {
        if (capture) r->type = 1;
        return json_string(r, capture ? r->out : NULL, capture ? r->size : 0);
    }
    if (*r->p == '{') {
        if (capture) return 0;
        return json_object(r, depth + 1u);
    }
    if (*r->p == '[') {
        if (capture) return 0;
        r->p++;
        json_space(r);
        if (*r->p == ']') { r->p++; return 1; }
        do {
            if (!json_value(r, depth + 1u, 0)) return 0;
            json_space(r);
            if (*r->p == ']') { r->p++; return 1; }
            if (*r->p++ != ',') return 0;
            json_space(r);
        } while (*r->p);
        return 0;
    }
    start = r->p;
    if (strncmp(r->p, "true", 4) == 0) r->p += 4;
    else if (strncmp(r->p, "false", 5) == 0) r->p += 5;
    else if (strncmp(r->p, "null", 4) == 0) r->p += 4;
    else {
        if (*r->p == '-') r->p++;
        if (*r->p == '0') r->p++;
        else {
            if (*r->p < '1' || *r->p > '9') return 0;
            do { r->p++; } while (*r->p >= '0' && *r->p <= '9');
        }
        if (*r->p == '.') {
            r->p++;
            if (*r->p < '0' || *r->p > '9') return 0;
            do { r->p++; } while (*r->p >= '0' && *r->p <= '9');
        }
        if (*r->p == 'e' || *r->p == 'E') {
            r->p++;
            if (*r->p == '+' || *r->p == '-') r->p++;
            if (*r->p < '0' || *r->p > '9') return 0;
            do { r->p++; } while (*r->p >= '0' && *r->p <= '9');
        }
    }
    n = (size_t)(r->p - start);
    if (capture) {
        if (n + 1u > r->size) return 0;
        memcpy(r->out, start, n);
        r->out[n] = 0;
        r->type = 2;
    }
    return 1;
}

static int json_field(const char *json, const char *key, char *out,
                       size_t size, int recursive)
{
    JsonReader reader;
    memset(&reader, 0, sizeof(reader));
    reader.p = json; reader.wanted = key; reader.out = out;
    reader.size = size; reader.recursive = recursive;
    out[0] = 0;
    json_space(&reader);
    if (*reader.p != '{' || !json_object(&reader, 1u)) return -1;
    json_space(&reader);
    if (*reader.p) return -1;
    return reader.found ? reader.type : 0;
}

int centen_parse_uint(const char *str, uint32_t min, uint32_t max, uint32_t *value)
{
    uint32_t v = 0;
    if (!str || !*str) return 0;
    do {
        uint32_t digit;
        if (*str < '0' || *str > '9') return 0;
        digit = (uint32_t)(*str++ - '0');
        if (v > max / 10u || (v == max / 10u && digit > max % 10u)) return 0;
        v = v * 10u + digit;
    } while (*str);
    if (v < min || v > max) return 0;
    *value = v;
    return 1;
}

static int frame_field_valid(const char *str, int identifier)
{
    const unsigned char *p = (const unsigned char *)str;
    if (identifier && !*p) return 0;
    while (*p) {
        if (*p < 0x20u || *p > 0x7eu || *p == '|') return 0;
        if (identifier && !((*p >= 'a' && *p <= 'z') ||
            (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_')) return 0;
        p++;
    }
    return 1;
}

int centen_parse_command(const char *json, CentenCommand *command)
{
    /* Single main-loop consumer; keep the outer decoded payload off the stack. */
    static char inner[CENTEN_FRAME_SIZE];
    char time[16];
    int result;
    memset(command, 0, sizeof(*command));
    if (json_field(json, "cominfo", inner, sizeof(inner), 1) != 1) return 0;
    if (json_field(inner, "cmd", command->cmd, sizeof(command->cmd), 0) != 1 ||
        json_field(inner, "deviceId", command->deviceId, sizeof(command->deviceId), 0) != 1)
        return 0;
    result = json_field(inner, "value", command->value, sizeof(command->value), 0);
    if (result < 0) return 0;
    result = json_field(inner, "time", time, sizeof(time), 0);
    if (result < 0 || (result && !centen_parse_uint(time, 0, 120, &command->timeMin))) return 0;
    return frame_field_valid(command->cmd, 1) &&
           frame_field_valid(command->deviceId, 1) && frame_field_valid(command->value, 0);
}

int centen_json_escape(const char *input, char *output, size_t size)
{
    static const char hex[] = "0123456789abcdef";
    size_t n = 0;
    while (*input) {
        unsigned char c = (unsigned char)*input++;
        size_t need = c < 0x20u ? 6u : (c == '"' || c == '\\' ? 2u : 1u);
        if (n + need >= size) return -1;
        if (need == 6u) {
            output[n++] = '\\'; output[n++] = 'u';
            output[n++] = '0'; output[n++] = '0';
            output[n++] = hex[c >> 4]; output[n++] = hex[c & 15u];
        } else {
            if (need == 2u) output[n++] = '\\';
            output[n++] = (char)c;
        }
    }
    if (!size) return -1;
    output[n] = 0;
    return (int)n;
}

int centen_command_frame(char *output, size_t size, const char *device,
                         const char *cmd, const char *value)
{
    int len;
    if (!frame_field_valid(device, 1) || !frame_field_valid(cmd, 1) ||
        !frame_field_valid(value, 0)) return -1;
    len = snprintf(output, size, "9|%s|%s|%s", device, cmd, value);
    return len > 0 && (size_t)len < size ? len : -1;
}

int centen_config_valid(const char *config)
{
    const char *comma = strchr(config, ',');
    const char *dict = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const char *hi, *lo;
    char interval[4];
    uint32_t minutes;
    size_t len;
    if (!comma || strlen(comma + 1) != 2u) return 0;
    len = (size_t)(comma - config);
    if (!len || len >= sizeof(interval)) return 0;
    memcpy(interval, config, len); interval[len] = 0;
    if (!centen_parse_uint(interval, 5, 60, &minutes)) return 0;
    hi = strchr(dict, comma[1]); lo = strchr(dict, comma[2]);
    return hi && lo && (hi - dict) * 62 + (lo - dict) < 276;
}

static int parse_coordinate(const char **cursor, double *value)
{
    const char *p = *cursor;
    uint32_t whole = 0, fraction = 0, scale = 1;
    int negative = 0;
    if (*p == '-' || *p == '+') { negative = *p == '-'; p++; }
    if (*p < '0' || *p > '9') return 0;
    do {
        whole = whole * 10u + (uint32_t)(*p++ - '0');
        if (whole > 180u) return 0;
    } while (*p >= '0' && *p <= '9');
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return 0;
        do {
            if (scale >= 1000000000u) return 0;
            fraction = fraction * 10u + (uint32_t)(*p++ - '0');
            scale *= 10u;
        } while (*p >= '0' && *p <= '9');
    }
    *value = (double)whole + (double)fraction / (double)scale;
    if (negative) *value = -*value;
    *cursor = p;
    return 1;
}

int centen_center_parse(const char *str, double *lat, double *lng)
{
    if (!parse_coordinate(&str, lat) || *str++ != ',') return 0;
    if (!parse_coordinate(&str, lng) || *str) return 0;
    return *lat >= -90.0 && *lat <= 90.0 && *lng >= -180.0 && *lng <= 180.0;
}

int centen_datetime(int year, int month, int day, int hour, int minute,
                     int second, uint32_t *timestamp)
{
    static const uint8_t daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint64_t seconds;
    uint32_t days = 0;
    int y, m, limit;
    if (year < 1970 || year > 2106 || month < 1 || month > 12 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return 0;
    limit = daysInMonth[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) limit++;
    if (day < 1 || day > limit) return 0;
    for (y = 1970; y < year; y++)
        days += (uint32_t)(365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    for (m = 1; m < month; m++) {
        days += daysInMonth[m - 1];
        if (m == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) days++;
    }
    days += (uint32_t)(day - 1);
    seconds = (uint64_t)days * 86400u + (uint32_t)hour * 3600u +
              (uint32_t)minute * 60u + (uint32_t)second;
    if (seconds > 0xffffffffu) return 0;
    *timestamp = (uint32_t)seconds;
    return 1;
}

uint32_t centen_crc32(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    unsigned int i;
    while (size--) {
        crc ^= *p++;
        for (i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

int centen_record_valid(const CentenConfigRecord *record)
{
    double lat, lng;
    return record->magic == CENTEN_CONFIG_MAGIC &&
        record->version == CENTEN_CONFIG_VERSION && record->commit == CENTEN_CONFIG_COMMIT &&
        memchr(record->config, 0, sizeof(record->config)) &&
        memchr(record->center, 0, sizeof(record->center)) &&
        record->crc == centen_crc32(record, offsetof(CentenConfigRecord, crc)) &&
        centen_config_valid(record->config) &&
        (!record->center[0] || centen_center_parse(record->center, &lat, &lng));
}

void centen_record_prepare(CentenConfigRecord *record, const char *config,
                           const char *center, uint32_t sequence)
{
    memset(record, 0, sizeof(*record));
    record->magic = CENTEN_CONFIG_MAGIC;
    record->version = CENTEN_CONFIG_VERSION;
    record->sequence = sequence;
    strncpy(record->config, config, sizeof(record->config) - 1u);
    strncpy(record->center, center, sizeof(record->center) - 1u);
    record->crc = centen_crc32(record, offsetof(CentenConfigRecord, crc));
    record->commit = CENTEN_CONFIG_COMMIT;
}

int centen_due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}
