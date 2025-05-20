# PropLink Library

PropLink is a C++ client-server communication library via ZeroMQ. It provides a robust mechanism for sharing variables and triggers between processes with minimal setup. The communication protocols following have been tested:
- TCP for Windows, Linux
- IPC for Linux

## Features

- **Bidirectional Variable Sharing**: Share variables between client and server with automatic type handling (string, double, int, bool)
- **Trigger Mechanisms**: Register and execute triggers remotely
- **Asynchronous & Synchronous Communication**: Choose between blocking and non-blocking operation modes
- **Thread Safety**: Built-in thread pool and mutex protection for concurrent access
- **Automatic Reconnection**: Clients automatically attempt to reconnect on connection failure
- **Notification System**: Subscribe to variable changes with callback functions
- **Read-Only Protection**: Server-side enforcement of read-only variables

## Core Concepts

- **Variables**: Named values that can be shared between client and server
- **Triggers**: Named events that can be triggered by clients and handled by the server
- **Callbacks**: Functions that are executed when variables change or triggers are executed

## Requirements

- C++17 compiler
- CMake 3.10 or higher
- Protocol Buffers 3.6.x or higher
- ZeroMQ 4.3+ and cppzmq

## Building the Library

### Windows (vcpkg)

1. Install prerequisites:
   - Visual Studio 2019 or later
   - CMake 3.10+
    ```powershell
    vcpkg install protobuf:x64-windows-static
    vcpkg install zeromq:x64-windows-static
    vcpkg install cppzmq:x64-windows-static
    ```

2. Clone the repository:
   ```powershell
   git clone https://github.com/HILLAB-Software/libproplink-cpp.git
   cd libproplink-cpp
   ```

3. Generate protobuf files and move them to the correct directories(run from the libproplink-cpp project root):
   ```powershell
    protoc.exe --cpp_out=. property.proto
    mv property.pb.cc src/property.pb.cc
    mv property.pb.h include/proplink/property.pb.h
   ```

4. Build the library:
   ```powershell
   mkdir build
   cd build
   cmake .. 
   cmake --build . --config Release
   cmake --build . --config Debug
   ```

### Linux

1. Install prerequisites:
   ```bash
   sudo apt-get update
   sudo apt-get install build-essential cmake libprotobuf-dev protobuf-compiler libzmq3-dev
   ```

2. Clone the repository:
   ```bash
   git clone https://github.com/HILLAB-Software/libproplink-cpp.git
   cd libproplink-cpp
   ```

3. Generate protobuf files and move them to the correct directories(run from the libproplink-cpp project root):
   ```bash
    protoc --cpp_out=. property.proto
    mv property.pb.cc src/property.pb.cc
    mv property.pb.h include/proplink/property.pb.h
   ```

4. Build the library:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

5. Install the library (optional):
   ```bash
   sudo make install
   ```
   This will install the library and headers to your system directories, typically under `/usr/local/`.

## Basic Usage
The endpoint follows ZeroMQ's Socket API. For example,
```cpp
#ifdef _WIN32
  Server server("tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556");
#else
  Server server("ipc:///tmp/server1", "ipc:///tmp/server2");
#endif

#ifdef _WIN32
  Client client("tcp://192.168.56.100:5555", "tcp://192.168.56.100:5556");
#else
  Client client("ipc:///tmp/server1", "ipc:///tmp/server2");
#endif
```


### Server Example
```cpp
#include "libproplink/server.h"

int main() {
    // Create server with endpoints
    proplink::Server server("tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556");
    
    // Register a variable with a callback function
    server.RegisterVariable(proplink::Variable("temperature", 25.0), 
        [](const proplink::Value& value) {
            std::cout << "Temperature changed to: " << std::get<double>(value) << std::endl;
        });
    
    // Register a trigger with a callback function
    server.RegisterTrigger("refresh", []() {
        std::cout << "Refresh trigger executed" << std::endl;
    });
    
    // Start the server
    server.Start();
    
    // Main application loop
    while(running) {
        // Update server-side variables as needed
        server.SetVariable("temperature", 26.5);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // Stop the server when done
    server.Stop();
    
    return 0;
}
```

### Client Example

```cpp
#include "libproplink/client.h"

int main() {
    // Create client with same endpoints as server
    proplink::Client client("tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556");
    
    // Connect to server
    if (!client.Connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    // Register callback for variable changes
    client.RegisterCallback("temperature", [](const proplink::Value& value) {
        std::cout << "Received temperature update: " << std::get<double>(value) << std::endl;
    });
    
    // Get a variable synchronously
    proplink::Value temp = client.GetVariable("temperature");
    if (std::holds_alternative<double>(temp)) {
        std::cout << "Current temperature: " << std::get<double>(temp) << std::endl;
    }
    
    // Set a variable asynchronously
    client.SetVariable("temperature", 27.5, proplink::AsyncConnection,
        [](const proplink::ResponseMessage& resp) {
            if (resp.success()) {
                std::cout << "Temperature update successful" << std::endl;
            }
        });
    
    // Execute a trigger synchronously
    client.ExecuteTrigger("refresh", proplink::SyncConnection,
        [](const proplink::ResponseMessage& resp) {
            if (resp.success()) {
                std::cout << "Refresh trigger executed successfully" << std::endl;
            }
        });
    
    // Disconnect when done
    client.Disconnect();
    
    return 0;
}
```

## Connection Options

- **SyncConnection**: Blocks until the server responds (synchronous mode)
- **AsyncConnection**: Doesn't wait for server response and returns immediately (asynchronous mode)

## Advanced Features

### Thread Pool

PropLink uses a thread pool for handling server-side requests, allowing for parallel processing of client commands without blocking the main communication loop.

### Error Handling

The library includes robust error handling with detailed error messages for debugging. All operations return a success/failure status, and error messages are provided in the response.

### Reconnection Logic

Clients automatically attempt to reconnect on connection failures with an exponential backoff strategy.

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
