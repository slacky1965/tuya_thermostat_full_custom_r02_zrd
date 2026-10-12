Все материалы по железу взяты вот [отсюда](https://blog.softdev.online/index.php?controller=post&action=view&id_post=54)

## Схема термостата.

<img src="images/schematic_high.jpg" alt="image" width="20%" height="auto"> <img src="images/schematic_low.jpg" alt="image" width="20%" height="auto" />


## Слаботочная плата термостата с экраном.

<img src="images/board_top.jpg" alt="image" width="18%" height="auto"> <img src="images/board_bottom.jpg" alt="image" width="18%" height="auto" />

Для сборки проекта под Windows нужно скачать несколько инструментов.

- [clang](https://github.com/devbis/llvm-mcs51/releases) (обязательно)
- [SDCC](https://sdcc.sourceforge.net/snap.php) (опционально, по умолчанию сборка для **CA51F253L3** сделана на clang)
- [Telink IoT Studio](https://doc.telink-semi.cn/tools/telink_iot_studio/TelinkIoTStudio_V2025.2.zip) (обязательно)
- [Git for Windows](https://git-scm.com/install/windows?ref=faun) (рекомендуется)

В системе должен быть инсталлирован **python**.

Если нужно только прошить **CA51F253L3**, то **SDCC** и **Telink IoT Studio** можно не скачивать.

После установки всех компонентов запускаем Git Bash и в переходим в папку проекта. Далее выполняем

```
make bootloader clean all
```

Проверяем вывод компилятора. Должно собраться три основных файла - **bootloader** для **TLSR8258**, и по две **firmware** для **TLSR8258** и **CA51F253L3**. Попутно еще создаются образы **ОТА**.

## Как обновить.

Сразу скажу, обновить **CA51F253L3** по ОТА из оригинальной прошивки в кастомную нельзя. В протоколе Tuya есть таком механизм, но проектировщики этого термостата не реализовали такую возможность. Но с другой стороны, для перепрошивки **CA51F253L3** не нужен какой-то отдельный программатор. Достаточно обычного **USB-UART** адаптера.

Итак. Обновление будет происходить в два этапа. Сначала **ОТА** обновление zigbee модуль ZT3L. А затем мы уже прошьем проводами **CA51F253L3**.

Для начало нужно добавить оригинальный термостат в z2m У вас должно быть что-то вот такого

<img src="images/z2m_about_orig.jpg"/>

Далее жмем на синеем облачко и выбираем **custom firmware** и добавляем **ОТА** образ для обновления - [1141-d3a3-1111114b-osrctherm_r02_zrd_ZT3L.zigbee](https://github.com/slacky1965/tuya_thermostat_full_custom_r02_zrd/raw/refs/heads/main/bin/1141-d3a3-1111114b-osrctherm_r02_zrd_ZT3L.zigbee) и жмем обновить.

<img src="images/z2m_update_zt3l_orig.jpg"/>

Далее в логе можно наблюдать отчеты об обновлении.

<img src="images/z2m_ota_update_log1.jpg"/>

В процессе обновления в логе также можно увидеть информацию о кол-ве оставшихся секунд и процент загрузки.

<img src="images/z2m_ota_update_log2.jpg"/>

Также в логе можно наблюдать окончание обновления.

<img src="images/z2m_ota_update_log3.jpg"/>

После успешного обновления нужно разрешить добавление новых устройств в z2m и термостат сам подключится к сети уже с новым именем и адресом. Старую версию термостата принудительно удаляем. У вас должно получиться что-то в этом роде.

<img src="images/z2m_about.jpg"/>

Так. Следующий шаг - прошивка **CA51F253L3**. Для этого нужно обесточить теромстат. 

> [!WARNING]
> Внимание!!! При отсоединенной слаботочной плате нельзя подавать питание на силовую часть термостата!!!

Сдвинуть в сторону экран, отсединить разъем и открутив 4 небольших самореза достать слаботочную плату. На ней нужно найти место с нераспаянным разъемом J1. Далее подключаем к **USB-UART** адаптеру согласно надписям на фото ниже - контакт RX на плате нужно подключать к RX адаптера, контакт TX на плате - к TX адаптера. Также подключаем GND, а вот 3v3 пока не подключаем.

Предварительно проверяем на каком порту сидит на **USB-UART** адаптер и редактируем в корне проекта в makefile параметр CA51F2_PORT на тот, который у вас.

<img src="images/makefile_port_ca51f2.jpg"/>

Далее заходим в Git Bash, переходим в директорию проекта. И даем такую команду.

```
$ make flash-cachip
```

Запускается небольшая программа **python**'е, которая и прошивает **CA51F253L3**. Программа немного модифицирована под себя. Оригинал - [https://github.com/MrdUkk/cachip-ispuartflasher](https://github.com/MrdUkk/cachip-ispuartflasher)

Тут есть два варианта. 

1. Чип прошьется.
2. Программа напишет, что он ожидает 4 секунды для сброса или подачи питания. 

Если у нас второй вариант, то в эти 4 секунды мы подаем питание 3v3 (мы же помним, что его не подавали). В конце концов должно получиться следующее.

```
$ make flash-cachip
make -C CA51F253L3 PORT=COM4 flash-cachip
py ./tools/cachip-isp-serial_new.py COM4 upload ../bin/osrctherm_r02_v1.01_CA51F253L3.bin
using embedded phase1 bootloader (642 bytes)
opening serial port COM4
trying ISP-mode activation, waiting 4 seconds (if power is already applied, pulse RESET pin 2 low->high !)
MCU accepted connection
sending some magic cmd
got mcu success response
switching port speed
sending next magic cmd
got mcu response, asserting values
valid
sending our bootloader
got mcu success response
got mcu success response
got mcu success response
got mcu success response
got mcu success response
got mcu success response
reading hidden 0x2B area to identify the chip
chip identified: CA51F253L3, flash size 32768 bytes
requesting verify chipID for next phases
got mcu success response
requesting access to flash with designated chipID
got mcu success response
requesting onchip flash memory partial 229 sectors erase
got mcu success response
uploading from ../bin/osrctherm_r02_v1.01_CA51F253L3.bin to onchip memory
written 29206/29206 bytes (100%)
upload done: 29206 bytes written to onchip flash
sending session end cmd
got mcu success response
end, remove power from MCU
```

Все. На этом можно все собираться обратно.


