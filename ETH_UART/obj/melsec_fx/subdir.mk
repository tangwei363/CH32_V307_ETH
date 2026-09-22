################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../melsec_fx/melsec_fx_core.c \
../melsec_fx/melsec_fx_mapping.c \
../melsec_fx/melsec_fx_net.c \
../melsec_fx/melsec_fx_tables.c 

C_DEPS += \
./melsec_fx/melsec_fx_core.d \
./melsec_fx/melsec_fx_mapping.d \
./melsec_fx/melsec_fx_net.d \
./melsec_fx/melsec_fx_tables.d 

OBJS += \
./melsec_fx/melsec_fx_core.o \
./melsec_fx/melsec_fx_mapping.o \
./melsec_fx/melsec_fx_net.o \
./melsec_fx/melsec_fx_tables.o 

DIR_OBJS += \
./melsec_fx/*.o \

DIR_DEPS += \
./melsec_fx/*.d \

DIR_EXPANDS += \
./melsec_fx/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
melsec_fx/%.o: ../melsec_fx/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/User" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/NetLib" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Core" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Debug" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Peripheral/inc" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/https" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/melsec_fx" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/modbus" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

