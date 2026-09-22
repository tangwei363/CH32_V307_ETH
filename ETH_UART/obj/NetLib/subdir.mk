################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
d:/XQ_Work/CH32_ETH/CH32_V307_ETH/NetLib/eth_driver_10M.c 

C_DEPS += \
./NetLib/eth_driver_10M.d 

OBJS += \
./NetLib/eth_driver_10M.o 

DIR_OBJS += \
./NetLib/*.o \

DIR_DEPS += \
./NetLib/*.d \

DIR_EXPANDS += \
./NetLib/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
NetLib/eth_driver_10M.o: d:/XQ_Work/CH32_ETH/CH32_V307_ETH/NetLib/eth_driver_10M.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/User" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/NetLib" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Core" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Debug" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Peripheral/inc" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/https" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/melsec_fx" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/modbus" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

