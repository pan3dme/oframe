// Byte-preserving edit: existing main.c contains mixed legacy encodings.
const fs = require('fs');
let s = fs.readFileSync('Core/Src/main.c').toString('latin1');
function replace(a, b) {
  if (!s.includes(a)) throw Error('Missing edit anchor: ' + a.slice(0, 100));
  s = s.replace(a, b);
}
function fn(name, code) {
  const re = new RegExp('^(?:static )?(?:void|uint8_t|uint16_t|uint32_t) '+name+'\\([^;]*?\\)\\r?\\n\\{[\\s\\S]*?^\\}', 'm');
  if (!re.test(s)) throw Error('Missing function: '+name);
  s = s.replace(re, code.trim().replace(/\n/g, '\r\n'));
}
replace('#include "radio.h"', '#include "radio.h"\r\n#include "reliability.h"\r\n#include "nvstore.h"');
s=s.replace(/^static uint32_t lastLoRaRxTs = 0;[\s\S]*?\*\//m, `/* Link health uses monotonic RTC time, independent of wall-clock sync. */
static uint32_t linkLastRtc, linkSilentSec;
static uint8_t linkPollReady, linkWasEnabled, linkRecoveryStep;
static uint8_t clockFallback;
`);
replace('static void linkSilenceCheck(void);', `static void linkSilenceCheck(void);
static void linkNoteValid(void);
static uint32_t rtc_read_counter(void);
static uint8_t configTimeFieldValid(const char *f);
static void getConfigFieldStrFrom(const char *src, uint8_t idx, char *out, uint8_t outLen);
static void nvSaveOrReset(uint32_t page, uint32_t magic, const void *data, uint32_t length);`);
fn('loadDeviceNameFromFlash', `static void loadDeviceNameFromFlash(void)
{
  char value[sizeof(deviceName)];
  if (NvStore_Load(DEVNAME_FLASH_ADDR, DEVNAME_MAGIC, value, sizeof(value), 1u) &&
      value[0] && strlen(value) <= 5u)
    strcpy(deviceName, value);
}`);
fn('saveDeviceNameToFlash', `static void saveDeviceNameToFlash(const char *name)
{
  nvSaveOrReset(DEVNAME_FLASH_ADDR, DEVNAME_MAGIC, name, strlen(name) + 1u);
}`);
fn('loadConfigFromFlash', `static void loadConfigFromFlash(void)
{
  char value[sizeof(configStr)], wf[8], gf[8];
  uint32_t minutes;
  if (!NvStore_Load(CONFIG_FLASH_ADDR, CONFIG_MAGIC, value, sizeof(value), 1u)) return;
  minutes = (uint32_t)atoi(value);
  getConfigFieldStrFrom(value, 1u, wf, sizeof(wf));
  getConfigFieldStrFrom(value, 2u, gf, sizeof(gf));
  if (minutes >= 1u && minutes <= 60u && configTimeFieldValid(wf) &&
      configTimeFieldValid(gf)) strcpy(configStr, value);
}`);
fn('saveConfigToFlash', `static void saveConfigToFlash(const char *str)
{
  nvSaveOrReset(CONFIG_FLASH_ADDR, CONFIG_MAGIC, str, strlen(str) + 1u);
}`);
fn('loadDeviceCountFromFlash', `static void loadDeviceCountFromFlash(void)
{
  uint32_t value;
  if (NvStore_Load(DEVCNT_FLASH_ADDR, DEVCNT_MAGIC, &value, sizeof(value), 0u) &&
      value >= 1u && value <= 60u) deviceCount = (uint8_t)value;
}`);
fn('saveDeviceCountToFlash', `static void saveDeviceCountToFlash(uint8_t count)
{
  uint32_t value = count;
  nvSaveOrReset(DEVCNT_FLASH_ADDR, DEVCNT_MAGIC, &value, sizeof(value));
}`);
fn('loadTxPowerFromFlash', `static void loadTxPowerFromFlash(void)
{
  uint32_t value;
  if (NvStore_Load(TXPWR_FLASH_ADDR, TXPWR_MAGIC, &value, sizeof(value), 0u) &&
      value >= 10u && value <= 20u) currentTxPower = (uint8_t)value;
}`);
fn('saveTxPowerToFlash', `static void saveTxPowerToFlash(uint8_t power)
{
  uint32_t value = power;
  nvSaveOrReset(TXPWR_FLASH_ADDR, TXPWR_MAGIC, &value, sizeof(value));
}`);
fn('loadSearchTimeFromFlash', `static void loadSearchTimeFromFlash(void)
{
  uint32_t value;
  if (NvStore_Load(SRCHTM_FLASH_ADDR, SRCHTM_MAGIC, &value, sizeof(value), 0u) &&
      value >= GPS_SEARCH_MIN_MIN * 60000u && value <= GPS_SEARCH_MAX_MIN * 60000u)
    gpsSearchMaxMs = value;
}`);
fn('saveSearchTimeToFlash', `static void saveSearchTimeToFlash(uint32_t ms)
{
  nvSaveOrReset(SRCHTM_FLASH_ADDR, SRCHTM_MAGIC, &ms, sizeof(ms));
}`);
fn('flashReadString', `static void nvSaveOrReset(uint32_t page, uint32_t magic,
                          const void *data, uint32_t length)
{
  if (!NvStore_Save(page, magic, data, length)) Safety_Reset(RESET_FLASH);
  printf("[FLASH] committed\\r\\n");
}`);
fn('flashWriteString', `/* String writes are handled by the journal, including the final NUL. */`);
fn('loadCenterFromFlash', `static void loadCenterFromFlash(void)
{
  char value[sizeof(centerStr)];
  int32_t la, lo;
  const char *comma;
  if (!NvStore_Load(CENTER_FLASH_ADDR, CENTER_MAGIC, value, sizeof(value), 1u)) return;
  comma = strchr(value, ',');
  if (comma && parseDeg1e5(value, &la) && parseDeg1e5(comma + 1, &lo) &&
      centerCoordValid(la, lo)) {
    strcpy(centerStr, value);
    centerLat1e5 = la;
    centerLng1e5 = lo;
    centerValid = 1u;
  }
}`);
fn('saveCenterToFlash', `static void saveCenterToFlash(const char *str)
{
  nvSaveOrReset(CENTER_FLASH_ADDR, CENTER_MAGIC, str, strlen(str) + 1u);
}`);
fn('loraInitFirstTime', `static void loraInitFirstTime(void)
{
  uint8_t tries;
  Safety_Begin(STAGE_RADIO, 10000u);
  for (tries = 0u; tries < 3u; ++tries) {
    wdg_feed();
    HAL_GPIO_WritePin(LORA_CSN_GPIO_Port, LORA_CSN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(20u);
    HAL_GPIO_WritePin(LORA_RST_GPIO_Port, LORA_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100u);
    if (rf_init() == RF_OK && rf_set_default_para() == RF_OK &&
        rf_set_tx_power(currentTxPower) == RF_OK) break;
    HAL_Delay(200u);
  }
  if (tries == 3u) Safety_Reset(RESET_RADIO);
  loraInitialized = 1u;
  Safety_Begin(STAGE_MAIN, 15000u);
  printf("[LoRa] %dM SF%d PWR=%d\\r\\n",
         DEFAULT_FREQ / 1000000, DEFAULT_SF, currentTxPower);
}`);
fn('loraTx', `static void loraTx(const char *msg)
{
  uint32_t tx_time = 1000u, start = HAL_GetTick();
  Safety_Begin(STAGE_TX, 6000u);
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  if (rf_single_tx_data((uint8_t *)msg, strlen(msg) + 1u, &tx_time) != RF_OK)
    Safety_Reset(RESET_TX);
  while (rf_get_transmit_flag() == RADIO_FLAG_IDLE) {
    if ((uint32_t)(HAL_GetTick() - start) >= 5000u) Safety_Reset(RESET_TX);
    wdg_feed();
  }
  rf_set_transmit_flag(RADIO_FLAG_IDLE);
  Safety_Begin(STAGE_MAIN, 15000u);
}`);
replace('      gpsTask();\r\n      HAL_Delay(100);', `      gpsTask();
      wdg_feed();
      HAL_Delay(100);`);
replace('    while ((HAL_GetTick() - slotStart) <', `    Safety_Begin(STAGE_TRACK_WAIT, (uint32_t)GPS_CONT_INTERVAL_SEC * 1000u + 2000u);
    while ((HAL_GetTick() - slotStart) <`);
replace('  lastLoRaRxTs = getCurrentTimestamp();', '  linkLastRtc = rtc_read_counter();');
// The parser used to treat even invalid payloads as a healthy link.
replace('  lastLoRaRxTs = getCurrentTimestamp();', '  /* Health is refreshed only after protocol/target validation below. */');
fn('wdg_init', `static void wdg_init(void) { Safety_Init(); }`);
fn('wdg_feed', `static void wdg_feed(void) { Safety_Feed(); }`);
fn('linkSilenceCheck', `static void linkNoteValid(void)
{
  linkSilentSec = 0u;
  linkRecoveryStep = 0u;
  linkLastRtc = rtc_read_counter();
  Safety_MarkLinkHealthy();
}

/* Count only the overlap with the configured work window, including sleep. */
static uint32_t workElapsed(uint32_t endTs, uint32_t elapsed)
{
  uint8_t sh, eh;
  uint32_t beginTs, total = 0u;
  if (!timeSynced) return elapsed;
  if (!timeIdxToRange(getConfigWorkStartIdx(), &sh, &eh)) return 0u;
  if (eh == TIME_HOUR_END) eh = 24u;
  beginTs = endTs - elapsed;
  while (elapsed) {
    uint32_t tod = beginTs % 86400u;
    uint32_t part = 86400u - tod;
    uint32_t a, b;
    if (part > elapsed) part = elapsed;
    a = (tod > (uint32_t)sh * 3600u) ? tod : (uint32_t)sh * 3600u;
    b = (tod + part < (uint32_t)eh * 3600u) ? tod + part : (uint32_t)eh * 3600u;
    if (b > a) total += b - a;
    beginTs += part;
    elapsed -= part;
  }
  return total;
}

static void linkSilenceCheck(void)
{
  uint32_t now = rtc_read_counter(), elapsed;
  uint8_t enabled = !gpsContTotalMin && cachedBatteryPercent >= BATTERY_SKIP_PCT;
  if (!linkPollReady) {
    linkPollReady = 1u;
    linkLastRtc = now;
    linkWasEnabled = enabled;
    return;
  }
  elapsed = now - linkLastRtc;
  if (!elapsed) return;
  linkLastRtc = now;
  if (elapsed > 86400u) Safety_Reset(RESET_RTC);
  if (enabled && linkWasEnabled)
    linkSilentSec += workElapsed(getCurrentTimestamp(), elapsed);
  linkWasEnabled = enabled;
  if (linkSilentSec < LINK_SILENCE_REBOOT_SEC) return;
  linkSilentSec = 0u;
  if (!linkRecoveryStep) {
    /* First recover just the radio; retry at the existing scheduled slot. */
    linkRecoveryStep = 1u;
    if (loraInitialized) rf_deepsleep();
    loraInitialized = 0u;
    printf("[LINK] silence: radio reinit next slot\\r\\n");
  } else {
    Safety_Reset(RESET_LINK);
  }
}`);
replace('#define WDG_SEG_SEC 5u', '#define WDG_SEG_SEC 3u');
fn('rcc_wait_flag', `static uint8_t rcc_wait_flag(volatile uint32_t *reg, uint32_t mask, uint32_t ms)
{
  uint32_t start = HAL_GetTick();
  while (!(*reg & mask)) {
    if ((uint32_t)(HAL_GetTick() - start) >= ms) return 0u;
    wdg_feed();
  }
  return 1u;
}`);
fn('rtc_wait_rtoff', `static void rtc_wait_rtoff(void)
{
  uint32_t guard = 500000u;
  while (!(RTC->CRL & RTC_CRL_RTOFF) && --guard) { }
  if (!guard) Safety_Reset(RESET_RTC);
}`);
fn('rtc_wait_rsf', `static void rtc_wait_rsf(void)
{
  uint32_t guard = 500000u;
  while (!(RTC->CRL & RTC_CRL_RSF) && --guard) { }
  if (!guard) Safety_Reset(RESET_RTC);
}`);
fn('rtc_read_counter', `static uint32_t rtc_read_counter(void)
{
  uint32_t high, low, high2;
  rtc_wait_rtoff();
  RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
  (void)RTC->CRL;
  rtc_wait_rsf();
  high = RTC->CNTH;
  low = RTC->CNTL;
  high2 = RTC->CNTH;
  if (high != high2) low = RTC->CNTL;
  return (high2 << 16) | low;
}`);
fn('rtc_ensure_clock_running', `static void rtc_ensure_clock_running(void)
{
  if ((RCC->BDCR & RCC_BDCR_RTCSEL) == RCC_BDCR_RTCSEL_LSE) {
    if (!rcc_wait_flag(&RCC->BDCR, RCC_BDCR_LSERDY, 500u)) Safety_Reset(RESET_RTC);
  } else {
    RCC->CSR |= RCC_CSR_LSION;
    if (!rcc_wait_flag(&RCC->CSR, RCC_CSR_LSIRDY, 500u)) Safety_Reset(RESET_RTC);
  }
}`);
fn('rtc_init', `static void rtc_init(void)
{
  uint32_t before, start;
  Safety_Begin(STAGE_RTC, 8000u);
  /* RAM sync anchors are lost at reset; always rebuild RTC, keep diagnostics. */
  Safety_ResetBackupDomain();
  if (!Safety_UseLsi()) {
    RCC->BDCR |= RCC_BDCR_LSEON;
    if (rcc_wait_flag(&RCC->BDCR, RCC_BDCR_LSERDY, 4000u)) {
      rtcPrl = 32767u;
      RCC->BDCR |= RCC_BDCR_RTCSEL_LSE;
    }
  }
  if (!(RCC->BDCR & RCC_BDCR_RTCSEL)) {
    RCC->BDCR &= ~RCC_BDCR_LSEON;
    RCC->CSR |= RCC_CSR_LSION;
    if (!rcc_wait_flag(&RCC->CSR, RCC_CSR_LSIRDY, 500u)) Safety_Reset(RESET_RTC);
    rtcPrl = 39999u;
    RCC->BDCR |= RCC_BDCR_RTCSEL_LSI;
  }
  RCC->BDCR |= RCC_BDCR_RTCEN;
  rtc_ensure_clock_running();
  RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
  rtc_wait_rsf();
  rtc_wait_rtoff();
  RTC->CRL |= RTC_CRL_CNF;
  RTC->PRLH = (uint16_t)(rtcPrl >> 16);
  RTC->PRLL = (uint16_t)rtcPrl;
  RTC->CNTH = 0u;
  RTC->CNTL = 0u;
  RTC->CRL &= (uint16_t)~RTC_CRL_CNF;
  (void)RTC->CRL;
  rtc_wait_rtoff();
  RTC->CRH = 0u; /* Keep the board-tested ALRIE=0 WFE event path. */
  rtc_clear_alarm_flag();
  EXTI->PR = EXTI_PR_PR17;
  EXTI->RTSR |= EXTI_RTSR_TR17;
  EXTI->EMR |= EXTI_EMR_MR17;
  EXTI->IMR &= ~EXTI_IMR_MR17;
  /* An asserted RDY bit alone is insufficient: verify actual counter motion. */
  before = rtc_read_counter();
  start = HAL_GetTick();
  while (rtc_read_counter() == before) {
    if ((uint32_t)(HAL_GetTick() - start) >= 1600u) Safety_Reset(RESET_RTC);
    wdg_feed();
  }
  Safety_Begin(STAGE_MAIN, 15000u);
}`);
fn('enterStopUntil', `static void enterStopUntil(uint32_t wakeTs)
{
  uint32_t rtcNow = rtc_read_counter(), wakeRtc = wakeTs;
  uint32_t savedCtrl;
  if (timeSynced) {
    uint32_t realDelta = wakeTs - syncedTimestamp;
    uint32_t localDelta = realDelta;
    if (driftConverged && rtcDriftPerHour && realDelta < 1000000u)
      localDelta = (uint32_t)((uint64_t)realDelta *
                   (uint32_t)(3600 + rtcDriftPerHour) / 3600u);
    wakeRtc = syncedRtcSec + localDelta;
  }
  if ((int32_t)(wakeRtc - rtcNow) <= 1) return;
  savedCtrl = SysTick->CTRL;
  SysTick->CTRL = 0u;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  while ((int32_t)(wakeRtc - rtcNow) > 0) {
    uint32_t target = ((uint32_t)(wakeRtc - rtcNow) > WDG_SEG_SEC) ?
                       rtcNow + WDG_SEG_SEC : wakeRtc;
    uint32_t after;
    rtc_clear_alarm_flag();
    EXTI->PR = EXTI_PR_PR17;
    /* Drain stale events BEFORE arming, so an arriving alarm cannot be lost
     * by HAL's SEV/WFE/WFE sequence after the last deadline check. */
    __SEV();
    __WFE();
    rtc_set_alarm(target);
    Safety_PrepareStop();
    for (;;) {
      after = rtc_read_counter();
      if ((int32_t)(after - target) >= 0) break;
      MODIFY_REG(PWR->CR, PWR_CR_PDDS | PWR_CR_LPDS, PWR_CR_LPDS);
      SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
      __DSB();
      __WFE();
      __ISB();
      SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;
      /* Spurious wake: no feed; RTC must reach the armed deadline first. */
    }
    if ((uint32_t)(after - rtcNow) > WDG_SEG_SEC + 2u) Safety_Reset(RESET_RTC);
    rtcNow = after;
    rtc_clear_alarm_flag();
    EXTI->PR = EXTI_PR_PR17;
    Safety_AfterStop();
  }
  SystemCoreClockUpdate(); /* STOP wakes on HSI; make SysTick valid at 8 MHz. */
  if (HAL_InitTick(TICK_INT_PRIORITY) != HAL_OK) Safety_Reset(RESET_INIT);
  if (!(savedCtrl & SysTick_CTRL_TICKINT_Msk)) HAL_SuspendTick();
  Safety_Begin(STAGE_MAIN, 15000u);
}`);
fn('getCurrentTimestamp', `static uint32_t getCurrentTimestamp(void)
{
  uint32_t localDelta, rtcNow = rtc_read_counter();
  if (!timeSynced) return rtcNow;
  localDelta = rtcNow - syncedRtcSec;
  if (driftConverged && rtcDriftPerHour && localDelta < 1000000u)
    return syncedTimestamp + (uint32_t)((uint64_t)localDelta * 3600u /
                             (uint32_t)(3600 + rtcDriftPerHour));
  return syncedTimestamp + localDelta;
}`);
replace('  uint32_t startTick  = HAL_GetTick();', '  uint32_t startTick  = HAL_GetTick();\r\n  Safety_Begin(STAGE_GPS, timeoutMs + 3000u);');
s=s.replace('(driftSec * 3600 / (int32_t)remoteElapsedSec)', '(int32_t)((int64_t)driftSec * 3600 / (int64_t)remoteElapsedSec)');
// Refresh valid protocol health only after verification, not arbitrary RF bytes.
replace('        syncedTimestamp = recvTs;', '        linkNoteValid();\r\n        syncedTimestamp = recvTs;');
// Target validation is shared by all recognized commands.
const cmdStart=s.indexOf('static uint8_t cmdIsForMe(char *fields[])\r\n{');
const cmdEnd=s.indexOf('\r\n}',cmdStart);
if(cmdStart<0||cmdEnd<0)throw Error('Missing cmd validation');
let cmd=s.slice(cmdStart,cmdEnd);cmd=cmd.replace('  return 1;', '  linkNoteValid();\r\n  return 1;');s=s.slice(0,cmdStart)+cmd+s.slice(cmdEnd);
// Replace only RX setup/wait; keep receive/parse behavior.
replace('  rf_enter_single_timeout_rx(3000);', `  Safety_Begin(STAGE_RX, 6000u);
  rf_set_recv_flag(RADIO_FLAG_IDLE);
  if (rf_enter_single_timeout_rx(3000u) != RF_OK) Safety_Reset(RESET_RX);`);
s=s.replace(/      printf\("\[LoRa\] RX wait timeout![\s\S]*?      NVIC_SystemReset\(\);/, '      Safety_Reset(RESET_RX);');
const rxStart=s.indexOf('static void openRxWindow3000(void)\r\n{');
const rxEnd=s.indexOf('\r\n}',rxStart);
if(rxStart<0||rxEnd<0)throw Error('Missing rx function');
s=s.slice(0,rxEnd)+'\r\n  Safety_Begin(STAGE_MAIN, 15000u);'+s.slice(rxEnd);
// Early IWDG covers all subsequent initialization failures.
replace('  HAL_Init();\r\n  SystemClock_Config();', '  wdg_init();\r\n  HAL_Init();\r\n  SystemClock_Config();');
// Remove the old late-watchdog/reset-cause/backup-wipe block using ASCII anchors.
const late=s.indexOf('  /* HW watchdog first:');
if(late>=0){const stop=s.indexOf('  printf("[BOOT] step: rtc',late);s=s.slice(0,late)+`  strncpy(bootRstCause, Safety_BootCause(), sizeof(bootRstCause) - 1u);
  bootRstPending = 1u;
  printf("[BOOT] reset=%s stage=%u clock=%s\\r\\n", bootRstCause,
         (unsigned)Safety_LastStage(), clockFallback ? "HSI64" : "HSE72");
`+s.slice(stop);}
else throw Error('Missing late boot block');
replace('  loadDeviceNameFromFlash();', '  if (!NvStore_Recover()) Safety_Reset(RESET_FLASH);\r\n  loadDeviceNameFromFlash();');
replace('  nextTxTimestamp = 0;', `  {
    uint32_t backoff = Safety_BackoffSeconds();
    if (backoff) {
      printf("[BOOT] repeated fault, backoff %lus\\r\\n", (unsigned long)backoff);
      enterStopUntil(getCurrentTimestamp() + backoff);
    }
  }
  nextTxTimestamp = 0;`);
replace('    uint32_t nowTs = getCurrentTimestamp();', `    uint32_t nowTs;
    Safety_Begin(STAGE_MAIN, 15000u);
    nowTs = getCurrentTimestamp();
    linkSilenceCheck();`);
// Remove the now-duplicated link check inside the radio-active branch.
const active=s.indexOf('    if (loraInitialized) {',s.indexOf('int main(void)'));
const check=s.indexOf('      linkSilenceCheck();',active);
if(check<0)throw Error('Missing old link check');s=s.slice(0,check)+s.slice(check+'      linkSilenceCheck();'.length);
fn('SystemClock_Config', `void SystemClock_Config(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  Safety_Begin(STAGE_CLOCK, 3000u);
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI;
  osc.HSEState = RCC_HSE_ON;
  osc.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  osc.HSIState = RCC_HSI_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    /* Keep the device recoverable if the external crystal cannot start. */
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
    osc.PLL.PLLMUL = RCC_PLL_MUL16;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Safety_Reset(RESET_INIT);
    clockFallback = 1u;
  } else clockFallback = 0u;
  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) Safety_Reset(RESET_INIT);
  Safety_Begin(STAGE_MAIN, 15000u);
}`);
// ADC calibrated once at boot, explicitly powered down between measurements.
replace('  uint32_t timeout;\r\n\r\n  /* 1.', '  uint32_t timeout;\r\n  Safety_Begin(STAGE_ADC, 2000u);\r\n\r\n  /* 1.');
replace('  while ((ADC1->CR2 & ADC_CR2_RSTCAL) && --timeout);', '  while ((ADC1->CR2 & ADC_CR2_RSTCAL) && --timeout);\r\n  if (!timeout) Safety_Reset(RESET_ADC);');
replace('  while ((ADC1->CR2 & ADC_CR2_CAL) && --timeout);', '  while ((ADC1->CR2 & ADC_CR2_CAL) && --timeout);\r\n  if (!timeout) Safety_Reset(RESET_ADC);');
replace('  (void)ADC1_Read();', '  (void)ADC1_Read();\r\n  ADC1->CR2 &= ~ADC_CR2_ADON;\r\n  Safety_Begin(STAGE_MAIN, 15000u);');
replace('  while (!(ADC1->SR & ADC_SR_EOC) && --timeout);', '  while (!(ADC1->SR & ADC_SR_EOC) && --timeout);\r\n  if (!timeout) Safety_Reset(RESET_ADC);');
replace('  uint32_t adc_sum = 0;', '  uint32_t adc_sum = 0;\r\n  Safety_Begin(STAGE_ADC, 2000u);\r\n  ADC1->CR2 |= ADC_CR2_ADON;\r\n  HAL_Delay(1u);');
replace('  uint16_t adc_val = adc_sum / 8;', '  ADC1->CR2 &= ~ADC_CR2_ADON;\r\n  Safety_Begin(STAGE_MAIN, 15000u);\r\n  uint16_t adc_val = adc_sum / 8;');
fn('Error_Handler', `void Error_Handler(void)
{
  /* Works before UART and SysTick exist, and from interrupt context. */
  Safety_Reset(RESET_INIT);
}`);
// Explicit remote reboot reason is excluded from fault streak/backoff.
replace('            NVIC_SystemReset();', '            Safety_Reset(RESET_REMOTE);');
// Bound diagnostic UART output; it must not stall the application for 65 s/byte.
replace('1, 0xFFFF);', '1, 20u);');
replace('printf("[SCHED] fast boot 3 rounds\\r\\n", (unsigned)fastStartRounds);', 'printf("[SCHED] fast boot %u rounds\\r\\n", (unsigned)fastStartRounds);');
replace('printf("[DRIFT] sample=%ds/h unconverged\\r\\n", (int)driftPerHour, (int)diff);', 'printf("[DRIFT] sample=%ds/h unconverged\\r\\n", (int)driftPerHour);');
// Include the previous phase without changing the field count/type of reset frames.
replace('"%d|%s|rst:%s|%lu|",', '"%d|%s|rst:%s-%u|%lu|",');
s=s.replace(/MSG_TYPE_READINFO, deviceName, bootRstCause,\s*\(unsigned long\)tx_count\);/, 'MSG_TYPE_READINFO, deviceName, bootRstCause, (unsigned)Safety_LastStage(),\r\n            (unsigned long)tx_count);');
replace('"%d|%s|rst:%s|",', '"%d|%s|rst:%s-%u|",');
replace('MSG_TYPE_READINFO, deviceName, bootRstCause);', 'MSG_TYPE_READINFO, deviceName, bootRstCause, (unsigned)Safety_LastStage());');
fs.writeFileSync('Core/Src/main.c', Buffer.from(s.replace(/(?<!\r)\n/g,'\r\n'), 'latin1'));

let it=fs.readFileSync('Core/Src/stm32f1xx_it.c').toString('latin1');
it=it.replace('#include "main.h"','#include "main.h"\r\n#include "reliability.h"');
for(const [name,reason] of [['NMI_Handler','NMI'],['HardFault_Handler','HARDFAULT'],['MemManage_Handler','MEMFAULT'],['BusFault_Handler','BUSFAULT'],['UsageFault_Handler','USAGEFAULT']]){
  const re=new RegExp('void '+name+'\\(void\\)\\r?\\n\\{[\\s\\S]*?^\\}', 'm');
  if(!re.test(it))throw Error(name);
  it=it.replace(re,`void ${name}(void)\r\n{\r\n  Safety_Reset(RESET_${reason});\r\n}`);
}
it=it.replace('    RTC->CRL &= (uint16_t)~(RTC_CRL_SECF | RTC_CRL_ALRF);', '    if (!g) Safety_Reset(RESET_RTC);\r\n    RTC->CRL &= (uint16_t)~(RTC_CRL_SECF | RTC_CRL_ALRF);');
fs.writeFileSync('Core/Src/stm32f1xx_it.c',Buffer.from(it,'latin1'));

let project=fs.readFileSync('MDK-ARM/STM32_ONLY_SEND.uvprojx','utf8');
project=project.replaceAll('../../Common/RadioF103/inc','../Radio/inc').replaceAll('../../Common/RadioF103/src/','../Radio/src/');
project=project.replace('<FileName>stm32f1xx_it.c</FileName>', '<FileName>stm32f1xx_it.c</FileName>');
const at='          </Files>\n        </Group>\n        <Group>\n          <GroupName>Drivers/STM32F1xx_HAL_Driver</GroupName>';
const norm=project.replace(/\r\n/g,'\n');
if(!norm.includes(at))throw Error('project group');
project=norm.replace(at,['reliability','nvstore'].map(n=>`            <File>\n              <FileName>${n}.c</FileName>\n              <FileType>1</FileType>\n              <FilePath>../Core/Src/${n}.c</FilePath>\n            </File>\n`).join('')+at);
// Both the target GUI memory size and explicit scatter file reserve all config pages.
project=project.replace('<Size>0x10000</Size>','<Size>0xE400</Size>').replace('IROM(0x8000000-0x800FFFF)','IROM(0x8000000-0x800E3FF)');
fs.writeFileSync('MDK-ARM/STM32_ONLY_SEND.uvprojx',project.replace(/\n/g,'\r\n'));
let scatter=fs.readFileSync('MDK-ARM/LY-020-software/LY-020-software.sct','utf8');
scatter=scatter.replaceAll('0x00010000','0x0000E400');
fs.writeFileSync('MDK-ARM/LY-020-software/LY-020-software.sct',scatter);
let radio=fs.readFileSync('Radio/src/pan3029_rf.c').toString('latin1');
radio=radio.replace('static int packet_received','static volatile int packet_received').replace('static int packet_transmit','static volatile int packet_transmit');
fs.writeFileSync('Radio/src/pan3029_rf.c',Buffer.from(radio,'latin1'));
console.log('Applied watchdog, RTC, persistence, clock, ADC, IRQ and local-radio changes.');
