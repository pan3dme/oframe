# SEND GPIO 低功耗修改

日期：2026-11-28。版本：V2.13，APP_VERSION=24。已修改源码并生成新固件；未烧录、未测量实板电流。

## 改动

新增 `Core/Src/low_power.c` 和 `Core/Inc/low_power.h`，已加入 Keil 工程。

- 开机把本板未使用引脚设为无上下拉模拟输入：PA4/PA8/PA11/PA12/PA15，PB3/PB4/PB5/PB6/PB7/PB8/PB9/PB12/PB13/PB14，以及 PC13。BOOT1、SWD 和外部晶振引脚不在掩码内。
- GPS 关闭时，先禁止接收回调继续工作、屏蔽 USART3 中断、终止接收并关闭 UART 和外设时钟，把 PA3 RESET_N、PB10、PB11 设为模拟输入；随后拉低 PB1、关闭 PB15 控制的电源。
- GPS 上电时，RESET_N 使用无外部上拉的开漏释放，PB10 继续保持模拟输入，PB11 恢复为串口输入。PB1 WAKEUP 改为开漏，使用模块内部上拉，MCU 不再主动向 GPS 这两个控制接口输出高电平。
- USART3 初始化根据 `gpsPowerState` 决定启用接收还是继续关闭；系统时钟恢复后也遵守该状态，不会因为重新初始化串口而重新激活已关闭 GPS 的接收接口。
- GPS 接收和错误回调检查电源状态，避免过期回调在断电后重新武装接收。电源状态改为 volatile。
- 调试 USART1 改为仅发送，未使用的 PA10 为模拟输入。
- SPI 主机 MISO/PA6 单独配置为输入，PA5/PA7 为复用推挽输出。
- 整段 STOP 前保存 PA2/PA6/PA9/PA10 的配置，将其设为模拟输入，并屏蔽 EXTI2 的中断和事件。三个 RTC 秒的看门狗分段期间保持这些配置；整段等待结束后恢复，再清理挂起状态、恢复 NVIC。RTC17 事件路径不变。

保留低功耗稳压器 STOP、每三个 RTC 秒唤醒喂狗、首次十秒下载窗口、SWD、GPS 电量门槛和业务计时。当前原有的发送超时 1500ms、首条回复延迟 4 秒和射频提前 3 秒准备没有修改。

RESET_N 和 WAKEUP 的开漏释放方式依据 Quectel《L76K Reference Design V1.0》第 3 页：模块内部已有上拉，控制电路通过拉低或释放信号控制复位和待机。该修改减少 MCU 主动给已断电 GPS 接口供电的路径，不代表模块备用电源、板上附加电路或电源开关一定没有静态电流。

## 验证

运行：

```powershell
node tools/build_watchdog.cjs
node tools/test_watchdog.cjs
node tools/test_low_power.cjs
node tools/test_gpio_low_power.cjs
```

ARMCLANG6 编译链接成功。Code=28892，RO-data=3980，RW-data=180，ZI-data=4580 字节。无 C 编译警告；旧 armasm 汇编器有一条弃用提示。初次在 Codex 沙箱账户构建时遇到许可证账户不匹配，随后在正常用户环境构建成功，未改动许可证设置。

- 看门狗、存储掉电模型、HEX 和源码/输出哈希检查：12847 项通过。
- 现有低功耗、RTC/射频中断、GPS 调度判定：17091 项通过。
- 新增 GPIO 寄存器和 GPS 串口生命周期模型：1789 项通过，包括重复上电/断电、关闭状态下时钟恢复、活动接收及串口错误恢复、关闭后的过期回调、UART 失败处理、低电量门槛，以及 256 组 GPIO/EXTI 状态的精确恢复。

现有脚本原本仍按旧的 2000ms TX 超时、1 秒首条回包和 1 秒射频准备进行断言，本次更新为当前代码已有的 1500ms、4 秒和 3 秒策略，并补充相应边界场景；未为了通过测试而改变业务参数。原 STOP 函数去掉新增两个 GPIO 钩子后，与修改前保持一致。

HEX 实际编程数据 33056 字节，最高地址 `0x0800811F`，未进入 `0x0800E000` 开始的持久化页。新模块、头文件和验证脚本包含在构建清单内。

以上是本机编译和源码推导模型检查，未在 MCU 上运行，不能作为微安级电流测量或硬件可靠性的证明。

## 产物与回退

新固件：

- `MDK-ARM/watchdog_build/STM32_ONLY_SEND.hex`
- `MDK-ARM/watchdog_build/STM32_ONLY_SEND.bin`
- `MDK-ARM/watchdog_build/STM32_ONLY_SEND.axf`

修改前源码、工程、测试/构建脚本和当时的固件产物保存在 `backups/gpio_low_power_20261128/`。备份的旧固件是修改前已存在的产物，不声称与当时源码完整一致。

使用新 HEX 烧录时应保留版本、配置和日志页，避免全片擦除。APP_VERSION 更新后首次启动会按已有机制上报设备信息。

上板需验证 GPS 多次冷启动/定位和断电、LoRa 收发与 RTC 唤醒，并脱离下载器/串口适配器测量稳定 STOP 平台电流及包含三秒唤醒脉冲的平均电流。若实际板新增了本次模拟输入掩码中的功能，应先更新掩码再使用该固件。
