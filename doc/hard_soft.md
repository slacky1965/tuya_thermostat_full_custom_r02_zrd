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

После установки всех компонентов запускаем Git Bash и в переходим в папку проекта. Далее выполняем

```
make bootloader clean all
```

Проверяем вывод компилятора. Должно собраться три основных файла - **bootloader** для **TLSR8258**, и по две **firmware** для **TLSR8258** и **CA51F253L3**. Попутно еще создаются образы **ОТА**.

## Как обновить.

Сразу скажу, обновить **CA51F253L3** по ОТА из оригинальной прошивки в кастомную нельзя. В протоколе Tuya есть таком механизм, но проектировщики этого термостата не реализовали такую возможность. Но с другой стороны, для перепрошивки **CA51F253L3** не нужен какой-то отдельный программатор. Достаточно обычного USB-UART адаптера.

Итак. Обновление будет происходить в два этапа. Сначала ОТА, которое обновит zigbee модуль ZT3L. А затем мы уже прошьем проводами CA51F253L3.

Для начало нужно добавить оригинальный термостат в z2m У вас должно быть что-то вот такого

<img src="images/z2m_about_orig.jpg"/>

Далее жмем на синеем облачко и выбираем **custom firmware** и добавляем **ОТА** образ для обновления - [1141-d3a3-1111114b-osrctherm_r02_zrd_ZT3L.zigbee](https://github.com/slacky1965/tuya_thermostat_full_custom_r02_zrd/raw/refs/heads/main/bin/1141-d3a3-1111114b-osrctherm_r02_zrd_ZT3L.zigbee) и жмем обновить.

<img src="images/z2m_update_zt3l_orig.jpg"/>

Далее в логе можно наблюдать отчеты об обновлении.

<img src="images/z2m_ota_update_log1.jpg"/>

В процессе обновления в логе также можно увидеть информацию о кол-ве оставшихся секунд и процент загрузки.

<img src="images/z2m_ota_update_log2.jpg"/>

