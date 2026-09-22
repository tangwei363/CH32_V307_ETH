################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/bsp_flash.c \
../User/bsp_rtc.c \
../User/bsp_uart.c \
../User/bsp_wch_net.c \
../User/ch32v30x_it.c \
../User/ethernet_app.c \
../User/main.c \
../User/sntp.c \
../User/system_ch32v30x.c \
../User/xqBufferManage.c \
../User/xqLoopList.c 

C_DEPS += \
./User/bsp_flash.d \
./User/bsp_rtc.d \
./User/bsp_uart.d \
./User/bsp_wch_net.d \
./User/ch32v30x_it.d \
./User/ethernet_app.d \
./User/main.d \
./User/sntp.d \
./User/system_ch32v30x.d \
./User/xqBufferManage.d \
./User/xqLoopList.d 

OBJS += \
./User/bsp_flash.o \
./User/bsp_rtc.o \
./User/bsp_uart.o \
./User/bsp_wch_net.o \
./User/ch32v30x_it.o \
./User/ethernet_app.o \
./User/main.o \
./User/sntp.o \
./User/system_ch32v30x.o \
./User/xqBufferManage.o \
./User/xqLoopList.o 

DIR_OBJS += \
./User/*.o \

DIR_DEPS += \
./User/*.d \

DIR_EXPANDS += \
./User/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/User" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/NetLib" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Core" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Debug" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Peripheral/inc" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/https" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/melsec_fx" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/modbus" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

