# Sony XM5 для Omarchy

[English](README.md) | Русский

Управление наушниками Sony WH-1000XM5 и WF-1000XM5 из бара
[Omarchy](https://omarchy.org): заряд, шумоподавление, эквалайзер и остальные
настройки из приложения Sony. Телефон не нужен.

| WH-1000XM5 | WF-1000XM5 |
|---|---|
| ![Панель с WH-1000XM5](docs/screenshots/wh-1000xm5.png) | ![Панель с WF-1000XM5](docs/screenshots/wf-1000xm5.png) |

## Установка

Нужны Omarchy и наушники, сопряжённые и подключённые по Bluetooth.

```bash
omarchy plugin add https://github.com/enxy0/omarchy-sony-xm5 --enable
```

Потом нажмите на иконку наушников в баре и затем **Install**. Это соберёт и
запустит небольшой фоновый сервис от вашего пользователя. Ему нужны BlueZ и
D-Bus, они в Omarchy уже есть, а недостающие пакеты для сборки (`cmake`,
`ninja`, `gcc`, `bluez-libs`) ставятся через pacman; только тогда sudo
спросит пароль.

Обновление: `omarchy plugin update io.github.enxy0.sonyxm5`, панель сама
предложит **Update**, если сервис тоже нужно обновить.

Удаление:

```bash
~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup --uninstall
omarchy plugin remove io.github.enxy0.sonyxm5
```

Подробнее о setup, удалении и пакете Arch: [docs/install.md](docs/install.md)
(на английском).

## Использование

- **Левый клик** по иконке открывает панель, **правый** переключает
  шумоподавление, внешний звук и выключено.
- Если подключены обе модели, в панели по вкладке на каждую, а иконка
  показывает ту, что играет с этого компьютера.
- Панелью можно управлять с клавиатуры: `j` `k` перемещение, `h` `l`
  изменение, `Enter` выбор, `[` `]` смена наушников, `Esc` закрыть.

![Подсказка в баре с двумя гарнитурами](docs/screenshots/bar-tooltip.png)

То же самое из терминала:

```bash
sony-ctl noise anc
sony-ctl -H wf-1000xm5 ambient-level 12
sony-ctl eq bright
```

Все команды: [docs/cli.md](docs/cli.md).

## Возможности

Шумоподавление и внешний звук с Focus on Voice, пресеты эквалайзера и свой
5-полосный эквалайзер с Clear Bass, DSEE Extreme, Speak-to-Chat, громкость,
голосовые подсказки, автовыключение, пауза при снятии, multipoint с
переключением источника звука, кодек и заряд (у вкладышей левый, правый и
кейс). Только у WH-1000XM5: приоритет соединения, кнопка NC/AMB, сенсорная
панель, Quick Access. Только у WF-1000XM5: функция сенсора каждого наушника,
свой голос во время звонка.

Каждая настройка проверена на WH-1000XM5 (прошивка 2.5.1) и WF-1000XM5
(прошивка 6.1.0). Панель показывает только то, что поддерживает подключённая
гарнитура.

## Дальше

Подробная документация на английском:

- [docs/install.md](docs/install.md): что делает setup, удаление, пакет
  Arch, выбор гарнитур.
- [docs/cli.md](docs/cli.md): команды `sony-ctl`.
- [docs/troubleshooting.md](docs/troubleshooting.md): если что-то не
  работает и что приложить к issue.
- [docs/development.md](docs/development.md): сборка, тесты, мок-демон.
- [docs/protocol.md](docs/protocol.md): протокол Sony в том виде, в каком
  его использует проект, и результаты проверки на железе.

Неофициальный проект, Sony к нему отношения не имеет. Начинался как форк
[andROYdified/omarchy-sony](https://github.com/andROYdified/omarchy-sony);
знания о протоколе из
[mos9527/SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient)
и [Gadgetbridge](https://codeberg.org/Freeyourgadget/Gadgetbridge)
([лицензии](THIRD_PARTY_NOTICES.md)). Лицензия MIT.
