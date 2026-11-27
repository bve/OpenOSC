# Arm CMSIS-DAP

DAP command and SWD transfer sources from [Arm CMSIS-DAP v2.1.2](https://github.com/ARM-software/CMSIS-DAP/tree/1fd47bed772ea40923472c90dfe11516e76033ee). The upstream license is in this directory. The adjacent ESP32-C3 port supplies GPIO functions and validates packets before calling the Arm command processor.

The only upstream change is a guard in `DAP.h` for the RISC-V clock delay implementation in `DAP_config.h`.
