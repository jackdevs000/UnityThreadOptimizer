# UnityOptimizer

A performance optimization DLL that enhances thread synchronization in Unity applications through intelligent hooking of UnityPlayer.dll's synchronization primitives.

## Overview

UnityOptimizer replaces Unity's default thread synchronization mechanisms (`WaitOnAddress` and `WakeByAddressSingle`) with optimized implementations featuring:

- Adaptive spinning with configurable limits
- Power-aware CPU pausing
- Thread-aware performance adjustments
- Reduced CPU overhead while maintaining frame rate stability

## Motivation

During testing, it was observed that Unity spawns a number of threads roughly equal to the number of logical processors. This behavior can lead to CPU resource fragmentation and suboptimal performance in high-load scenarios. These observations motivated an investigation into the underlying synchronization mechanism, leading to the development of custom optimizations to reduce CPU usage while preserving or enhancing FPS.

Profiling revealed that a significant portion of CPU time in Unity games under load is spent in synchronization primitives within UnityPlayer.dll. Further analysis using stack traces pinpointed the function at UnityPlayer.dll+0x575BC0 as a frequently called synchronization point across multiple worker threads. This suggested that optimizing this specific function could markedly reduce overall CPU usage and yield noticeable performance improvements.

## Implementation

- **Function Hooking:** The DLL hooks the sub_575BC0 function within UnityPlayer.dll (at address `UnityPlayer.dll+0x575BC0`). This interception redirects execution flow to the OptimizedSynchronization function within the DLL, thereby applying custom, optimized synchronization behavior.
- **Custom Synchronization Routines:** The DLL implements custom synchronization routines, OptimizedWaitOnAddress and OptimizedSynchronization, which replace the original synchronization logic. These routines do not directly replace Windows API functions like WaitOnAddress and WakeByAddressSingle; rather, they implement similar synchronization concepts with enhanced mechanisms, featuring:
  - Adaptive spinning with configurable spin limits based on thread performance metrics.
  - Power-aware pausing using `_mm_pause` and selective yielding to balance responsiveness with reduced CPU consumption.
  - Dynamic adjustments to spinning behavior based on historical success and wait metrics tracked through a thread-aware state manager.
- **Low-Level Techniques:** Leverages Windows API calls and interlocked operations for efficient, thread-safe synchronization.

## Debugging & Performance Analysis

Extensive debugging and performance metrics were gathered to assess the impact of the custom optimizations:

- **Instrumentation:** Detailed logs were collected from various systems, capturing per-thread CPU usage, frame rates, and execution addresses.
- **Stack Analysis & Reverse Engineering:** Thread stack captures consistently revealed the address `0x575DFC` across multiple threads. Analysis using IDA64 provided pseudocode of the synchronization function, unveiling the use of interlocked operations and calls to WaitOnAddress and WakeByAddressSingle. These findings confirmed that the synchronization mechanism was a significant contributor to CPU overhead.
- **Offline Testing:** In controlled environments, individual thread CPU usage dropped from approximately 0.40% to as low as 0.07–0.08%, with FPS improvements of up to 100–200 FPS.
- **Real-World Observations:** In live game scenarios, the behavior was more variable:
  - On some systems, while CPU usage was substantially reduced, FPS performance dropped by approximately 10–20 FPS (e.g., from 50–70 FPS down to 40–30 FPS), suggesting a trade-off between CPU efficiency and rendering performance.
  - Hardware differences (such as 6-core versus 32-thread systems) led to varied outcomes, with some configurations showing modest performance gains and others experiencing FPS losses depending on the setup of our optimizations.
- **Sample Debug Logs:**
  - **Laptop (Pre-Injection):** Measurements included values such as `0.70%` indicating a higher CPU load per thread across all threads that were targeted.
  - **Laptop (Post-Injection):** Logs showed reductions in CPU overhead, with some thread readings dropping to as low as `0.03%` in certain contexts up to `0.08%` in others showing a massive improvement.
  - **High-End PC (14900K):** Overall CPU usage dropped from roughly 25–33% to about 11% after injection, though FPS responses were not always consistent.

### Example Debug Logs (14900K)

**Pre-Injection Logs:**

```
TID,   CPU%,  Cycle Deltas,  Thread StartAddress,    Priority
26448, 2.76,  2,813,029,135, UnityPlayer.dll+0x6b5660, Lowest
24272, 1.21,  1,231,200,647, UnityPlayer.dll+0x6b5660, Normal
22152, 1.16,  1,184,604,998, UnityPlayer.dll+0x6b5660, Normal
23756, 1.10,  1,124,099,447, UnityPlayer.dll+0x6b5660, Normal
20736, 1.01,  1,029,909,765, UnityPlayer.dll+0x6b5660, Normal
22564, 0.94,  956,571,841,   UnityPlayer.dll+0x6b5660, Normal
5624,  0.89,  902,865,480,   UnityPlayer.dll+0x6b5660, Normal
27144, 0.79,  801,308,782,   UnityPlayer.dll+0x6b5660, Normal
18576, 0.76,  772,018,765,   UnityPlayer.dll+0x6b5660, Normal
4648,  0.70,  709,294,565,   UnityPlayer.dll+0x6b5660, Normal
17112, 0.68,  689,498,301,   UnityPlayer.dll+0x6b5660, Normal
26836, 0.60,  608,788,278,   UnityPlayer.dll+0x6b5660, Normal
14864, 0.54,  554,523,343,   UnityPlayer.dll+0x6b5660, Normal
17348, 0.53,  541,107,979,   UnityPlayer.dll+0x6b5660, Normal
22184, 0.49,  496,363,023,   UnityPlayer.dll+0x6b5660, Normal
27092, 0.48,  484,528,156,   UnityPlayer.dll+0x6b5660, Normal
26588, 0.47,  479,414,814,   UnityPlayer.dll+0x6b5660, Normal
14516, 0.46,  469,954,069,   UnityPlayer.dll+0x6b5660, Normal
20196, 0.46,  469,242,857,   UnityPlayer.dll+0x6b5660, Normal
17340, 0.46,  464,789,338,   UnityPlayer.dll+0x6b5660, Normal
26260, 0.45,  462,309,002,   UnityPlayer.dll+0x6b5660, Normal
24156, 0.45,  461,959,126,   UnityPlayer.dll+0x6b5660, Normal
13920, 0.45,  459,598,406,   UnityPlayer.dll+0x6b5660, Normal
26272, 0.45,  455,734,460,   UnityPlayer.dll+0x6b5660, Normal
27036, 0.44,  452,588,760,   UnityPlayer.dll+0x6b5660, Normal
27140, 0.44,  447,728,435,   UnityPlayer.dll+0x6b5660, Normal
15688, 0.44,  447,611,288,   UnityPlayer.dll+0x6b5660, Normal
26244, 0.44,  446,648,164,   UnityPlayer.dll+0x6b5660, Normal
14588, 0.43,  440,486,039,   UnityPlayer.dll+0x6b5660, Normal
9984,  0.42,  423,720,243,   UnityPlayer.dll+0x6b5660, Normal
26552, 0.40,  408,383,019,   UnityPlayer.dll+0x6b5660, Normal
20420, 0.39,  393,370,359,   UnityPlayer.dll+0x6b5660, Normal
24536, 0.38,  386,091,378,   UnityPlayer.dll+0x6b5660, Normal
```

**Post-Injection Logs:**

```
TID,   CPU%,  Cycle Deltas,  Thread StartAddress,    Priority
26448, 3.06, 3,089,514,451,   UnityPlayer.dll+0x6b5660, Lowest
5624,  1.78, 1,795,350,169,   UnityPlayer.dll+0x6b5660, Normal
22152, 1.32, 1,334,362,010,   UnityPlayer.dll+0x6b5660, Normal
22564, 0.07, 68,228,212,      UnityPlayer.dll+0x6b5660, Normal
26588, 0.07, 67,791,951,      UnityPlayer.dll+0x6b5660, Normal
17112, 0.07, 66,263,200,      UnityPlayer.dll+0x6b5660, Normal
14516, 0.07, 66,258,581,      UnityPlayer.dll+0x6b5660, Normal
27144, 0.07, 65,992,181,      UnityPlayer.dll+0x6b5660, Normal
22184, 0.07, 65,965,399,      UnityPlayer.dll+0x6b5660, Normal
26836, 0.06, 65,564,388,      UnityPlayer.dll+0x6b5660, Normal
17340, 0.06, 65,391,384,      UnityPlayer.dll+0x6b5660, Normal
18576, 0.06, 64,563,647,      UnityPlayer.dll+0x6b5660, Normal
17348, 0.06, 64,321,335,      UnityPlayer.dll+0x6b5660, Normal
24272, 0.06, 64,205,580,      UnityPlayer.dll+0x6b5660, Normal
20196, 0.06, 64,039,384,      UnityPlayer.dll+0x6b5660, Normal
27092, 0.06, 63,954,267,      UnityPlayer.dll+0x6b5660, Normal
23756, 0.06, 62,932,183,      UnityPlayer.dll+0x6b5660, Normal
20736, 0.06, 62,440,481,      UnityPlayer.dll+0x6b5660, Normal
20420, 0.06, 61,407,976,      UnityPlayer.dll+0x6b5660, Normal
4648,  0.06, 61,331,768,      UnityPlayer.dll+0x6b5660, Normal
26552, 0.06, 60,731,653,      UnityPlayer.dll+0x6b5660, Normal
24156, 0.06, 60,681,115,      UnityPlayer.dll+0x6b5660, Normal
14864, 0.06, 60,352,844,      UnityPlayer.dll+0x6b5660, Normal
9984,  0.06, 59,737,637,      UnityPlayer.dll+0x6b5660, Normal
15688, 0.06, 59,418,306,      UnityPlayer.dll+0x6b5660, Normal
13920, 0.06, 59,369,492,      UnityPlayer.dll+0x6b5660, Normal
24536, 0.06, 58,816,987,      UnityPlayer.dll+0x6b5660, Normal
26244, 0.06, 58,713,959,      UnityPlayer.dll+0x6b5660, Normal
27036, 0.06, 58,530,906,      UnityPlayer.dll+0x6b5660, Normal
26272, 0.06, 58,391,608,      UnityPlayer.dll+0x6b5660, Normal
26260, 0.06, 57,460,201,      UnityPlayer.dll+0x6b5660, Normal
27140, 0.06, 57,449,891,      UnityPlayer.dll+0x6b5660, Normal
14588, 0.05, 55,110,723,      UnityPlayer.dll+0x6b5660, Normal
```

*CPU usage dropped from approximately 25% pre-injection to around 11% post-injection, focusing solely on the UnityPlayer.dll+0x6b5660 metrics.*

## Limitations

- **Inconsistent Behavior and Potential FPS Trade-off:** While offline tests showed substantial CPU reductions with fps gains, real-world testing revealed that FPS could drop by 10–20 FPS in some cases, highlighting a trade-off between lower CPU usage and rendering performance.
- **Hardware Sensitivity:** The effectiveness of the optimizations varies across different CPU architectures and thread configurations.
- **Experimental Nature:** This approach remains experimental and may require further tuning and refinement to ensure consistent performance gains without adverse effects.

## Contributing

This is an experimental project open to improvements. Contributions from performance tuning experts are particularly welcome.

## Conclusion

UnityOptimizer demonstrates a practical approach to enhancing thread synchronization in Unity applications. Offline testing shows that our custom synchronization routines can effectively reduce CPU usage. However, real-world performance remains variable, with FPS improvements not always meeting expectations. This variability highlights the challenges of optimizing complex systems, and further fine-tuning may be necessary to achieve consistent results in live environments.