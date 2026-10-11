Все материалы по железу взяты вот [отсюда](https://blog.softdev.online/index.php?controller=post&action=view&id_post=54)

## Схема термостата.

<img src="images/schematic_high.jpg" alt="image" width="20%" height="auto"> <img src="images/schematic_low.jpg" alt="image" width="20%" height="auto" />


## Слаботочная плата термостата с экраном.

<img src="images/board_top.jpg" alt="image" width="18%" height="auto"> <img src="images/board_bottom.jpg" alt="image" width="18%" height="auto" />

Для сборки проекта под Windows нужно скачать несколько инструментов.

- [clang](https://github.com/devbis/llvm-mcs51/releases) (обязательно)
- [SDCC](https://sdcc.sourceforge.net/snap.php) (опционально, по умолчанию сборка для CA51F253L3 сделана на clang)
- [Telink IoT Studio](https://doc.telink-semi.cn/tools/telink_iot_studio/TelinkIoTStudio_V2025.2.zip) (обязательно)
- [Git for Windows](https://git-scm.com/install/windows?ref=faun) (рекомендуется)

После установки всех компонентов запускаем Git Bash и в переходим в папку проекта. Далее выполняем

```
make bootloader clean all
```

Проверяем вывод компилятора. Должно собраться три основных файла - bootloader для TLSR8258, и по две firmware для TLST8258 и CA51F253L3. Попутно еще создаются образы ОТА.