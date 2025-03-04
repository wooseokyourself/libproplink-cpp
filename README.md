# PropLink Library

## Features

- Zero-copy message passing between applications
- Thread-safe variable access and management
- Event-driven architecture with trigger support
- Cross-platform support for Windows and Linux
- Asynchronous and synchronous communication options
  
## Requirements

- C++17 compiler
- CMake 3.10 or higher
- **libprotoc 3.6.x**
- ZeroMQ 4.3+

## Building the Library

### Windows (vcpkg)

1. Install prerequisites:
   - Visual Studio 2019 or later
   - CMake 3.10+
    ```powershell
    vcpkg install protobuf:x64-windows-static --version 3.6.1
    vcpkg install zeromq:x64-windows-static
    vcpkg install cppzmq:x64-windows-static
    ```

2. Clone the repository:
   ```powershell
   git clone https://github.com/HILLAB-Software/libproplink-cpp.git
   cd libproplink-cpp
   ```

3. Build the library:
   ```powershell
   # Replace [path\to\vcpkg] with your actual vcpkg installation path.
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022" -A x64 -T v142 -DCMAKE_TOOLCHAIN_FILE=[path\to\vcpkg]\scripts\buildsystems\vcpkg.cmake
   cmake --build . --config Release
   ```
   or
   ```powershell
   ./rebuild.ps1
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

3. Build the library:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

## Running the Example Program

After building the library, you can run the example programs:

### Windows
```powershell
cd build\Release
.\client_example.exe
```
In another terminal:
```powershell
cd build\Release
.\server_example.exe
```

### Linux
```bash
cd build
./client_example
```
In another terminal:
```bash
cd build
./server_example
```

## License

This project is licensed under the GNU General Public License v3.0 - see the [LICENSE](LICENSE) file for details.
