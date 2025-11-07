# group_04_project


```bash
clang++ -std=c++17 bybit_orderbook.cpp \
  -I/opt/homebrew/include \
  -I/opt/homebrew/opt/openssl@3/include \
  -L/opt/homebrew/opt/openssl@3/lib \
  -lssl -lcrypto -lpthread -O2 -o bybit_ws
  ```

# Action Items
- [ ] Orderbook data model class
- [ ] Tick data model class
- [ ] Bybit websocket client (very stable: error handling, connection management, heartbeat, etc.)
- [ ] Bybit orderbook handler (update orderbook data model class)
- [ ] Bybit tick handler
- [ ] Process for flushing orderbook data to a NoSQL database (snapshot and delta)
    - [ ] Generate snapshot data from orderbook data model class
    - [ ] Generate delta data from orderbook data model class
    - [ ] Flush snapshot data to NoSQL database
    - [ ] Flush delta data to NoSQL database
