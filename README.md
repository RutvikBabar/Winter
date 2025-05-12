# Winter

**Winter** aims to be an ultra–low-latency C++20 framework designed to let you:

- **Warm your cache** up front  
- Leverage **`constexpr`** to push computations to compile time
- Avoid Locks -Hurt the cache *ALOT.
- Emit machine code on the fly via a simple **JIT compiler**  

…and hit **microsecond-level** strategy execution on a **dummy socket** feeding you historical tick data.

---



- **Cache Warming**  
  Pre-loads your hot-path data into L1 so you never stall on a cold line.

- **`constexpr` Everywhere**  
  Push as much work as possible into compile time for zero-overhead runtime.

- **Dummy Socket Engine**  
  Replay historical trade feeds in real time, so you can back-test strategies in a realistic environment.

- **Microsecond-Scale Metrics**  
  Instrument every stage from parse → compute → emit with sub-µs timing.

---

How to use?
Clone the repo.
use "make" command.
./build/simulate to start the simulation, by default it starts with a 100,000$ budget and utilizes a mean-reversion trading strategy. 

once the code starts running, it will wait for the dummy socket data feed. 
start the dummy socket python code. 
