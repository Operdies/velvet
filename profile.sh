#!/usr/bin/env sh

# based on:
# https://stackoverflow.com/questions/73922182/how-to-do-profiling-using-clang-compiler-and-cmake
make profile -j
LLVM_PROFILE_FILE="vv.profraw" ./profile/vv "$@"
llvm-profdata merge -sparse vv.profraw -o vv.profdata

llvm-cov show ./profile/vv -instr-profile=vv.profdata > vv.show
llvm-cov report ./profile/vv -instr-profile=vv.profdata
