# group_04_project


```bash
clang++ -std=c++17 bybit_orderbook.cpp \
  -I/opt/homebrew/include \
  -I/opt/homebrew/opt/openssl@3/include \
  -L/opt/homebrew/opt/openssl@3/lib \
  -lssl -lcrypto -lpthread -O2 -o bybit_ws
  ```