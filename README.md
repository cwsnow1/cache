# Cache Simulator in C

Reads and parse memory access trace file produced by [Intel's pin tool](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html) and runs it through a cache simulation.

## Quick start Guide
Download [Intel's pin tool](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html), update the path to it in ./setup.sh, and run
```
$ sudo apt-get install clang cmake python
$ ./setup.sh
```
This will build the pinatrace tool and then record all the memory accesses (i.e. reads and writes) when running the command
```
$ ls -l
```
and move the trace file to this directory. It then builds the program and simulates the cache using the produced trace.  
In the <code>./build</code> directory a file called <code>test_params.ini</code> will be created that can be used to vary the parameters of the simulation. A recompile is not necessary after changing this file.

## Custom Traces
You can make your own trace files using the pin tool. A few simple programs are provided that can be used with the pin tool to make more traces.
They are compiled alongside the main build and can be found in the <code>./build/trace_generation</code> directory.  
Run the pin tool using the produced program with this command
```
$ <pin path>/pin -t <pin path>source/tools/ManualExamples/obj-intel64/pinatrace.so -- <program> [program args]
```
.trace files are ignored by git, so I recommend moving them out of the build directory so that they aren't deleted during clean builds.  
 Better documentation for pin can be found on its site.

## Normal Operation
The ./build.py script has been made for normal building. It takes the optional arguments
```
  -b, --build <Debug/Release>
  -c, --clean
  -S, --sim-trace
  -C, --console-print
```
Sim trace and console print are explained below. Debug is the default build type.  

To run the program, the command is
```
$ ./cache <tracefile> [output file]
```
If an output file is specified, the statistics of each config simluated will be output to that file rather than to the console and a csv with the same stats will be generated.
## Console Print
If <code>--console-print</code> is passed to <code>build.py</code>, the program will step through the simulation one clock cycle at a time with consle prints describing the processing. Example:
```
====================
TICK 0000001172
====================

Cache[0] New request added at index 5, call back at tick 1175
Cache[0] Trying request 3, addr=0x7f4749971980
Cache[0] set 51 is busy
Cache[0] Trying request 0, addr=0x7f47499719b8
Cache[0] set 51 is busy
Cache[0] Trying request 1, addr=0x7f4749944c78
Cache[0] hit, set=24
Cache[0] Trying request 7, addr=0x7f47499719f0
2/3 cycles for this operation in cache_level=0
Cache[0] next useful cycle set to 1173
Cache[0] Trying request 6, addr=0x7f4749944c80
1/3 cycles for this operation in cache_level=0
Cache[0] Trying request 5, addr=0x7f4749944c88
0/3 cycles for this operation in cache_level=0

Cache[1] Trying request 13, addr=0x7f4749971980
7/12 cycles for this operation in cache_level=1
```
This mode is not recommended if multiple sim threads are running. Change <code>./build/test_params.ini:8</code> to <code>MAX_NUM_THREADS=1</code> to run with a single thread.
## Sim Trace
If <code>--sim-trace</code> is passed to <code>build.py</code>, the program will produce a binary file that can be decoded by the decoder in <code>./build/sim_trace_decoder</code>. The text file(s) will contain a trace of the operation of the cache simulation. These files can be large, so I recommend using a log viewing program such as [glogg](https://github.com/nickbnf/glogg) or [klogg](https://klogg.filimonov.dev/).  
Example excerpt of a sim trace:
```
Cycle           Cache level     Message
=============================================================
000018569992    0               ACCESS_BEGIN:  pool_index=09, r, block_address=0x00fe8e690eb2, set_index=0x00000032
000018569992    0               LRU_UPDATE:    set_index=0x00000032, MRU: block_index=0x01, LRU: block_index=0x00
000018569992    0               ACCESS_BEGIN:  pool_index=08, r, block_address=0x00fe8e690eb2, set_index=0x00000032
000018569992    0               LRU_UPDATE:    set_index=0x00000032, MRU: block_index=0x01, LRU: block_index=0x00
000018569992    0               ACCESS_BEGIN:  pool_index=03, r, block_address=0x00fe8e690eb3, set_index=0x00000033
000018569992    0               MISS:          pool_index=03, requesting block in set_index=0x00000033
000018569992    0               EVICT:         set_index=0x00000033, block_index=0x00
000018569992    1               REQUEST_ADDED: pool_index=00, addr=0x7f4734875980, access_time=12
000018569992    0               ACCESS_BEGIN:  pool_index=01, w, block_address=0x00ac680a75d3, set_index=0x00000013
000018569992    0               MISS:          pool_index=01, requesting block in set_index=0x00000013
000018569992    1               REQUEST_ADDED: pool_index=29, addr=0x5634053ac980, access_time=12
000018569992    0               EVICT:         set_index=0x00000013, block_index=0x01
```
