################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################
-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include modbus/subdir.mk
-include melsec_fx/subdir.mk
-include https/subdir.mk
-include User/subdir.mk
-include Startup/subdir.mk
-include Peripheral/src/subdir.mk
-include NetLib/subdir.mk
-include Debug/subdir.mk
-include Core/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
ETH_UART.hex \

SECONDARY_LIST += \
ETH_UART.lst \

SECONDARY_SIZE += \
ETH_UART.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: ETH_UART.elf secondary-outputs

# Tool invocations
ETH_UART.elf: $(OBJS) $(USER_OBJS_ESCAPE)
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -T "d:\XQ_Work\CH32_ETH\CH32_V307_ETH\ETH_UART\Ld\Link.ld" -nostartfiles -Xlinker --gc-sections -L"d:\XQ_Work\CH32_ETH\CH32_V307_ETH\NetLib" -Wl,-Map,"ETH_UART.map" --specs=nano.specs --specs=nosys.specs -o "ETH_UART.elf" $(OBJS) $(LIBS) -lprintf
ETH_UART.hex: ETH_UART.elf
	@	riscv-none-embed-objcopy -O ihex "ETH_UART.elf" "ETH_UART.hex"
ETH_UART.lst: ETH_UART.elf
	@	riscv-none-embed-objdump --source --all-headers --demangle -M xw --line-numbers --wide "ETH_UART.elf" > "ETH_UART.lst"
ETH_UART.siz: ETH_UART.elf
	riscv-none-embed-size --format=berkeley "ETH_UART.elf"

# Other Targets
clean:
	-$(RM) $(DIR_OBJS) $(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE) ETH_UART.elf
	-$(RM) $(DIR_EXPANDS) $(CALLGRAPH_DOT)
	-$(RM) $(DIR_DEPS)
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets