#ifndef NVSTORE_H
#define NVSTORE_H
#include <stdint.h>

/* Keeps all six legacy page addresses and data at offset 4.
 * The previously unused last page (0x0800FC00) is the recovery journal. */
uint8_t NvStore_Load(uint32_t page, uint32_t magic, void *data,
                     uint32_t capacity, uint8_t stringValue);
uint8_t NvStore_Save(uint32_t page, uint32_t magic, const void *data,
                     uint32_t length);
uint8_t NvStore_Recover(void);
#endif
