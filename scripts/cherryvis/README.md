# CherryVis
A visualizer for the CherryPi bot. Can load a replay (a.rep), and bot state
information file if available (a.rep.cvis).

# Setup
## Setup django
```
virtualenv -p python3 venv_django
source venv_django/bin/activate  # On Windows: venv_django\Scripts\activate
pip install django zstandard
```

## Build OpenBW
Install emscripten from sources (https://kripken.github.io/emscripten-site/docs/getting_started/downloads.html), and then:
```
./build_js.sh
```

## Start the server
```
./server.py
```
Then open your web browser at http://127.0.0.1:8770/

By default, CherryVis is configured only to accept local connections. This is for your security; CherryVis is a window into your filesystem and you probably don't want to make its contents publicly accessible. But if you do want to accept incoming connections, you can run:
```
./server.py --host 0.0.0.0:8770
```

# Generating replays in CherryPi
The following command will run a game against another bot.
At the end, a replay will be created (`game.rep`),
along with a bot trace readable by CherryVis (`game.rep.cvis` folder).
```
./build/scripts/botplay/botplay -map "maps/(4)Fighting Spirit.scx" -opponent path/to/opponent.dll -replay_path game.rep
```
