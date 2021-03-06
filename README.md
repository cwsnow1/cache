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
## Sim Trace
If <code>SIM_TRACE</code> is defined in <code>./inc/sim_trace.h</code>, the program will produce a binary file that can be decoded by the decoder in <code>./sim_trace_decoder</code>. The text file(s) will contain a trace of the operation of the cache simulation. These files can be large, so I recommend using a log viewing program such as [glogg](https://github.com/nickbnf/glogg) or [klogg](https://klogg.filimonov.dev/).
