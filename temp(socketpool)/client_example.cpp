#include "client.h"
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

// 시간 측정 헬퍼 클래스
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

// 콜백 카운터
std::atomic<int> g_client_callback_counter(0);

int main() {
  // 시그널 핸들러 등록
  signal(SIGINT, signalHandler);

  // 클라이언트 생성 및 서버 연결
  std::cout << "Creating client and connecting to server..." << std::endl;
#ifdef _WIN32
  Client client("tcp://127.0.0.1", 35557, "tcp://127.0.0.1:35556");
#else
  Client client("tcp://127.0.0.1", 35557, "tcp://127.0.0.1:35556");
#endif

  std::cout << "Connecting to server..." << std::endl;
  if (!client.Connect()) {
    std::cerr << "Failed to connect to server" << std::endl;
    return 1;
  }
  std::cout << "Connected successfully" << std::endl << std::endl;
  
  // 콜백 함수 등록 (서버에서 변수가 변경될 때 호출됨)
  auto clientCallback = [](const Value& value) {
    int id = ++g_client_callback_counter;
    std::stringstream ss;
    if (std::holds_alternative<std::string>(value)) {
      ss << std::get<std::string>(value);
    } else if (std::holds_alternative<double>(value)) {
      ss << std::fixed << std::setprecision(2) << std::get<double>(value);
    } else if (std::holds_alternative<bool>(value)) {
      ss << (std::get<bool>(value) ? "true" : "false");
    }
    
    std::cout << "[Callback " << id << "] 서버에서 변수가 변경됨: " << ss.str() << std::endl;
  };
  
  // 모든 변수에 대한 콜백 등록
  std::cout << "모든 변수에 대한 콜백 등록 중..." << std::endl;
  auto variables = client.GetAllVariables();
  for (const auto& [name, value] : variables) {
    client.RegisterCallback(name, clientCallback, AsyncConnection);
    std::cout << "  - " << name << "에 콜백 등록됨" << std::endl;
  }
  
  // 모든 트리거 표시
  std::cout << "\n사용 가능한 트리거:" << std::endl;
  auto triggers = client.GetAllTriggers();
  for (const auto& trigger : triggers) {
    std::cout << "  - " << trigger << std::endl;
  }
  
  std::cout << "\n테스트 시작 - 다양한 연결 옵션으로 변수 설정 및 트리거 실행\n" << std::endl;
  
  // 테스트 시작
  int test_number = 0;
  
  while (gRunning && test_number < 10) {
    test_number++;
    std::cout << "\n======= 테스트 #" << test_number << " ========" << std::endl;
    
    // 테스트 1: 동기 방식으로 변수 설정 (서버의 응답을 기다림)
    std::cout << "\n[테스트 " << test_number << ".1] SyncConnection으로 'exposure' 변수 설정 (블로킹 됨)" << std::endl;
    {
      Timer timer("SyncConnection SetVariable");
      double new_value = 100.0 + test_number * 10.0;
      bool result = client.SetVariable("exposure", new_value, SyncConnection);
      std::cout << "변수 설정 결과: " << (result ? "성공" : "실패") << std::endl;
    }
    
    // 잠시 대기
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 테스트 2: 비동기 방식으로 변수 설정 (서버의 응답을 기다리지 않음)
    std::cout << "\n[테스트 " << test_number << ".2] AsyncConnection으로 'gain' 변수 설정 (논블로킹)" << std::endl;
    {
      Timer timer("AsyncConnection SetVariable");
      double new_value = 1.0 + test_number * 0.5;
      bool result = client.SetVariable("gain", new_value, AsyncConnection);
      std::cout << "변수 설정 요청 결과: " << (result ? "성공" : "실패") << std::endl;
    }
    
    // 잠시 대기
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 테스트 3: 동기 방식으로 트리거 실행 (서버의 응답을 기다림)
    std::cout << "\n[테스트 " << test_number << ".3] SyncConnection으로 'start' 트리거 실행 (블로킹 됨)" << std::endl;
    {
      Timer timer("SyncConnection ExecuteTrigger");
      bool result = client.ExecuteTrigger("start", SyncConnection);
      std::cout << "트리거 실행 결과: " << (result ? "성공" : "실패") << std::endl;
    }
    
    // 잠시 대기
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 테스트 4: 비동기 방식으로 트리거 실행 (서버의 응답을 기다리지 않음)
    std::cout << "\n[테스트 " << test_number << ".4] AsyncConnection으로 'stop' 트리거 실행 (논블로킹)" << std::endl;
    {
      Timer timer("AsyncConnection ExecuteTrigger");
      bool result = client.ExecuteTrigger("stop", AsyncConnection);
      std::cout << "트리거 실행 요청 결과: " << (result ? "성공" : "실패") << std::endl;
    }
    
    // 테스트 5: 병렬로 여러 변수 설정 (소켓 풀 테스트)
    std::cout << "\n[테스트 " << test_number << ".5] 병렬로 여러 변수 설정 (소켓 풀 테스트)" << std::endl;
    {
      Timer timer("Parallel SetVariable");
      
      // 여러 스레드에서 동시에 변수 설정
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
          
          std::cout << "  스레드 " << i << ": " << name << " 설정 중..." << std::endl;
          client.SetVariable(name, value, SyncConnection);
          std::cout << "  스레드 " << i << ": " << name << " 설정 완료" << std::endl;
        }));
      }
      
      // 모든 스레드 완료 기다림
      for (auto& t : threads) {
        t.join();
      }
    }
    
    // 현재 서버 상태 확인
    std::cout << "\n[테스트 " << test_number << ".6] 현재 서버 변수 상태 확인" << std::endl;
    {
      Timer timer("GetAllVariables");
      auto current_vars = client.GetAllVariables();
      
      std::cout << "서버 변수 목록:" << std::endl;
      for (const auto& [name, value] : current_vars) {
        std::cout << "  " << std::setw(12) << name << ": ";
        printValue(value);
        std::cout << std::endl;
      }
    }
    
    // 다음 테스트 전 대기
    std::cout << "\n다음 테스트까지 5초 대기 중..." << std::endl;
    for (int i = 0; i < 5 && gRunning; i++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "." << std::flush;
    }
    std::cout << std::endl;
  }
  
  // 테스트 결과 요약
  std::cout << "\n===== 테스트 결과 요약 =====" << std::endl;
  std::cout << "클라이언트 콜백 실행 횟수: " << g_client_callback_counter << std::endl;
  
  // 연결 종료
  std::cout << "클라이언트 연결 종료 중..." << std::endl;
  client.Disconnect();
  std::cout << "클라이언트 연결 종료 완료" << std::endl;
  
  return 0;
}