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

// 전역 변수로 종료 플래그 설정
volatile sig_atomic_t gRunning = 1;

// 시그널 핸들러
void signalHandler(int signum) {
  std::cout << "Interrupt signal (" << signum << ") received.\n";
  gRunning = 0;
}

// 변수 값을 출력하는 헬퍼 함수
void printValue(const Value& value) {
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

// 콜백 함수 모니터링을 위한
std::atomic<int> g_callback_counter(0);
std::atomic<int> g_trigger_counter(0);

int main(const int argc, char* argv[]) {
  // 시그널 핸들러 등록
  signal(SIGINT, signalHandler);

  // Initializes server
#ifdef _WIN32
  Server server_("tcp://127.0.0.1", 35557, "tcp://127.0.0.1:35556");
#elif __linux__
  Server server_("tcp://127.0.0.1", 35557, "tcp://127.0.0.1:35556");
#endif

  // 랜덤 시간 지연을 위한 초기화
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> delay_dist(3000, 5000); // 3-5초 사이의 지연

  // 변수에 대한 콜백 함수 정의 (의도적으로 시간이 오래 걸리게 함)
  auto variableCallback = [&](const Value& value) {
    int callback_id = ++g_callback_counter;
    int delay = delay_dist(gen);
    
    std::cout << "[" << callback_id << "] Variable callback started - will sleep for " 
              << (delay/1000.0) << " seconds. Value: ";
    printValue(value);
    std::cout << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    
    std::cout << "[" << callback_id << "] Variable callback completed after " 
              << (delay/1000.0) << " seconds" << std::endl;
  };

  // 트리거에 대한 콜백 함수 정의 (의도적으로 시간이 오래 걸리게 함)
  auto triggerCallback = [&]() {
    int trigger_id = ++g_trigger_counter;
    int delay = delay_dist(gen);
    
    std::cout << "[" << trigger_id << "] Trigger callback started - will sleep for " 
              << (delay/1000.0) << " seconds" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    
    std::cout << "[" << trigger_id << "] Trigger callback completed after " 
              << (delay/1000.0) << " seconds" << std::endl;
  };

  // 다양한 변수 등록 (각각 콜백 함수 포함)
  std::cout << "Registering variables with long-running callbacks..." << std::endl;
  server_.RegisterVariable(Variable("exposure", 100.0), variableCallback);
  server_.RegisterVariable(Variable("gain", 1.0), variableCallback);
  server_.RegisterVariable(Variable("fps", 30.0), variableCallback);
  server_.RegisterVariable(Variable("width", 1920.0), variableCallback);
  server_.RegisterVariable(Variable("height", 1080.0), variableCallback);
  server_.RegisterVariable(Variable("status", std::string("idle")), variableCallback);
  server_.RegisterVariable(Variable("connected", true), variableCallback);
  
  // 다양한 트리거 등록 (각각 콜백 함수 포함)
  std::cout << "Registering triggers with long-running callbacks..." << std::endl;
  server_.RegisterTrigger("start", triggerCallback);
  server_.RegisterTrigger("stop", triggerCallback);
  server_.RegisterTrigger("reset", triggerCallback);
  server_.RegisterTrigger("capture", triggerCallback);
  server_.RegisterTrigger("save", triggerCallback);

  // 서버 시작
  std::cout << "Starting server..." << std::endl;
  server_.Start();
  std::cout << "Server started and ready for connections" << std::endl;
  
  // 메인 루프
  int count = 0;
  while (gRunning && count < 30000) { // 최대 5분 실행
    std::this_thread::sleep_for(std::chrono::seconds(1));
    count++;
    /*
    // 10초마다 서버의 변수 값을 출력
    if (count % 10 == 0) {
      std::cout << "\n=== Server Variables (" << count << " seconds) ===" << std::endl;
      auto variables = server_.GetVariables();
      for (const auto& [name, value] : variables) {
        std::cout << std::setw(12) << name << ": ";
        printValue(value);
        std::cout << std::endl;
      }
      
      // 활성 콜백 수 보고
      std::cout << "Total variable callbacks executed: " << g_callback_counter << std::endl;
      std::cout << "Total trigger callbacks executed: " << g_trigger_counter << std::endl;
      std::cout << std::endl;
    }
    
    // 30초마다 서버쪽에서 변수 값 변경 (이것도 클라이언트로 전파됨)
    if (count % 30 == 0) {
      std::cout << "\n=== Server is updating variables ===" << std::endl;
      server_.SetVariable("exposure", static_cast<double>(100 + (count % 100)));
      server_.SetVariable("fps", static_cast<double>(30 + (count % 10)));
      server_.SetVariable("status", std::string("running_" + std::to_string(count)));
    }
    */
  }
  
  std::cout << "Stopping server..." << std::endl;
  server_.Stop();
  std::cout << "Server stopped" << std::endl;
  
  return 0;
}