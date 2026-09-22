################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../modbus/mb_slave.c 

C_DEPS += \
./modbus/mb_slave.d 

OBJS += \
./modbus/mb_slave.o 

DIR_OBJS += \
./modbus/*.o \

DIR_DEPS += \
./modbus/*.d \

DIR_EXPANDS += \
./modbus/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
modbus/%.o: ../modbus/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -fdump-rtl-expand -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/User" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/NetLib" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Core" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Debug" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Peripheral/inc" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/https" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/melsec_fx" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/modbus" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

