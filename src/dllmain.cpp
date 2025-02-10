#include <Windows.h>
#include <cstdint>
#include <intrin.h>
#include <algorithm>
#include "defs.h"

uint8_t original_bytes[12] = {0};  // Store original function bytes

typedef bool(__fastcall* Original_sub_575BC0)(__int64 a1, unsigned int* a2, int a3, int a4);
typedef void(__fastcall* Original_sub_35A260)(uintptr_t a1, int a2, int a3, int a4);
typedef __int64(__fastcall* Original_sub_5756E0)(__int64 a1, unsigned int* a2, int a3, __int64* a4, volatile signed __int64** a5, unsigned int* a6, unsigned int a7);
typedef void(__fastcall* Original_sub_5758F0)(__int64 a1, unsigned int* a2, __int64 a3);
typedef void(__fastcall* Original_sub_5759E0)(__int64 a1, unsigned int* a2, volatile signed __int64* a3, __int64 a4, unsigned int a5);

// Store original functions
Original_sub_575BC0 original_575BC0 = nullptr;
Original_sub_5756E0 original_5756E0 = nullptr;
Original_sub_5758F0 original_5758F0 = nullptr;
Original_sub_5759E0 original_5759E0 = nullptr;

void LogError(const char* message, DWORD error = GetLastError()) {
    char buffer[256];
    sprintf_s(buffer, "[UnityOptimizer] Error: %s (Code: %lu)", message, error);
    OutputDebugStringA(buffer);
}

bool ValidatePointer(void* ptr, const char* name) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) {
        LogError(name);
        return false;
    }
    return (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_EXECUTE_READ || mbi.Protect & PAGE_EXECUTE_READWRITE));
}

const DWORD WAIT_TIMEOUT_MS = 1;              // Ultra-short timeout for better FPS
const unsigned int ADAPTIVE_MIN_SPINS = 32;   // Increased minimum spins for FPS
const unsigned int ADAPTIVE_MAX_SPINS = 1024; // Moderate maximum spins
const unsigned int YIELD_EVERY_N_SPINS = 32;  // Less frequent yields
const unsigned int PAUSE_ITERATIONS = 6;     // Fewer pauses for better response

struct alignas(64) WaitNode {
    volatile LONG state;
    HANDLE event;
    WaitNode* next;
    bool is_waiting;
    void* wait_address;
    volatile LONG ref_count; 
    char padding[32]; 
};

// event pool for direct synchronization
class EventPool {
private:
    static constexpr size_t POOL_SIZE = 256;
    HANDLE events[POOL_SIZE];
    volatile LONG next_index;

public:
    EventPool() : next_index(0) {
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            events[i] = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        }
    }

    ~EventPool() {
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            if (events[i]) CloseHandle(events[i]);
        }
    }

    HANDLE AcquireEvent() {
        LONG index = (_InterlockedIncrement(&next_index) - 1) % POOL_SIZE;
        return events[index];
    }
}; EventPool g_EventPool;

// Thread aware spinning control
struct alignas(64) ThreadState {
    volatile LONG64 spin_count;
    volatile LONG64 success_count;
    volatile LONG64 wait_count;
    DWORD thread_id;
    bool is_worker;
    bool is_main_thread;
    char padding[30];
};

class ThreadStateManager {
private:
    static constexpr size_t MAX_THREADS = 64;
    ThreadState states[MAX_THREADS];
    volatile LONG next_index;
    DWORD main_thread_id;
    char padding[56];

public:
    ThreadStateManager() : next_index(0), main_thread_id(0) {
        memset(states, 0, sizeof(states));
    }

    ThreadState* GetState() {
        DWORD tid = GetCurrentThreadId();
        
        // First try to find existing state
        for (size_t i = 0; i < MAX_THREADS; i++) {
            if (states[i].thread_id == tid) {
                return &states[i];
            }
        }

        // Create new state if needed
        LONG index = _InterlockedIncrement(&next_index) - 1;
        index = index % MAX_THREADS;
        
        states[index].thread_id = tid;
        states[index].is_worker = true;
        states[index].is_main_thread = (main_thread_id == 0 && !main_thread_id);  // First thread is usually main
        states[index].spin_count = states[index].is_main_thread ? ADAPTIVE_MAX_SPINS : ADAPTIVE_MIN_SPINS;
        
        if (states[index].is_main_thread) {
            main_thread_id = tid;
        }
        
        return &states[index];
    }

    void UpdateState(ThreadState* state, bool success, bool waited) {
        if (state->is_main_thread) {
            // Main thread always keep high spin count for FPS
            state->spin_count = ADAPTIVE_MAX_SPINS;
            return;
        }

        if (success) {
            _InterlockedIncrement64(&state->success_count);
            // Worker threads gradually reduce spin count for successful threads
            if (state->spin_count > ADAPTIVE_MIN_SPINS) {
                _InterlockedDecrement64(&state->spin_count);
            }
        } else if (waited) {
            _InterlockedIncrement64(&state->wait_count);
            // Increase spin count if we had to wait
            if (state->spin_count < ADAPTIVE_MAX_SPINS) {
                _InterlockedIncrement64(&state->spin_count);
            }
        }
    }
}; ThreadStateManager g_ThreadManager;

__forceinline void PowerAwarePause(ThreadState* state, unsigned int currentSpin) {
    if (state->is_main_thread) {
        // Main thread very light pausing for FPS
        if (currentSpin < 8) {
            _mm_pause();
        } else if (currentSpin < 32) {
            for (int i = 0; i < 2; ++i) _mm_pause();
        } else {
            for (int i = 0; i < 4; ++i) _mm_pause();
        }
        return;
    }

    // Worker threads more aggressive power saving
    if (currentSpin < 4) {
        _mm_pause();
        return;
    }
    
    if (currentSpin < 16) {
        for (int i = 0; i < 4; ++i) _mm_pause();
        return;
    }

    if ((currentSpin & (YIELD_EVERY_N_SPINS - 1)) == 0) {
        Sleep(0);  // for worker threads
    } else {
        for (int i = 0; i < PAUSE_ITERATIONS; ++i) _mm_pause();
    }
}

__forceinline bool OptimizedWaitOnAddress(volatile void* addr, void* compare_addr, size_t size, DWORD timeout_ms) {
    ThreadState* state = g_ThreadManager.GetState();
    
    // Fast path check
    if (memcmp((void*)addr, compare_addr, size) != 0) {
        g_ThreadManager.UpdateState(state, true, false);
        return true;
    }

    // Get an event for waiting if needed
    HANDLE event = g_EventPool.AcquireEvent();
    ResetEvent(event);

    // Adaptive spinning based on thread history
    unsigned int spinLimit = (unsigned int)state->spin_count;
    
    for (unsigned int spin = 0; spin < spinLimit; ++spin) {
        if (memcmp((void*)addr, compare_addr, size) != 0) {
            g_ThreadManager.UpdateState(state, true, false);
            return true;
        }

        PowerAwarePause(state, spin);
    }

    // If spinning didn't help do a short wait
    bool waited = WaitForSingleObject(event, timeout_ms) == WAIT_TIMEOUT;
    bool result = memcmp((void*)addr, compare_addr, size) != 0;
    
    g_ThreadManager.UpdateState(state, result, waited);
    return result;
}

__forceinline bool OptimizedSynchronization(__int64 a1, unsigned int* a2, unsigned int CompareAddress) {
    _InterlockedExchangeAdd64((volatile signed __int64*)(a1 + 64), 0xFFFFFFFF);

    ThreadState* state = g_ThreadManager.GetState();
    
    // Quick check with pause to prevent memory contention
    if (*(volatile LONG*)a2 != CompareAddress) {
        _InterlockedExchangeAdd64((volatile signed __int64*)(a1 + 64), 1u);
        g_ThreadManager.UpdateState(state, true, false);
        return true;
    }
    _mm_pause();

    // Use thread specific spin count
    unsigned int spinCount = (unsigned int)state->spin_count;
    
    while (true) {
        for (unsigned int i = 0; i < spinCount; ++i) {
            if (*(volatile LONG*)a2 != CompareAddress) {
                _InterlockedExchangeAdd64((volatile signed __int64*)(a1 + 64), 1u);
                g_ThreadManager.UpdateState(state, true, false);
                return true;
            }
            
            PowerAwarePause(state, i);
        }

        if (!OptimizedWaitOnAddress(a2, &CompareAddress, sizeof(CompareAddress), WAIT_TIMEOUT_MS)) {
            spinCount = (std::min)(spinCount * 2, ADAPTIVE_MAX_SPINS);
            continue;
        }

        if (*(volatile LONG*)a2 != CompareAddress) {
            _InterlockedExchangeAdd64((volatile signed __int64*)(a1 + 64), 1u);
            g_ThreadManager.UpdateState(state, true, true);
            return true;
        }
    }
}

bool __fastcall Optimized_sub_575BC0(__int64 a1, unsigned int *a2, int a3, int a4)
{
  int v7; // esi
  int v8; // ebp
  __int64 v10; // r9
  signed __int64 v11; // r10
  signed __int64 v12; // rax
  unsigned __int8 v13; // cl
  signed __int64 v14; // rtt
  unsigned int v15; // ebx
  __int64 v16; // rax
  int v17; // eax
  signed __int32 v18; // eax
  signed __int32 v19; // ett
  __int64 v20; // rax
  unsigned int *v21; // rdx
  __int64 v22; // r8
  __int64 v23; // rdx
  unsigned __int64 v24; // rcx
  unsigned int v25; // edx
  signed __int32 v27[8]; // [rsp+0h] [rbp-78h] BYREF
  unsigned int CompareAddress; // [rsp+30h] [rbp-48h] BYREF
  volatile signed __int64 *v29; // [rsp+38h] [rbp-40h] BYREF
  __int64 v30; // [rsp+40h] [rbp-38h] BYREF
  signed __int64 v31; // [rsp+48h] [rbp-30h]
  unsigned int v32; // [rsp+88h] [rbp+10h] BYREF

  v7 = 0;
  v29 = 0LL;
  v8 = 0;
  while ( 1 )
  {
LABEL_2:
    v10 = *((_QWORD *)a2 + 33) - 1LL;
    *((_QWORD *)a2 + 33) = v10;
    *((_QWORD *)a2 + 32) = v10;
    _InterlockedOr64((volatile signed __int64 *)v27, 0);
    v11 = *((_QWORD *)a2 + 24);
    if ( (unsigned __int64)(v10 - v11) >= 0x1000 )
    {
      *((_QWORD *)a2 + 32) = ++*((_QWORD *)a2 + 33);
    }
    else
    {
      v30 = *(_QWORD *)&a2[2 * (v10 & 0xFFF) + 80];
      v29 = (volatile signed __int64 *)(*(_QWORD *)(a1 + 8) + ((unsigned __int64)(unsigned int)v30 << 7));
      v12 = *v29;
      do
      {
        if ( (_DWORD)v12 != HIDWORD(v30) || (v12 & 0x800000000000000LL) == 0 || !BYTE4(v12) )
        {
          v15 = 0;
          v29 = 0LL;
          v32 = 0;
          goto LABEL_11;
        }
        v13 = BYTE4(v12) - 1;
        v31 = v12;
        BYTE4(v31) = BYTE4(v12) - 1;
        v14 = v12;
        v12 = _InterlockedCompareExchange64(v29, v31, v12);
      }
      while ( v14 != v12 );
      v15 = v13 + 1;
      v32 = v15;
      if ( v15 > 1 )
      {
        *((_QWORD *)a2 + 32) = ++*((_QWORD *)a2 + 33);
        goto LABEL_37;
      }
LABEL_11:
      if ( v11 == v10 )
      {
        _InterlockedCompareExchange64((volatile signed __int64 *)a2 + 24, v11 + 1, v11);
        ++*((_QWORD *)a2 + 33);
        v15 = v32;
        *((_QWORD *)a2 + 32) = *((_QWORD *)a2 + 33);
      }
      if ( v15 )
        goto LABEL_37;
    }
    if ( *(_DWORD *)(a1 + 60) )
      break;
    if ( a4 )
    {
      v8 = *a2;
      v16 = original_5756E0(a1, a2, *a2, &v30, &v29, &v32, CompareAddress);
      if ( v16 )
      {
        v15 = v32;
        v21 = (unsigned int *)v16;
        v22 = v32;
        goto LABEL_40;
      }
    }
    if ( !a3 )
      break;
    v17 = v7++;
    if ( v17 >= 10 )
    {
      v18 = *a2;
      CompareAddress = 0;
      if ( v18 == v8 )
      {
        while ( 1 )
        {
          v19 = v18;
          v18 = _InterlockedCompareExchange((volatile long *)a2, (long)((v8 + 1) | 0xFF800000), (long)v18);
          if ( v19 == v18 )
            break;
          if ( v18 != v8 )

          {
            v7 = 0;
            _mm_pause();
            goto LABEL_2;
          }
        }
        v20 = *((_QWORD *)a2 + 32);
        CompareAddress = (v8 + 1) | 0xFF800000;
        if ( *((_QWORD *)a2 + 24) == v20 )
        {
          OptimizedSynchronization(a1, a2, CompareAddress);
        }
      }
      v7 = 0;
    }
    _mm_pause();
  }
  v15 = v32;
LABEL_37:
  v23 = *((_QWORD *)a2 + 24);
  v24 = *((_QWORD *)a2 + 32) - v23;
  v25 = v23 - *((_QWORD *)a2 + 32);
  if ( v24 <= 0x1000 )
    v25 = v24;
  v22 = v25;
  v21 = a2;
LABEL_40:
  original_5758F0(a1, v21, v22);
  if ( v29 )
    original_5759E0(a1, a2, v29, v30, v15);
  return v29 != 0LL;

}

bool InstallHooks() {
    HMODULE unityModule = GetModuleHandleW(L"UnityPlayer.dll");
    if (!unityModule) {
        LogError("Failed to get UnityPlayer.dll handle");
        return false;
    }

    // validate all function addresses
    void* addr575BC0 = (void*)((uintptr_t)unityModule + 0x575BC0);
    void* addr5756E0 = (void*)((uintptr_t)unityModule + 0x5756E0);
    void* addr5758F0 = (void*)((uintptr_t)unityModule + 0x5758F0);
    void* addr5759E0 = (void*)((uintptr_t)unityModule + 0x5759E0);

    if (!ValidatePointer(addr575BC0, "Invalid 575BC0 address") ||
        !ValidatePointer(addr5756E0, "Invalid 5756E0 address") ||
        !ValidatePointer(addr5758F0, "Invalid 5758F0 address") ||
        !ValidatePointer(addr5759E0, "Invalid 5759E0 address")) {
        return false;
    }

    original_575BC0 = (Original_sub_575BC0)addr575BC0;
    original_5756E0 = (Original_sub_5756E0)addr5756E0;
    original_5758F0 = (Original_sub_5758F0)addr5758F0;
    original_5759E0 = (Original_sub_5759E0)addr5759E0;


    if (!original_575BC0 || !original_5756E0 || !original_5758F0 || !original_5759E0) {
        OutputDebugStringA("[UnityOptimizer] Failed to get all function addresses");
        return false;
    }

    void* hookAddress = (void*)original_575BC0;
    DWORD oldProtect;
    
    if (!VirtualProtect(hookAddress, 12, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        OutputDebugStringA("[UnityOptimizer] Failed to modify memory protection");
        return false;
    }
    
    memcpy(original_bytes, hookAddress, sizeof(original_bytes));
    uint8_t jumpCode[] = {
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, immediate
        0xFF, 0xE0  // jmp rax
    };
    
    *(uintptr_t*)(jumpCode + 2) = (uintptr_t)Optimized_sub_575BC0;
    memcpy(hookAddress, jumpCode, sizeof(jumpCode));
    
    VirtualProtect(hookAddress, 12, oldProtect, &oldProtect);
    OutputDebugStringA("[UnityOptimizer] Hook installed successfully");
    return true;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            OutputDebugStringA("[UnityOptimizer] DLL Loaded");
            InstallHooks();
            break;
            
        case DLL_PROCESS_DETACH: {
            if (original_575BC0) {
                DWORD oldProtect;
                void* hookAddress = (void*)original_575BC0;
                if (VirtualProtect(hookAddress, 12, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    memcpy(hookAddress, original_bytes, sizeof(original_bytes));
                    VirtualProtect(hookAddress, 12, oldProtect, &oldProtect);
                }
            }
            OutputDebugStringA("[UnityOptimizer] DLL Unloaded");
            break;
        }
    }
    return TRUE;
}
