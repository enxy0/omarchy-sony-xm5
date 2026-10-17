# Sony XM5 для Omarchy

[English](README.md) | Русский

Виджет для бара [Omarchy](https://omarchy.org) под наушники Sony WH-1000XM5
и WF-1000XM5. Показывает заряд и управляет шумоподавлением, эквалайзером и
остальными настройками гарнитуры без приложения Sony. С гарнитурой по
Bluetooth общается фоновый демон, с ним же работает команда `sony-ctl`.

| WH-1000XM5 | WF-1000XM5 |
|---|---|
| ![Панель с WH-1000XM5](docs/screenshots/wh-1000xm5.png) | ![Панель с WF-1000XM5](docs/screenshots/wf-1000xm5.png) |

Это неофициальный проект, Sony к нему отношения не имеет. «Sony»,
«WH-1000XM5» и «WF-1000XM5» являются товарными знаками Sony Corporation.

## Возможности

Проверено на WH-1000XM5 (прошивка 2.5.1) и WF-1000XM5 (прошивка 6.1.0):
каждую настройку из таблицы меняли, читали заново после переподключения по
Bluetooth и возвращали обратно. Результаты в
[docs/protocol.md](docs/protocol.md), раздел 10. Панель показывает только те
настройки, которые сообщает подключённая гарнитура.

| Настройка | WH-1000XM5 | WF-1000XM5 |
|---|---|---|
| Заряд | да | левый, правый и кейс |
| Шумоподавление, внешний звук, выключено; уровень внешнего звука; Focus on Voice | да | да |
| Пресеты эквалайзера, свой 5-полосный эквалайзер, Clear Bass | да | да |
| DSEE Extreme | да | да |
| Speak-to-Chat с чувствительностью и таймаутом | да | да |
| Приоритет соединения: качество звука или стабильность | да | нет |
| Громкость гарнитуры | да | да |
| Голосовые подсказки | да | да, с громкостью |
| Автовыключение: выключено или при снятии | да | да |
| Пауза при снятии | да | да |
| Кнопка NC/AMB: какие режимы она перебирает | да | нет |
| Сенсорная панель вкл/выкл | да | нет |
| Quick Access: Spotify по двойному или тройному нажатию | да | нет |
| Функция сенсора каждого наушника | нет | да |
| Слышать свой голос во время звонка | нет | да |
| Multipoint, подключённые устройства, переключение источника звука | да | да |
| Кодек, модель и прошивка | да | да |

Обе модели можно подключить одновременно, тогда у каждой будет своя вкладка
в панели.

### В баре

![Подсказка в баре с двумя гарнитурами](docs/screenshots/bar-tooltip.png)

- Иконка показывает гарнитуру, которая играет с этого компьютера. Если
  играют обе или ни одна, показывает подключённую последней. У наушников и
  вкладышей разные иконки.
- Во время зарядки иконка зелёная, при заряде ниже 20 % красная. Через
  3 секунды после отключения последней гарнитуры иконка скрывается.
- В подсказке по строке на каждую гарнитуру, `(bar)` отмечает ту, что на
  иконке.
- Левый клик открывает панель. Правый клик переключает режим шумоподавления
  этой гарнитуры: шумоподавление, внешний звук, выключено.

Клавиши в панели:

| Клавиша | Действие |
|---|---|
| `j` `k` или стрелки | переход между настройками |
| `h` `l` | изменить значение, открыть или закрыть раздел |
| `Enter` `Space` | выбрать |
| `[` `]` | предыдущая или следующая гарнитура |
| `Esc` | закрыть |

## Установка

Нужны Omarchy и сопряжённая и подключённая гарнитура (в панели Bluetooth
Omarchy или через `bluetoothctl`).

1. Добавьте виджет:

   ```bash
   omarchy plugin add https://github.com/enxy0/omarchy-sony-xm5 --enable
   ```

2. Нажмите на иконку наушников в баре и затем **Install**. Откроется
   терминал и запустит `setup`. Его можно запустить и самому:

   ```bash
   ~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup
   ```

Что делает `setup`:

- ставит через pacman недостающие пакеты для сборки (`cmake`, `ninja`,
  `gcc`, `bluez-libs`, `dbus`), только тогда sudo спросит пароль;
- собирает демон и `sony-ctl` в `~/.cache/omarchy-sony-xm5`;
- кладёт `sony-headphones-daemon` и `sony-ctl` в `~/.local/bin`;
- ставит пользовательский systemd-юнит `sony-headphones.service`, включает и
  запускает его.

Демон работает от вашего пользователя. Кроме пакетов для сборки, ничего не
ставится за пределы домашней папки.

### Обновление

```bash
omarchy plugin update io.github.enxy0.sonyxm5
```

Если демон окажется старее виджета, панель покажет «Update the daemon» с
кнопкой **Update**. Она запускает `setup` ещё раз.

### Удаление

```bash
~/.config/omarchy/plugins/io.github.enxy0.sonyxm5/setup --uninstall
omarchy plugin remove io.github.enxy0.sonyxm5
```

`setup --uninstall` останавливает сервис и удаляет демон, `sony-ctl`, юнит,
файл статуса и папку сборки.

### Пакет Arch вместо setup

Если хотите, чтобы демоном владел pacman, соберите пакет из `PKGBUILD` в
этом репозитории. Он ставится в `/usr`. Виджет по-прежнему ставится через
`omarchy plugin add`.

```bash
git clone https://github.com/enxy0/omarchy-sony-xm5
cd omarchy-sony-xm5
makepkg -si
systemctl --user enable --now sony-headphones.service
```

Используйте что-то одно: пакет или `setup`. Удаление пакета:
`systemctl --user disable --now sony-headphones.service`, затем
`sudo pacman -Rns omarchy-sony-xm5`.

### Какие гарнитуры берёт демон

Демон берёт каждую подключённую гарнитуру, в Bluetooth-имени которой есть
`WH-1000XM5` или `WF-1000XM5`. Сам он гарнитуры не подключает: подключите её
как обычно, и через несколько секунд демон её подхватит. Чтобы ограничить
его конкретными гарнитурами, передайте `--device <MAC>` (по одному на
гарнитуру) в drop-in для `sony-headphones.service`, который переопределяет
`ExecStart`.

## Командная строка

```
sony-ctl [-s <socket>] [-H <headset>] <verb> [args...]
```

`-H` принимает Bluetooth-адрес или модель (`wh-1000xm5`, `wf-1000xm5`). Без
него команда уходит гарнитуре, которая на иконке в баре.

```bash
sony-ctl status                      # все гарнитуры одной строкой JSON
sony-ctl noise anc
sony-ctl -H wf-1000xm5 ambient-level 12
sony-ctl eq custom 2 1 0 2 3 1       # 5 полос и Clear Bass, от -10 до 10
```

| Команда | Аргументы |
|---|---|
| `status`, `probe` | |
| `noise` | `anc`, `ambient`, `off` |
| `ambient-level` | `1`..`20` (заодно включает внешний звук) |
| `focus-voice` | `on`, `off` |
| `eq` | пресет: `off`, `bright`, `excited`, `mellow`, `relaxed`, `vocal`, `treble`, `bass`, `speech`, `custom`, `user1`, `user2` |
| `eq custom` | `<400Hz> <1k> <2.5k> <6.3k> <16k> <clear_bass>`, каждое от `-10` до `10` |
| `dsee` | `on`, `off` |
| `speak-to-chat` | `on`, `off` |
| `speak-to-chat-config` | `auto`, `high` или `low`, затем `short`, `standard`, `long` или `off` |
| `voice-guidance` | `on`, `off` |
| `voice-guidance-volume` | `-2`..`2` |
| `auto-power-off` | `off`, `when_taken_off` |
| `nc-amb-button` | `nc_ambient_off`, `nc_ambient`, `nc_off`, `ambient_off` |
| `ear-detect` | `on`, `off` (пауза при снятии) |
| `volume` | `0`..`30` |
| `connection-quality` | `sound`, `connection` |
| `touch-panel` | `on`, `off` |
| `quick-access` | `double` или `triple`, затем `off` или `spotify` |
| `touch-function` | `left` или `right`, затем `ambient_sound_control`, `playback_control` или `none` |
| `sidetone` | `on`, `off` |
| `multipoint` | `on`, `off` (гарнитура может переподключиться) |
| `source` | Bluetooth-адрес подключённого устройства |

Команда-сеттер печатает `OK`, когда гарнитура её подтвердила. Коды выхода:
`0` успех, `1` неверные аргументы или ошибка от демона, `2` демон не
запущен. Полный синтаксис и формат статуса описаны в
[docs/backend-contract.md](docs/backend-contract.md) (на английском).

## Как это устроено

```
Omarchy bar widget  ─── runs ───►  sony-ctl
      │                               │
    reads                        UNIX socket
      ▼                               ▼
status.json  ◄─── writes ───  sony-headphones-daemon
                                      │
                          Bluetooth RFCOMM, MDR v2
                                      ▼
                           WH-1000XM5, WF-1000XM5
```

- Файл статуса: `$XDG_STATE_HOME/sony-headphones/status.json`
  (по умолчанию `~/.local/state/...`).
- Сокет: `$XDG_RUNTIME_DIR/sony-headphones.sock`.
- Лог демона: `journalctl --user -u sony-headphones -f`.

Гарнитура допускает только одну управляющую сессию. Если виджет долго
пишет «reading headset settings», закройте приложение Sony на телефоне.

## Если что-то не работает

С подключённой гарнитурой выполните:

```bash
sony-ctl probe > probe.json
sony-ctl status > status.json
journalctl --user -u sony-headphones -b > daemon.log
```

`probe` выводит модель, прошивку, функции, которые сообщает гарнитура, и её
последние 64 кадра протокола. Если не работает какая-то настройка,
переключите её один раз и выполните `probe` ещё раз. Приложите три файла к
issue и опишите, что делали и что получилось. В файлах есть
Bluetooth-адреса, их можно заменить на `XX:XX:XX:XX:XX:XX`.

## Разработка

```bash
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure    # протокол, сессии, IPC
node tests/model.test.js                      # логика виджета
tests/integration.sh                          # демон, CLI и логика виджета целиком
tests/slider_wheel.sh                         # нужны qmltestrunner и Omarchy shell
```

Bluetooth ни одному из них не нужен. С `--mock` демон работает с
симулированными гарнитурами (`--mock-profile both` даёт обе модели):

```bash
./build/daemon/sony-headphones-daemon --mock --mock-profile both \
  --state-dir /tmp/sony-mock/state --runtime-dir /tmp/sony-mock/run
./build/cli/sony-ctl -s /tmp/sony-mock/run/sony-headphones.sock status
```

`./setup` из клона ставит демон и копирует виджет в
`~/.config/omarchy/plugins`. Документация по протоколу, формату статуса и
панели лежит в [docs/](docs/) (на английском).

Ветки: в `main` релизы, `develop` для интеграции, каждое изменение делается
в ветке `feature/<name>` от `develop`.

## Благодарности

- Проект начинался как форк
  [andROYdified/omarchy-sony](https://github.com/andROYdified/omarchy-sony)
  (MIT) и с тех пор в основном переписан.
- Знания о протоколе взяты из
  [mos9527/SonyHeadphonesClient](https://github.com/mos9527/SonyHeadphonesClient)
  (MIT), таблицы имён функций в `daemon/src/MDRProtocol.cpp` взяты оттуда.
- Некоторые факты о XM5 взяты из
  [Gadgetbridge](https://codeberg.org/Freeyourgadget/Gadgetbridge). Кода
  Gadgetbridge в проекте нет.

Тексты лицензий в [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Лицензия

MIT. См. [LICENSE](LICENSE).
