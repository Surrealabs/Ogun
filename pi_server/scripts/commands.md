# Ogun Pi Commands

## Version and update

```bash
ogun version
sudo ogun update system
sudo ogun update teensy
sudo ogun update all
sudo ogun update all --version v0.9.0
```

## Rover service control

```bash
sudo systemctl status rover.service
sudo systemctl restart rover.service
sudo systemctl stop rover.service
sudo systemctl start rover.service
journalctl -u rover.service -n 80 --no-pager
journalctl -u rover.service -f
```


## Auto-updater

```bash
sudo ogun enable autoupdate
sudo ogun check autoupdate
sudo ogun disable autoupdate
sudo ogun autoupdate run-now

# Direct systemd equivalents:
sudo systemctl status rover-auto-update.timer
systemctl list-timers --all | grep rover-auto-update
sudo systemctl start rover-auto-update.service
journalctl -u rover-auto-update.service -n 80 --no-pager
tail -n 80 /var/log/rover-auto-update.log
```

## New command style (v0.9.0)

```bash
# Logs/status
ogun log teensy
ogun log rover <pi-ip>
ogun check system <pi-ip>

# Build safety on Pi
ogun build pi --safe

# Legacy aliases still accepted:
ogun monitor serial
ogun monitor rover <pi-ip>
ogun status <pi-ip>
```

## Network endpoints

```bash
ss -tlnp | grep rover_server
curl -I http://localhost:8080         # WebUI
curl -I http://localhost:8081         # Cam0 MJPEG
curl -I http://localhost:8082         # Cam1 MJPEG
```

## Camera checks

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
v4l2-ctl -d /dev/video2 --list-formats-ext
```

## Teensy serial checks

```bash
ls -l /dev/ttyACM* /dev/ttyUSB*
dmesg | grep -i ttyACM | tail -20
lsusb | grep -i teensy
```

## Audio checks and playback

```bash
aplay -l
amixer -c 0 scontents
speaker-test -D hw:0,0 -t sine -f 440 -l 1
aplay -D hw:0,0 /opt/rover/sounds/horn.wav
mpg123 -q /opt/rover/sounds/engine.mp3
```

## Config and install paths

```bash
cat /opt/rover/INSTALLED_VERSION
ls -la /etc/rover
cat /etc/rover/rover.conf
ls -la /opt/rover/webui
```
