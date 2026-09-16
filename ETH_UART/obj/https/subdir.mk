################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../https/HTTPS.c \
../https/fifo_queue.c \
../https/fx_acclog.c \
../https/fx_devmon.c \
../https/fx_enetinf.c \
../https/fx_plcinf.c \
../https/fx_status.c \
../https/html_components.c \
../https/index.c \
../https/sx_stream.c 

C_DEPS += \
./https/HTTPS.d \
./https/fifo_queue.d \
./https/fx_acclog.d \
./https/fx_devmon.d \
./https/fx_enetinf.d \
./https/fx_plcinf.d \
./https/fx_status.d \
./https/html_components.d \
./https/index.d \
./https/sx_stream.d 

OBJS += \
./https/HTTPS.o \
./https/fifo_queue.o \
./https/fx_acclog.o \
./https/fx_devmon.o \
./https/fx_enetinf.o \
./https/fx_plcinf.o \
./https/fx_status.o \
./https/html_components.o \
./https/index.o \
./https/sx_stream.o 

DIR_OBJS += \
./https/*.o \

DIR_DEPS += \
./https/*.d \

DIR_EXPANDS += \
./https/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
https/%.o: ../https/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/User" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/NetLib" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Core" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Debug" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/Peripheral/inc" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/https" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/melsec_fx" -I"d:/XQ_Work/CH32_ETH/CH32_V307_ETH/ETH_UART/modbus" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

