#include "../include/proplink/client.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>
#include <string>
#include <atomic>
#include <vector>
#include <csignal>
#include <functional>
#include <sstream>

using namespace proplink;

// Set termination flag as global variable
volatile sig_atomic_t g_running = 1;

// Signal handler
void SignalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received.\n";
  g_running = 0;
}

// Helper function to print variable values
void PrintValue(const Value& value) {
  if (std::holds_alternative<std::string>(value)) {
    std::cout << std::get<std::string>(value);
  } else if (std::holds_alternative<double>(value)) {
    std::cout << std::fixed << std::setprecision(2) << std::get<double>(value);
  } else if (std::holds_alternative<bool>(value)) {
    std::cout << (std::get<bool>(value) ? "true" : "false");
  } else {
    std::cout << "<empty>";
  }
}

// Helper class for time measurement
class Timer {
public:
  Timer(const std::string& name) : name_(name), start_(std::chrono::high_resolution_clock::now()) {
    std::cout << "Starting operation: " << name_ << std::endl;
  }
  
  ~Timer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    std::cout << "Operation " << name_ << " completed in " << duration << " ms" << std::endl;
  }

private:
  std::string name_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

// Callback counter
std::atomic<int> g_client_callback_counter(0);

int main() {
  // Register signal handler
  signal(SIGINT, SignalHandler);

  // Create client and connect to server
  std::cout << "Creating client and connecting to server..." << std::endl;
#ifdef _WIN32
  Client client("tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556");
#else
  Client client("ipc:///tmp/server1", "ipc:///tmp/server2");
#endif

  std::cout << "Connecting to server..." << std::endl;
  if (!client.Open()) {
    std::cerr << "Failed to connect to server" << std::endl;
    return 1;
  }
  std::cout << "Connected successfully" << std::endl << std::endl;
  
  // Register callback function (called when variables are changed on the server)
  auto client_callback = [](const Value& value) {
    int id = ++g_client_callback_counter;
    std::stringstream ss;
    ss << "[Callback " << id << "] Variable changed on server: ";
    
    if (std::holds_alternative<std::string>(value)) {
      ss << std::get<std::string>(value);
    } else if (std::holds_alternative<double>(value)) {
      ss << std::fixed << std::setprecision(2) << std::get<double>(value);
    } else if (std::holds_alternative<bool>(value)) {
      ss << (std::get<bool>(value) ? "true" : "false");
    }
    
    std::cout << ss.str() << std::endl;
  };
  
  // Register callbacks for all variables
  std::cout << "Registering callbacks for all variables..." << std::endl;
  auto variables = client.GetAllVariables();
  for (const auto& [name, value] : variables) {
    client.RegisterCallback(name, client_callback);
    std::cout << "  - Callback registered for " << name << std::endl;
  }
  
  // Display all available triggers
  std::cout << "\nAvailable triggers:" << std::endl;
  auto triggers = client.GetAllTriggers();
  for (const auto& trigger : triggers) {
    std::cout << "  - " << trigger << std::endl;
  }
  
  std::cout << "\nStarting tests - Setting variables and executing triggers with various connection options\n" << std::endl;
  
  // Start testing
  int test_number = 0;
  
  while (g_running && test_number < 10) {
    test_number++;
    std::cout << "\n======= Test #" << test_number << " ========" << std::endl;
    
    // Test 1: Set variable synchronously (wait for server response)
    std::cout << "\n[Test " << test_number << ".1] Setting 'exposure' variable with SyncConnection (blocking)" << std::endl;
    {
      Timer timer("SyncConnection SetVariable");
      double new_value = 100.0 + test_number * 10.0;
      
      bool result = client.SetVariable("exposure", new_value, SyncConnection, 
        [](const ResponseMessage& resp) {
          std::cout << "Synchronous response received: " << (resp.success() ? "Success" : "Failure");
          if (!resp.message().empty()) {
            std::cout << " - " << resp.message();
          }
          if (!resp.error_message().empty()) {
            std::cout << " - Error: " << resp.error_message();
          }
          std::cout << std::endl;
        });
      
      std::cout << "Variable setting result: " << (result ? "Success" : "Failure") << std::endl;
    }
    
    // Wait briefly
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Test 2: Set variable asynchronously (don't wait for server response)
    std::cout << "\n[Test " << test_number << ".2] Setting 'gain' variable with AsyncConnection (non-blocking)" << std::endl;
    {
      Timer timer("AsyncConnection SetVariable");
      double new_value = 1.0 + test_number * 0.5;
      
      bool result = client.SetVariable("gain", new_value, AsyncConnection,
        [](const ResponseMessage& resp) {
          std::cout << "Asynchronous response received: " << (resp.success() ? "Success" : "Failure");
          if (!resp.message().empty()) {
            std::cout << " - " << resp.message();
          }
          if (!resp.error_message().empty()) {
            std::cout << " - Error: " << resp.error_message();
          }
          std::cout << std::endl;
        });
      
      std::cout << "Variable setting request result: " << (result ? "Success" : "Failure") << std::endl;
    }
    
    // Wait briefly
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Test 3: Execute trigger synchronously (wait for server response)
    std::cout << "\n[Test " << test_number << ".3] Executing 'start' trigger with SyncConnection (blocking)" << std::endl;
    {
      Timer timer("SyncConnection ExecuteTrigger");
      
      bool result = client.ExecuteTrigger("start", SyncConnection,
        [](const ResponseMessage& resp) {
          std::cout << "Synchronous trigger response received: " << (resp.success() ? "Success" : "Failure");
          if (!resp.message().empty()) {
            std::cout << " - " << resp.message();
          }
          if (!resp.error_message().empty()) {
            std::cout << " - Error: " << resp.error_message();
          }
          std::cout << std::endl;
        });
      
      std::cout << "Trigger execution result: " << (result ? "Success" : "Failure") << std::endl;
    }
    
    // Wait briefly
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Test 4: Execute trigger asynchronously (don't wait for server response)
    std::cout << "\n[Test " << test_number << ".4] Executing 'stop' trigger with AsyncConnection (non-blocking)" << std::endl;
    {
      Timer timer("AsyncConnection ExecuteTrigger");
      
      bool result = client.ExecuteTrigger("stop", AsyncConnection,
        [](const ResponseMessage& resp) {
          std::cout << "Asynchronous trigger response received: " << (resp.success() ? "Success" : "Failure");
          if (!resp.message().empty()) {
            std::cout << " - " << resp.message();
          }
          if (!resp.error_message().empty()) {
            std::cout << " - Error: " << resp.error_message();
          }
          std::cout << std::endl;
        });
      
      std::cout << "Trigger execution request result: " << (result ? "Success" : "Failure") << std::endl;
    }
    
    // Test 5: Set multiple variables in parallel (testing asynchronous nature of DEALER socket)
    std::cout << "\n[Test " << test_number << ".5] Setting multiple variables in parallel (multithreading test)" << std::endl;
    {
      Timer timer("Parallel SetVariable");
      
      // Set variables simultaneously from multiple threads
      std::vector<std::thread> threads;
      for (int i = 0; i < 5; i++) {
        threads.push_back(std::thread([&client, i, test_number]() {
          std::string name;
          Value value;
          
          switch (i % 5) {
            case 0: name = "width"; value = 1920.0 + test_number; break;
            case 1: name = "height"; value = 1080.0 + test_number; break;
            case 2: name = "fps"; value = 30.0 + test_number; break;
            case 3: name = "status"; value = std::string("test_" + std::to_string(test_number)); break;
            case 4: name = "connected"; value = (test_number % 2 == 0); break;
          }
          
          std::cout << "  Thread " << i << ": Setting " << name << "..." << std::endl;
          
          // Use asynchronous setting
          client.SetVariable(name, value, AsyncConnection, 
            [i](const ResponseMessage& resp) {
              std::cout << "  Thread " << i << " response: " << (resp.success() ? "Success" : "Failure") << std::endl;
            });
          
          std::cout << "  Thread " << i << ": " << name << " setting request completed" << std::endl;
        }));
      }
      
      // Wait for all threads to complete
      for (auto& t : threads) {
        t.join();
      }
    }
    
    // Check current server status
    std::cout << "\n[Test " << test_number << ".6] Checking current server variable status" << std::endl;
    {
      Timer timer("GetAllVariables");
      auto current_vars = client.GetAllVariables();
      
      std::cout << "Server variable list:" << std::endl;
      for (const auto& [name, value] : current_vars) {
        std::cout << "  " << std::setw(12) << name << ": ";
        PrintValue(value);
        std::cout << std::endl;
      }
    }
    
    // Wait before next test
    std::cout << "\nWaiting 5 seconds before next test..." << std::endl;
    for (int i = 0; i < 5 && g_running; i++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "." << std::flush;
    }
    std::cout << std::endl;
  }
  
  // Test result summary
  std::cout << "\n===== Test Result Summary =====" << std::endl;
  std::cout << "Client callback execution count: " << g_client_callback_counter << std::endl;
  
  // Close connection
  std::cout << "Closing client connection..." << std::endl;
  client.Close();
  std::cout << "Client connection closed" << std::endl;
  
  return 0;
}