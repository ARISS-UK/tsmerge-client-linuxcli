# tsmerge linux-cli client

## Build Dependencies

```bash
sudo apt install gcc make cppcheck
````

## Compile

```bash
make cppcheck && make
```

## Run

A callsign and key pair must be requested from the HAMTV tsmerger project.

Depending on your geographic location you may be asked to use a different host server to optimise latency variation.

```bash
./tsmerge-client-linuxcli -c A0AAA -k passkey -h us.live.ariss.org
```

### Valid Connection

```bash
$ ./tsmerge-client-linuxcli -c TEST -k rightkey
========== tsmerge client ===========
 * Build Version: 
 * Build Date:    2025-08-13 17:49:23
=====================================
Sending to 139.162.251.17:5678
Heartbeat response: Tested size: 215B (max batch now: 2), TS Uploaded Total: 6610518, TS Uploaded Loss: 45
Heartbeat response: Tested size: 430B (max batch now: 2), TS Uploaded Total: 6610518, TS Uploaded Loss: 45
Heartbeat response: Tested size: 645B (max batch now: 3), TS Uploaded Total: 6610518, TS Uploaded Loss: 45
<...>
```

### Invalid Credentials

```bash
$ ./tsmerge-client-linuxcli -c TEST -k wrongkey
========== tsmerge client ===========
 * Build Version: 
 * Build Date:    2025-08-13 17:49:23
=====================================
Sending to 139.162.251.17:5678
Error: Server reported authentication failed. Please check callsign and key
$
```
(application exits)

### License

tsmerge-client-linuxcli
Copyright (C) 2025 ARISS-UK

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
