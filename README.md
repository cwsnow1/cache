# Cache Simulator in C

Reads and parse memory access trace file produced by [Intel's pin tool](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-dynamic-binary-instrumentation-tool.html) and runs it through a cache simulation.

## Quick start Guide
Download [Intel's pin tool](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html), update the path to it in ./setup.sh, and run
```
$ ./setup.sh
```
This will build the pinatrace tool and then record all the memory accesses (i.e. reads and writes) when running the command
```
$ ls -l
```
and move the trace file to this directory. It then build the program and simulate the cache using the produced trace.

## Custom Traces
You can make your own trace files using the pin tool. A few simple programs are provided that can be used with the pin tool to make more traces.
To make those programs, simply run
```
$ make
```
in the directory of the program you wish to make, then run the pin tool using the produced program. Better documentation for pin can be found on its site, but the basic command can be copied in ./setup.sh.

## Normal Operation
The ./build.sh script has been made for normal building. It takes the arguments
```
$ ./build.sh
$ ./build.sh -c
$ ./build.sh debug
```
For a -O3 build, a clean -O3 build, and a -g build, respectively. Since it builds in a few milliseconds, I usually use the clean build every time. This script could be improved in the future.

To run the program, the command is
```
$ ./cache <tracefile>
```
## Console Print
If <code>CONSOLE_PRINT</code> is defined as <code>1</code> in <code>./inc/cache.h</code>, the program will step through the simulation one clock cycle at a time with consle prints describing the processing. Example:
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
## Sim Trace
If <code>SIM_TRACE</code> is defined in <code>./inc/sim_trace.h</code>, the program will produce a binary file that can be decoded by the decoder in <code>./sim_trace_decoder</code>. The text file(s) will contain a trace of the operation of the cache simulation. These files can be large, so I recommend using a log viewing program such as [glogg](https://github.com/nickbnf/glogg) or [klogg](https://klogg.filimonov.dev/).
