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
