#include "server.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <future>
#include <string>
#include <csignal>
#include <vector>
#include <random>

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

// For monitoring callback functions
std::atomic<int> g_callback_counter(0);
std::atomic<int> g_trigger_counter(0);

int main() {
  // Register signal handler
  signal(SIGINT, SignalHandler);

  // Initialize server
#ifdef _WIN32
  Server server("tcp://127.0.0.1:5555", "tcp://127.0.0.1:5556");
#else
  Server server("ipc:///tmp/server1", "ipc:///tmp/server2");
#endif

  // Initialize for random time delay
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> delay_dist(3000, 5000); // 3-5 seconds delay

  // Define callback function for variables (intentionally made to take a long time)
  auto variable_callback = [&gen, &delay_dist](const Value& value) {
    int callback_id = ++g_callback_counter;
    int delay = delay_dist(gen);
    
    std::cout << "[" << callback_id << "] Variable callback started - will sleep for " 
              << (delay/1000.0) << " seconds. Value: ";
    
    if (std::holds_alternative<std::string>(value)) {
      std::cout << std::get<std::string>(value);
    } else if (std::holds_alternative<double>(value)) {
      std::cout << std::fixed << std::setprecision(2) << std::get<double>(value);
    } else if (std::holds_alternative<bool>(value)) {
      std::cout << (std::get<bool>(value) ? "true" : "false");
    }
    std::cout << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    
    std::cout << "[" << callback_id << "] Variable callback completed after " 
              << (delay/1000.0) << " seconds" << std::endl;
  };

  // Define callback function for triggers (intentionally made to take a long time)
  auto trigger_callback = [&gen, &delay_dist]() {
    int trigger_id = ++g_trigger_counter;
    int delay = delay_dist(gen);
    
    std::cout << "[" << trigger_id << "] Trigger callback started - will sleep for " 
              << (delay/1000.0) << " seconds" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    
    std::cout << "[" << trigger_id << "] Trigger callback completed after " 
              << (delay/1000.0) << " seconds" << std::endl;
  };

  // Register various variables (each with callback function)
  std::cout << "Registering variables with long-running callbacks..." << std::endl;
  server.RegisterVariable(Variable("exposure", 100.0), variable_callback);
  server.RegisterVariable(Variable("gain", 1.0), variable_callback);
  server.RegisterVariable(Variable("fps", 30.0), variable_callback);
  server.RegisterVariable(Variable("width", 1920.0), variable_callback);
  server.RegisterVariable(Variable("height", 1080.0), variable_callback);
  server.RegisterVariable(Variable("status", std::string("idle")), variable_callback);
  server.RegisterVariable(Variable("connected", true), variable_callback);
  
  // Register various triggers (each with callback function)
  std::cout << "Registering triggers with long-running callbacks..." << std::endl;
  server.RegisterTrigger("start", trigger_callback);
  server.RegisterTrigger("stop", trigger_callback);
  server.RegisterTrigger("reset", trigger_callback);
  server.RegisterTrigger("capture", trigger_callback);
  server.RegisterTrigger("save", trigger_callback);

  // Start server
  std::cout << "Starting server with thread pool..." << std::endl;
  server.Start();
  std::cout << "Server started and ready for connections" << std::endl;
  
  // Main loop
  int count = 0;
  while (g_running && count < 600) { // Run for max 10 minutes (1 second interval)
    std::this_thread::sleep_for(std::chrono::seconds(1));
    count++;
    
    // Print server variable values every 10 seconds
    if (count % 10 == 0) {
      std::cout << "\n=== Server Variables (" << count << " seconds) ===" << std::endl;
      auto variables = server.GetVariables();
      for (const auto& [name, value] : variables) {
        std::cout << std::setw(12) << name << ": ";
        PrintValue(value);
        std::cout << std::endl;
      }
      
      // Report number of active callbacks
      std::cout << "Total variable callbacks executed: " << g_callback_counter << std::endl;
      std::cout << "Total trigger callbacks executed: " << g_trigger_counter << std::endl;
      std::cout << std::endl;
    }
    
    // Change variable values from server side every 30 seconds (this will propagate to clients)
    if (count % 30 == 0) {
      std::cout << "\n=== Server is updating variables ===" << std::endl;
      server.SetVariable("exposure", static_cast<double>(100 + (count % 100)));
      server.SetVariable("fps", static_cast<double>(30 + (count % 10)));
      server.SetVariable("status", std::string("running_" + std::to_string(count)));
      std::cout << "Variables updated by server" << std::endl;
    }
  }
  
  std::cout << "Stopping server..." << std::endl;
  server.Stop();
  std::cout << "Server stopped" << std::endl;
  
  return 0;
}