#!/usr/bin/python3
import os
import argparse
import shutil

build_dir = "./build"

def parseArgs():
    parser = argparse.ArgumentParser()
    parser.add_argument('-b', '--build', dest="build_type", default="Debug")
    parser.add_argument('-c', '--clean', action='store_true')
    parser.add_argument('-S', '--sim-trace', action='store_true')
    parser.add_argument('-C', '--console-print', action='store_true')

    args = parser.parse_args()
    return args

if __name__ == "__main__":
    args = parseArgs()
    print(args)
    defines = []
    if args.clean:
        if os.path.isdir(build_dir):
            print("Clean build. Deleting " + build_dir)
            shutil.rmtree(build_dir)
            os.mkdir(build_dir)
    else:
        if not os.path.isdir(build_dir):
            os.mkdir(build_dir)
    if args.console_print:
        defines.append("-DCONSOLE_PRINT=1")
    if args.sim_trace:
        defines.append("-DSIM_TRACE=1")
    else:
        defines.append("-DSIM_TRACE=0")
    if args.build_type == "Release":
        defines.append("-DCMAKE_C_BUILD_TYPE=Release")
    else:
        if args.build_type != "Debug":
            print("Invalid build type specified, defaulting to Debug")
        defines.append("-DCMAKE_C_BUILD_TYPE=Debug")
    os.system('cmake -S . -B ' + build_dir + ' ' + ''.join(str(x) for x in defines))

