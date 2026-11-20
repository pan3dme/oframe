#include "nvstore.h"
#include "reliability.h"
#include <string.h>

#define JOURNAL_PAGE 0x0800FC00u
#define FIRST_PAGE   0x0800E400u
#define LAST_PAGE    0x0800F800u
#define PAGE_SIZE    1024u
#define FOOTER       128u
#define RECORD_TAG   0x32564E31u
#define COMMITTED    0xA55Au
#define MAX_DATA     64u

typedef struct {
  uint32_t tag, page, magic, length, crc;
  uint8_t data[MAX_DATA];
  uint16_t commit, reserved;
} Record;

static uint8_t valid_page(uint32_t page, uint32_t magic)
{
  static const uint32_t magics[] = {
    0xA5A55A5Fu, 0xA5A55A5Eu, 0xA5A55A5Du,
    0xA5A55A5Cu, 0x5A5AA5A6u, 0xA5A55A5Bu
  };
  if (page < FIRST_PAGE || page > LAST_PAGE || (page & (PAGE_SIZE - 1u)))
    return 0u;
  return magics[(page - FIRST_PAGE) / PAGE_SIZE] == magic;
}

static uint32_t checksum(const Record *r)
{
  /* CRC includes identity, length, all initialized payload/padding bytes. */
  const uint8_t *p = (const uint8_t *)r;
  uint32_t crc = 0xFFFFFFFFu, i, j;
  for (i = 0u; i < 20u + MAX_DATA; ++i) {
    uint8_t b = (i >= 16u && i < 20u) ? 0u : p[i];
    crc ^= b;
    for (j = 0u; j < 8u; ++j)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
  }
  return ~crc;
}

static uint8_t valid_record(const Record *r)
{
  return r->tag == RECORD_TAG && r->commit == COMMITTED &&
         valid_page(r->page, r->magic) && r->length > 0u &&
         r->length <= MAX_DATA && r->crc == checksum(r);
}

static uint8_t canonical_matches(const Record *r)
{
  const Record *footer = (const Record *)(r->page + FOOTER);
  return valid_record(footer) && footer->crc == r->crc &&
         *(const uint32_t *)r->page == r->magic &&
         memcmp((const void *)(r->page + 4u), r->data, r->length) == 0;
}

static uint8_t program(uint32_t addr, const void *data, uint32_t length)
{
  const uint8_t *p = (const uint8_t *)data;
  uint32_t i;
  for (i = 0u; i < length; i += 2u) {
    uint16_t value = p[i];
    if (i + 1u < length) value |= (uint16_t)p[i + 1u] << 8;
    else value |= 0xFF00u;
    Safety_Feed();
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, value) != HAL_OK)
      return 0u;
    if (*(const uint16_t *)(addr + i) != value) return 0u;
  }
  return 1u;
}

static uint8_t erase(uint32_t page)
{
  FLASH_EraseInitTypeDef init;
  uint32_t error = 0u;
  init.TypeErase = FLASH_TYPEERASE_PAGES;
  init.PageAddress = page;
  init.NbPages = 1u;
  Safety_Feed();
  return HAL_FLASHEx_Erase(&init, &error) == HAL_OK;
}

static uint8_t restore(const Record *r)
{
  uint16_t commit = COMMITTED;
  if (canonical_matches(r)) return 1u;
  if (!erase(r->page) ||
      !program(r->page + 4u, r->data, r->length) ||
      !program(r->page + FOOTER, r, 84u) ||
      !program(r->page, &r->magic, sizeof(r->magic)) ||
      !program(r->page + FOOTER + 84u, &commit, sizeof(commit)))
    return 0u;
  return canonical_matches(r);
}

uint8_t NvStore_Recover(void)
{
  const Record *journal = (const Record *)JOURNAL_PAGE;
  uint8_t ok;
  if (!valid_record(journal) || canonical_matches(journal)) return 1u;
  Safety_Begin(STAGE_FLASH, 5000u);
  if (HAL_FLASH_Unlock() != HAL_OK) return 0u;
  ok = restore(journal);
  HAL_FLASH_Lock();
  Safety_Begin(STAGE_MAIN, 15000u);
  return ok;
}

uint8_t NvStore_Load(uint32_t page, uint32_t magic, void *data,
                     uint32_t capacity, uint8_t stringValue)
{
  const Record *journal = (const Record *)JOURNAL_PAGE;
  const Record *footer = (const Record *)(page + FOOTER);
  const uint8_t *source;
  uint32_t length, i;
  if (!valid_page(page, magic) || !capacity || capacity > MAX_DATA) return 0u;
  if (valid_record(journal) && journal->page == page) {
    source = journal->data;
    length = journal->length;
  } else if (valid_record(footer) && footer->page == page &&
             footer->magic == magic && canonical_matches(footer)) {
    source = footer->data;
    length = footer->length;
  } else {
    /* Only a completely erased footer is eligible for legacy migration. */
    const uint8_t *p = (const uint8_t *)footer;
    for (i = 0u; i < sizeof(Record); ++i)
      if (p[i] != 0xFFu) return 0u;
    if (*(const uint32_t *)page != magic) return 0u;
    source = (const uint8_t *)(page + 4u);
    length = capacity;
    if (stringValue) {
      for (i = 0u; i < capacity && source[i]; ++i) { }
      if (i == capacity) return 0u;
      length = i + 1u;
    }
  }
  if (length > capacity || (!stringValue && length != capacity)) return 0u;
  if (stringValue && source[length - 1u] != 0u) return 0u;
  memcpy(data, source, length);
  return 1u;
}

uint8_t NvStore_Save(uint32_t page, uint32_t magic, const void *data,
                     uint32_t length)
{
  Record r;
  uint8_t existing[MAX_DATA];
  uint16_t commit = COMMITTED;
  uint8_t ok;
  if (!valid_page(page, magic) || !length || length > MAX_DATA) return 0u;
  /* Finish the previous transaction before ever erasing its journal. */
  if (!NvStore_Recover()) return 0u;
  if (NvStore_Load(page, magic, existing, length, 0u) &&
      memcmp(existing, data, length) == 0) return 1u;
  memset(&r, 0xFF, sizeof(r));
  r.tag = RECORD_TAG;
  r.page = page;
  r.magic = magic;
  r.length = length;
  memcpy(r.data, data, length);
  r.crc = checksum(&r);
  Safety_Begin(STAGE_FLASH, 5000u);
  if (HAL_FLASH_Unlock() != HAL_OK) return 0u;
  /* Journal commit is the point where the new value becomes durable. */
  ok = erase(JOURNAL_PAGE) && program(JOURNAL_PAGE, &r, 84u) &&
       program(JOURNAL_PAGE + 84u, &commit, sizeof(commit));
  if (ok) ok = valid_record((const Record *)JOURNAL_PAGE) &&
               restore((const Record *)JOURNAL_PAGE);
  HAL_FLASH_Lock();
  Safety_Begin(STAGE_MAIN, 15000u);
  return ok;
}
