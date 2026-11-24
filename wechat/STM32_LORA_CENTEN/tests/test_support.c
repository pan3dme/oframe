#include "centen_support.h"
#include "main.h"
#include <string.h>

volatile uint32_t centen_test_checks;
static uint32_t receiveCalls;
static uint8_t *receiveDestination;
UART_HandleTypeDef huart3;
int main(void) { return 0; }
void SystemInit(void) { }

/* Exercise the production UART callback without memory-mapped peripherals. */
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t size)
{
    (void)uart;
    if (size != 1u) return HAL_ERROR;
    receiveDestination = data;
    receiveCalls++;
    return HAL_OK;
}
uint32_t HAL_GetTick(void) { return 1000u; }

#define CHECK(expr) do { centen_test_checks++; if (!(expr)) return __LINE__; } while (0)

int test_entry(void)
{
    static CentenRxRing ring;
    static CentenFramer frame;
    static CentenConfigRecord record, torn;
    CentenCommand command;
    uint8_t byte;
    uint32_t v, i, j;
    double lat, lng;
    char out[128], longField[32];
    const char *json = "{ \"params\": {\"cominfo\" : \"{\\\"cmd\\\":\\\"A\\\",\\\"deviceId\\\":\\\"v3-100\\\",\\\"value\\\":\\\"30,0M\\\",\\\"time\\\":\\\"120\\\"}\"}}";
    const char *stream = "config,csq,ok,20\r\nPING1200\r\n{\"a\":\"}\\\"{\"}{\"b\":{\"c\":1}}";
    const char *bad[] = {
        "{}", "{\"cominfo\":123}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"x\\\",\\\"deviceId\\\":\\\"v3-1\\\",\\\"time\\\":-1}\"}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"x\\\",\\\"deviceId\\\":\\\"v3-1\\\",\\\"time\\\":121}\"}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"x\\\",\\\"deviceId\\\":\\\"v3-1\\\",\\\"value\\\":\\\"a|b\\\"}\"}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"x\\\",\\\"cmd\\\":\\\"y\\\",\\\"deviceId\\\":\\\"v3-1\\\"}\"}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"x\\\",\\\"deviceId\\\":\\\"v3-1\\\",}\"}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"x\\\",\\\"deviceId\\\":\\\"v3-1\\\",\\\"value\\\":\\\"\\\\u0000\\\"}\"}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"abcdefghijklmnopqrstuvwxyz0123456789\\\",\\\"deviceId\\\":\\\"v3-1\\\"}\"}",
        "{\"cominfo\":\"{\\\"cmd\\\":\\\"x\\\",\\\"deviceId\\\":\\\"v3-1\\\",\\\"time\\\":01}\"}"
    };
    CHECK(centen_parse_command(json, &command));
    CHECK(strcmp(command.cmd, "A") == 0 && strcmp(command.deviceId, "v3-100") == 0);
    CHECK(strcmp(command.value, "30,0M") == 0 && command.timeMin == 120);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(!centen_parse_command(bad[i], &command));
    CHECK(centen_parse_command("{\"cominfo\":\"{\\\"cmd\\\":\\\"readinfo\\\",\\\"deviceId\\\":\\\"v3-100\\\"}\"}", &command));
    CHECK(command.timeMin == 0 && command.value[0] == 0);
    CHECK(centen_parse_command("{\"cominfo\":\"{\\\"cmd\\\":\\\"lorasw\\\",\\\"deviceId\\\":\\\"v3-100\\\",\\\"value\\\":1}\"}", &command));
    CHECK(strcmp(command.value, "1") == 0);

    for (i = 0; i < 6000; i++) {
        centen_ring_push(&ring, (uint8_t)i);
        CHECK(centen_ring_pop(&ring, &byte) && byte == (uint8_t)i);
    }
    CHECK(!centen_ring_pop(&ring, &byte));
    for (i = 0; i < CENTEN_RX_RING_SIZE + 77u; i++) centen_ring_push(&ring, (uint8_t)i);
    CHECK(ring.dropped == 78u);
    for (i = 0; i < CENTEN_RX_RING_SIZE - 1u; i++) CHECK(centen_ring_pop(&ring, &byte) && byte == (uint8_t)i);
    CHECK(!centen_ring_pop(&ring, &byte));
    centen_ring_push(&ring, 'R'); CHECK(centen_ring_pop(&ring, &byte) && byte == 'R');

    j = 0;
    for (i = 0; stream[i]; i++) if (centen_frame_feed(&frame, (uint8_t)stream[i])) j++;
    CHECK(j == 4);
    CHECK(!centen_frame_idle(&frame));
    for (i = 0; i < 4000; i++) CHECK(!centen_frame_feed(&frame, 'x'));
    CHECK(!centen_frame_feed(&frame, '\n'));
    CHECK(frame.rejected == 1);
    for (i = 0; "PING60"[i]; i++) CHECK(!centen_frame_feed(&frame, (uint8_t)"PING60"[i]));
    CHECK(centen_frame_idle(&frame) && strcmp(frame.data, "PING60") == 0);
    CHECK(!centen_frame_feed(&frame, '{'));
    CHECK(!centen_frame_idle(&frame));
    CHECK(frame.rejected == 2);
    CHECK(!centen_frame_feed(&frame, 0));
    CHECK(!centen_frame_feed(&frame, '\n'));
    CHECK(frame.rejected == 3);

    CHECK(centen_json_escape("a\"b\\c\n", out, sizeof(out)) > 0);
    CHECK(strcmp(out, "a\\\"b\\\\c\\u000a") == 0);
    out[2] = 'Z'; CHECK(centen_json_escape("\"", out, 2) == -1 && out[2] == 'Z');
    CHECK(centen_json_escape("", out, 0) == -1);
    memset(longField, 'a', 31); longField[31] = 0;
    CHECK(centen_command_frame(out, 64, longField, longField, longField) == -1);
    CHECK(centen_command_frame(out, 100, longField, longField, longField) == 97);
    CHECK(strlen(out) == 97 && out[97] == 0);
    CHECK(centen_command_frame(out, sizeof(out), "v3-1", "A", "x|y") == -1);

    CHECK(centen_text_payload_size((const uint8_t *)"2|v3-1|123|85|", sizeof("2|v3-1|123|85|"), 96) == 14);
    CHECK(centen_text_payload_size((const uint8_t *)"2|v3-1|123|85|", strlen("2|v3-1|123|85|"), 96) == 14);
    memset(out, 'a', 96); out[96] = 0;
    CHECK(centen_text_payload_size((uint8_t *)out, 96, 96) == 96);
    CHECK(centen_text_payload_size((uint8_t *)out, 97, 96) == 96);
    CHECK(centen_text_payload_size((uint8_t *)out, 36, 36) == 36);
    out[35] = 0;
    CHECK(centen_text_payload_size((uint8_t *)out, 97, 96) == -1);
    CHECK(centen_text_payload_size((const uint8_t *)"", 1, 96) == -1);
    CHECK(centen_text_payload_size((const uint8_t *)"", 0, 96) == -1);

    CHECK(centen_parse_uint("4294967295", 0, 0xffffffffu, &v) && v == 0xffffffffu);
    CHECK(!centen_parse_uint("4294967296", 0, 0xffffffffu, &v));
    CHECK(!centen_parse_uint("-1", 0, 120, &v));
    CHECK(!centen_parse_uint("", 0, 120, &v));
    CHECK(!centen_parse_uint("30junk", 0, 120, &v));
    CHECK(centen_config_valid("30,0M") && centen_config_valid("5,00") && centen_config_valid("60,38"));
    CHECK(!centen_config_valid("4,0M") && !centen_config_valid("61,0M") && !centen_config_valid("30,zz"));
    CHECK(!centen_config_valid("30,0Mextra"));
    CHECK(centen_center_parse("-90.00000,180.00000", &lat, &lng) && lat == -90.0 && lng == 180.0);
    CHECK(centen_center_parse("30.12345,109.39087", &lat, &lng) && lat > 30.123449 && lat < 30.123451);
    CHECK(!centen_center_parse("nan,1", &lat, &lng));
    CHECK(!centen_center_parse("90.00001,180", &lat, &lng));
    CHECK(!centen_center_parse("1,2junk", &lat, &lng));
    CHECK(!centen_center_parse("1,", &lat, &lng));

    CHECK(centen_datetime(1970, 1, 1, 0, 0, 0, &v) && v == 0);
    CHECK(centen_datetime(2024, 2, 29, 0, 0, 0, &v) && v == 1709164800u);
    CHECK(!centen_datetime(2023, 2, 29, 0, 0, 0, &v));
    CHECK(!centen_datetime(2026, 0, 1, 0, 0, 0, &v));
    CHECK(!centen_datetime(2026, 13, 1, 0, 0, 0, &v));
    CHECK(!centen_datetime(2026, 1, 1, 24, 0, 0, &v));
    CHECK(centen_datetime(2106, 2, 7, 6, 28, 15, &v) && v == 0xffffffffu);
    CHECK(!centen_datetime(2106, 2, 7, 6, 28, 16, &v));
    CHECK(centen_due(5, 0xfffffff0u));
    CHECK(!centen_due(0xfffffff0u, 5));
    CHECK(centen_due(0, 0));

    CHECK(centen_crc32("123456789", 9) == 0xcbf43926u);
    centen_record_prepare(&record, "30,0M", "30.12345,109.39087", 0xffffffffu);
    CHECK(centen_record_valid(&record));
    memset(&torn, 0xff, sizeof(torn));
    for (i = 0; i < sizeof(record); i += 2) {
        CHECK(!centen_record_valid(&torn));
        memcpy((uint8_t *)&torn + i, (uint8_t *)&record + i, 2);
    }
    CHECK(centen_record_valid(&torn));
    for (i = 0; i < sizeof(record); i++) {
        memcpy(&torn, &record, sizeof(torn));
        ((uint8_t *)&torn)[i] ^= 1;
        CHECK(!centen_record_valid(&torn));
    }
    centen_record_prepare(&record, "30,zz", "", 1);
    CHECK(!centen_record_valid(&record));

    /* The old callback stopped at byte 256. The production callback must
     * re-arm even when its ring is full and dropping input. */
    huart3.Instance = USART3;
    for (i = 0; i < 1400u; i++) {
        if (receiveDestination) *receiveDestination = (uint8_t)i;
        HAL_UART_RxCpltCallback(&huart3);
        CHECK(receiveCalls == i + 1u && receiveDestination != NULL);
    }
    return 0;
}
