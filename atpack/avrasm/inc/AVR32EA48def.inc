;***** THIS IS A MACHINE GENERATED FILE - DO NOT EDIT ********************

;*************************************************************************
;* A P P L I C A T I O N   N O T E   F O R   T H E   A V R   F A M I L Y
;*
;* Number            : AVR000
;* File Name         : AVR32EA48def.inc
;* Title             : Register/Bit Definitions for the AVR32EA48
;* Created           : 2023-06-26 11:39
;* Version           : 1.00
;* Support           : https://microchipsupport.force.com/
;* Target MCU        : AVR32EA48
;*
;* DESCRIPTION
;* When including this file in the assembly program file, all I/O register
;* names and I/O register bit names appearing in the data book can be used.
;* In addition, the six registers forming the three data pointers X, Y and
;* Z have been assigned names XL - ZH. Highest RAM address for Internal
;* SRAM is also defined
;*
;*************************************************************************

#ifndef _AVR32EA48DEF_INC_
#define _AVR32EA48DEF_INC_


#pragma partinc 0

; ***** SPECIFY DEVICE ***************************************************
.device	AVR32EA48

#pragma AVRPART ADMIN PART_NAME AVR32EA48

.equ	SIGNATURE_000	= 0x1E
.equ	SIGNATURE_001	= 0x95
.equ	SIGNATURE_002	= 0x3C

#pragma AVRPART CORE CORE_VERSION V4S


; ***** ABSOLUTE I/O REGISTER LOCATIONS **********************************


;*************************************************************************
;** AC0 - Analog Comparator
;*************************************************************************

.equ AC0_CTRLA = 0x0680                  ; Control A
.equ AC0_CTRLB = 0x0681                  ; Control B
.equ AC0_MUXCTRL = 0x0682                ; Mux Control A
.equ AC0_DACREF = 0x0685                 ; DAC Voltage Reference
.equ AC0_INTCTRL = 0x0686                ; Interrupt Control
.equ AC0_STATUS = 0x0687                 ; Status

;*************************************************************************
;** AC1 - Analog Comparator
;*************************************************************************

.equ AC1_CTRLA = 0x0688                  ; Control A
.equ AC1_CTRLB = 0x0689                  ; Control B
.equ AC1_MUXCTRL = 0x068A                ; Mux Control A
.equ AC1_DACREF = 0x068D                 ; DAC Voltage Reference
.equ AC1_INTCTRL = 0x068E                ; Interrupt Control
.equ AC1_STATUS = 0x068F                 ; Status

;*************************************************************************
;** ADC0 - Analog to Digital Converter
;*************************************************************************

.equ ADC0_CTRLA = 0x0600                 ; Control A
.equ ADC0_CTRLB = 0x0601                 ; Control B
.equ ADC0_CTRLC = 0x0602                 ; Control C
.equ ADC0_CTRLD = 0x0603                 ; Control D
.equ ADC0_INTCTRL = 0x0604               ; Interrupt Control
.equ ADC0_INTFLAGS = 0x0605              ; Interrupt Flags
.equ ADC0_STATUS = 0x0606                ; Status register
.equ ADC0_DBGCTRL = 0x0607               ; Debug Control
.equ ADC0_CTRLE = 0x0608                 ; Control E
.equ ADC0_CTRLF = 0x0609                 ; Control F
.equ ADC0_COMMAND = 0x060A               ; Command register
.equ ADC0_PGACTRL = 0x060B               ; PGA Control
.equ ADC0_MUXPOS = 0x060C                ; Positive Input Multiplexer
.equ ADC0_MUXNEG = 0x060D                ; Negative Input Multiplexer
.equ ADC0_RESULT = 0x0610                ; Result
.equ ADC0_RESULT0 = 0x0610               ; Result 0
.equ ADC0_RESULT1 = 0x0611               ; Result 1
.equ ADC0_RESULT2 = 0x0612               ; Result 2
.equ ADC0_RESULT3 = 0x0613               ; Result 3
.equ ADC0_SAMPLE = 0x0614                ; Sample
.equ ADC0_SAMPLEL = 0x0614               ; Sample low byte
.equ ADC0_SAMPLEH = 0x0615               ; Sample hi byte
.equ ADC0_TEMP0 = 0x0618                 ; Temporary Data 0
.equ ADC0_TEMP1 = 0x0619                 ; Temporary Data 1
.equ ADC0_TEMP2 = 0x061A                 ; Temporary Data 2
.equ ADC0_WINLT = 0x061C                 ; Window Low Threshold
.equ ADC0_WINLTL = 0x061C                ; Window Low Threshold low byte
.equ ADC0_WINLTH = 0x061D                ; Window Low Threshold hi byte
.equ ADC0_WINHT = 0x061E                 ; Window High Threshold
.equ ADC0_WINHTL = 0x061E                ; Window High Threshold low byte
.equ ADC0_WINHTH = 0x061F                ; Window High Threshold hi byte

;*************************************************************************
;** BOD - Bod interface
;*************************************************************************

.equ BOD_CTRLA = 0x00A0                  ; Control A
.equ BOD_CTRLB = 0x00A1                  ; Control B
.equ BOD_VLMCTRLA = 0x00A8               ; Voltage level monitor Control
.equ BOD_INTCTRL = 0x00A9                ; Voltage level monitor interrupt Control
.equ BOD_INTFLAGS = 0x00AA               ; Voltage level monitor interrupt Flags
.equ BOD_STATUS = 0x00AB                 ; Voltage level monitor status

;*************************************************************************
;** CCL - Configurable Custom Logic
;*************************************************************************

.equ CCL_CTRLA = 0x01C0                  ; Control Register A
.equ CCL_SEQCTRL0 = 0x01C1               ; Sequential Control 0
.equ CCL_SEQCTRL1 = 0x01C2               ; Sequential Control 1
.equ CCL_INTCTRL0 = 0x01C5               ; Interrupt Control 0
.equ CCL_INTFLAGS = 0x01C7               ; Interrupt Flags
.equ CCL_LUT0CTRLA = 0x01C8              ; LUT 0 Control A
.equ CCL_LUT0CTRLB = 0x01C9              ; LUT 0 Control B
.equ CCL_LUT0CTRLC = 0x01CA              ; LUT 0 Control C
.equ CCL_TRUTH0 = 0x01CB                 ; Truth 0
.equ CCL_LUT1CTRLA = 0x01CC              ; LUT 1 Control A
.equ CCL_LUT1CTRLB = 0x01CD              ; LUT 1 Control B
.equ CCL_LUT1CTRLC = 0x01CE              ; LUT 1 Control C
.equ CCL_TRUTH1 = 0x01CF                 ; Truth 1
.equ CCL_LUT2CTRLA = 0x01D0              ; LUT 2 Control A
.equ CCL_LUT2CTRLB = 0x01D1              ; LUT 2 Control B
.equ CCL_LUT2CTRLC = 0x01D2              ; LUT 2 Control C
.equ CCL_TRUTH2 = 0x01D3                 ; Truth 2
.equ CCL_LUT3CTRLA = 0x01D4              ; LUT 3 Control A
.equ CCL_LUT3CTRLB = 0x01D5              ; LUT 3 Control B
.equ CCL_LUT3CTRLC = 0x01D6              ; LUT 3 Control C
.equ CCL_TRUTH3 = 0x01D7                 ; Truth 3

;*************************************************************************
;** CLKCTRL - Clock controller
;*************************************************************************

.equ CLKCTRL_MCLKCTRLA = 0x0060          ; MCLK Control A
.equ CLKCTRL_MCLKCTRLB = 0x0061          ; MCLK Control B
.equ CLKCTRL_MCLKCTRLC = 0x0062          ; MCLK Control C
.equ CLKCTRL_MCLKINTCTRL = 0x0063        ; MCLK Interrupt Control
.equ CLKCTRL_MCLKINTFLAGS = 0x0064       ; MCLK Interrupt Flags
.equ CLKCTRL_MCLKSTATUS = 0x0065         ; MCLK Status
.equ CLKCTRL_MCLKTIMEBASE = 0x0066       ; MCLK Timebase
.equ CLKCTRL_OSCHFCTRLA = 0x0068         ; OSCHF Control A
.equ CLKCTRL_OSCHFTUNE = 0x0069          ; OSCHF Tune
.equ CLKCTRL_OSC32KCTRLA = 0x0078        ; OSC32K Control A
.equ CLKCTRL_XOSC32KCTRLA = 0x007C       ; XOSC32K Control A
.equ CLKCTRL_XOSCHFCTRLA = 0x0080        ; XOSCHF Control A

;*************************************************************************
;** CPU - CPU
;*************************************************************************

.equ CPU_CCP = 0x0034                    ; Configuration Change Protection
.equ CPU_SP = 0x003D                     ; Stack Pointer
.equ CPU_SPL = 0x003D                    ; Stack Pointer low byte
.equ CPU_SPH = 0x003E                    ; Stack Pointer hi byte
.equ CPU_SREG = 0x003F                   ; Status Register

;*************************************************************************
;** CPUINT - Interrupt Controller
;*************************************************************************

.equ CPUINT_CTRLA = 0x0110               ; Control A
.equ CPUINT_STATUS = 0x0111              ; Status
.equ CPUINT_LVL0PRI = 0x0112             ; Interrupt Level 0 Priority
.equ CPUINT_LVL1VEC = 0x0113             ; Interrupt Level 1 Priority Vector

;*************************************************************************
;** CRCSCAN - CRCSCAN
;*************************************************************************

.equ CRCSCAN_CTRLA = 0x0120              ; Control A
.equ CRCSCAN_CTRLB = 0x0121              ; Control B
.equ CRCSCAN_STATUS = 0x0122             ; Status

;*************************************************************************
;** DAC0 - Digital to Analog Converter
;*************************************************************************

.equ DAC0_CTRLA = 0x06A0                 ; Control Register A
.equ DAC0_DATA = 0x06A2                  ; DATA Register
.equ DAC0_DATAL = 0x06A2                 ; DATA Register low byte
.equ DAC0_DATAH = 0x06A3                 ; DATA Register hi byte

;*************************************************************************
;** EVSYS - Event System
;*************************************************************************

.equ EVSYS_SWEVENTA = 0x0200             ; Software Event A
.equ EVSYS_CHANNEL0 = 0x0210             ; Multiplexer Channel 0
.equ EVSYS_CHANNEL1 = 0x0211             ; Multiplexer Channel 1
.equ EVSYS_CHANNEL2 = 0x0212             ; Multiplexer Channel 2
.equ EVSYS_CHANNEL3 = 0x0213             ; Multiplexer Channel 3
.equ EVSYS_CHANNEL4 = 0x0214             ; Multiplexer Channel 4
.equ EVSYS_CHANNEL5 = 0x0215             ; Multiplexer Channel 5
.equ EVSYS_USERCCLLUT0A = 0x0220         ; CCL0 Event A
.equ EVSYS_USERCCLLUT0B = 0x0221         ; CCL0 Event B
.equ EVSYS_USERCCLLUT1A = 0x0222         ; CCL1 Event A
.equ EVSYS_USERCCLLUT1B = 0x0223         ; CCL1 Event B
.equ EVSYS_USERCCLLUT2A = 0x0224         ; CCL2 Event A
.equ EVSYS_USERCCLLUT2B = 0x0225         ; CCL2 Event B
.equ EVSYS_USERCCLLUT3A = 0x0226         ; CCL3 Event A
.equ EVSYS_USERCCLLUT3B = 0x0227         ; CCL3 Event B
.equ EVSYS_USERADC0START = 0x0228        ; ADC0
.equ EVSYS_USEREVSYSEVOUTA = 0x0229      ; EVOUTA
.equ EVSYS_USEREVSYSEVOUTB = 0x022A      ; EVOUTB
.equ EVSYS_USEREVSYSEVOUTC = 0x022B      ; EVOUTC
.equ EVSYS_USEREVSYSEVOUTD = 0x022C      ; EVOUTD
.equ EVSYS_USEREVSYSEVOUTE = 0x022D      ; EVOUTE
.equ EVSYS_USEREVSYSEVOUTF = 0x022E      ; EVOUTF
.equ EVSYS_USERUSART0IRDA = 0x022F       ; USART0
.equ EVSYS_USERUSART1IRDA = 0x0230       ; USART1
.equ EVSYS_USERUSART2IRDA = 0x0231       ; USART2
.equ EVSYS_USERTCA0CNTA = 0x0232         ; TCA0 Event A
.equ EVSYS_USERTCA0CNTB = 0x0233         ; TCA0 Event B
.equ EVSYS_USERTCA1CNTA = 0x0234         ; TCA1 Event A
.equ EVSYS_USERTCA1CNTB = 0x0235         ; TCA1 Event B
.equ EVSYS_USERTCB0CAPT = 0x0236         ; TCB0 Event A
.equ EVSYS_USERTCB0COUNT = 0x0237        ; TCB0 Event B
.equ EVSYS_USERTCB1CAPT = 0x0238         ; TCB1 Event A
.equ EVSYS_USERTCB1COUNT = 0x0239        ; TCB1 Event B
.equ EVSYS_USERTCB2CAPT = 0x023A         ; TCB2 Event A
.equ EVSYS_USERTCB2COUNT = 0x023B        ; TCB2 Event B
.equ EVSYS_USERTCB3CAPT = 0x023C         ; TCB3 Event A
.equ EVSYS_USERTCB3COUNT = 0x023D        ; TCB3 Event B

;*************************************************************************
;** FUSE - Fuses
;*************************************************************************

.equ FUSE_WDTCFG = 0x1050                ; Watchdog Configuration
.equ FUSE_BODCFG = 0x1051                ; BOD Configuration
.equ FUSE_OSCCFG = 0x1052                ; Oscillator Configuration
.equ FUSE_SYSCFG0 = 0x1055               ; System Configuration 0
.equ FUSE_SYSCFG1 = 0x1056               ; System Configuration 1
.equ FUSE_CODESIZE = 0x1057              ; Code Section Size
.equ FUSE_BOOTSIZE = 0x1058              ; Boot Section Size

;*************************************************************************
;** GPR - General Purpose Registers
;*************************************************************************

.equ GPR_GPR0 = 0x001C                   ; General Purpose Register 0
.equ GPR_GPR1 = 0x001D                   ; General Purpose Register 1
.equ GPR_GPR2 = 0x001E                   ; General Purpose Register 2
.equ GPR_GPR3 = 0x001F                   ; General Purpose Register 3

;*************************************************************************
;** LOCK - Lockbits
;*************************************************************************

.equ LOCK_KEY = 0x1040                   ; Lock Key Bits
.equ LOCK_KEY0 = 0x1040                  ; Lock Key Bits 0
.equ LOCK_KEY1 = 0x1041                  ; Lock Key Bits 1
.equ LOCK_KEY2 = 0x1042                  ; Lock Key Bits 2
.equ LOCK_KEY3 = 0x1043                  ; Lock Key Bits 3

;*************************************************************************
;** NVMCTRL - Non-volatile Memory Controller
;*************************************************************************

.equ NVMCTRL_CTRLA = 0x1000              ; Control A
.equ NVMCTRL_CTRLB = 0x1001              ; Control B
.equ NVMCTRL_INTCTRL = 0x1004            ; Interrupt Control
.equ NVMCTRL_INTFLAGS = 0x1005           ; Interrupt Flags
.equ NVMCTRL_STATUS = 0x1006             ; Status
.equ NVMCTRL_DATA = 0x1008               ; Data
.equ NVMCTRL_DATAL = 0x1008              ; Data low byte
.equ NVMCTRL_DATAH = 0x1009              ; Data hi byte
.equ NVMCTRL_ADDR = 0x100C               ; Address
.equ NVMCTRL_ADDR0 = 0x100C              ; Address 0
.equ NVMCTRL_ADDR1 = 0x100D              ; Address 1
.equ NVMCTRL_ADDR2 = 0x100E              ; Address 2
.equ NVMCTRL_ADDR3 = 0x100F              ; Address 3

;*************************************************************************
;** PORTA - I/O Ports
;*************************************************************************

.equ PORTA_DIR = 0x0400                  ; Data Direction
.equ PORTA_DIRSET = 0x0401               ; Data Direction Set
.equ PORTA_DIRCLR = 0x0402               ; Data Direction Clear
.equ PORTA_DIRTGL = 0x0403               ; Data Direction Toggle
.equ PORTA_OUT = 0x0404                  ; Output Value
.equ PORTA_OUTSET = 0x0405               ; Output Value Set
.equ PORTA_OUTCLR = 0x0406               ; Output Value Clear
.equ PORTA_OUTTGL = 0x0407               ; Output Value Toggle
.equ PORTA_IN = 0x0408                   ; Input Value
.equ PORTA_INTFLAGS = 0x0409             ; Interrupt Flags
.equ PORTA_PORTCTRL = 0x040A             ; Port Control
.equ PORTA_PINCONFIG = 0x040B            ; Pin Control Config
.equ PORTA_PINCTRLUPD = 0x040C           ; Pin Control Update
.equ PORTA_PINCTRLSET = 0x040D           ; Pin Control Set
.equ PORTA_PINCTRLCLR = 0x040E           ; Pin Control Clear
.equ PORTA_PIN0CTRL = 0x0410             ; Pin 0 Control
.equ PORTA_PIN1CTRL = 0x0411             ; Pin 1 Control
.equ PORTA_PIN2CTRL = 0x0412             ; Pin 2 Control
.equ PORTA_PIN3CTRL = 0x0413             ; Pin 3 Control
.equ PORTA_PIN4CTRL = 0x0414             ; Pin 4 Control
.equ PORTA_PIN5CTRL = 0x0415             ; Pin 5 Control
.equ PORTA_PIN6CTRL = 0x0416             ; Pin 6 Control
.equ PORTA_PIN7CTRL = 0x0417             ; Pin 7 Control
.equ PORTA_EVGENCTRL = 0x0418            ; Event Generation Control

;*************************************************************************
;** PORTB - I/O Ports
;*************************************************************************

.equ PORTB_DIR = 0x0420                  ; Data Direction
.equ PORTB_DIRSET = 0x0421               ; Data Direction Set
.equ PORTB_DIRCLR = 0x0422               ; Data Direction Clear
.equ PORTB_DIRTGL = 0x0423               ; Data Direction Toggle
.equ PORTB_OUT = 0x0424                  ; Output Value
.equ PORTB_OUTSET = 0x0425               ; Output Value Set
.equ PORTB_OUTCLR = 0x0426               ; Output Value Clear
.equ PORTB_OUTTGL = 0x0427               ; Output Value Toggle
.equ PORTB_IN = 0x0428                   ; Input Value
.equ PORTB_INTFLAGS = 0x0429             ; Interrupt Flags
.equ PORTB_PORTCTRL = 0x042A             ; Port Control
.equ PORTB_PINCONFIG = 0x042B            ; Pin Control Config
.equ PORTB_PINCTRLUPD = 0x042C           ; Pin Control Update
.equ PORTB_PINCTRLSET = 0x042D           ; Pin Control Set
.equ PORTB_PINCTRLCLR = 0x042E           ; Pin Control Clear
.equ PORTB_PIN0CTRL = 0x0430             ; Pin 0 Control
.equ PORTB_PIN1CTRL = 0x0431             ; Pin 1 Control
.equ PORTB_PIN2CTRL = 0x0432             ; Pin 2 Control
.equ PORTB_PIN3CTRL = 0x0433             ; Pin 3 Control
.equ PORTB_PIN4CTRL = 0x0434             ; Pin 4 Control
.equ PORTB_PIN5CTRL = 0x0435             ; Pin 5 Control
.equ PORTB_PIN6CTRL = 0x0436             ; Pin 6 Control
.equ PORTB_PIN7CTRL = 0x0437             ; Pin 7 Control
.equ PORTB_EVGENCTRL = 0x0438            ; Event Generation Control

;*************************************************************************
;** PORTC - I/O Ports
;*************************************************************************

.equ PORTC_DIR = 0x0440                  ; Data Direction
.equ PORTC_DIRSET = 0x0441               ; Data Direction Set
.equ PORTC_DIRCLR = 0x0442               ; Data Direction Clear
.equ PORTC_DIRTGL = 0x0443               ; Data Direction Toggle
.equ PORTC_OUT = 0x0444                  ; Output Value
.equ PORTC_OUTSET = 0x0445               ; Output Value Set
.equ PORTC_OUTCLR = 0x0446               ; Output Value Clear
.equ PORTC_OUTTGL = 0x0447               ; Output Value Toggle
.equ PORTC_IN = 0x0448                   ; Input Value
.equ PORTC_INTFLAGS = 0x0449             ; Interrupt Flags
.equ PORTC_PORTCTRL = 0x044A             ; Port Control
.equ PORTC_PINCONFIG = 0x044B            ; Pin Control Config
.equ PORTC_PINCTRLUPD = 0x044C           ; Pin Control Update
.equ PORTC_PINCTRLSET = 0x044D           ; Pin Control Set
.equ PORTC_PINCTRLCLR = 0x044E           ; Pin Control Clear
.equ PORTC_PIN0CTRL = 0x0450             ; Pin 0 Control
.equ PORTC_PIN1CTRL = 0x0451             ; Pin 1 Control
.equ PORTC_PIN2CTRL = 0x0452             ; Pin 2 Control
.equ PORTC_PIN3CTRL = 0x0453             ; Pin 3 Control
.equ PORTC_PIN4CTRL = 0x0454             ; Pin 4 Control
.equ PORTC_PIN5CTRL = 0x0455             ; Pin 5 Control
.equ PORTC_PIN6CTRL = 0x0456             ; Pin 6 Control
.equ PORTC_PIN7CTRL = 0x0457             ; Pin 7 Control
.equ PORTC_EVGENCTRL = 0x0458            ; Event Generation Control

;*************************************************************************
;** PORTD - I/O Ports
;*************************************************************************

.equ PORTD_DIR = 0x0460                  ; Data Direction
.equ PORTD_DIRSET = 0x0461               ; Data Direction Set
.equ PORTD_DIRCLR = 0x0462               ; Data Direction Clear
.equ PORTD_DIRTGL = 0x0463               ; Data Direction Toggle
.equ PORTD_OUT = 0x0464                  ; Output Value
.equ PORTD_OUTSET = 0x0465               ; Output Value Set
.equ PORTD_OUTCLR = 0x0466               ; Output Value Clear
.equ PORTD_OUTTGL = 0x0467               ; Output Value Toggle
.equ PORTD_IN = 0x0468                   ; Input Value
.equ PORTD_INTFLAGS = 0x0469             ; Interrupt Flags
.equ PORTD_PORTCTRL = 0x046A             ; Port Control
.equ PORTD_PINCONFIG = 0x046B            ; Pin Control Config
.equ PORTD_PINCTRLUPD = 0x046C           ; Pin Control Update
.equ PORTD_PINCTRLSET = 0x046D           ; Pin Control Set
.equ PORTD_PINCTRLCLR = 0x046E           ; Pin Control Clear
.equ PORTD_PIN0CTRL = 0x0470             ; Pin 0 Control
.equ PORTD_PIN1CTRL = 0x0471             ; Pin 1 Control
.equ PORTD_PIN2CTRL = 0x0472             ; Pin 2 Control
.equ PORTD_PIN3CTRL = 0x0473             ; Pin 3 Control
.equ PORTD_PIN4CTRL = 0x0474             ; Pin 4 Control
.equ PORTD_PIN5CTRL = 0x0475             ; Pin 5 Control
.equ PORTD_PIN6CTRL = 0x0476             ; Pin 6 Control
.equ PORTD_PIN7CTRL = 0x0477             ; Pin 7 Control
.equ PORTD_EVGENCTRL = 0x0478            ; Event Generation Control

;*************************************************************************
;** PORTE - I/O Ports
;*************************************************************************

.equ PORTE_DIR = 0x0480                  ; Data Direction
.equ PORTE_DIRSET = 0x0481               ; Data Direction Set
.equ PORTE_DIRCLR = 0x0482               ; Data Direction Clear
.equ PORTE_DIRTGL = 0x0483               ; Data Direction Toggle
.equ PORTE_OUT = 0x0484                  ; Output Value
.equ PORTE_OUTSET = 0x0485               ; Output Value Set
.equ PORTE_OUTCLR = 0x0486               ; Output Value Clear
.equ PORTE_OUTTGL = 0x0487               ; Output Value Toggle
.equ PORTE_IN = 0x0488                   ; Input Value
.equ PORTE_INTFLAGS = 0x0489             ; Interrupt Flags
.equ PORTE_PORTCTRL = 0x048A             ; Port Control
.equ PORTE_PINCONFIG = 0x048B            ; Pin Control Config
.equ PORTE_PINCTRLUPD = 0x048C           ; Pin Control Update
.equ PORTE_PINCTRLSET = 0x048D           ; Pin Control Set
.equ PORTE_PINCTRLCLR = 0x048E           ; Pin Control Clear
.equ PORTE_PIN0CTRL = 0x0490             ; Pin 0 Control
.equ PORTE_PIN1CTRL = 0x0491             ; Pin 1 Control
.equ PORTE_PIN2CTRL = 0x0492             ; Pin 2 Control
.equ PORTE_PIN3CTRL = 0x0493             ; Pin 3 Control
.equ PORTE_PIN4CTRL = 0x0494             ; Pin 4 Control
.equ PORTE_PIN5CTRL = 0x0495             ; Pin 5 Control
.equ PORTE_PIN6CTRL = 0x0496             ; Pin 6 Control
.equ PORTE_PIN7CTRL = 0x0497             ; Pin 7 Control
.equ PORTE_EVGENCTRL = 0x0498            ; Event Generation Control

;*************************************************************************
;** PORTF - I/O Ports
;*************************************************************************

.equ PORTF_DIR = 0x04A0                  ; Data Direction
.equ PORTF_DIRSET = 0x04A1               ; Data Direction Set
.equ PORTF_DIRCLR = 0x04A2               ; Data Direction Clear
.equ PORTF_DIRTGL = 0x04A3               ; Data Direction Toggle
.equ PORTF_OUT = 0x04A4                  ; Output Value
.equ PORTF_OUTSET = 0x04A5               ; Output Value Set
.equ PORTF_OUTCLR = 0x04A6               ; Output Value Clear
.equ PORTF_OUTTGL = 0x04A7               ; Output Value Toggle
.equ PORTF_IN = 0x04A8                   ; Input Value
.equ PORTF_INTFLAGS = 0x04A9             ; Interrupt Flags
.equ PORTF_PORTCTRL = 0x04AA             ; Port Control
.equ PORTF_PINCONFIG = 0x04AB            ; Pin Control Config
.equ PORTF_PINCTRLUPD = 0x04AC           ; Pin Control Update
.equ PORTF_PINCTRLSET = 0x04AD           ; Pin Control Set
.equ PORTF_PINCTRLCLR = 0x04AE           ; Pin Control Clear
.equ PORTF_PIN0CTRL = 0x04B0             ; Pin 0 Control
.equ PORTF_PIN1CTRL = 0x04B1             ; Pin 1 Control
.equ PORTF_PIN2CTRL = 0x04B2             ; Pin 2 Control
.equ PORTF_PIN3CTRL = 0x04B3             ; Pin 3 Control
.equ PORTF_PIN4CTRL = 0x04B4             ; Pin 4 Control
.equ PORTF_PIN5CTRL = 0x04B5             ; Pin 5 Control
.equ PORTF_PIN6CTRL = 0x04B6             ; Pin 6 Control
.equ PORTF_PIN7CTRL = 0x04B7             ; Pin 7 Control
.equ PORTF_EVGENCTRL = 0x04B8            ; Event Generation Control

;*************************************************************************
;** PORTMUX - Port Multiplexer
;*************************************************************************

.equ PORTMUX_EVSYSROUTEA = 0x05E0        ; EVSYS route A
.equ PORTMUX_CCLROUTEA = 0x05E1          ; CCL route A
.equ PORTMUX_USARTROUTEA = 0x05E2        ; USART route A
.equ PORTMUX_USARTROUTEB = 0x05E3        ; USART route B
.equ PORTMUX_SPIROUTEA = 0x05E5          ; SPI route A
.equ PORTMUX_TWIROUTEA = 0x05E6          ; TWI route A
.equ PORTMUX_TCAROUTEA = 0x05E7          ; TCA route A
.equ PORTMUX_TCBROUTEA = 0x05E8          ; TCB route A
.equ PORTMUX_ACROUTEA = 0x05EA           ; AC route A

;*************************************************************************
;** RSTCTRL - Reset controller
;*************************************************************************

.equ RSTCTRL_RSTFR = 0x0040              ; Reset Flags
.equ RSTCTRL_SWRR = 0x0041               ; Software Reset

;*************************************************************************
;** RTC - Real-Time Counter
;*************************************************************************

.equ RTC_CTRLA = 0x0140                  ; Control A
.equ RTC_STATUS = 0x0141                 ; Status
.equ RTC_INTCTRL = 0x0142                ; Interrupt Control
.equ RTC_INTFLAGS = 0x0143               ; Interrupt Flags
.equ RTC_TEMP = 0x0144                   ; Temporary
.equ RTC_DBGCTRL = 0x0145                ; Debug control
.equ RTC_CALIB = 0x0146                  ; Calibration
.equ RTC_CLKSEL = 0x0147                 ; Clock Select
.equ RTC_CNT = 0x0148                    ; Counter
.equ RTC_CNTL = 0x0148                   ; Counter low byte
.equ RTC_CNTH = 0x0149                   ; Counter hi byte
.equ RTC_PER = 0x014A                    ; Period
.equ RTC_PERL = 0x014A                   ; Period low byte
.equ RTC_PERH = 0x014B                   ; Period hi byte
.equ RTC_CMP = 0x014C                    ; Compare
.equ RTC_CMPL = 0x014C                   ; Compare low byte
.equ RTC_CMPH = 0x014D                   ; Compare hi byte
.equ RTC_PITCTRLA = 0x0150               ; PIT Control A
.equ RTC_PITSTATUS = 0x0151              ; PIT Status
.equ RTC_PITINTCTRL = 0x0152             ; PIT Interrupt Control
.equ RTC_PITINTFLAGS = 0x0153            ; PIT Interrupt Flags
.equ RTC_PITDBGCTRL = 0x0155             ; PIT Debug control
.equ RTC_PITEVGENCTRLA = 0x0156          ; PIT Event Generation Control A

;*************************************************************************
;** SIGROW - Signature row
;*************************************************************************

.equ SIGROW_DEVICEID0 = 0x1100           ; Device ID Byte 0
.equ SIGROW_DEVICEID1 = 0x1101           ; Device ID Byte 1
.equ SIGROW_DEVICEID2 = 0x1102           ; Device ID Byte 2
.equ SIGROW_TEMPSENSE0 = 0x1104          ; Temperature Calibration 0
.equ SIGROW_TEMPSENSE0L = 0x1104         ; Temperature Calibration 0 low byte
.equ SIGROW_TEMPSENSE0H = 0x1105         ; Temperature Calibration 0 hi byte
.equ SIGROW_TEMPSENSE1 = 0x1106          ; Temperature Calibration 1
.equ SIGROW_TEMPSENSE1L = 0x1106         ; Temperature Calibration 1 low byte
.equ SIGROW_TEMPSENSE1H = 0x1107         ; Temperature Calibration 1 hi byte
.equ SIGROW_SERNUM0 = 0x1110             ; Serial Number Byte 0
.equ SIGROW_SERNUM1 = 0x1111             ; Serial Number Byte 1
.equ SIGROW_SERNUM2 = 0x1112             ; Serial Number Byte 2
.equ SIGROW_SERNUM3 = 0x1113             ; Serial Number Byte 3
.equ SIGROW_SERNUM4 = 0x1114             ; Serial Number Byte 4
.equ SIGROW_SERNUM5 = 0x1115             ; Serial Number Byte 5
.equ SIGROW_SERNUM6 = 0x1116             ; Serial Number Byte 6
.equ SIGROW_SERNUM7 = 0x1117             ; Serial Number Byte 7
.equ SIGROW_SERNUM8 = 0x1118             ; Serial Number Byte 8
.equ SIGROW_SERNUM9 = 0x1119             ; Serial Number Byte 9
.equ SIGROW_SERNUM10 = 0x111A            ; Serial Number Byte 10
.equ SIGROW_SERNUM11 = 0x111B            ; Serial Number Byte 11
.equ SIGROW_SERNUM12 = 0x111C            ; Serial Number Byte 12
.equ SIGROW_SERNUM13 = 0x111D            ; Serial Number Byte 13
.equ SIGROW_SERNUM14 = 0x111E            ; Serial Number Byte 14
.equ SIGROW_SERNUM15 = 0x111F            ; Serial Number Byte 15

;*************************************************************************
;** SLPCTRL - Sleep Controller
;*************************************************************************

.equ SLPCTRL_CTRLA = 0x0050              ; Control A

;*************************************************************************
;** SPI0 - Serial Peripheral Interface
;*************************************************************************

.equ SPI0_CTRLA = 0x0940                 ; Control A
.equ SPI0_CTRLB = 0x0941                 ; Control B
.equ SPI0_INTCTRL = 0x0942               ; Interrupt Control
.equ SPI0_INTFLAGS = 0x0943              ; Interrupt Flags
.equ SPI0_DATA = 0x0944                  ; Data

;*************************************************************************
;** SYSCFG - System Configuration Registers
;*************************************************************************

.equ SYSCFG_REVID = 0x0F01               ; Revision ID
.equ SYSCFG_OCDMCTRL = 0x0F04            ; OCD Message Control
.equ SYSCFG_OCDMSTATUS = 0x0F05          ; OCD Message Status

;*************************************************************************
;** TCA0 - 16-bit Timer/Counter Type A
;*************************************************************************

.equ TCA0_SINGLE_CTRLA = 0x0A00          ; SINGLE Control A
.equ TCA0_SPLIT_CTRLA = 0x0A00           ; SPLIT Control A
.equ TCA0_SINGLE_CTRLB = 0x0A01          ; SINGLE Control B
.equ TCA0_SPLIT_CTRLB = 0x0A01           ; SPLIT Control B
.equ TCA0_SINGLE_CTRLC = 0x0A02          ; SINGLE Control C
.equ TCA0_SPLIT_CTRLC = 0x0A02           ; SPLIT Control C
.equ TCA0_SINGLE_CTRLD = 0x0A03          ; SINGLE Control D
.equ TCA0_SPLIT_CTRLD = 0x0A03           ; SPLIT Control D
.equ TCA0_SINGLE_CTRLECLR = 0x0A04       ; SINGLE Control E Clear
.equ TCA0_SPLIT_CTRLECLR = 0x0A04        ; SPLIT Control E Clear
.equ TCA0_SINGLE_CTRLESET = 0x0A05       ; SINGLE Control E Set
.equ TCA0_SPLIT_CTRLESET = 0x0A05        ; SPLIT Control E Set
.equ TCA0_SINGLE_CTRLFCLR = 0x0A06       ; SINGLE Control F Clear
.equ TCA0_SINGLE_CTRLFSET = 0x0A07       ; SINGLE Control F Set
.equ TCA0_SINGLE_EVCTRL = 0x0A09         ; SINGLE Event Control
.equ TCA0_SINGLE_INTCTRL = 0x0A0A        ; SINGLE Interrupt Control
.equ TCA0_SPLIT_INTCTRL = 0x0A0A         ; SPLIT Interrupt Control
.equ TCA0_SINGLE_INTFLAGS = 0x0A0B       ; SINGLE Interrupt Flags
.equ TCA0_SPLIT_INTFLAGS = 0x0A0B        ; SPLIT Interrupt Flags
.equ TCA0_SINGLE_DBGCTRL = 0x0A0E        ; SINGLE Debug Control
.equ TCA0_SPLIT_DBGCTRL = 0x0A0E         ; SPLIT Debug Control
.equ TCA0_SINGLE_TEMP = 0x0A0F           ; SINGLE Temporary data for 16-bit Access
.equ TCA0_SINGLE_CNT = 0x0A20            ; SINGLE Count
.equ TCA0_SINGLE_CNTL = 0x0A20           ; SINGLE Count low byte
.equ TCA0_SINGLE_CNTH = 0x0A21           ; SINGLE Count hi byte
.equ TCA0_SPLIT_LCNT = 0x0A20            ; SPLIT Low Count
.equ TCA0_SPLIT_HCNT = 0x0A21            ; SPLIT High Count
.equ TCA0_SINGLE_PER = 0x0A26            ; SINGLE Period
.equ TCA0_SINGLE_PERL = 0x0A26           ; SINGLE Period low byte
.equ TCA0_SINGLE_PERH = 0x0A27           ; SINGLE Period hi byte
.equ TCA0_SPLIT_LPER = 0x0A26            ; SPLIT Low Period
.equ TCA0_SPLIT_HPER = 0x0A27            ; SPLIT High Period
.equ TCA0_SINGLE_CMP0 = 0x0A28           ; SINGLE Compare 0
.equ TCA0_SINGLE_CMP0L = 0x0A28          ; SINGLE Compare 0 low byte
.equ TCA0_SINGLE_CMP0H = 0x0A29          ; SINGLE Compare 0 hi byte
.equ TCA0_SPLIT_LCMP0 = 0x0A28           ; SPLIT Low Compare
.equ TCA0_SPLIT_HCMP0 = 0x0A29           ; SPLIT High Compare
.equ TCA0_SINGLE_CMP1 = 0x0A2A           ; SINGLE Compare 1
.equ TCA0_SINGLE_CMP1L = 0x0A2A          ; SINGLE Compare 1 low byte
.equ TCA0_SINGLE_CMP1H = 0x0A2B          ; SINGLE Compare 1 hi byte
.equ TCA0_SPLIT_LCMP1 = 0x0A2A           ; SPLIT Low Compare
.equ TCA0_SPLIT_HCMP1 = 0x0A2B           ; SPLIT High Compare
.equ TCA0_SINGLE_CMP2 = 0x0A2C           ; SINGLE Compare 2
.equ TCA0_SINGLE_CMP2L = 0x0A2C          ; SINGLE Compare 2 low byte
.equ TCA0_SINGLE_CMP2H = 0x0A2D          ; SINGLE Compare 2 hi byte
.equ TCA0_SPLIT_LCMP2 = 0x0A2C           ; SPLIT Low Compare
.equ TCA0_SPLIT_HCMP2 = 0x0A2D           ; SPLIT High Compare
.equ TCA0_SINGLE_PERBUF = 0x0A36         ; SINGLE Period Buffer
.equ TCA0_SINGLE_PERBUFL = 0x0A36        ; SINGLE Period Buffer low byte
.equ TCA0_SINGLE_PERBUFH = 0x0A37        ; SINGLE Period Buffer hi byte
.equ TCA0_SINGLE_CMP0BUF = 0x0A38        ; SINGLE Compare 0 Buffer
.equ TCA0_SINGLE_CMP0BUFL = 0x0A38       ; SINGLE Compare 0 Buffer low byte
.equ TCA0_SINGLE_CMP0BUFH = 0x0A39       ; SINGLE Compare 0 Buffer hi byte
.equ TCA0_SINGLE_CMP1BUF = 0x0A3A        ; SINGLE Compare 1 Buffer
.equ TCA0_SINGLE_CMP1BUFL = 0x0A3A       ; SINGLE Compare 1 Buffer low byte
.equ TCA0_SINGLE_CMP1BUFH = 0x0A3B       ; SINGLE Compare 1 Buffer hi byte
.equ TCA0_SINGLE_CMP2BUF = 0x0A3C        ; SINGLE Compare 2 Buffer
.equ TCA0_SINGLE_CMP2BUFL = 0x0A3C       ; SINGLE Compare 2 Buffer low byte
.equ TCA0_SINGLE_CMP2BUFH = 0x0A3D       ; SINGLE Compare 2 Buffer hi byte

;*************************************************************************
;** TCA1 - 16-bit Timer/Counter Type A
;*************************************************************************

.equ TCA1_SINGLE_CTRLA = 0x0A40          ; SINGLE Control A
.equ TCA1_SPLIT_CTRLA = 0x0A40           ; SPLIT Control A
.equ TCA1_SINGLE_CTRLB = 0x0A41          ; SINGLE Control B
.equ TCA1_SPLIT_CTRLB = 0x0A41           ; SPLIT Control B
.equ TCA1_SINGLE_CTRLC = 0x0A42          ; SINGLE Control C
.equ TCA1_SPLIT_CTRLC = 0x0A42           ; SPLIT Control C
.equ TCA1_SINGLE_CTRLD = 0x0A43          ; SINGLE Control D
.equ TCA1_SPLIT_CTRLD = 0x0A43           ; SPLIT Control D
.equ TCA1_SINGLE_CTRLECLR = 0x0A44       ; SINGLE Control E Clear
.equ TCA1_SPLIT_CTRLECLR = 0x0A44        ; SPLIT Control E Clear
.equ TCA1_SINGLE_CTRLESET = 0x0A45       ; SINGLE Control E Set
.equ TCA1_SPLIT_CTRLESET = 0x0A45        ; SPLIT Control E Set
.equ TCA1_SINGLE_CTRLFCLR = 0x0A46       ; SINGLE Control F Clear
.equ TCA1_SINGLE_CTRLFSET = 0x0A47       ; SINGLE Control F Set
.equ TCA1_SINGLE_EVCTRL = 0x0A49         ; SINGLE Event Control
.equ TCA1_SINGLE_INTCTRL = 0x0A4A        ; SINGLE Interrupt Control
.equ TCA1_SPLIT_INTCTRL = 0x0A4A         ; SPLIT Interrupt Control
.equ TCA1_SINGLE_INTFLAGS = 0x0A4B       ; SINGLE Interrupt Flags
.equ TCA1_SPLIT_INTFLAGS = 0x0A4B        ; SPLIT Interrupt Flags
.equ TCA1_SINGLE_DBGCTRL = 0x0A4E        ; SINGLE Debug Control
.equ TCA1_SPLIT_DBGCTRL = 0x0A4E         ; SPLIT Debug Control
.equ TCA1_SINGLE_TEMP = 0x0A4F           ; SINGLE Temporary data for 16-bit Access
.equ TCA1_SINGLE_CNT = 0x0A60            ; SINGLE Count
.equ TCA1_SINGLE_CNTL = 0x0A60           ; SINGLE Count low byte
.equ TCA1_SINGLE_CNTH = 0x0A61           ; SINGLE Count hi byte
.equ TCA1_SPLIT_LCNT = 0x0A60            ; SPLIT Low Count
.equ TCA1_SPLIT_HCNT = 0x0A61            ; SPLIT High Count
.equ TCA1_SINGLE_PER = 0x0A66            ; SINGLE Period
.equ TCA1_SINGLE_PERL = 0x0A66           ; SINGLE Period low byte
.equ TCA1_SINGLE_PERH = 0x0A67           ; SINGLE Period hi byte
.equ TCA1_SPLIT_LPER = 0x0A66            ; SPLIT Low Period
.equ TCA1_SPLIT_HPER = 0x0A67            ; SPLIT High Period
.equ TCA1_SINGLE_CMP0 = 0x0A68           ; SINGLE Compare 0
.equ TCA1_SINGLE_CMP0L = 0x0A68          ; SINGLE Compare 0 low byte
.equ TCA1_SINGLE_CMP0H = 0x0A69          ; SINGLE Compare 0 hi byte
.equ TCA1_SPLIT_LCMP0 = 0x0A68           ; SPLIT Low Compare
.equ TCA1_SPLIT_HCMP0 = 0x0A69           ; SPLIT High Compare
.equ TCA1_SINGLE_CMP1 = 0x0A6A           ; SINGLE Compare 1
.equ TCA1_SINGLE_CMP1L = 0x0A6A          ; SINGLE Compare 1 low byte
.equ TCA1_SINGLE_CMP1H = 0x0A6B          ; SINGLE Compare 1 hi byte
.equ TCA1_SPLIT_LCMP1 = 0x0A6A           ; SPLIT Low Compare
.equ TCA1_SPLIT_HCMP1 = 0x0A6B           ; SPLIT High Compare
.equ TCA1_SINGLE_CMP2 = 0x0A6C           ; SINGLE Compare 2
.equ TCA1_SINGLE_CMP2L = 0x0A6C          ; SINGLE Compare 2 low byte
.equ TCA1_SINGLE_CMP2H = 0x0A6D          ; SINGLE Compare 2 hi byte
.equ TCA1_SPLIT_LCMP2 = 0x0A6C           ; SPLIT Low Compare
.equ TCA1_SPLIT_HCMP2 = 0x0A6D           ; SPLIT High Compare
.equ TCA1_SINGLE_PERBUF = 0x0A76         ; SINGLE Period Buffer
.equ TCA1_SINGLE_PERBUFL = 0x0A76        ; SINGLE Period Buffer low byte
.equ TCA1_SINGLE_PERBUFH = 0x0A77        ; SINGLE Period Buffer hi byte
.equ TCA1_SINGLE_CMP0BUF = 0x0A78        ; SINGLE Compare 0 Buffer
.equ TCA1_SINGLE_CMP0BUFL = 0x0A78       ; SINGLE Compare 0 Buffer low byte
.equ TCA1_SINGLE_CMP0BUFH = 0x0A79       ; SINGLE Compare 0 Buffer hi byte
.equ TCA1_SINGLE_CMP1BUF = 0x0A7A        ; SINGLE Compare 1 Buffer
.equ TCA1_SINGLE_CMP1BUFL = 0x0A7A       ; SINGLE Compare 1 Buffer low byte
.equ TCA1_SINGLE_CMP1BUFH = 0x0A7B       ; SINGLE Compare 1 Buffer hi byte
.equ TCA1_SINGLE_CMP2BUF = 0x0A7C        ; SINGLE Compare 2 Buffer
.equ TCA1_SINGLE_CMP2BUFL = 0x0A7C       ; SINGLE Compare 2 Buffer low byte
.equ TCA1_SINGLE_CMP2BUFH = 0x0A7D       ; SINGLE Compare 2 Buffer hi byte

;*************************************************************************
;** TCB0 - 16-bit Timer Type B
;*************************************************************************

.equ TCB0_CTRLA = 0x0B00                 ; Control A
.equ TCB0_CTRLB = 0x0B01                 ; Control Register B
.equ TCB0_EVCTRL = 0x0B04                ; Event Control
.equ TCB0_INTCTRL = 0x0B05               ; Interrupt Control
.equ TCB0_INTFLAGS = 0x0B06              ; Interrupt Flags
.equ TCB0_STATUS = 0x0B07                ; Status
.equ TCB0_DBGCTRL = 0x0B08               ; Debug Control
.equ TCB0_TEMP = 0x0B09                  ; Temporary Value
.equ TCB0_CNT = 0x0B0A                   ; Count
.equ TCB0_CNTL = 0x0B0A                  ; Count low byte
.equ TCB0_CNTH = 0x0B0B                  ; Count hi byte
.equ TCB0_CCMP = 0x0B0C                  ; Compare or Capture
.equ TCB0_CCMPL = 0x0B0C                 ; Compare or Capture low byte
.equ TCB0_CCMPH = 0x0B0D                 ; Compare or Capture hi byte

;*************************************************************************
;** TCB1 - 16-bit Timer Type B
;*************************************************************************

.equ TCB1_CTRLA = 0x0B10                 ; Control A
.equ TCB1_CTRLB = 0x0B11                 ; Control Register B
.equ TCB1_EVCTRL = 0x0B14                ; Event Control
.equ TCB1_INTCTRL = 0x0B15               ; Interrupt Control
.equ TCB1_INTFLAGS = 0x0B16              ; Interrupt Flags
.equ TCB1_STATUS = 0x0B17                ; Status
.equ TCB1_DBGCTRL = 0x0B18               ; Debug Control
.equ TCB1_TEMP = 0x0B19                  ; Temporary Value
.equ TCB1_CNT = 0x0B1A                   ; Count
.equ TCB1_CNTL = 0x0B1A                  ; Count low byte
.equ TCB1_CNTH = 0x0B1B                  ; Count hi byte
.equ TCB1_CCMP = 0x0B1C                  ; Compare or Capture
.equ TCB1_CCMPL = 0x0B1C                 ; Compare or Capture low byte
.equ TCB1_CCMPH = 0x0B1D                 ; Compare or Capture hi byte

;*************************************************************************
;** TCB2 - 16-bit Timer Type B
;*************************************************************************

.equ TCB2_CTRLA = 0x0B20                 ; Control A
.equ TCB2_CTRLB = 0x0B21                 ; Control Register B
.equ TCB2_EVCTRL = 0x0B24                ; Event Control
.equ TCB2_INTCTRL = 0x0B25               ; Interrupt Control
.equ TCB2_INTFLAGS = 0x0B26              ; Interrupt Flags
.equ TCB2_STATUS = 0x0B27                ; Status
.equ TCB2_DBGCTRL = 0x0B28               ; Debug Control
.equ TCB2_TEMP = 0x0B29                  ; Temporary Value
.equ TCB2_CNT = 0x0B2A                   ; Count
.equ TCB2_CNTL = 0x0B2A                  ; Count low byte
.equ TCB2_CNTH = 0x0B2B                  ; Count hi byte
.equ TCB2_CCMP = 0x0B2C                  ; Compare or Capture
.equ TCB2_CCMPL = 0x0B2C                 ; Compare or Capture low byte
.equ TCB2_CCMPH = 0x0B2D                 ; Compare or Capture hi byte

;*************************************************************************
;** TCB3 - 16-bit Timer Type B
;*************************************************************************

.equ TCB3_CTRLA = 0x0B30                 ; Control A
.equ TCB3_CTRLB = 0x0B31                 ; Control Register B
.equ TCB3_EVCTRL = 0x0B34                ; Event Control
.equ TCB3_INTCTRL = 0x0B35               ; Interrupt Control
.equ TCB3_INTFLAGS = 0x0B36              ; Interrupt Flags
.equ TCB3_STATUS = 0x0B37                ; Status
.equ TCB3_DBGCTRL = 0x0B38               ; Debug Control
.equ TCB3_TEMP = 0x0B39                  ; Temporary Value
.equ TCB3_CNT = 0x0B3A                   ; Count
.equ TCB3_CNTL = 0x0B3A                  ; Count low byte
.equ TCB3_CNTH = 0x0B3B                  ; Count hi byte
.equ TCB3_CCMP = 0x0B3C                  ; Compare or Capture
.equ TCB3_CCMPL = 0x0B3C                 ; Compare or Capture low byte
.equ TCB3_CCMPH = 0x0B3D                 ; Compare or Capture hi byte

;*************************************************************************
;** TWI0 - Two-Wire Interface
;*************************************************************************

.equ TWI0_CTRLA = 0x0900                 ; Control A
.equ TWI0_DUALCTRL = 0x0901              ; Dual Mode Control
.equ TWI0_DBGCTRL = 0x0902               ; Debug Control
.equ TWI0_MCTRLA = 0x0903                ; Host Control A
.equ TWI0_MCTRLB = 0x0904                ; Host Control B
.equ TWI0_MSTATUS = 0x0905               ; Host STATUS
.equ TWI0_MBAUD = 0x0906                 ; Host Baud Rate
.equ TWI0_MADDR = 0x0907                 ; Host Address
.equ TWI0_MDATA = 0x0908                 ; Host Data
.equ TWI0_SCTRLA = 0x0909                ; Client Control A
.equ TWI0_SCTRLB = 0x090A                ; Client Control B
.equ TWI0_SSTATUS = 0x090B               ; Client Status
.equ TWI0_SADDR = 0x090C                 ; Client Address
.equ TWI0_SDATA = 0x090D                 ; Client Data
.equ TWI0_SADDRMASK = 0x090E             ; Client Address Mask

;*************************************************************************
;** USART0 - Universal Synchronous and Asynchronous Receiver and Transmitter
;*************************************************************************

.equ USART0_RXDATAL = 0x0800             ; Receive Data Low Byte
.equ USART0_RXDATAH = 0x0801             ; Receive Data High Byte
.equ USART0_TXDATAL = 0x0802             ; Transmit Data Low Byte
.equ USART0_TXDATAH = 0x0803             ; Transmit Data High Byte
.equ USART0_STATUS = 0x0804              ; Status
.equ USART0_CTRLA = 0x0805               ; Control A
.equ USART0_CTRLB = 0x0806               ; Control B
.equ USART0_CTRLC = 0x0807               ; Control C
.equ USART0_BAUD = 0x0808                ; Baud Rate
.equ USART0_BAUDL = 0x0808               ; Baud Rate low byte
.equ USART0_BAUDH = 0x0809               ; Baud Rate hi byte
.equ USART0_CTRLD = 0x080A               ; Control D
.equ USART0_DBGCTRL = 0x080B             ; Debug Control
.equ USART0_EVCTRL = 0x080C              ; Event Control
.equ USART0_TXPLCTRL = 0x080D            ; IRCOM Transmitter Pulse Length Control
.equ USART0_RXPLCTRL = 0x080E            ; IRCOM Receiver Pulse Length Control

;*************************************************************************
;** USART1 - Universal Synchronous and Asynchronous Receiver and Transmitter
;*************************************************************************

.equ USART1_RXDATAL = 0x0820             ; Receive Data Low Byte
.equ USART1_RXDATAH = 0x0821             ; Receive Data High Byte
.equ USART1_TXDATAL = 0x0822             ; Transmit Data Low Byte
.equ USART1_TXDATAH = 0x0823             ; Transmit Data High Byte
.equ USART1_STATUS = 0x0824              ; Status
.equ USART1_CTRLA = 0x0825               ; Control A
.equ USART1_CTRLB = 0x0826               ; Control B
.equ USART1_CTRLC = 0x0827               ; Control C
.equ USART1_BAUD = 0x0828                ; Baud Rate
.equ USART1_BAUDL = 0x0828               ; Baud Rate low byte
.equ USART1_BAUDH = 0x0829               ; Baud Rate hi byte
.equ USART1_CTRLD = 0x082A               ; Control D
.equ USART1_DBGCTRL = 0x082B             ; Debug Control
.equ USART1_EVCTRL = 0x082C              ; Event Control
.equ USART1_TXPLCTRL = 0x082D            ; IRCOM Transmitter Pulse Length Control
.equ USART1_RXPLCTRL = 0x082E            ; IRCOM Receiver Pulse Length Control

;*************************************************************************
;** USART2 - Universal Synchronous and Asynchronous Receiver and Transmitter
;*************************************************************************

.equ USART2_RXDATAL = 0x0840             ; Receive Data Low Byte
.equ USART2_RXDATAH = 0x0841             ; Receive Data High Byte
.equ USART2_TXDATAL = 0x0842             ; Transmit Data Low Byte
.equ USART2_TXDATAH = 0x0843             ; Transmit Data High Byte
.equ USART2_STATUS = 0x0844              ; Status
.equ USART2_CTRLA = 0x0845               ; Control A
.equ USART2_CTRLB = 0x0846               ; Control B
.equ USART2_CTRLC = 0x0847               ; Control C
.equ USART2_BAUD = 0x0848                ; Baud Rate
.equ USART2_BAUDL = 0x0848               ; Baud Rate low byte
.equ USART2_BAUDH = 0x0849               ; Baud Rate hi byte
.equ USART2_CTRLD = 0x084A               ; Control D
.equ USART2_DBGCTRL = 0x084B             ; Debug Control
.equ USART2_EVCTRL = 0x084C              ; Event Control
.equ USART2_TXPLCTRL = 0x084D            ; IRCOM Transmitter Pulse Length Control
.equ USART2_RXPLCTRL = 0x084E            ; IRCOM Receiver Pulse Length Control

;*************************************************************************
;** USERROW - User Row
;*************************************************************************

.equ USERROW_USERROW0 = 0x1080           ; User Row Byte 0
.equ USERROW_USERROW1 = 0x1081           ; User Row Byte 1
.equ USERROW_USERROW2 = 0x1082           ; User Row Byte 2
.equ USERROW_USERROW3 = 0x1083           ; User Row Byte 3
.equ USERROW_USERROW4 = 0x1084           ; User Row Byte 4
.equ USERROW_USERROW5 = 0x1085           ; User Row Byte 5
.equ USERROW_USERROW6 = 0x1086           ; User Row Byte 6
.equ USERROW_USERROW7 = 0x1087           ; User Row Byte 7
.equ USERROW_USERROW8 = 0x1088           ; User Row Byte 8
.equ USERROW_USERROW9 = 0x1089           ; User Row Byte 9
.equ USERROW_USERROW10 = 0x108A          ; User Row Byte 10
.equ USERROW_USERROW11 = 0x108B          ; User Row Byte 11
.equ USERROW_USERROW12 = 0x108C          ; User Row Byte 12
.equ USERROW_USERROW13 = 0x108D          ; User Row Byte 13
.equ USERROW_USERROW14 = 0x108E          ; User Row Byte 14
.equ USERROW_USERROW15 = 0x108F          ; User Row Byte 15
.equ USERROW_USERROW16 = 0x1090          ; User Row Byte 16
.equ USERROW_USERROW17 = 0x1091          ; User Row Byte 17
.equ USERROW_USERROW18 = 0x1092          ; User Row Byte 18
.equ USERROW_USERROW19 = 0x1093          ; User Row Byte 19
.equ USERROW_USERROW20 = 0x1094          ; User Row Byte 20
.equ USERROW_USERROW21 = 0x1095          ; User Row Byte 21
.equ USERROW_USERROW22 = 0x1096          ; User Row Byte 22
.equ USERROW_USERROW23 = 0x1097          ; User Row Byte 23
.equ USERROW_USERROW24 = 0x1098          ; User Row Byte 24
.equ USERROW_USERROW25 = 0x1099          ; User Row Byte 25
.equ USERROW_USERROW26 = 0x109A          ; User Row Byte 26
.equ USERROW_USERROW27 = 0x109B          ; User Row Byte 27
.equ USERROW_USERROW28 = 0x109C          ; User Row Byte 28
.equ USERROW_USERROW29 = 0x109D          ; User Row Byte 29
.equ USERROW_USERROW30 = 0x109E          ; User Row Byte 30
.equ USERROW_USERROW31 = 0x109F          ; User Row Byte 31
.equ USERROW_USERROW32 = 0x10A0          ; User Row Byte 32
.equ USERROW_USERROW33 = 0x10A1          ; User Row Byte 33
.equ USERROW_USERROW34 = 0x10A2          ; User Row Byte 34
.equ USERROW_USERROW35 = 0x10A3          ; User Row Byte 35
.equ USERROW_USERROW36 = 0x10A4          ; User Row Byte 36
.equ USERROW_USERROW37 = 0x10A5          ; User Row Byte 37
.equ USERROW_USERROW38 = 0x10A6          ; User Row Byte 38
.equ USERROW_USERROW39 = 0x10A7          ; User Row Byte 39
.equ USERROW_USERROW40 = 0x10A8          ; User Row Byte 40
.equ USERROW_USERROW41 = 0x10A9          ; User Row Byte 41
.equ USERROW_USERROW42 = 0x10AA          ; User Row Byte 42
.equ USERROW_USERROW43 = 0x10AB          ; User Row Byte 43
.equ USERROW_USERROW44 = 0x10AC          ; User Row Byte 44
.equ USERROW_USERROW45 = 0x10AD          ; User Row Byte 45
.equ USERROW_USERROW46 = 0x10AE          ; User Row Byte 46
.equ USERROW_USERROW47 = 0x10AF          ; User Row Byte 47
.equ USERROW_USERROW48 = 0x10B0          ; User Row Byte 48
.equ USERROW_USERROW49 = 0x10B1          ; User Row Byte 49
.equ USERROW_USERROW50 = 0x10B2          ; User Row Byte 50
.equ USERROW_USERROW51 = 0x10B3          ; User Row Byte 51
.equ USERROW_USERROW52 = 0x10B4          ; User Row Byte 52
.equ USERROW_USERROW53 = 0x10B5          ; User Row Byte 53
.equ USERROW_USERROW54 = 0x10B6          ; User Row Byte 54
.equ USERROW_USERROW55 = 0x10B7          ; User Row Byte 55
.equ USERROW_USERROW56 = 0x10B8          ; User Row Byte 56
.equ USERROW_USERROW57 = 0x10B9          ; User Row Byte 57
.equ USERROW_USERROW58 = 0x10BA          ; User Row Byte 58
.equ USERROW_USERROW59 = 0x10BB          ; User Row Byte 59
.equ USERROW_USERROW60 = 0x10BC          ; User Row Byte 60
.equ USERROW_USERROW61 = 0x10BD          ; User Row Byte 61
.equ USERROW_USERROW62 = 0x10BE          ; User Row Byte 62
.equ USERROW_USERROW63 = 0x10BF          ; User Row Byte 63

;*************************************************************************
;** VPORTA - Virtual Ports
;*************************************************************************

.equ VPORTA_DIR = 0x0000                 ; Data Direction
.equ VPORTA_OUT = 0x0001                 ; Output Value
.equ VPORTA_IN = 0x0002                  ; Input Value
.equ VPORTA_INTFLAGS = 0x0003            ; Interrupt Flags

;*************************************************************************
;** VPORTB - Virtual Ports
;*************************************************************************

.equ VPORTB_DIR = 0x0004                 ; Data Direction
.equ VPORTB_OUT = 0x0005                 ; Output Value
.equ VPORTB_IN = 0x0006                  ; Input Value
.equ VPORTB_INTFLAGS = 0x0007            ; Interrupt Flags

;*************************************************************************
;** VPORTC - Virtual Ports
;*************************************************************************

.equ VPORTC_DIR = 0x0008                 ; Data Direction
.equ VPORTC_OUT = 0x0009                 ; Output Value
.equ VPORTC_IN = 0x000A                  ; Input Value
.equ VPORTC_INTFLAGS = 0x000B            ; Interrupt Flags

;*************************************************************************
;** VPORTD - Virtual Ports
;*************************************************************************

.equ VPORTD_DIR = 0x000C                 ; Data Direction
.equ VPORTD_OUT = 0x000D                 ; Output Value
.equ VPORTD_IN = 0x000E                  ; Input Value
.equ VPORTD_INTFLAGS = 0x000F            ; Interrupt Flags

;*************************************************************************
;** VPORTE - Virtual Ports
;*************************************************************************

.equ VPORTE_DIR = 0x0010                 ; Data Direction
.equ VPORTE_OUT = 0x0011                 ; Output Value
.equ VPORTE_IN = 0x0012                  ; Input Value
.equ VPORTE_INTFLAGS = 0x0013            ; Interrupt Flags

;*************************************************************************
;** VPORTF - Virtual Ports
;*************************************************************************

.equ VPORTF_DIR = 0x0014                 ; Data Direction
.equ VPORTF_OUT = 0x0015                 ; Output Value
.equ VPORTF_IN = 0x0016                  ; Input Value
.equ VPORTF_INTFLAGS = 0x0017            ; Interrupt Flags

;*************************************************************************
;** VREF - Voltage reference
;*************************************************************************

.equ VREF_DAC0REF = 0x00B2               ; DAC0 Reference
.equ VREF_ACREF = 0x00B4                 ; AC Reference

;*************************************************************************
;** WDT - Watch-Dog Timer
;*************************************************************************

.equ WDT_CTRLA = 0x0100                  ; Control A
.equ WDT_STATUS = 0x0101                 ; Status


; ***** ALL MODULE BASE ADRESSES *****************************************

.equ AC0_base = 0x0680                   ; Analog Comparator
.equ AC1_base = 0x0688                   ; Analog Comparator
.equ ADC0_base = 0x0600                  ; Analog to Digital Converter
.equ BOD_base = 0x00A0                   ; Bod interface
.equ CCL_base = 0x01C0                   ; Configurable Custom Logic
.equ CLKCTRL_base = 0x0060               ; Clock controller
.equ CPU_base = 0x0030                   ; CPU
.equ CPUINT_base = 0x0110                ; Interrupt Controller
.equ CRCSCAN_base = 0x0120               ; CRCSCAN
.equ DAC0_base = 0x06A0                  ; Digital to Analog Converter
.equ EVSYS_base = 0x0200                 ; Event System
.equ FUSE_base = 0x1050                  ; Fuses
.equ GPR_base = 0x001C                   ; General Purpose Registers
.equ LOCK_base = 0x1040                  ; Lockbits
.equ NVMCTRL_base = 0x1000               ; Non-volatile Memory Controller
.equ PORTA_base = 0x0400                 ; I/O Ports
.equ PORTB_base = 0x0420                 ; I/O Ports
.equ PORTC_base = 0x0440                 ; I/O Ports
.equ PORTD_base = 0x0460                 ; I/O Ports
.equ PORTE_base = 0x0480                 ; I/O Ports
.equ PORTF_base = 0x04A0                 ; I/O Ports
.equ PORTMUX_base = 0x05E0               ; Port Multiplexer
.equ RSTCTRL_base = 0x0040               ; Reset controller
.equ RTC_base = 0x0140                   ; Real-Time Counter
.equ SIGROW_base = 0x1100                ; Signature row
.equ SLPCTRL_base = 0x0050               ; Sleep Controller
.equ SPI0_base = 0x0940                  ; Serial Peripheral Interface
.equ SYSCFG_base = 0x0F00                ; System Configuration Registers
.equ TCA0_base = 0x0A00                  ; 16-bit Timer/Counter Type A
.equ TCA1_base = 0x0A40                  ; 16-bit Timer/Counter Type A
.equ TCB0_base = 0x0B00                  ; 16-bit Timer Type B
.equ TCB1_base = 0x0B10                  ; 16-bit Timer Type B
.equ TCB2_base = 0x0B20                  ; 16-bit Timer Type B
.equ TCB3_base = 0x0B30                  ; 16-bit Timer Type B
.equ TWI0_base = 0x0900                  ; Two-Wire Interface
.equ USART0_base = 0x0800                ; Universal Synchronous and Asynchronous Receiver and Transmitter
.equ USART1_base = 0x0820                ; Universal Synchronous and Asynchronous Receiver and Transmitter
.equ USART2_base = 0x0840                ; Universal Synchronous and Asynchronous Receiver and Transmitter
.equ USERROW_base = 0x1080               ; User Row
.equ VPORTA_base = 0x0000                ; Virtual Ports
.equ VPORTB_base = 0x0004                ; Virtual Ports
.equ VPORTC_base = 0x0008                ; Virtual Ports
.equ VPORTD_base = 0x000C                ; Virtual Ports
.equ VPORTE_base = 0x0010                ; Virtual Ports
.equ VPORTF_base = 0x0014                ; Virtual Ports
.equ VREF_base = 0x00B0                  ; Voltage reference
.equ WDT_base = 0x0100                   ; Watch-Dog Timer


; ***** IO REGISTER OFFSETS **********************************************


;*************************************************************************
;** AC - Analog Comparator
;*************************************************************************
.equ AC_CTRLA_offset = 0x00              ; Control A
.equ AC_CTRLB_offset = 0x01              ; Control B
.equ AC_MUXCTRL_offset = 0x02            ; Mux Control A
.equ AC_DACREF_offset = 0x05             ; DAC Voltage Reference
.equ AC_INTCTRL_offset = 0x06            ; Interrupt Control
.equ AC_STATUS_offset = 0x07             ; Status

;*************************************************************************
;** ADC - Analog to Digital Converter
;*************************************************************************
.equ ADC_CTRLA_offset = 0x00             ; Control A
.equ ADC_CTRLB_offset = 0x01             ; Control B
.equ ADC_CTRLC_offset = 0x02             ; Control C
.equ ADC_CTRLD_offset = 0x03             ; Control D
.equ ADC_INTCTRL_offset = 0x04           ; Interrupt Control
.equ ADC_INTFLAGS_offset = 0x05          ; Interrupt Flags
.equ ADC_STATUS_offset = 0x06            ; Status register
.equ ADC_DBGCTRL_offset = 0x07           ; Debug Control
.equ ADC_CTRLE_offset = 0x08             ; Control E
.equ ADC_CTRLF_offset = 0x09             ; Control F
.equ ADC_COMMAND_offset = 0x0A           ; Command register
.equ ADC_PGACTRL_offset = 0x0B           ; PGA Control
.equ ADC_MUXPOS_offset = 0x0C            ; Positive Input Multiplexer
.equ ADC_MUXNEG_offset = 0x0D            ; Negative Input Multiplexer
.equ ADC_RESULT_offset = 0x10            ; Result
.equ ADC_SAMPLE_offset = 0x14            ; Sample
.equ ADC_TEMP0_offset = 0x18             ; Temporary Data 0
.equ ADC_TEMP1_offset = 0x19             ; Temporary Data 1
.equ ADC_TEMP2_offset = 0x1A             ; Temporary Data 2
.equ ADC_WINLT_offset = 0x1C             ; Window Low Threshold
.equ ADC_WINHT_offset = 0x1E             ; Window High Threshold

;*************************************************************************
;** BOD - Bod interface
;*************************************************************************
.equ BOD_CTRLA_offset = 0x00             ; Control A
.equ BOD_CTRLB_offset = 0x01             ; Control B
.equ BOD_VLMCTRLA_offset = 0x08          ; Voltage level monitor Control
.equ BOD_INTCTRL_offset = 0x09           ; Voltage level monitor interrupt Control
.equ BOD_INTFLAGS_offset = 0x0A          ; Voltage level monitor interrupt Flags
.equ BOD_STATUS_offset = 0x0B            ; Voltage level monitor status

;*************************************************************************
;** CCL - Configurable Custom Logic
;*************************************************************************
.equ CCL_CTRLA_offset = 0x00             ; Control Register A
.equ CCL_SEQCTRL0_offset = 0x01          ; Sequential Control 0
.equ CCL_SEQCTRL1_offset = 0x02          ; Sequential Control 1
.equ CCL_INTCTRL0_offset = 0x05          ; Interrupt Control 0
.equ CCL_INTFLAGS_offset = 0x07          ; Interrupt Flags
.equ CCL_LUT0CTRLA_offset = 0x08         ; LUT 0 Control A
.equ CCL_LUT0CTRLB_offset = 0x09         ; LUT 0 Control B
.equ CCL_LUT0CTRLC_offset = 0x0A         ; LUT 0 Control C
.equ CCL_TRUTH0_offset = 0x0B            ; Truth 0
.equ CCL_LUT1CTRLA_offset = 0x0C         ; LUT 1 Control A
.equ CCL_LUT1CTRLB_offset = 0x0D         ; LUT 1 Control B
.equ CCL_LUT1CTRLC_offset = 0x0E         ; LUT 1 Control C
.equ CCL_TRUTH1_offset = 0x0F            ; Truth 1
.equ CCL_LUT2CTRLA_offset = 0x10         ; LUT 2 Control A
.equ CCL_LUT2CTRLB_offset = 0x11         ; LUT 2 Control B
.equ CCL_LUT2CTRLC_offset = 0x12         ; LUT 2 Control C
.equ CCL_TRUTH2_offset = 0x13            ; Truth 2
.equ CCL_LUT3CTRLA_offset = 0x14         ; LUT 3 Control A
.equ CCL_LUT3CTRLB_offset = 0x15         ; LUT 3 Control B
.equ CCL_LUT3CTRLC_offset = 0x16         ; LUT 3 Control C
.equ CCL_TRUTH3_offset = 0x17            ; Truth 3

;*************************************************************************
;** CLKCTRL - Clock controller
;*************************************************************************
.equ CLKCTRL_MCLKCTRLA_offset = 0x00     ; MCLK Control A
.equ CLKCTRL_MCLKCTRLB_offset = 0x01     ; MCLK Control B
.equ CLKCTRL_MCLKCTRLC_offset = 0x02     ; MCLK Control C
.equ CLKCTRL_MCLKINTCTRL_offset = 0x03   ; MCLK Interrupt Control
.equ CLKCTRL_MCLKINTFLAGS_offset = 0x04  ; MCLK Interrupt Flags
.equ CLKCTRL_MCLKSTATUS_offset = 0x05    ; MCLK Status
.equ CLKCTRL_MCLKTIMEBASE_offset = 0x06  ; MCLK Timebase
.equ CLKCTRL_OSCHFCTRLA_offset = 0x08    ; OSCHF Control A
.equ CLKCTRL_OSCHFTUNE_offset = 0x09     ; OSCHF Tune
.equ CLKCTRL_OSC32KCTRLA_offset = 0x18   ; OSC32K Control A
.equ CLKCTRL_XOSC32KCTRLA_offset = 0x1C  ; XOSC32K Control A
.equ CLKCTRL_XOSCHFCTRLA_offset = 0x20   ; XOSCHF Control A

;*************************************************************************
;** CPU - CPU
;*************************************************************************
.equ CPU_CCP_offset = 0x04               ; Configuration Change Protection
.equ CPU_SP_offset = 0x0D                ; Stack Pointer
.equ CPU_SREG_offset = 0x0F              ; Status Register

;*************************************************************************
;** CPUINT - Interrupt Controller
;*************************************************************************
.equ CPUINT_CTRLA_offset = 0x00          ; Control A
.equ CPUINT_STATUS_offset = 0x01         ; Status
.equ CPUINT_LVL0PRI_offset = 0x02        ; Interrupt Level 0 Priority
.equ CPUINT_LVL1VEC_offset = 0x03        ; Interrupt Level 1 Priority Vector

;*************************************************************************
;** CRCSCAN - CRCSCAN
;*************************************************************************
.equ CRCSCAN_CTRLA_offset = 0x00         ; Control A
.equ CRCSCAN_CTRLB_offset = 0x01         ; Control B
.equ CRCSCAN_STATUS_offset = 0x02        ; Status

;*************************************************************************
;** DAC - Digital to Analog Converter
;*************************************************************************
.equ DAC_CTRLA_offset = 0x00             ; Control Register A
.equ DAC_DATA_offset = 0x02              ; DATA Register

;*************************************************************************
;** EVSYS - Event System
;*************************************************************************
.equ EVSYS_SWEVENTA_offset = 0x00        ; Software Event A
.equ EVSYS_CHANNEL0_offset = 0x10        ; Multiplexer Channel 0
.equ EVSYS_CHANNEL1_offset = 0x11        ; Multiplexer Channel 1
.equ EVSYS_CHANNEL2_offset = 0x12        ; Multiplexer Channel 2
.equ EVSYS_CHANNEL3_offset = 0x13        ; Multiplexer Channel 3
.equ EVSYS_CHANNEL4_offset = 0x14        ; Multiplexer Channel 4
.equ EVSYS_CHANNEL5_offset = 0x15        ; Multiplexer Channel 5
.equ EVSYS_USERCCLLUT0A_offset = 0x20    ; CCL0 Event A
.equ EVSYS_USERCCLLUT0B_offset = 0x21    ; CCL0 Event B
.equ EVSYS_USERCCLLUT1A_offset = 0x22    ; CCL1 Event A
.equ EVSYS_USERCCLLUT1B_offset = 0x23    ; CCL1 Event B
.equ EVSYS_USERCCLLUT2A_offset = 0x24    ; CCL2 Event A
.equ EVSYS_USERCCLLUT2B_offset = 0x25    ; CCL2 Event B
.equ EVSYS_USERCCLLUT3A_offset = 0x26    ; CCL3 Event A
.equ EVSYS_USERCCLLUT3B_offset = 0x27    ; CCL3 Event B
.equ EVSYS_USERADC0START_offset = 0x28   ; ADC0
.equ EVSYS_USEREVSYSEVOUTA_offset = 0x29 ; EVOUTA
.equ EVSYS_USEREVSYSEVOUTB_offset = 0x2A ; EVOUTB
.equ EVSYS_USEREVSYSEVOUTC_offset = 0x2B ; EVOUTC
.equ EVSYS_USEREVSYSEVOUTD_offset = 0x2C ; EVOUTD
.equ EVSYS_USEREVSYSEVOUTE_offset = 0x2D ; EVOUTE
.equ EVSYS_USEREVSYSEVOUTF_offset = 0x2E ; EVOUTF
.equ EVSYS_USERUSART0IRDA_offset = 0x2F  ; USART0
.equ EVSYS_USERUSART1IRDA_offset = 0x30  ; USART1
.equ EVSYS_USERUSART2IRDA_offset = 0x31  ; USART2
.equ EVSYS_USERTCA0CNTA_offset = 0x32    ; TCA0 Event A
.equ EVSYS_USERTCA0CNTB_offset = 0x33    ; TCA0 Event B
.equ EVSYS_USERTCA1CNTA_offset = 0x34    ; TCA1 Event A
.equ EVSYS_USERTCA1CNTB_offset = 0x35    ; TCA1 Event B
.equ EVSYS_USERTCB0CAPT_offset = 0x36    ; TCB0 Event A
.equ EVSYS_USERTCB0COUNT_offset = 0x37   ; TCB0 Event B
.equ EVSYS_USERTCB1CAPT_offset = 0x38    ; TCB1 Event A
.equ EVSYS_USERTCB1COUNT_offset = 0x39   ; TCB1 Event B
.equ EVSYS_USERTCB2CAPT_offset = 0x3A    ; TCB2 Event A
.equ EVSYS_USERTCB2COUNT_offset = 0x3B   ; TCB2 Event B
.equ EVSYS_USERTCB3CAPT_offset = 0x3C    ; TCB3 Event A
.equ EVSYS_USERTCB3COUNT_offset = 0x3D   ; TCB3 Event B

;*************************************************************************
;** FUSE - Fuses
;*************************************************************************
.equ FUSE_WDTCFG_offset = 0x00           ; Watchdog Configuration
.equ FUSE_BODCFG_offset = 0x01           ; BOD Configuration
.equ FUSE_OSCCFG_offset = 0x02           ; Oscillator Configuration
.equ FUSE_SYSCFG0_offset = 0x05          ; System Configuration 0
.equ FUSE_SYSCFG1_offset = 0x06          ; System Configuration 1
.equ FUSE_CODESIZE_offset = 0x07         ; Code Section Size
.equ FUSE_BOOTSIZE_offset = 0x08         ; Boot Section Size

;*************************************************************************
;** GPR - General Purpose Registers
;*************************************************************************
.equ GPR_GPR0_offset = 0x00              ; General Purpose Register 0
.equ GPR_GPR1_offset = 0x01              ; General Purpose Register 1
.equ GPR_GPR2_offset = 0x02              ; General Purpose Register 2
.equ GPR_GPR3_offset = 0x03              ; General Purpose Register 3

;*************************************************************************
;** LOCK - Lockbits
;*************************************************************************
.equ LOCK_KEY_offset = 0x00              ; Lock Key Bits

;*************************************************************************
;** NVMCTRL - Non-volatile Memory Controller
;*************************************************************************
.equ NVMCTRL_CTRLA_offset = 0x00         ; Control A
.equ NVMCTRL_CTRLB_offset = 0x01         ; Control B
.equ NVMCTRL_INTCTRL_offset = 0x04       ; Interrupt Control
.equ NVMCTRL_INTFLAGS_offset = 0x05      ; Interrupt Flags
.equ NVMCTRL_STATUS_offset = 0x06        ; Status
.equ NVMCTRL_DATA_offset = 0x08          ; Data
.equ NVMCTRL_ADDR_offset = 0x0C          ; Address

;*************************************************************************
;** PORT - I/O Ports
;*************************************************************************
.equ PORT_DIR_offset = 0x00              ; Data Direction
.equ PORT_DIRSET_offset = 0x01           ; Data Direction Set
.equ PORT_DIRCLR_offset = 0x02           ; Data Direction Clear
.equ PORT_DIRTGL_offset = 0x03           ; Data Direction Toggle
.equ PORT_OUT_offset = 0x04              ; Output Value
.equ PORT_OUTSET_offset = 0x05           ; Output Value Set
.equ PORT_OUTCLR_offset = 0x06           ; Output Value Clear
.equ PORT_OUTTGL_offset = 0x07           ; Output Value Toggle
.equ PORT_IN_offset = 0x08               ; Input Value
.equ PORT_INTFLAGS_offset = 0x09         ; Interrupt Flags
.equ PORT_PORTCTRL_offset = 0x0A         ; Port Control
.equ PORT_PINCONFIG_offset = 0x0B        ; Pin Control Config
.equ PORT_PINCTRLUPD_offset = 0x0C       ; Pin Control Update
.equ PORT_PINCTRLSET_offset = 0x0D       ; Pin Control Set
.equ PORT_PINCTRLCLR_offset = 0x0E       ; Pin Control Clear
.equ PORT_PIN0CTRL_offset = 0x10         ; Pin 0 Control
.equ PORT_PIN1CTRL_offset = 0x11         ; Pin 1 Control
.equ PORT_PIN2CTRL_offset = 0x12         ; Pin 2 Control
.equ PORT_PIN3CTRL_offset = 0x13         ; Pin 3 Control
.equ PORT_PIN4CTRL_offset = 0x14         ; Pin 4 Control
.equ PORT_PIN5CTRL_offset = 0x15         ; Pin 5 Control
.equ PORT_PIN6CTRL_offset = 0x16         ; Pin 6 Control
.equ PORT_PIN7CTRL_offset = 0x17         ; Pin 7 Control
.equ PORT_EVGENCTRL_offset = 0x18        ; Event Generation Control

;*************************************************************************
;** PORTMUX - Port Multiplexer
;*************************************************************************
.equ PORTMUX_EVSYSROUTEA_offset = 0x00   ; EVSYS route A
.equ PORTMUX_CCLROUTEA_offset = 0x01     ; CCL route A
.equ PORTMUX_USARTROUTEA_offset = 0x02   ; USART route A
.equ PORTMUX_USARTROUTEB_offset = 0x03   ; USART route B
.equ PORTMUX_SPIROUTEA_offset = 0x05     ; SPI route A
.equ PORTMUX_TWIROUTEA_offset = 0x06     ; TWI route A
.equ PORTMUX_TCAROUTEA_offset = 0x07     ; TCA route A
.equ PORTMUX_TCBROUTEA_offset = 0x08     ; TCB route A
.equ PORTMUX_ACROUTEA_offset = 0x0A      ; AC route A

;*************************************************************************
;** RSTCTRL - Reset controller
;*************************************************************************
.equ RSTCTRL_RSTFR_offset = 0x00         ; Reset Flags
.equ RSTCTRL_SWRR_offset = 0x01          ; Software Reset

;*************************************************************************
;** RTC - Real-Time Counter
;*************************************************************************
.equ RTC_CTRLA_offset = 0x00             ; Control A
.equ RTC_STATUS_offset = 0x01            ; Status
.equ RTC_INTCTRL_offset = 0x02           ; Interrupt Control
.equ RTC_INTFLAGS_offset = 0x03          ; Interrupt Flags
.equ RTC_TEMP_offset = 0x04              ; Temporary
.equ RTC_DBGCTRL_offset = 0x05           ; Debug control
.equ RTC_CALIB_offset = 0x06             ; Calibration
.equ RTC_CLKSEL_offset = 0x07            ; Clock Select
.equ RTC_CNT_offset = 0x08               ; Counter
.equ RTC_PER_offset = 0x0A               ; Period
.equ RTC_CMP_offset = 0x0C               ; Compare
.equ RTC_PITCTRLA_offset = 0x10          ; PIT Control A
.equ RTC_PITSTATUS_offset = 0x11         ; PIT Status
.equ RTC_PITINTCTRL_offset = 0x12        ; PIT Interrupt Control
.equ RTC_PITINTFLAGS_offset = 0x13       ; PIT Interrupt Flags
.equ RTC_PITDBGCTRL_offset = 0x15        ; PIT Debug control
.equ RTC_PITEVGENCTRLA_offset = 0x16     ; PIT Event Generation Control A

;*************************************************************************
;** SIGROW - Signature row
;*************************************************************************
.equ SIGROW_DEVICEID0_offset = 0x00      ; Device ID Byte 0
.equ SIGROW_DEVICEID1_offset = 0x01      ; Device ID Byte 1
.equ SIGROW_DEVICEID2_offset = 0x02      ; Device ID Byte 2
.equ SIGROW_TEMPSENSE0_offset = 0x04     ; Temperature Calibration 0
.equ SIGROW_TEMPSENSE1_offset = 0x06     ; Temperature Calibration 1
.equ SIGROW_SERNUM0_offset = 0x10        ; Serial Number Byte 0
.equ SIGROW_SERNUM1_offset = 0x11        ; Serial Number Byte 1
.equ SIGROW_SERNUM2_offset = 0x12        ; Serial Number Byte 2
.equ SIGROW_SERNUM3_offset = 0x13        ; Serial Number Byte 3
.equ SIGROW_SERNUM4_offset = 0x14        ; Serial Number Byte 4
.equ SIGROW_SERNUM5_offset = 0x15        ; Serial Number Byte 5
.equ SIGROW_SERNUM6_offset = 0x16        ; Serial Number Byte 6
.equ SIGROW_SERNUM7_offset = 0x17        ; Serial Number Byte 7
.equ SIGROW_SERNUM8_offset = 0x18        ; Serial Number Byte 8
.equ SIGROW_SERNUM9_offset = 0x19        ; Serial Number Byte 9
.equ SIGROW_SERNUM10_offset = 0x1A       ; Serial Number Byte 10
.equ SIGROW_SERNUM11_offset = 0x1B       ; Serial Number Byte 11
.equ SIGROW_SERNUM12_offset = 0x1C       ; Serial Number Byte 12
.equ SIGROW_SERNUM13_offset = 0x1D       ; Serial Number Byte 13
.equ SIGROW_SERNUM14_offset = 0x1E       ; Serial Number Byte 14
.equ SIGROW_SERNUM15_offset = 0x1F       ; Serial Number Byte 15

;*************************************************************************
;** SLPCTRL - Sleep Controller
;*************************************************************************
.equ SLPCTRL_CTRLA_offset = 0x00         ; Control A

;*************************************************************************
;** SPI - Serial Peripheral Interface
;*************************************************************************
.equ SPI_CTRLA_offset = 0x00             ; Control A
.equ SPI_CTRLB_offset = 0x01             ; Control B
.equ SPI_INTCTRL_offset = 0x02           ; Interrupt Control
.equ SPI_INTFLAGS_offset = 0x03          ; Interrupt Flags
.equ SPI_DATA_offset = 0x04              ; Data

;*************************************************************************
;** SYSCFG - System Configuration Registers
;*************************************************************************
.equ SYSCFG_REVID_offset = 0x01          ; Revision ID
.equ SYSCFG_OCDMCTRL_offset = 0x04       ; OCD Message Control
.equ SYSCFG_OCDMSTATUS_offset = 0x05     ; OCD Message Status

;*************************************************************************
;** TCA - 16-bit Timer/Counter Type A
;*************************************************************************
.equ TCA_SINGLE_CTRLA_offset = 0x00      ; Control A
.equ TCA_SPLIT_CTRLA_offset = 0x00       ; Control A
.equ TCA_SINGLE_CTRLB_offset = 0x01      ; Control B
.equ TCA_SPLIT_CTRLB_offset = 0x01       ; Control B
.equ TCA_SINGLE_CTRLC_offset = 0x02      ; Control C
.equ TCA_SPLIT_CTRLC_offset = 0x02       ; Control C
.equ TCA_SINGLE_CTRLD_offset = 0x03      ; Control D
.equ TCA_SPLIT_CTRLD_offset = 0x03       ; Control D
.equ TCA_SINGLE_CTRLECLR_offset = 0x04   ; Control E Clear
.equ TCA_SPLIT_CTRLECLR_offset = 0x04    ; Control E Clear
.equ TCA_SINGLE_CTRLESET_offset = 0x05   ; Control E Set
.equ TCA_SPLIT_CTRLESET_offset = 0x05    ; Control E Set
.equ TCA_SINGLE_CTRLFCLR_offset = 0x06   ; Control F Clear
.equ TCA_SINGLE_CTRLFSET_offset = 0x07   ; Control F Set
.equ TCA_SINGLE_EVCTRL_offset = 0x09     ; Event Control
.equ TCA_SINGLE_INTCTRL_offset = 0x0A    ; Interrupt Control
.equ TCA_SPLIT_INTCTRL_offset = 0x0A     ; Interrupt Control
.equ TCA_SINGLE_INTFLAGS_offset = 0x0B   ; Interrupt Flags
.equ TCA_SPLIT_INTFLAGS_offset = 0x0B    ; Interrupt Flags
.equ TCA_SINGLE_DBGCTRL_offset = 0x0E    ; Debug Control
.equ TCA_SPLIT_DBGCTRL_offset = 0x0E     ; Debug Control
.equ TCA_SINGLE_TEMP_offset = 0x0F       ; Temporary data for 16-bit Access
.equ TCA_SINGLE_CNT_offset = 0x20        ; Count
.equ TCA_SPLIT_LCNT_offset = 0x20        ; Low Count
.equ TCA_SPLIT_HCNT_offset = 0x21        ; High Count
.equ TCA_SINGLE_PER_offset = 0x26        ; Period
.equ TCA_SPLIT_LPER_offset = 0x26        ; Low Period
.equ TCA_SPLIT_HPER_offset = 0x27        ; High Period
.equ TCA_SINGLE_CMP0_offset = 0x28       ; Compare 0
.equ TCA_SPLIT_LCMP0_offset = 0x28       ; Low Compare
.equ TCA_SPLIT_HCMP0_offset = 0x29       ; High Compare
.equ TCA_SINGLE_CMP1_offset = 0x2A       ; Compare 1
.equ TCA_SPLIT_LCMP1_offset = 0x2A       ; Low Compare
.equ TCA_SPLIT_HCMP1_offset = 0x2B       ; High Compare
.equ TCA_SINGLE_CMP2_offset = 0x2C       ; Compare 2
.equ TCA_SPLIT_LCMP2_offset = 0x2C       ; Low Compare
.equ TCA_SPLIT_HCMP2_offset = 0x2D       ; High Compare
.equ TCA_SINGLE_PERBUF_offset = 0x36     ; Period Buffer
.equ TCA_SINGLE_CMP0BUF_offset = 0x38    ; Compare 0 Buffer
.equ TCA_SINGLE_CMP1BUF_offset = 0x3A    ; Compare 1 Buffer
.equ TCA_SINGLE_CMP2BUF_offset = 0x3C    ; Compare 2 Buffer

;*************************************************************************
;** TCB - 16-bit Timer Type B
;*************************************************************************
.equ TCB_CTRLA_offset = 0x00             ; Control A
.equ TCB_CTRLB_offset = 0x01             ; Control Register B
.equ TCB_EVCTRL_offset = 0x04            ; Event Control
.equ TCB_INTCTRL_offset = 0x05           ; Interrupt Control
.equ TCB_INTFLAGS_offset = 0x06          ; Interrupt Flags
.equ TCB_STATUS_offset = 0x07            ; Status
.equ TCB_DBGCTRL_offset = 0x08           ; Debug Control
.equ TCB_TEMP_offset = 0x09              ; Temporary Value
.equ TCB_CNT_offset = 0x0A               ; Count
.equ TCB_CCMP_offset = 0x0C              ; Compare or Capture

;*************************************************************************
;** TWI - Two-Wire Interface
;*************************************************************************
.equ TWI_CTRLA_offset = 0x00             ; Control A
.equ TWI_DUALCTRL_offset = 0x01          ; Dual Mode Control
.equ TWI_DBGCTRL_offset = 0x02           ; Debug Control
.equ TWI_MCTRLA_offset = 0x03            ; Host Control A
.equ TWI_MCTRLB_offset = 0x04            ; Host Control B
.equ TWI_MSTATUS_offset = 0x05           ; Host STATUS
.equ TWI_MBAUD_offset = 0x06             ; Host Baud Rate
.equ TWI_MADDR_offset = 0x07             ; Host Address
.equ TWI_MDATA_offset = 0x08             ; Host Data
.equ TWI_SCTRLA_offset = 0x09            ; Client Control A
.equ TWI_SCTRLB_offset = 0x0A            ; Client Control B
.equ TWI_SSTATUS_offset = 0x0B           ; Client Status
.equ TWI_SADDR_offset = 0x0C             ; Client Address
.equ TWI_SDATA_offset = 0x0D             ; Client Data
.equ TWI_SADDRMASK_offset = 0x0E         ; Client Address Mask

;*************************************************************************
;** USART - Universal Synchronous and Asynchronous Receiver and Transmitter
;*************************************************************************
.equ USART_RXDATAL_offset = 0x00         ; Receive Data Low Byte
.equ USART_RXDATAH_offset = 0x01         ; Receive Data High Byte
.equ USART_TXDATAL_offset = 0x02         ; Transmit Data Low Byte
.equ USART_TXDATAH_offset = 0x03         ; Transmit Data High Byte
.equ USART_STATUS_offset = 0x04          ; Status
.equ USART_CTRLA_offset = 0x05           ; Control A
.equ USART_CTRLB_offset = 0x06           ; Control B
.equ USART_CTRLC_offset = 0x07           ; Control C
.equ USART_BAUD_offset = 0x08            ; Baud Rate
.equ USART_CTRLD_offset = 0x0A           ; Control D
.equ USART_DBGCTRL_offset = 0x0B         ; Debug Control
.equ USART_EVCTRL_offset = 0x0C          ; Event Control
.equ USART_TXPLCTRL_offset = 0x0D        ; IRCOM Transmitter Pulse Length Control
.equ USART_RXPLCTRL_offset = 0x0E        ; IRCOM Receiver Pulse Length Control

;*************************************************************************
;** USERROW - User Row
;*************************************************************************
.equ USERROW_USERROW0_offset = 0x00      ; User Row Byte 0
.equ USERROW_USERROW1_offset = 0x01      ; User Row Byte 1
.equ USERROW_USERROW2_offset = 0x02      ; User Row Byte 2
.equ USERROW_USERROW3_offset = 0x03      ; User Row Byte 3
.equ USERROW_USERROW4_offset = 0x04      ; User Row Byte 4
.equ USERROW_USERROW5_offset = 0x05      ; User Row Byte 5
.equ USERROW_USERROW6_offset = 0x06      ; User Row Byte 6
.equ USERROW_USERROW7_offset = 0x07      ; User Row Byte 7
.equ USERROW_USERROW8_offset = 0x08      ; User Row Byte 8
.equ USERROW_USERROW9_offset = 0x09      ; User Row Byte 9
.equ USERROW_USERROW10_offset = 0x0A     ; User Row Byte 10
.equ USERROW_USERROW11_offset = 0x0B     ; User Row Byte 11
.equ USERROW_USERROW12_offset = 0x0C     ; User Row Byte 12
.equ USERROW_USERROW13_offset = 0x0D     ; User Row Byte 13
.equ USERROW_USERROW14_offset = 0x0E     ; User Row Byte 14
.equ USERROW_USERROW15_offset = 0x0F     ; User Row Byte 15
.equ USERROW_USERROW16_offset = 0x10     ; User Row Byte 16
.equ USERROW_USERROW17_offset = 0x11     ; User Row Byte 17
.equ USERROW_USERROW18_offset = 0x12     ; User Row Byte 18
.equ USERROW_USERROW19_offset = 0x13     ; User Row Byte 19
.equ USERROW_USERROW20_offset = 0x14     ; User Row Byte 20
.equ USERROW_USERROW21_offset = 0x15     ; User Row Byte 21
.equ USERROW_USERROW22_offset = 0x16     ; User Row Byte 22
.equ USERROW_USERROW23_offset = 0x17     ; User Row Byte 23
.equ USERROW_USERROW24_offset = 0x18     ; User Row Byte 24
.equ USERROW_USERROW25_offset = 0x19     ; User Row Byte 25
.equ USERROW_USERROW26_offset = 0x1A     ; User Row Byte 26
.equ USERROW_USERROW27_offset = 0x1B     ; User Row Byte 27
.equ USERROW_USERROW28_offset = 0x1C     ; User Row Byte 28
.equ USERROW_USERROW29_offset = 0x1D     ; User Row Byte 29
.equ USERROW_USERROW30_offset = 0x1E     ; User Row Byte 30
.equ USERROW_USERROW31_offset = 0x1F     ; User Row Byte 31
.equ USERROW_USERROW32_offset = 0x20     ; User Row Byte 32
.equ USERROW_USERROW33_offset = 0x21     ; User Row Byte 33
.equ USERROW_USERROW34_offset = 0x22     ; User Row Byte 34
.equ USERROW_USERROW35_offset = 0x23     ; User Row Byte 35
.equ USERROW_USERROW36_offset = 0x24     ; User Row Byte 36
.equ USERROW_USERROW37_offset = 0x25     ; User Row Byte 37
.equ USERROW_USERROW38_offset = 0x26     ; User Row Byte 38
.equ USERROW_USERROW39_offset = 0x27     ; User Row Byte 39
.equ USERROW_USERROW40_offset = 0x28     ; User Row Byte 40
.equ USERROW_USERROW41_offset = 0x29     ; User Row Byte 41
.equ USERROW_USERROW42_offset = 0x2A     ; User Row Byte 42
.equ USERROW_USERROW43_offset = 0x2B     ; User Row Byte 43
.equ USERROW_USERROW44_offset = 0x2C     ; User Row Byte 44
.equ USERROW_USERROW45_offset = 0x2D     ; User Row Byte 45
.equ USERROW_USERROW46_offset = 0x2E     ; User Row Byte 46
.equ USERROW_USERROW47_offset = 0x2F     ; User Row Byte 47
.equ USERROW_USERROW48_offset = 0x30     ; User Row Byte 48
.equ USERROW_USERROW49_offset = 0x31     ; User Row Byte 49
.equ USERROW_USERROW50_offset = 0x32     ; User Row Byte 50
.equ USERROW_USERROW51_offset = 0x33     ; User Row Byte 51
.equ USERROW_USERROW52_offset = 0x34     ; User Row Byte 52
.equ USERROW_USERROW53_offset = 0x35     ; User Row Byte 53
.equ USERROW_USERROW54_offset = 0x36     ; User Row Byte 54
.equ USERROW_USERROW55_offset = 0x37     ; User Row Byte 55
.equ USERROW_USERROW56_offset = 0x38     ; User Row Byte 56
.equ USERROW_USERROW57_offset = 0x39     ; User Row Byte 57
.equ USERROW_USERROW58_offset = 0x3A     ; User Row Byte 58
.equ USERROW_USERROW59_offset = 0x3B     ; User Row Byte 59
.equ USERROW_USERROW60_offset = 0x3C     ; User Row Byte 60
.equ USERROW_USERROW61_offset = 0x3D     ; User Row Byte 61
.equ USERROW_USERROW62_offset = 0x3E     ; User Row Byte 62
.equ USERROW_USERROW63_offset = 0x3F     ; User Row Byte 63

;*************************************************************************
;** VPORT - Virtual Ports
;*************************************************************************
.equ VPORT_DIR_offset = 0x00             ; Data Direction
.equ VPORT_OUT_offset = 0x01             ; Output Value
.equ VPORT_IN_offset = 0x02              ; Input Value
.equ VPORT_INTFLAGS_offset = 0x03        ; Interrupt Flags

;*************************************************************************
;** VREF - Voltage reference
;*************************************************************************
.equ VREF_DAC0REF_offset = 0x02          ; DAC0 Reference
.equ VREF_ACREF_offset = 0x04            ; AC Reference

;*************************************************************************
;** WDT - Watch-Dog Timer
;*************************************************************************
.equ WDT_CTRLA_offset = 0x00             ; Control A
.equ WDT_STATUS_offset = 0x01            ; Status


; ***** LOCKBIT REGISTER LOCATIONS ***************************************




; ***** FUSE REGISTER LOCATIONS ******************************************




; ***** BIT AND VALUE DEFINITIONS ****************************************


;*************************************************************************
;** AC - Analog Comparator
;*************************************************************************

; AC_CTRLA masks
.equ AC_ENABLE_bm = 0x01                 ; Enable bit mask
.equ AC_ENABLE_bp = 0                    ; Enable bit position
.equ AC_HYSMODE_gm = 0x06                ; Hysteresis Mode group mask
.equ AC_HYSMODE_gp = 1                   ; Hysteresis Mode group position
.equ AC_HYSMODE_0_bm = (1<<1)            ; Hysteresis Mode bit 0 mask
.equ AC_HYSMODE_0_bp = 1                 ; Hysteresis Mode bit 0 position
.equ AC_HYSMODE_1_bm = (1<<2)            ; Hysteresis Mode bit 1 mask
.equ AC_HYSMODE_1_bp = 2                 ; Hysteresis Mode bit 1 position
.equ AC_POWER_gm = 0x18                  ; Power profile group mask
.equ AC_POWER_gp = 3                     ; Power profile group position
.equ AC_POWER_0_bm = (1<<3)              ; Power profile bit 0 mask
.equ AC_POWER_0_bp = 3                   ; Power profile bit 0 position
.equ AC_POWER_1_bm = (1<<4)              ; Power profile bit 1 mask
.equ AC_POWER_1_bp = 4                   ; Power profile bit 1 position
.equ AC_OUTEN_bm = 0x40                  ; Output Pad Enable bit mask
.equ AC_OUTEN_bp = 6                     ; Output Pad Enable bit position
.equ AC_RUNSTDBY_bm = 0x80               ; Run in Standby Mode bit mask
.equ AC_RUNSTDBY_bp = 7                  ; Run in Standby Mode bit position

; AC_CTRLB masks
.equ AC_WINSEL_gm = 0x03                 ; Window selection mode group mask
.equ AC_WINSEL_gp = 0                    ; Window selection mode group position
.equ AC_WINSEL_0_bm = (1<<0)             ; Window selection mode bit 0 mask
.equ AC_WINSEL_0_bp = 0                  ; Window selection mode bit 0 position
.equ AC_WINSEL_1_bm = (1<<1)             ; Window selection mode bit 1 mask
.equ AC_WINSEL_1_bp = 1                  ; Window selection mode bit 1 position

; AC_MUXCTRL masks
.equ AC_MUXNEG_gm = 0x07                 ; Negative Input MUX Selection group mask
.equ AC_MUXNEG_gp = 0                    ; Negative Input MUX Selection group position
.equ AC_MUXNEG_0_bm = (1<<0)             ; Negative Input MUX Selection bit 0 mask
.equ AC_MUXNEG_0_bp = 0                  ; Negative Input MUX Selection bit 0 position
.equ AC_MUXNEG_1_bm = (1<<1)             ; Negative Input MUX Selection bit 1 mask
.equ AC_MUXNEG_1_bp = 1                  ; Negative Input MUX Selection bit 1 position
.equ AC_MUXNEG_2_bm = (1<<2)             ; Negative Input MUX Selection bit 2 mask
.equ AC_MUXNEG_2_bp = 2                  ; Negative Input MUX Selection bit 2 position
.equ AC_MUXPOS_gm = 0x38                 ; Positive Input MUX Selection group mask
.equ AC_MUXPOS_gp = 3                    ; Positive Input MUX Selection group position
.equ AC_MUXPOS_0_bm = (1<<3)             ; Positive Input MUX Selection bit 0 mask
.equ AC_MUXPOS_0_bp = 3                  ; Positive Input MUX Selection bit 0 position
.equ AC_MUXPOS_1_bm = (1<<4)             ; Positive Input MUX Selection bit 1 mask
.equ AC_MUXPOS_1_bp = 4                  ; Positive Input MUX Selection bit 1 position
.equ AC_MUXPOS_2_bm = (1<<5)             ; Positive Input MUX Selection bit 2 mask
.equ AC_MUXPOS_2_bp = 5                  ; Positive Input MUX Selection bit 2 position
.equ AC_INITVAL_bm = 0x40                ; AC Output Initial Value bit mask
.equ AC_INITVAL_bp = 6                   ; AC Output Initial Value bit position
.equ AC_INVERT_bm = 0x80                 ; Invert AC Output bit mask
.equ AC_INVERT_bp = 7                    ; Invert AC Output bit position

; AC_DACREF masks
.equ AC_DACREF_gm = 0xFF                 ; DACREF group mask
.equ AC_DACREF_gp = 0                    ; DACREF group position
.equ AC_DACREF_0_bm = (1<<0)             ; DACREF bit 0 mask
.equ AC_DACREF_0_bp = 0                  ; DACREF bit 0 position
.equ AC_DACREF_1_bm = (1<<1)             ; DACREF bit 1 mask
.equ AC_DACREF_1_bp = 1                  ; DACREF bit 1 position
.equ AC_DACREF_2_bm = (1<<2)             ; DACREF bit 2 mask
.equ AC_DACREF_2_bp = 2                  ; DACREF bit 2 position
.equ AC_DACREF_3_bm = (1<<3)             ; DACREF bit 3 mask
.equ AC_DACREF_3_bp = 3                  ; DACREF bit 3 position
.equ AC_DACREF_4_bm = (1<<4)             ; DACREF bit 4 mask
.equ AC_DACREF_4_bp = 4                  ; DACREF bit 4 position
.equ AC_DACREF_5_bm = (1<<5)             ; DACREF bit 5 mask
.equ AC_DACREF_5_bp = 5                  ; DACREF bit 5 position
.equ AC_DACREF_6_bm = (1<<6)             ; DACREF bit 6 mask
.equ AC_DACREF_6_bp = 6                  ; DACREF bit 6 position
.equ AC_DACREF_7_bm = (1<<7)             ; DACREF bit 7 mask
.equ AC_DACREF_7_bp = 7                  ; DACREF bit 7 position

; AC_INTCTRL masks
.equ AC_CMP_bm = 0x01                    ; Interrupt Enable bit mask
.equ AC_CMP_bp = 0                       ; Interrupt Enable bit position
.equ AC_INTMODE_NORMAL_gm = 0x30         ; Interrupt Mode group mask
.equ AC_INTMODE_NORMAL_gp = 4            ; Interrupt Mode group position
.equ AC_INTMODE_NORMAL_0_bm = (1<<4)     ; Interrupt Mode bit 0 mask
.equ AC_INTMODE_NORMAL_0_bp = 4          ; Interrupt Mode bit 0 position
.equ AC_INTMODE_NORMAL_1_bm = (1<<5)     ; Interrupt Mode bit 1 mask
.equ AC_INTMODE_NORMAL_1_bp = 5          ; Interrupt Mode bit 1 position
.equ AC_INTMODE_WINDOW_gm = 0x30         ; Interrupt Mode group mask
.equ AC_INTMODE_WINDOW_gp = 4            ; Interrupt Mode group position
.equ AC_INTMODE_WINDOW_0_bm = (1<<4)     ; Interrupt Mode bit 0 mask
.equ AC_INTMODE_WINDOW_0_bp = 4          ; Interrupt Mode bit 0 position
.equ AC_INTMODE_WINDOW_1_bm = (1<<5)     ; Interrupt Mode bit 1 mask
.equ AC_INTMODE_WINDOW_1_bp = 5          ; Interrupt Mode bit 1 position

; AC_STATUS masks
.equ AC_CMPIF_bm = 0x01                  ; Analog Comparator Interrupt Flag bit mask
.equ AC_CMPIF_bp = 0                     ; Analog Comparator Interrupt Flag bit position
.equ AC_CMPSTATE_bm = 0x10               ; Analog Comparator State bit mask
.equ AC_CMPSTATE_bp = 4                  ; Analog Comparator State bit position
.equ AC_WINSTATE_gm = 0xC0               ; Analog Comparator Window State group mask
.equ AC_WINSTATE_gp = 6                  ; Analog Comparator Window State group position
.equ AC_WINSTATE_0_bm = (1<<6)           ; Analog Comparator Window State bit 0 mask
.equ AC_WINSTATE_0_bp = 6                ; Analog Comparator Window State bit 0 position
.equ AC_WINSTATE_1_bm = (1<<7)           ; Analog Comparator Window State bit 1 mask
.equ AC_WINSTATE_1_bp = 7                ; Analog Comparator Window State bit 1 position

; Hysteresis Mode select
.equ AC_HYSMODE_NONE_gc = (0x00<<1)      ; No hysteresis
.equ AC_HYSMODE_SMALL_gc = (0x01<<1)     ; Small hysteresis
.equ AC_HYSMODE_MEDIUM_gc = (0x02<<1)    ; Medium hysteresis
.equ AC_HYSMODE_LARGE_gc = (0x03<<1)     ; Large hysteresis

; Power profile select
.equ AC_POWER_PROFILE0_gc = (0x00<<3)    ; Power profile 0, Fastest response time, highest consumption
.equ AC_POWER_PROFILE1_gc = (0x01<<3)    ; Power profile 1

; Window selection mode
.equ AC_WINSEL_DISABLED_gc = (0x00<<0)   ; Window function disabled
.equ AC_WINSEL_UPSEL1_gc = (0x01<<0)     ; Select ACn+1 as upper limit in window compare

; Interrupt Mode select
.equ AC_NORMAL_INTMODE_BOTHEDGE_gc = (0x00<<4) ; Positive and negative inputs crosses
.equ AC_NORMAL_INTMODE_NEGEDGE_gc = (0x02<<4) ; Positive input goes below negative input
.equ AC_NORMAL_INTMODE_POSEDGE_gc = (0x03<<4) ; Positive input goes above negative input

; Interrupt Mode select
.equ AC_WINDOW_INTMODE_ABOVE_gc = (0x00<<4) ; Window interrupt when input above both references
.equ AC_WINDOW_INTMODE_INSIDE_gc = (0x01<<4) ; Window interrupt when input betweeen references
.equ AC_WINDOW_INTMODE_BELOW_gc = (0x02<<4) ; Window interrupt when input below both references
.equ AC_WINDOW_INTMODE_OUTSIDE_gc = (0x03<<4) ; Window interrupt when input outside reference

; AC Output Initial Value select
.equ AC_INITVAL_LOW_gc = (0x00<<6)       ; Output initialized to 0
.equ AC_INITVAL_HIGH_gc = (0x01<<6)      ; Output initialized to 1

; Negative Input MUX Selection
.equ AC_MUXNEG_AINN0_gc = (0x00<<0)      ; Negative Pin 0
.equ AC_MUXNEG_AINN1_gc = (0x01<<0)      ; Negative Pin 1
.equ AC_MUXNEG_AINN2_gc = (0x02<<0)      ; Negative Pin 2
.equ AC_MUXNEG_AINN3_gc = (0x03<<0)      ; Negative Pin 3
.equ AC_MUXNEG_DACREF_gc = (0x04<<0)     ; DAC Reference

; Positive Input MUX Selection
.equ AC_MUXPOS_AINP0_gc = (0x00<<3)      ; Positive Pin 0
.equ AC_MUXPOS_AINP1_gc = (0x01<<3)      ; Positive Pin 1
.equ AC_MUXPOS_AINP2_gc = (0x02<<3)      ; Positive Pin 2
.equ AC_MUXPOS_AINP3_gc = (0x03<<3)      ; Positive Pin 3
.equ AC_MUXPOS_AINP4_gc = (0x04<<3)      ; Positive Pin 4

; Analog Comparator Window State select
.equ AC_WINSTATE_ABOVE_gc = (0x00<<6)    ; Above window
.equ AC_WINSTATE_INSIDE_gc = (0x01<<6)   ; Inside window
.equ AC_WINSTATE_BELOW_gc = (0x02<<6)    ; Below window


;*************************************************************************
;** ADC - Analog to Digital Converter
;*************************************************************************

; ADC_CTRLA masks
.equ ADC_ENABLE_bm = 0x01                ; ADC Enable bit mask
.equ ADC_ENABLE_bp = 0                   ; ADC Enable bit position
.equ ADC_LOWLAT_bm = 0x20                ; Low Latency bit mask
.equ ADC_LOWLAT_bp = 5                   ; Low Latency bit position
.equ ADC_RUNSTDBY_bm = 0x80              ; Run in Standby bit mask
.equ ADC_RUNSTDBY_bp = 7                 ; Run in Standby bit position

; ADC_CTRLB masks
.equ ADC_PRESC_gm = 0x0F                 ; Prescaler Value group mask
.equ ADC_PRESC_gp = 0                    ; Prescaler Value group position
.equ ADC_PRESC_0_bm = (1<<0)             ; Prescaler Value bit 0 mask
.equ ADC_PRESC_0_bp = 0                  ; Prescaler Value bit 0 position
.equ ADC_PRESC_1_bm = (1<<1)             ; Prescaler Value bit 1 mask
.equ ADC_PRESC_1_bp = 1                  ; Prescaler Value bit 1 position
.equ ADC_PRESC_2_bm = (1<<2)             ; Prescaler Value bit 2 mask
.equ ADC_PRESC_2_bp = 2                  ; Prescaler Value bit 2 position
.equ ADC_PRESC_3_bm = (1<<3)             ; Prescaler Value bit 3 mask
.equ ADC_PRESC_3_bp = 3                  ; Prescaler Value bit 3 position

; ADC_CTRLC masks
.equ ADC_REFSEL_gm = 0x07                ; Reference select group mask
.equ ADC_REFSEL_gp = 0                   ; Reference select group position
.equ ADC_REFSEL_0_bm = (1<<0)            ; Reference select bit 0 mask
.equ ADC_REFSEL_0_bp = 0                 ; Reference select bit 0 position
.equ ADC_REFSEL_1_bm = (1<<1)            ; Reference select bit 1 mask
.equ ADC_REFSEL_1_bp = 1                 ; Reference select bit 1 position
.equ ADC_REFSEL_2_bm = (1<<2)            ; Reference select bit 2 mask
.equ ADC_REFSEL_2_bp = 2                 ; Reference select bit 2 position

; ADC_CTRLD masks
.equ ADC_WINCM_gm = 0x07                 ; Window Comparator Mode group mask
.equ ADC_WINCM_gp = 0                    ; Window Comparator Mode group position
.equ ADC_WINCM_0_bm = (1<<0)             ; Window Comparator Mode bit 0 mask
.equ ADC_WINCM_0_bp = 0                  ; Window Comparator Mode bit 0 position
.equ ADC_WINCM_1_bm = (1<<1)             ; Window Comparator Mode bit 1 mask
.equ ADC_WINCM_1_bp = 1                  ; Window Comparator Mode bit 1 position
.equ ADC_WINCM_2_bm = (1<<2)             ; Window Comparator Mode bit 2 mask
.equ ADC_WINCM_2_bp = 2                  ; Window Comparator Mode bit 2 position
.equ ADC_WINSRC_bm = 0x08                ; Window Mode Source bit mask
.equ ADC_WINSRC_bp = 3                   ; Window Mode Source bit position

; ADC_INTCTRL masks
.equ ADC_RESRDY_bm = 0x01                ; Result Ready Interrupt Enable bit mask
.equ ADC_RESRDY_bp = 0                   ; Result Ready Interrupt Enable bit position
.equ ADC_SAMPRDY_bm = 0x02               ; Sample Ready Interrupt Enable bit mask
.equ ADC_SAMPRDY_bp = 1                  ; Sample Ready Interrupt Enable bit position
.equ ADC_WCMP_bm = 0x04                  ; Window Comparator Interrupt Enable bit mask
.equ ADC_WCMP_bp = 2                     ; Window Comparator Interrupt Enable bit position
.equ ADC_RESOVR_bm = 0x08                ; Result Overwrite Interrupt Enable bit mask
.equ ADC_RESOVR_bp = 3                   ; Result Overwrite Interrupt Enable bit position
.equ ADC_SAMPOVR_bm = 0x10               ; Sample Overwrite Interrupt Enable bit mask
.equ ADC_SAMPOVR_bp = 4                  ; Sample Overwrite Interrupt Enable bit position
.equ ADC_TRIGOVR_bm = 0x20               ; Trigger Overrun Interrupt Enable bit mask
.equ ADC_TRIGOVR_bp = 5                  ; Trigger Overrun Interrupt Enable bit position

; ADC_INTFLAGS masks
; Masks for ADC_RESRDY already defined
; Masks for ADC_SAMPRDY already defined
; Masks for ADC_WCMP already defined
; Masks for ADC_RESOVR already defined
; Masks for ADC_SAMPOVR already defined
; Masks for ADC_TRIGOVR already defined

; ADC_STATUS masks
.equ ADC_ADCBUSY_bm = 0x01               ; ADC Busy bit mask
.equ ADC_ADCBUSY_bp = 0                  ; ADC Busy bit position

; ADC_DBGCTRL masks
.equ ADC_DBGRUN_bm = 0x01                ; Run in Debug Mode bit mask
.equ ADC_DBGRUN_bp = 0                   ; Run in Debug Mode bit position

; ADC_CTRLE masks
.equ ADC_SAMPDUR_gm = 0xFF               ; Sample Duration group mask
.equ ADC_SAMPDUR_gp = 0                  ; Sample Duration group position
.equ ADC_SAMPDUR_0_bm = (1<<0)           ; Sample Duration bit 0 mask
.equ ADC_SAMPDUR_0_bp = 0                ; Sample Duration bit 0 position
.equ ADC_SAMPDUR_1_bm = (1<<1)           ; Sample Duration bit 1 mask
.equ ADC_SAMPDUR_1_bp = 1                ; Sample Duration bit 1 position
.equ ADC_SAMPDUR_2_bm = (1<<2)           ; Sample Duration bit 2 mask
.equ ADC_SAMPDUR_2_bp = 2                ; Sample Duration bit 2 position
.equ ADC_SAMPDUR_3_bm = (1<<3)           ; Sample Duration bit 3 mask
.equ ADC_SAMPDUR_3_bp = 3                ; Sample Duration bit 3 position
.equ ADC_SAMPDUR_4_bm = (1<<4)           ; Sample Duration bit 4 mask
.equ ADC_SAMPDUR_4_bp = 4                ; Sample Duration bit 4 position
.equ ADC_SAMPDUR_5_bm = (1<<5)           ; Sample Duration bit 5 mask
.equ ADC_SAMPDUR_5_bp = 5                ; Sample Duration bit 5 position
.equ ADC_SAMPDUR_6_bm = (1<<6)           ; Sample Duration bit 6 mask
.equ ADC_SAMPDUR_6_bp = 6                ; Sample Duration bit 6 position
.equ ADC_SAMPDUR_7_bm = (1<<7)           ; Sample Duration bit 7 mask
.equ ADC_SAMPDUR_7_bp = 7                ; Sample Duration bit 7 position

; ADC_CTRLF masks
.equ ADC_SAMPNUM_gm = 0x0F               ; Sample numbers group mask
.equ ADC_SAMPNUM_gp = 0                  ; Sample numbers group position
.equ ADC_SAMPNUM_0_bm = (1<<0)           ; Sample numbers bit 0 mask
.equ ADC_SAMPNUM_0_bp = 0                ; Sample numbers bit 0 position
.equ ADC_SAMPNUM_1_bm = (1<<1)           ; Sample numbers bit 1 mask
.equ ADC_SAMPNUM_1_bp = 1                ; Sample numbers bit 1 position
.equ ADC_SAMPNUM_2_bm = (1<<2)           ; Sample numbers bit 2 mask
.equ ADC_SAMPNUM_2_bp = 2                ; Sample numbers bit 2 position
.equ ADC_SAMPNUM_3_bm = (1<<3)           ; Sample numbers bit 3 mask
.equ ADC_SAMPNUM_3_bp = 3                ; Sample numbers bit 3 position
.equ ADC_LEFTADJ_bm = 0x10               ; Left Adjust bit mask
.equ ADC_LEFTADJ_bp = 4                  ; Left Adjust bit position
.equ ADC_FREERUN_bm = 0x20               ; Free-Running mode bit mask
.equ ADC_FREERUN_bp = 5                  ; Free-Running mode bit position
.equ ADC_CHOPPING_bm = 0x40              ; Sign Chopping bit mask
.equ ADC_CHOPPING_bp = 6                 ; Sign Chopping bit position

; ADC_COMMAND masks
.equ ADC_START_gm = 0x07                 ; Start command group mask
.equ ADC_START_gp = 0                    ; Start command group position
.equ ADC_START_0_bm = (1<<0)             ; Start command bit 0 mask
.equ ADC_START_0_bp = 0                  ; Start command bit 0 position
.equ ADC_START_1_bm = (1<<1)             ; Start command bit 1 mask
.equ ADC_START_1_bp = 1                  ; Start command bit 1 position
.equ ADC_START_2_bm = (1<<2)             ; Start command bit 2 mask
.equ ADC_START_2_bp = 2                  ; Start command bit 2 position
.equ ADC_MODE_gm = 0x70                  ; Mode group mask
.equ ADC_MODE_gp = 4                     ; Mode group position
.equ ADC_MODE_0_bm = (1<<4)              ; Mode bit 0 mask
.equ ADC_MODE_0_bp = 4                   ; Mode bit 0 position
.equ ADC_MODE_1_bm = (1<<5)              ; Mode bit 1 mask
.equ ADC_MODE_1_bp = 5                   ; Mode bit 1 position
.equ ADC_MODE_2_bm = (1<<6)              ; Mode bit 2 mask
.equ ADC_MODE_2_bp = 6                   ; Mode bit 2 position
.equ ADC_DIFF_bm = 0x80                  ; Differential mode bit mask
.equ ADC_DIFF_bp = 7                     ; Differential mode bit position

; ADC_PGACTRL masks
.equ ADC_PGAEN_bm = 0x01                 ; PGA Enable bit mask
.equ ADC_PGAEN_bp = 0                    ; PGA Enable bit position
.equ ADC_PGABIASSEL_gm = 0x18            ; PGA BIAS Select group mask
.equ ADC_PGABIASSEL_gp = 3               ; PGA BIAS Select group position
.equ ADC_PGABIASSEL_0_bm = (1<<3)        ; PGA BIAS Select bit 0 mask
.equ ADC_PGABIASSEL_0_bp = 3             ; PGA BIAS Select bit 0 position
.equ ADC_PGABIASSEL_1_bm = (1<<4)        ; PGA BIAS Select bit 1 mask
.equ ADC_PGABIASSEL_1_bp = 4             ; PGA BIAS Select bit 1 position
.equ ADC_GAIN_gm = 0xE0                  ; Gain group mask
.equ ADC_GAIN_gp = 5                     ; Gain group position
.equ ADC_GAIN_0_bm = (1<<5)              ; Gain bit 0 mask
.equ ADC_GAIN_0_bp = 5                   ; Gain bit 0 position
.equ ADC_GAIN_1_bm = (1<<6)              ; Gain bit 1 mask
.equ ADC_GAIN_1_bp = 6                   ; Gain bit 1 position
.equ ADC_GAIN_2_bm = (1<<7)              ; Gain bit 2 mask
.equ ADC_GAIN_2_bp = 7                   ; Gain bit 2 position

; ADC_MUXPOS masks
.equ ADC_MUXPOS_gm = 0x3F                ; Analog Channel Selection Bits group mask
.equ ADC_MUXPOS_gp = 0                   ; Analog Channel Selection Bits group position
.equ ADC_MUXPOS_0_bm = (1<<0)            ; Analog Channel Selection Bits bit 0 mask
.equ ADC_MUXPOS_0_bp = 0                 ; Analog Channel Selection Bits bit 0 position
.equ ADC_MUXPOS_1_bm = (1<<1)            ; Analog Channel Selection Bits bit 1 mask
.equ ADC_MUXPOS_1_bp = 1                 ; Analog Channel Selection Bits bit 1 position
.equ ADC_MUXPOS_2_bm = (1<<2)            ; Analog Channel Selection Bits bit 2 mask
.equ ADC_MUXPOS_2_bp = 2                 ; Analog Channel Selection Bits bit 2 position
.equ ADC_MUXPOS_3_bm = (1<<3)            ; Analog Channel Selection Bits bit 3 mask
.equ ADC_MUXPOS_3_bp = 3                 ; Analog Channel Selection Bits bit 3 position
.equ ADC_MUXPOS_4_bm = (1<<4)            ; Analog Channel Selection Bits bit 4 mask
.equ ADC_MUXPOS_4_bp = 4                 ; Analog Channel Selection Bits bit 4 position
.equ ADC_MUXPOS_5_bm = (1<<5)            ; Analog Channel Selection Bits bit 5 mask
.equ ADC_MUXPOS_5_bp = 5                 ; Analog Channel Selection Bits bit 5 position
.equ ADC_VIA_gm = 0xC0                   ; VIA group mask
.equ ADC_VIA_gp = 6                      ; VIA group position
.equ ADC_VIA_0_bm = (1<<6)               ; VIA bit 0 mask
.equ ADC_VIA_0_bp = 6                    ; VIA bit 0 position
.equ ADC_VIA_1_bm = (1<<7)               ; VIA bit 1 mask
.equ ADC_VIA_1_bp = 7                    ; VIA bit 1 position

; ADC_MUXNEG masks
.equ ADC_MUXNEG_gm = 0x3F                ; Analog Channel Selection Bits group mask
.equ ADC_MUXNEG_gp = 0                   ; Analog Channel Selection Bits group position
.equ ADC_MUXNEG_0_bm = (1<<0)            ; Analog Channel Selection Bits bit 0 mask
.equ ADC_MUXNEG_0_bp = 0                 ; Analog Channel Selection Bits bit 0 position
.equ ADC_MUXNEG_1_bm = (1<<1)            ; Analog Channel Selection Bits bit 1 mask
.equ ADC_MUXNEG_1_bp = 1                 ; Analog Channel Selection Bits bit 1 position
.equ ADC_MUXNEG_2_bm = (1<<2)            ; Analog Channel Selection Bits bit 2 mask
.equ ADC_MUXNEG_2_bp = 2                 ; Analog Channel Selection Bits bit 2 position
.equ ADC_MUXNEG_3_bm = (1<<3)            ; Analog Channel Selection Bits bit 3 mask
.equ ADC_MUXNEG_3_bp = 3                 ; Analog Channel Selection Bits bit 3 position
.equ ADC_MUXNEG_4_bm = (1<<4)            ; Analog Channel Selection Bits bit 4 mask
.equ ADC_MUXNEG_4_bp = 4                 ; Analog Channel Selection Bits bit 4 position
.equ ADC_MUXNEG_5_bm = (1<<5)            ; Analog Channel Selection Bits bit 5 mask
.equ ADC_MUXNEG_5_bp = 5                 ; Analog Channel Selection Bits bit 5 position
; Masks for ADC_VIA already defined

; Mode select
.equ ADC_MODE_SINGLE_8BIT_gc = (0x00<<4) ; Single Conversion with 8-bit resolution
.equ ADC_MODE_SINGLE_12BIT_gc = (0x01<<4) ; Single Conversion with 12-bit resolution
.equ ADC_MODE_SERIES_gc = (0x02<<4)      ; Series with accumulation, separate trigger for every 12-bit conversion
.equ ADC_MODE_SERIES_SCALING_gc = (0x03<<4) ; Series with accumulation and scaling, separate trigger for every 12-bit conversion
.equ ADC_MODE_BURST_gc = (0x04<<4)       ; Burst with accumulation, one trigger will run SAMPNUM 12-bit conversions
.equ ADC_MODE_BURST_SCALING_gc = (0x05<<4) ; Burst with accumulation and scaling, one trigger will run SAMPNUM 12-bit conversions

; Start command select
.equ ADC_START_STOP_gc = (0x00<<0)       ; Stop an ongoing conversion
.equ ADC_START_IMMEDIATE_gc = (0x01<<0)  ; Start a conversion immediately. This will be set back to STOP when the first conversion is done, unless Free-Running mode is enabled
.equ ADC_START_MUXPOS_WRITE_gc = (0x02<<0) ; Start when MUXPOS register is written
.equ ADC_START_MUXNEG_WRITE_gc = (0x03<<0) ; Start when MUXNEG register is written
.equ ADC_START_EVENT_TRIGGER_gc = (0x04<<0) ; Start when an event is received

; Prescaler Value select
.equ ADC_PRESC_DIV2_gc = (0x00<<0)       ; System clock divided by 2
.equ ADC_PRESC_DIV4_gc = (0x01<<0)       ; System clock divided by 4
.equ ADC_PRESC_DIV6_gc = (0x02<<0)       ; System clock divided by 6
.equ ADC_PRESC_DIV8_gc = (0x03<<0)       ; System clock divided by 8
.equ ADC_PRESC_DIV10_gc = (0x04<<0)      ; System clock divided by 10
.equ ADC_PRESC_DIV12_gc = (0x05<<0)      ; System clock divided by 12
.equ ADC_PRESC_DIV14_gc = (0x06<<0)      ; System clock divided by 14
.equ ADC_PRESC_DIV16_gc = (0x07<<0)      ; System clock divided by 16
.equ ADC_PRESC_DIV20_gc = (0x08<<0)      ; System clock divided by 20
.equ ADC_PRESC_DIV24_gc = (0x09<<0)      ; System clock divided by 24
.equ ADC_PRESC_DIV28_gc = (0x0A<<0)      ; System clock divided by 28
.equ ADC_PRESC_DIV32_gc = (0x0B<<0)      ; System clock divided by 32
.equ ADC_PRESC_DIV40_gc = (0x0C<<0)      ; System clock divided by 40
.equ ADC_PRESC_DIV48_gc = (0x0D<<0)      ; System clock divided by 48
.equ ADC_PRESC_DIV56_gc = (0x0E<<0)      ; System clock divided by 56
.equ ADC_PRESC_DIV64_gc = (0x0F<<0)      ; System clock divided by 64

; Reference select
.equ ADC_REFSEL_VDD_gc = (0x00<<0)       ; VDD
.equ ADC_REFSEL_VREFA_gc = (0x02<<0)     ; External Reference
.equ ADC_REFSEL_1V024_gc = (0x04<<0)     ; Internal 1.024V Reference
.equ ADC_REFSEL_2V048_gc = (0x05<<0)     ; Internal 2.048V Reference
.equ ADC_REFSEL_4V096_gc = (0x06<<0)     ; Internal 4.096V Reference
.equ ADC_REFSEL_2V500_gc = (0x07<<0)     ; Internal 2.500V Reference

; Window Comparator Mode select
.equ ADC_WINCM_NONE_gc = (0x00<<0)       ; No Window Comparison
.equ ADC_WINCM_BELOW_gc = (0x01<<0)      ; Below Window
.equ ADC_WINCM_ABOVE_gc = (0x02<<0)      ; Above Window
.equ ADC_WINCM_INSIDE_gc = (0x03<<0)     ; Inside Window
.equ ADC_WINCM_OUTSIDE_gc = (0x04<<0)    ; Outside Window

; Window Mode Source select
.equ ADC_WINSRC_RESULT_gc = (0x00<<3)    ; Result register used as Window Comparator Source
.equ ADC_WINSRC_SAMPLE_gc = (0x01<<3)    ; Sample register used as Window Comparator Source

; Sign Chopping select
.equ ADC_CHOPPING_DISABLE_gc = (0x00<<6) ; Sign Chopping Disabled
.equ ADC_CHOPPING_ENABLE_gc = (0x01<<6)  ; Sign Chopping Enabled

; Sample numbers select
.equ ADC_SAMPNUM_NONE_gc = (0x00<<0)     ; No accumulation
.equ ADC_SAMPNUM_ACC2_gc = (0x01<<0)     ; 2 samples accumulated
.equ ADC_SAMPNUM_ACC4_gc = (0x02<<0)     ; 4 samples accumulated
.equ ADC_SAMPNUM_ACC8_gc = (0x03<<0)     ; 8 samples accumulated
.equ ADC_SAMPNUM_ACC16_gc = (0x04<<0)    ; 16 samples accumulated
.equ ADC_SAMPNUM_ACC32_gc = (0x05<<0)    ; 32 samples accumulated
.equ ADC_SAMPNUM_ACC64_gc = (0x06<<0)    ; 64 samples accumulated
.equ ADC_SAMPNUM_ACC128_gc = (0x07<<0)   ; 128 samples accumulated
.equ ADC_SAMPNUM_ACC256_gc = (0x08<<0)   ; 256 samples accumulated
.equ ADC_SAMPNUM_ACC512_gc = (0x09<<0)   ; 512 samples accumulated
.equ ADC_SAMPNUM_ACC1024_gc = (0x0A<<0)  ; 1024 samples accumulated

; Analog Channel Selection Bits
.equ ADC_MUXNEG_AIN0_gc = (0x00<<0)      ; ADC input pin 0
.equ ADC_MUXNEG_AIN1_gc = (0x01<<0)      ; ADC input pin 1
.equ ADC_MUXNEG_AIN2_gc = (0x02<<0)      ; ADC input pin 2
.equ ADC_MUXNEG_AIN3_gc = (0x03<<0)      ; ADC input pin 3
.equ ADC_MUXNEG_AIN4_gc = (0x04<<0)      ; ADC input pin 4
.equ ADC_MUXNEG_AIN5_gc = (0x05<<0)      ; ADC input pin 5
.equ ADC_MUXNEG_AIN6_gc = (0x06<<0)      ; ADC input pin 6
.equ ADC_MUXNEG_AIN7_gc = (0x07<<0)      ; ADC input pin 7
.equ ADC_MUXNEG_AIN8_gc = (0x08<<0)      ; ADC input pin 8
.equ ADC_MUXNEG_AIN9_gc = (0x09<<0)      ; ADC input pin 9
.equ ADC_MUXNEG_AIN10_gc = (0x0A<<0)     ; ADC input pin 10
.equ ADC_MUXNEG_AIN11_gc = (0x0B<<0)     ; ADC input pin 11
.equ ADC_MUXNEG_AIN16_gc = (0x10<<0)     ; ADC input pin 16
.equ ADC_MUXNEG_AIN17_gc = (0x11<<0)     ; ADC input pin 17
.equ ADC_MUXNEG_AIN18_gc = (0x12<<0)     ; ADC input pin 18
.equ ADC_MUXNEG_AIN19_gc = (0x13<<0)     ; ADC input pin 19
.equ ADC_MUXNEG_AIN20_gc = (0x14<<0)     ; ADC input pin 20
.equ ADC_MUXNEG_AIN21_gc = (0x15<<0)     ; ADC input pin 21
.equ ADC_MUXNEG_AIN22_gc = (0x16<<0)     ; ADC input pin 22
.equ ADC_MUXNEG_AIN23_gc = (0x17<<0)     ; ADC input pin 23
.equ ADC_MUXNEG_AIN24_gc = (0x18<<0)     ; ADC input pin 24
.equ ADC_MUXNEG_AIN25_gc = (0x19<<0)     ; ADC input pin 25
.equ ADC_MUXNEG_AIN26_gc = (0x1A<<0)     ; ADC input pin 26
.equ ADC_MUXNEG_AIN27_gc = (0x1B<<0)     ; ADC input pin 27
.equ ADC_MUXNEG_AIN28_gc = (0x1C<<0)     ; ADC input pin 28
.equ ADC_MUXNEG_AIN29_gc = (0x1D<<0)     ; ADC input pin 29
.equ ADC_MUXNEG_AIN30_gc = (0x1E<<0)     ; ADC input pin 30
.equ ADC_MUXNEG_AIN31_gc = (0x1F<<0)     ; ADC input pin 31
.equ ADC_MUXNEG_GND_gc = (0x30<<0)       ; Ground
.equ ADC_MUXNEG_DAC0_gc = (0x38<<0)      ; Digital to Analog Converter 0
.equ ADC_MUXNEG_DACREF0_gc = (0x39<<0)   ; AC0 DAC reference
.equ ADC_MUXNEG_DACREF1_gc = (0x3A<<0)   ; AC1 DAC reference

; VIA select
.equ ADC_VIA_DIRECT_gc = (0x00<<6)       ; Inputs connected directly to ADC
.equ ADC_VIA_PGA_gc = (0x01<<6)          ; Inputs connected via PGA

; Analog Channel Selection Bits
.equ ADC_MUXPOS_AIN0_gc = (0x00<<0)      ; ADC input pin 0
.equ ADC_MUXPOS_AIN1_gc = (0x01<<0)      ; ADC input pin 1
.equ ADC_MUXPOS_AIN2_gc = (0x02<<0)      ; ADC input pin 2
.equ ADC_MUXPOS_AIN3_gc = (0x03<<0)      ; ADC input pin 3
.equ ADC_MUXPOS_AIN4_gc = (0x04<<0)      ; ADC input pin 4
.equ ADC_MUXPOS_AIN5_gc = (0x05<<0)      ; ADC input pin 5
.equ ADC_MUXPOS_AIN6_gc = (0x06<<0)      ; ADC input pin 6
.equ ADC_MUXPOS_AIN7_gc = (0x07<<0)      ; ADC input pin 7
.equ ADC_MUXPOS_AIN8_gc = (0x08<<0)      ; ADC input pin 8
.equ ADC_MUXPOS_AIN9_gc = (0x09<<0)      ; ADC input pin 9
.equ ADC_MUXPOS_AIN10_gc = (0x0A<<0)     ; ADC input pin 10
.equ ADC_MUXPOS_AIN11_gc = (0x0B<<0)     ; ADC input pin 11
.equ ADC_MUXPOS_AIN16_gc = (0x10<<0)     ; ADC input pin 16
.equ ADC_MUXPOS_AIN17_gc = (0x11<<0)     ; ADC input pin 17
.equ ADC_MUXPOS_AIN18_gc = (0x12<<0)     ; ADC input pin 18
.equ ADC_MUXPOS_AIN19_gc = (0x13<<0)     ; ADC input pin 19
.equ ADC_MUXPOS_AIN20_gc = (0x14<<0)     ; ADC input pin 20
.equ ADC_MUXPOS_AIN21_gc = (0x15<<0)     ; ADC input pin 21
.equ ADC_MUXPOS_AIN22_gc = (0x16<<0)     ; ADC input pin 22
.equ ADC_MUXPOS_AIN23_gc = (0x17<<0)     ; ADC input pin 23
.equ ADC_MUXPOS_AIN24_gc = (0x18<<0)     ; ADC input pin 24
.equ ADC_MUXPOS_AIN25_gc = (0x19<<0)     ; ADC input pin 25
.equ ADC_MUXPOS_AIN26_gc = (0x1A<<0)     ; ADC input pin 26
.equ ADC_MUXPOS_AIN27_gc = (0x1B<<0)     ; ADC input pin 27
.equ ADC_MUXPOS_AIN28_gc = (0x1C<<0)     ; ADC input pin 28
.equ ADC_MUXPOS_AIN29_gc = (0x1D<<0)     ; ADC input pin 29
.equ ADC_MUXPOS_AIN30_gc = (0x1E<<0)     ; ADC input pin 30
.equ ADC_MUXPOS_AIN31_gc = (0x1F<<0)     ; ADC input pin 31
.equ ADC_MUXPOS_GND_gc = (0x30<<0)       ; Ground
.equ ADC_MUXPOS_VDD10_gc = (0x31<<0)     ; VDD Divided by 10
.equ ADC_MUXPOS_TEMPSENSE_gc = (0x32<<0) ; Temperature Sensor
.equ ADC_MUXPOS_DAC0_gc = (0x38<<0)      ; Digital to Analog Converter 0

; Gain select
.equ ADC_GAIN_1X_gc = (0x00<<5)          ; 1x gain
.equ ADC_GAIN_2X_gc = (0x01<<5)          ; 2x gain
.equ ADC_GAIN_4X_gc = (0x02<<5)          ; 4x gain
.equ ADC_GAIN_8X_gc = (0x03<<5)          ; 8x gain
.equ ADC_GAIN_16X_gc = (0x04<<5)         ; 16x gain

; PGA BIAS Select
.equ ADC_PGABIASSEL_100PCT_gc = (0x00<<3) ; 100% BIAS current.
.equ ADC_PGABIASSEL_75PCT_gc = (0x01<<3) ; 75% BIAS current. Usable for CLK_ADC<4.5MHz
.equ ADC_PGABIASSEL_50PCT_gc = (0x02<<3) ; 50% BIAS current. Usable for CLK_ADC<3MHz
.equ ADC_PGABIASSEL_25PCT_gc = (0x03<<3) ; 25% BIAS current. Usable for CLK_ADC<1.5MHz


;*************************************************************************
;** BOD - Bod interface
;*************************************************************************

; BOD_CTRLA masks
.equ BOD_SLEEP_gm = 0x03                 ; Operation in sleep mode group mask
.equ BOD_SLEEP_gp = 0                    ; Operation in sleep mode group position
.equ BOD_SLEEP_0_bm = (1<<0)             ; Operation in sleep mode bit 0 mask
.equ BOD_SLEEP_0_bp = 0                  ; Operation in sleep mode bit 0 position
.equ BOD_SLEEP_1_bm = (1<<1)             ; Operation in sleep mode bit 1 mask
.equ BOD_SLEEP_1_bp = 1                  ; Operation in sleep mode bit 1 position
.equ BOD_ACTIVE_gm = 0x0C                ; Operation in active mode group mask
.equ BOD_ACTIVE_gp = 2                   ; Operation in active mode group position
.equ BOD_ACTIVE_0_bm = (1<<2)            ; Operation in active mode bit 0 mask
.equ BOD_ACTIVE_0_bp = 2                 ; Operation in active mode bit 0 position
.equ BOD_ACTIVE_1_bm = (1<<3)            ; Operation in active mode bit 1 mask
.equ BOD_ACTIVE_1_bp = 3                 ; Operation in active mode bit 1 position
.equ BOD_SAMPFREQ_bm = 0x10              ; Sample frequency bit mask
.equ BOD_SAMPFREQ_bp = 4                 ; Sample frequency bit position

; BOD_CTRLB masks
.equ BOD_LVL_gm = 0x07                   ; Bod level group mask
.equ BOD_LVL_gp = 0                      ; Bod level group position
.equ BOD_LVL_0_bm = (1<<0)               ; Bod level bit 0 mask
.equ BOD_LVL_0_bp = 0                    ; Bod level bit 0 position
.equ BOD_LVL_1_bm = (1<<1)               ; Bod level bit 1 mask
.equ BOD_LVL_1_bp = 1                    ; Bod level bit 1 position
.equ BOD_LVL_2_bm = (1<<2)               ; Bod level bit 2 mask
.equ BOD_LVL_2_bp = 2                    ; Bod level bit 2 position

; BOD_VLMCTRLA masks
.equ BOD_VLMLVL_gm = 0x03                ; voltage level monitor level group mask
.equ BOD_VLMLVL_gp = 0                   ; voltage level monitor level group position
.equ BOD_VLMLVL_0_bm = (1<<0)            ; voltage level monitor level bit 0 mask
.equ BOD_VLMLVL_0_bp = 0                 ; voltage level monitor level bit 0 position
.equ BOD_VLMLVL_1_bm = (1<<1)            ; voltage level monitor level bit 1 mask
.equ BOD_VLMLVL_1_bp = 1                 ; voltage level monitor level bit 1 position

; BOD_INTCTRL masks
.equ BOD_VLMIE_bm = 0x01                 ; voltage level monitor interrrupt enable bit mask
.equ BOD_VLMIE_bp = 0                    ; voltage level monitor interrrupt enable bit position
.equ BOD_VLMCFG_gm = 0x06                ; Configuration group mask
.equ BOD_VLMCFG_gp = 1                   ; Configuration group position
.equ BOD_VLMCFG_0_bm = (1<<1)            ; Configuration bit 0 mask
.equ BOD_VLMCFG_0_bp = 1                 ; Configuration bit 0 position
.equ BOD_VLMCFG_1_bm = (1<<2)            ; Configuration bit 1 mask
.equ BOD_VLMCFG_1_bp = 2                 ; Configuration bit 1 position

; BOD_INTFLAGS masks
.equ BOD_VLMIF_bm = 0x01                 ; Voltage level monitor interrupt flag bit mask
.equ BOD_VLMIF_bp = 0                    ; Voltage level monitor interrupt flag bit position

; BOD_STATUS masks
.equ BOD_VLMS_bm = 0x01                  ; Voltage level monitor status bit mask
.equ BOD_VLMS_bp = 0                     ; Voltage level monitor status bit position

; Operation in active mode select
.equ BOD_ACTIVE_DISABLE_gc = (0x00<<2)   ; Disabled
.equ BOD_ACTIVE_ENABLED_gc = (0x01<<2)   ; Enabled in continuous mode
.equ BOD_ACTIVE_SAMPLED_gc = (0x02<<2)   ; Enabled in sampled mode
.equ BOD_ACTIVE_ENABLEWAIT_gc = (0x03<<2) ; Enabled in continuous mode. Execution halted at wake-up until BOD is running

; Sample frequency select
.equ BOD_SAMPFREQ_128HZ_gc = (0x00<<4)   ; Sampling frequency is 128 Hz
.equ BOD_SAMPFREQ_32HZ_gc = (0x01<<4)    ; Sample frequency is 32 Hz

; Operation in sleep mode select
.equ BOD_SLEEP_DISABLE_gc = (0x00<<0)    ; Disabled
.equ BOD_SLEEP_ENABLE_gc = (0x01<<0)     ; Enabled in continuous mode
.equ BOD_SLEEP_SAMPLE_gc = (0x02<<0)     ; Enabled in sampled mode

; Bod level select
.equ BOD_LVL_BODLEVEL0_gc = (0x00<<0)    ; BOD Disabled during normal operation
.equ BOD_LVL_BODLEVEL1_gc = (0x01<<0)    ; 1.9 V
.equ BOD_LVL_BODLEVEL2_gc = (0x02<<0)    ; 2.7 V
.equ BOD_LVL_BODLEVEL3_gc = (0x03<<0)    ; 4.5 V

; Configuration select
.equ BOD_VLMCFG_FALLING_gc = (0x00<<1)   ; VDD falls below VLM threshold
.equ BOD_VLMCFG_RISING_gc = (0x01<<1)    ; VDD rises above VLM threshold
.equ BOD_VLMCFG_BOTH_gc = (0x02<<1)      ; VDD crosses VLM threshold

; Voltage level monitor status select
.equ BOD_VLMS_ABOVE_gc = (0x00<<0)       ; The voltage is above the VLM threshold level
.equ BOD_VLMS_BELOW_gc = (0x01<<0)       ; The voltage is below the VLM threshold level

; voltage level monitor level select
.equ BOD_VLMLVL_OFF_gc = (0x00<<0)       ; VLM Disabled
.equ BOD_VLMLVL_5ABOVE_gc = (0x01<<0)    ; VLM threshold 5% above BOD level
.equ BOD_VLMLVL_15ABOVE_gc = (0x02<<0)   ; VLM threshold 15% above BOD level
.equ BOD_VLMLVL_25ABOVE_gc = (0x03<<0)   ; VLM threshold 25% above BOD level


;*************************************************************************
;** CCL - Configurable Custom Logic
;*************************************************************************

; CCL_CTRLA masks
.equ CCL_ENABLE_bm = 0x01                ; Enable bit mask
.equ CCL_ENABLE_bp = 0                   ; Enable bit position
.equ CCL_RUNSTDBY_bm = 0x40              ; Run in Standby bit mask
.equ CCL_RUNSTDBY_bp = 6                 ; Run in Standby bit position

; CCL_SEQCTRL0 masks
.equ CCL_SEQSEL_gm = 0x07                ; Sequential Selection group mask
.equ CCL_SEQSEL_gp = 0                   ; Sequential Selection group position
.equ CCL_SEQSEL_0_bm = (1<<0)            ; Sequential Selection bit 0 mask
.equ CCL_SEQSEL_0_bp = 0                 ; Sequential Selection bit 0 position
.equ CCL_SEQSEL_1_bm = (1<<1)            ; Sequential Selection bit 1 mask
.equ CCL_SEQSEL_1_bp = 1                 ; Sequential Selection bit 1 position
.equ CCL_SEQSEL_2_bm = (1<<2)            ; Sequential Selection bit 2 mask
.equ CCL_SEQSEL_2_bp = 2                 ; Sequential Selection bit 2 position

; CCL_SEQCTRL1 masks
; Masks for CCL_SEQSEL already defined

; CCL_INTCTRL0 masks
.equ CCL_INTMODE0_gm = 0x03              ; Interrupt Mode for LUT0 group mask
.equ CCL_INTMODE0_gp = 0                 ; Interrupt Mode for LUT0 group position
.equ CCL_INTMODE0_0_bm = (1<<0)          ; Interrupt Mode for LUT0 bit 0 mask
.equ CCL_INTMODE0_0_bp = 0               ; Interrupt Mode for LUT0 bit 0 position
.equ CCL_INTMODE0_1_bm = (1<<1)          ; Interrupt Mode for LUT0 bit 1 mask
.equ CCL_INTMODE0_1_bp = 1               ; Interrupt Mode for LUT0 bit 1 position
.equ CCL_INTMODE1_gm = 0x0C              ; Interrupt Mode for LUT1 group mask
.equ CCL_INTMODE1_gp = 2                 ; Interrupt Mode for LUT1 group position
.equ CCL_INTMODE1_0_bm = (1<<2)          ; Interrupt Mode for LUT1 bit 0 mask
.equ CCL_INTMODE1_0_bp = 2               ; Interrupt Mode for LUT1 bit 0 position
.equ CCL_INTMODE1_1_bm = (1<<3)          ; Interrupt Mode for LUT1 bit 1 mask
.equ CCL_INTMODE1_1_bp = 3               ; Interrupt Mode for LUT1 bit 1 position
.equ CCL_INTMODE2_gm = 0x30              ; Interrupt Mode for LUT2 group mask
.equ CCL_INTMODE2_gp = 4                 ; Interrupt Mode for LUT2 group position
.equ CCL_INTMODE2_0_bm = (1<<4)          ; Interrupt Mode for LUT2 bit 0 mask
.equ CCL_INTMODE2_0_bp = 4               ; Interrupt Mode for LUT2 bit 0 position
.equ CCL_INTMODE2_1_bm = (1<<5)          ; Interrupt Mode for LUT2 bit 1 mask
.equ CCL_INTMODE2_1_bp = 5               ; Interrupt Mode for LUT2 bit 1 position
.equ CCL_INTMODE3_gm = 0xC0              ; Interrupt Mode for LUT3 group mask
.equ CCL_INTMODE3_gp = 6                 ; Interrupt Mode for LUT3 group position
.equ CCL_INTMODE3_0_bm = (1<<6)          ; Interrupt Mode for LUT3 bit 0 mask
.equ CCL_INTMODE3_0_bp = 6               ; Interrupt Mode for LUT3 bit 0 position
.equ CCL_INTMODE3_1_bm = (1<<7)          ; Interrupt Mode for LUT3 bit 1 mask
.equ CCL_INTMODE3_1_bp = 7               ; Interrupt Mode for LUT3 bit 1 position

; CCL_INTFLAGS masks
.equ CCL_INT_gm = 0x0F                   ; Interrupt Flag group mask
.equ CCL_INT_gp = 0                      ; Interrupt Flag group position
.equ CCL_INT_0_bm = (1<<0)               ; Interrupt Flag bit 0 mask
.equ CCL_INT_0_bp = 0                    ; Interrupt Flag bit 0 position
.equ CCL_INT_1_bm = (1<<1)               ; Interrupt Flag bit 1 mask
.equ CCL_INT_1_bp = 1                    ; Interrupt Flag bit 1 position
.equ CCL_INT_2_bm = (1<<2)               ; Interrupt Flag bit 2 mask
.equ CCL_INT_2_bp = 2                    ; Interrupt Flag bit 2 position
.equ CCL_INT_3_bm = (1<<3)               ; Interrupt Flag bit 3 mask
.equ CCL_INT_3_bp = 3                    ; Interrupt Flag bit 3 position

; CCL_LUT0CTRLA masks
; Masks for CCL_ENABLE already defined
.equ CCL_CLKSRC_gm = 0x0E                ; Clock Source Selection group mask
.equ CCL_CLKSRC_gp = 1                   ; Clock Source Selection group position
.equ CCL_CLKSRC_0_bm = (1<<1)            ; Clock Source Selection bit 0 mask
.equ CCL_CLKSRC_0_bp = 1                 ; Clock Source Selection bit 0 position
.equ CCL_CLKSRC_1_bm = (1<<2)            ; Clock Source Selection bit 1 mask
.equ CCL_CLKSRC_1_bp = 2                 ; Clock Source Selection bit 1 position
.equ CCL_CLKSRC_2_bm = (1<<3)            ; Clock Source Selection bit 2 mask
.equ CCL_CLKSRC_2_bp = 3                 ; Clock Source Selection bit 2 position
.equ CCL_FILTSEL_gm = 0x30               ; Filter Selection group mask
.equ CCL_FILTSEL_gp = 4                  ; Filter Selection group position
.equ CCL_FILTSEL_0_bm = (1<<4)           ; Filter Selection bit 0 mask
.equ CCL_FILTSEL_0_bp = 4                ; Filter Selection bit 0 position
.equ CCL_FILTSEL_1_bm = (1<<5)           ; Filter Selection bit 1 mask
.equ CCL_FILTSEL_1_bp = 5                ; Filter Selection bit 1 position
.equ CCL_OUTEN_bm = 0x40                 ; Output Enable bit mask
.equ CCL_OUTEN_bp = 6                    ; Output Enable bit position
.equ CCL_EDGEDET_bm = 0x80               ; Edge Detection Enable bit mask
.equ CCL_EDGEDET_bp = 7                  ; Edge Detection Enable bit position

; CCL_LUT0CTRLB masks
.equ CCL_INSEL0_gm = 0x0F                ; LUT Input 0 Source Selection group mask
.equ CCL_INSEL0_gp = 0                   ; LUT Input 0 Source Selection group position
.equ CCL_INSEL0_0_bm = (1<<0)            ; LUT Input 0 Source Selection bit 0 mask
.equ CCL_INSEL0_0_bp = 0                 ; LUT Input 0 Source Selection bit 0 position
.equ CCL_INSEL0_1_bm = (1<<1)            ; LUT Input 0 Source Selection bit 1 mask
.equ CCL_INSEL0_1_bp = 1                 ; LUT Input 0 Source Selection bit 1 position
.equ CCL_INSEL0_2_bm = (1<<2)            ; LUT Input 0 Source Selection bit 2 mask
.equ CCL_INSEL0_2_bp = 2                 ; LUT Input 0 Source Selection bit 2 position
.equ CCL_INSEL0_3_bm = (1<<3)            ; LUT Input 0 Source Selection bit 3 mask
.equ CCL_INSEL0_3_bp = 3                 ; LUT Input 0 Source Selection bit 3 position
.equ CCL_INSEL1_gm = 0xF0                ; LUT Input 1 Source Selection group mask
.equ CCL_INSEL1_gp = 4                   ; LUT Input 1 Source Selection group position
.equ CCL_INSEL1_0_bm = (1<<4)            ; LUT Input 1 Source Selection bit 0 mask
.equ CCL_INSEL1_0_bp = 4                 ; LUT Input 1 Source Selection bit 0 position
.equ CCL_INSEL1_1_bm = (1<<5)            ; LUT Input 1 Source Selection bit 1 mask
.equ CCL_INSEL1_1_bp = 5                 ; LUT Input 1 Source Selection bit 1 position
.equ CCL_INSEL1_2_bm = (1<<6)            ; LUT Input 1 Source Selection bit 2 mask
.equ CCL_INSEL1_2_bp = 6                 ; LUT Input 1 Source Selection bit 2 position
.equ CCL_INSEL1_3_bm = (1<<7)            ; LUT Input 1 Source Selection bit 3 mask
.equ CCL_INSEL1_3_bp = 7                 ; LUT Input 1 Source Selection bit 3 position

; CCL_LUT0CTRLC masks
.equ CCL_INSEL2_gm = 0x0F                ; LUT Input 2 Source Selection group mask
.equ CCL_INSEL2_gp = 0                   ; LUT Input 2 Source Selection group position
.equ CCL_INSEL2_0_bm = (1<<0)            ; LUT Input 2 Source Selection bit 0 mask
.equ CCL_INSEL2_0_bp = 0                 ; LUT Input 2 Source Selection bit 0 position
.equ CCL_INSEL2_1_bm = (1<<1)            ; LUT Input 2 Source Selection bit 1 mask
.equ CCL_INSEL2_1_bp = 1                 ; LUT Input 2 Source Selection bit 1 position
.equ CCL_INSEL2_2_bm = (1<<2)            ; LUT Input 2 Source Selection bit 2 mask
.equ CCL_INSEL2_2_bp = 2                 ; LUT Input 2 Source Selection bit 2 position
.equ CCL_INSEL2_3_bm = (1<<3)            ; LUT Input 2 Source Selection bit 3 mask
.equ CCL_INSEL2_3_bp = 3                 ; LUT Input 2 Source Selection bit 3 position

; CCL_TRUTH0 masks
.equ CCL_TRUTH_gm = 0xFF                 ; Truth Table group mask
.equ CCL_TRUTH_gp = 0                    ; Truth Table group position
.equ CCL_TRUTH_0_bm = (1<<0)             ; Truth Table bit 0 mask
.equ CCL_TRUTH_0_bp = 0                  ; Truth Table bit 0 position
.equ CCL_TRUTH_1_bm = (1<<1)             ; Truth Table bit 1 mask
.equ CCL_TRUTH_1_bp = 1                  ; Truth Table bit 1 position
.equ CCL_TRUTH_2_bm = (1<<2)             ; Truth Table bit 2 mask
.equ CCL_TRUTH_2_bp = 2                  ; Truth Table bit 2 position
.equ CCL_TRUTH_3_bm = (1<<3)             ; Truth Table bit 3 mask
.equ CCL_TRUTH_3_bp = 3                  ; Truth Table bit 3 position
.equ CCL_TRUTH_4_bm = (1<<4)             ; Truth Table bit 4 mask
.equ CCL_TRUTH_4_bp = 4                  ; Truth Table bit 4 position
.equ CCL_TRUTH_5_bm = (1<<5)             ; Truth Table bit 5 mask
.equ CCL_TRUTH_5_bp = 5                  ; Truth Table bit 5 position
.equ CCL_TRUTH_6_bm = (1<<6)             ; Truth Table bit 6 mask
.equ CCL_TRUTH_6_bp = 6                  ; Truth Table bit 6 position
.equ CCL_TRUTH_7_bm = (1<<7)             ; Truth Table bit 7 mask
.equ CCL_TRUTH_7_bp = 7                  ; Truth Table bit 7 position

; CCL_LUT1CTRLA masks
; Masks for CCL_ENABLE already defined
; Masks for CCL_CLKSRC already defined
; Masks for CCL_FILTSEL already defined
; Masks for CCL_OUTEN already defined
; Masks for CCL_EDGEDET already defined

; CCL_LUT1CTRLB masks
; Masks for CCL_INSEL0 already defined
; Masks for CCL_INSEL1 already defined

; CCL_LUT1CTRLC masks
; Masks for CCL_INSEL2 already defined

; CCL_TRUTH1 masks
; Masks for CCL_TRUTH already defined

; CCL_LUT2CTRLA masks
; Masks for CCL_ENABLE already defined
; Masks for CCL_CLKSRC already defined
; Masks for CCL_FILTSEL already defined
; Masks for CCL_OUTEN already defined
; Masks for CCL_EDGEDET already defined

; CCL_LUT2CTRLB masks
; Masks for CCL_INSEL0 already defined
; Masks for CCL_INSEL1 already defined

; CCL_LUT2CTRLC masks
; Masks for CCL_INSEL2 already defined

; CCL_TRUTH2 masks
; Masks for CCL_TRUTH already defined

; CCL_LUT3CTRLA masks
; Masks for CCL_ENABLE already defined
; Masks for CCL_CLKSRC already defined
; Masks for CCL_FILTSEL already defined
; Masks for CCL_OUTEN already defined
; Masks for CCL_EDGEDET already defined

; CCL_LUT3CTRLB masks
; Masks for CCL_INSEL0 already defined
; Masks for CCL_INSEL1 already defined

; CCL_LUT3CTRLC masks
; Masks for CCL_INSEL2 already defined

; CCL_TRUTH3 masks
; Masks for CCL_TRUTH already defined

; Interrupt Mode for LUT0 select
.equ CCL_INTMODE0_INTDISABLE_gc = (0x00<<0) ; Interrupt disabled
.equ CCL_INTMODE0_RISING_gc = (0x01<<0)  ; Sense rising edge
.equ CCL_INTMODE0_FALLING_gc = (0x02<<0) ; Sense falling edge
.equ CCL_INTMODE0_BOTH_gc = (0x03<<0)    ; Sense both edges

; Interrupt Mode for LUT1 select
.equ CCL_INTMODE1_INTDISABLE_gc = (0x00<<2) ; Interrupt disabled
.equ CCL_INTMODE1_RISING_gc = (0x01<<2)  ; Sense rising edge
.equ CCL_INTMODE1_FALLING_gc = (0x02<<2) ; Sense falling edge
.equ CCL_INTMODE1_BOTH_gc = (0x03<<2)    ; Sense both edges

; Interrupt Mode for LUT2 select
.equ CCL_INTMODE2_INTDISABLE_gc = (0x00<<4) ; Interrupt disabled
.equ CCL_INTMODE2_RISING_gc = (0x01<<4)  ; Sense rising edge
.equ CCL_INTMODE2_FALLING_gc = (0x02<<4) ; Sense falling edge
.equ CCL_INTMODE2_BOTH_gc = (0x03<<4)    ; Sense both edges

; Interrupt Mode for LUT3 select
.equ CCL_INTMODE3_INTDISABLE_gc = (0x00<<6) ; Interrupt disabled
.equ CCL_INTMODE3_RISING_gc = (0x01<<6)  ; Sense rising edge
.equ CCL_INTMODE3_FALLING_gc = (0x02<<6) ; Sense falling edge
.equ CCL_INTMODE3_BOTH_gc = (0x03<<6)    ; Sense both edges

; Clock Source Selection
.equ CCL_CLKSRC_CLKPER_gc = (0x00<<1)    ; Peripheral Clock
.equ CCL_CLKSRC_IN2_gc = (0x01<<1)       ; INSEL2 selection
.equ CCL_CLKSRC_OSCHF_gc = (0x04<<1)     ; Internal High Frequency oscillator
.equ CCL_CLKSRC_OSC32K_gc = (0x05<<1)    ; Internal 32.768 kHz oscillator
.equ CCL_CLKSRC_OSC1K_gc = (0x06<<1)     ; Internal 32.768 kHz oscillator divided by 32

; Edge Detection Enable select
.equ CCL_EDGEDET_DIS_gc = (0x00<<7)      ; Edge detector is disabled
.equ CCL_EDGEDET_EN_gc = (0x01<<7)       ; Edge detector is enabled

; Filter Selection
.equ CCL_FILTSEL_DISABLE_gc = (0x00<<4)  ; Filter disabled
.equ CCL_FILTSEL_SYNCH_gc = (0x01<<4)    ; Synchronizer enabled
.equ CCL_FILTSEL_FILTER_gc = (0x02<<4)   ; Filter enabled

; LUT Input 0 Source Selection
.equ CCL_INSEL0_MASK_gc = (0x00<<0)      ; Masked input
.equ CCL_INSEL0_FEEDBACK_gc = (0x01<<0)  ; Feedback input
.equ CCL_INSEL0_LINK_gc = (0x02<<0)      ; Output from LUT[n+1] as input source
.equ CCL_INSEL0_EVENTA_gc = (0x03<<0)    ; Event A as input source
.equ CCL_INSEL0_EVENTB_gc = (0x04<<0)    ; Event B as input source
.equ CCL_INSEL0_IO_gc = (0x05<<0)        ; IN0 input source
.equ CCL_INSEL0_AC0_gc = (0x06<<0)       ; AC0 output input source
.equ CCL_INSEL0_USART0_gc = (0x07<<0)    ; USART0 TxD input source
.equ CCL_INSEL0_SPI0_gc = (0x08<<0)      ; SPI0 MISO input source
.equ CCL_INSEL0_TCA0_gc = (0x09<<0)      ; TCA0 WO0 input source
.equ CCL_INSEL0_TCA1_gc = (0x0A<<0)      ; TCA1 WO0 input source
.equ CCL_INSEL0_TCB0_gc = (0x0B<<0)      ; TCB0 WO input source

; LUT Input 1 Source Selection
.equ CCL_INSEL1_MASK_gc = (0x00<<4)      ; Masked input
.equ CCL_INSEL1_FEEDBACK_gc = (0x01<<4)  ; Feedback input
.equ CCL_INSEL1_LINK_gc = (0x02<<4)      ; Output from LUT[n+1] as input source
.equ CCL_INSEL1_EVENTA_gc = (0x03<<4)    ; Event A as input source
.equ CCL_INSEL1_EVENTB_gc = (0x04<<4)    ; Event B as input source
.equ CCL_INSEL1_IO_gc = (0x05<<4)        ; IN0 input source
.equ CCL_INSEL1_AC1_gc = (0x06<<4)       ; AC1 output input source
.equ CCL_INSEL1_USART1_gc = (0x07<<4)    ; USART1 TxD input source
.equ CCL_INSEL1_SPI0_gc = (0x08<<4)      ; SPI0 MISO input source
.equ CCL_INSEL1_TCA0_gc = (0x09<<4)      ; TCA0 WO1 input source
.equ CCL_INSEL1_TCA1_gc = (0x0A<<4)      ; TCA1 WO1 input source
.equ CCL_INSEL1_TCB1_gc = (0x0B<<4)      ; TCB1 WO input source

; LUT Input 2 Source Selection
.equ CCL_INSEL2_MASK_gc = (0x00<<0)      ; Masked input
.equ CCL_INSEL2_FEEDBACK_gc = (0x01<<0)  ; Feedback input
.equ CCL_INSEL2_LINK_gc = (0x02<<0)      ; Output from LUT[n+1] as input source
.equ CCL_INSEL2_EVENTA_gc = (0x03<<0)    ; Event A as input source
.equ CCL_INSEL2_EVENTB_gc = (0x04<<0)    ; Event B as input source
.equ CCL_INSEL2_IO_gc = (0x05<<0)        ; IN0 input source
.equ CCL_INSEL2_AC1_gc = (0x06<<0)       ; AC1 output input source
.equ CCL_INSEL2_USART2_gc = (0x07<<0)    ; USART2 TxD input source
.equ CCL_INSEL2_SPI0_gc = (0x08<<0)      ; SPI0 MISO input source
.equ CCL_INSEL2_TCA0_gc = (0x09<<0)      ; TCA0 WO2 input source
.equ CCL_INSEL2_TCA1_gc = (0x0A<<0)      ; TCA1 WO2 input source
.equ CCL_INSEL2_TCB2_gc = (0x0B<<0)      ; TCB2 WO input source

; Sequential Selection
.equ CCL_SEQSEL_DISABLE_gc = (0x00<<0)   ; Sequential logic disabled
.equ CCL_SEQSEL_DFF_gc = (0x01<<0)       ; D FlipFlop
.equ CCL_SEQSEL_JK_gc = (0x02<<0)        ; JK FlipFlop
.equ CCL_SEQSEL_LATCH_gc = (0x03<<0)     ; D Latch
.equ CCL_SEQSEL_RS_gc = (0x04<<0)        ; RS Latch


;*************************************************************************
;** CLKCTRL - Clock controller
;*************************************************************************

; CLKCTRL_MCLKCTRLA masks
.equ CLKCTRL_CLKSEL_gm = 0x07            ; Clock select group mask
.equ CLKCTRL_CLKSEL_gp = 0               ; Clock select group position
.equ CLKCTRL_CLKSEL_0_bm = (1<<0)        ; Clock select bit 0 mask
.equ CLKCTRL_CLKSEL_0_bp = 0             ; Clock select bit 0 position
.equ CLKCTRL_CLKSEL_1_bm = (1<<1)        ; Clock select bit 1 mask
.equ CLKCTRL_CLKSEL_1_bp = 1             ; Clock select bit 1 position
.equ CLKCTRL_CLKSEL_2_bm = (1<<2)        ; Clock select bit 2 mask
.equ CLKCTRL_CLKSEL_2_bp = 2             ; Clock select bit 2 position
.equ CLKCTRL_CLKOUT_bm = 0x80            ; System clock out bit mask
.equ CLKCTRL_CLKOUT_bp = 7               ; System clock out bit position

; CLKCTRL_MCLKCTRLB masks
.equ CLKCTRL_PEN_bm = 0x01               ; Prescaler enable bit mask
.equ CLKCTRL_PEN_bp = 0                  ; Prescaler enable bit position
.equ CLKCTRL_PDIV_gm = 0x1E              ; Prescaler division group mask
.equ CLKCTRL_PDIV_gp = 1                 ; Prescaler division group position
.equ CLKCTRL_PDIV_0_bm = (1<<1)          ; Prescaler division bit 0 mask
.equ CLKCTRL_PDIV_0_bp = 1               ; Prescaler division bit 0 position
.equ CLKCTRL_PDIV_1_bm = (1<<2)          ; Prescaler division bit 1 mask
.equ CLKCTRL_PDIV_1_bp = 2               ; Prescaler division bit 1 position
.equ CLKCTRL_PDIV_2_bm = (1<<3)          ; Prescaler division bit 2 mask
.equ CLKCTRL_PDIV_2_bp = 3               ; Prescaler division bit 2 position
.equ CLKCTRL_PDIV_3_bm = (1<<4)          ; Prescaler division bit 3 mask
.equ CLKCTRL_PDIV_3_bp = 4               ; Prescaler division bit 3 position

; CLKCTRL_MCLKCTRLC masks
.equ CLKCTRL_CFDEN_bm = 0x01             ; Clock Failure Detect Enable bit mask
.equ CLKCTRL_CFDEN_bp = 0                ; Clock Failure Detect Enable bit position
.equ CLKCTRL_CFDTST_bm = 0x02            ; CFD Test bit mask
.equ CLKCTRL_CFDTST_bp = 1               ; CFD Test bit position
.equ CLKCTRL_CFDSRC_gm = 0x0C            ; CFD Source group mask
.equ CLKCTRL_CFDSRC_gp = 2               ; CFD Source group position
.equ CLKCTRL_CFDSRC_0_bm = (1<<2)        ; CFD Source bit 0 mask
.equ CLKCTRL_CFDSRC_0_bp = 2             ; CFD Source bit 0 position
.equ CLKCTRL_CFDSRC_1_bm = (1<<3)        ; CFD Source bit 1 mask
.equ CLKCTRL_CFDSRC_1_bp = 3             ; CFD Source bit 1 position

; CLKCTRL_MCLKINTCTRL masks
.equ CLKCTRL_CFD_bm = 0x01               ; Interrupt Enable bit mask
.equ CLKCTRL_CFD_bp = 0                  ; Interrupt Enable bit position
.equ CLKCTRL_INTTYPE_bm = 0x80           ; Interrupt Type bit mask
.equ CLKCTRL_INTTYPE_bp = 7              ; Interrupt Type bit position

; CLKCTRL_MCLKINTFLAGS masks
; Masks for CLKCTRL_CFD already defined

; CLKCTRL_MCLKSTATUS masks
.equ CLKCTRL_SOSC_bm = 0x01              ; System Oscillator changing bit mask
.equ CLKCTRL_SOSC_bp = 0                 ; System Oscillator changing bit position
.equ CLKCTRL_OSCHFS_bm = 0x02            ; High frequency oscillator status bit mask
.equ CLKCTRL_OSCHFS_bp = 1               ; High frequency oscillator status bit position
.equ CLKCTRL_OSC32KS_bm = 0x04           ; 32KHz oscillator status bit mask
.equ CLKCTRL_OSC32KS_bp = 2              ; 32KHz oscillator status bit position
.equ CLKCTRL_XOSC32KS_bm = 0x08          ; 32.768 kHz Crystal Oscillator status bit mask
.equ CLKCTRL_XOSC32KS_bp = 3             ; 32.768 kHz Crystal Oscillator status bit position
.equ CLKCTRL_EXTS_bm = 0x10              ; External Clock status / XOSCHF status bit mask
.equ CLKCTRL_EXTS_bp = 4                 ; External Clock status / XOSCHF status bit position

; CLKCTRL_MCLKTIMEBASE masks
.equ CLKCTRL_TIMEBASE_gm = 0x1F          ; Timebase group mask
.equ CLKCTRL_TIMEBASE_gp = 0             ; Timebase group position
.equ CLKCTRL_TIMEBASE_0_bm = (1<<0)      ; Timebase bit 0 mask
.equ CLKCTRL_TIMEBASE_0_bp = 0           ; Timebase bit 0 position
.equ CLKCTRL_TIMEBASE_1_bm = (1<<1)      ; Timebase bit 1 mask
.equ CLKCTRL_TIMEBASE_1_bp = 1           ; Timebase bit 1 position
.equ CLKCTRL_TIMEBASE_2_bm = (1<<2)      ; Timebase bit 2 mask
.equ CLKCTRL_TIMEBASE_2_bp = 2           ; Timebase bit 2 position
.equ CLKCTRL_TIMEBASE_3_bm = (1<<3)      ; Timebase bit 3 mask
.equ CLKCTRL_TIMEBASE_3_bp = 3           ; Timebase bit 3 position
.equ CLKCTRL_TIMEBASE_4_bm = (1<<4)      ; Timebase bit 4 mask
.equ CLKCTRL_TIMEBASE_4_bp = 4           ; Timebase bit 4 position

; CLKCTRL_OSCHFCTRLA masks
.equ CLKCTRL_AUTOTUNE_gm = 0x03          ; Automatic Oscillator Tune group mask
.equ CLKCTRL_AUTOTUNE_gp = 0             ; Automatic Oscillator Tune group position
.equ CLKCTRL_AUTOTUNE_0_bm = (1<<0)      ; Automatic Oscillator Tune bit 0 mask
.equ CLKCTRL_AUTOTUNE_0_bp = 0           ; Automatic Oscillator Tune bit 0 position
.equ CLKCTRL_AUTOTUNE_1_bm = (1<<1)      ; Automatic Oscillator Tune bit 1 mask
.equ CLKCTRL_AUTOTUNE_1_bp = 1           ; Automatic Oscillator Tune bit 1 position
.equ CLKCTRL_RUNSTDBY_bm = 0x80          ; Run in standby bit mask
.equ CLKCTRL_RUNSTDBY_bp = 7             ; Run in standby bit position

; CLKCTRL_OSCHFTUNE masks
.equ CLKCTRL_TUNE_gm = 0xFF              ; Oscillator Tune group mask
.equ CLKCTRL_TUNE_gp = 0                 ; Oscillator Tune group position
.equ CLKCTRL_TUNE_0_bm = (1<<0)          ; Oscillator Tune bit 0 mask
.equ CLKCTRL_TUNE_0_bp = 0               ; Oscillator Tune bit 0 position
.equ CLKCTRL_TUNE_1_bm = (1<<1)          ; Oscillator Tune bit 1 mask
.equ CLKCTRL_TUNE_1_bp = 1               ; Oscillator Tune bit 1 position
.equ CLKCTRL_TUNE_2_bm = (1<<2)          ; Oscillator Tune bit 2 mask
.equ CLKCTRL_TUNE_2_bp = 2               ; Oscillator Tune bit 2 position
.equ CLKCTRL_TUNE_3_bm = (1<<3)          ; Oscillator Tune bit 3 mask
.equ CLKCTRL_TUNE_3_bp = 3               ; Oscillator Tune bit 3 position
.equ CLKCTRL_TUNE_4_bm = (1<<4)          ; Oscillator Tune bit 4 mask
.equ CLKCTRL_TUNE_4_bp = 4               ; Oscillator Tune bit 4 position
.equ CLKCTRL_TUNE_5_bm = (1<<5)          ; Oscillator Tune bit 5 mask
.equ CLKCTRL_TUNE_5_bp = 5               ; Oscillator Tune bit 5 position
.equ CLKCTRL_TUNE_6_bm = (1<<6)          ; Oscillator Tune bit 6 mask
.equ CLKCTRL_TUNE_6_bp = 6               ; Oscillator Tune bit 6 position
.equ CLKCTRL_TUNE_7_bm = (1<<7)          ; Oscillator Tune bit 7 mask
.equ CLKCTRL_TUNE_7_bp = 7               ; Oscillator Tune bit 7 position

; CLKCTRL_OSC32KCTRLA masks
; Masks for CLKCTRL_RUNSTDBY already defined

; CLKCTRL_XOSC32KCTRLA masks
.equ CLKCTRL_ENABLE_bm = 0x01            ; Enable bit mask
.equ CLKCTRL_ENABLE_bp = 0               ; Enable bit position
.equ CLKCTRL_LPMODE_bm = 0x02            ; Low power mode bit mask
.equ CLKCTRL_LPMODE_bp = 1               ; Low power mode bit position
.equ CLKCTRL_SEL_bm = 0x04               ; Select bit mask
.equ CLKCTRL_SEL_bp = 2                  ; Select bit position
.equ CLKCTRL_CSUT_gm = 0x30              ; Crystal startup time group mask
.equ CLKCTRL_CSUT_gp = 4                 ; Crystal startup time group position
.equ CLKCTRL_CSUT_0_bm = (1<<4)          ; Crystal startup time bit 0 mask
.equ CLKCTRL_CSUT_0_bp = 4               ; Crystal startup time bit 0 position
.equ CLKCTRL_CSUT_1_bm = (1<<5)          ; Crystal startup time bit 1 mask
.equ CLKCTRL_CSUT_1_bp = 5               ; Crystal startup time bit 1 position
; Masks for CLKCTRL_RUNSTDBY already defined

; CLKCTRL_XOSCHFCTRLA masks
; Masks for CLKCTRL_ENABLE already defined
.equ CLKCTRL_SELHF_bm = 0x02             ; Source Select bit mask
.equ CLKCTRL_SELHF_bp = 1                ; Source Select bit position
.equ CLKCTRL_CSUTHF_gm = 0x30            ; Start-Up Time group mask
.equ CLKCTRL_CSUTHF_gp = 4               ; Start-Up Time group position
.equ CLKCTRL_CSUTHF_0_bm = (1<<4)        ; Start-Up Time bit 0 mask
.equ CLKCTRL_CSUTHF_0_bp = 4             ; Start-Up Time bit 0 position
.equ CLKCTRL_CSUTHF_1_bm = (1<<5)        ; Start-Up Time bit 1 mask
.equ CLKCTRL_CSUTHF_1_bp = 5             ; Start-Up Time bit 1 position
; Masks for CLKCTRL_RUNSTDBY already defined

; Clock select
.equ CLKCTRL_CLKSEL_OSCHF_gc = (0x00<<0) ; Internal high-frequency oscillator
.equ CLKCTRL_CLKSEL_OSC32K_gc = (0x01<<0) ; Internal 32.768 kHz oscillator
.equ CLKCTRL_CLKSEL_XOSC32K_gc = (0x02<<0) ; 32.768 kHz crystal oscillator
.equ CLKCTRL_CLKSEL_EXTCLK_gc = (0x03<<0) ; External clock

; Prescaler division select
.equ CLKCTRL_PDIV_DIV2_gc = (0x00<<1)    ; Divide by 2
.equ CLKCTRL_PDIV_DIV4_gc = (0x01<<1)    ; Divide by 4
.equ CLKCTRL_PDIV_DIV8_gc = (0x02<<1)    ; Divide by 8
.equ CLKCTRL_PDIV_DIV16_gc = (0x03<<1)   ; Divide by 16
.equ CLKCTRL_PDIV_DIV32_gc = (0x04<<1)   ; Divide by 32
.equ CLKCTRL_PDIV_DIV64_gc = (0x05<<1)   ; Divide by 64
.equ CLKCTRL_PDIV_DIV6_gc = (0x08<<1)    ; Divide by 6
.equ CLKCTRL_PDIV_DIV10_gc = (0x09<<1)   ; Divide by 10
.equ CLKCTRL_PDIV_DIV12_gc = (0x0A<<1)   ; Divide by 12
.equ CLKCTRL_PDIV_DIV24_gc = (0x0B<<1)   ; Divide by 24
.equ CLKCTRL_PDIV_DIV48_gc = (0x0C<<1)   ; Divide by 48

; CFD Source select
.equ CLKCTRL_CFDSRC_CLKMAIN_gc = (0x00<<2) ; Main Clock
.equ CLKCTRL_CFDSRC_XOSCHF_gc = (0x01<<2) ; High Frequency Crystal Oscillator
.equ CLKCTRL_CFDSRC_XOSC32K_gc = (0x02<<2) ; 32.768 kHz Crystal Oscillator

; Interrupt Type select
.equ CLKCTRL_INTTYPE_INT_gc = (0x00<<7)  ; Regular interrupt
.equ CLKCTRL_INTTYPE_NMI_gc = (0x01<<7)  ; Non-maskable interrupt

; Automatic Oscillator Tune select
.equ CLKCTRL_AUTOTUNE_OFF_gc = (0x00<<0) ; Disabled
.equ CLKCTRL_AUTOTUNE_XOSC32K_gc = (0x01<<0) ; Tune against 32.768 kHz Crystal Oscillator

; Start-Up Time select
.equ CLKCTRL_CSUTHF_256CYC_gc = (0x00<<4) ; 256 XOSCHF Cycles
.equ CLKCTRL_CSUTHF_1KCYC_gc = (0x01<<4) ; 1K XOSCHF Cycles
.equ CLKCTRL_CSUTHF_4KCYC_gc = (0x02<<4) ; 4K XOSCHF Cycles

; Source Select
.equ CLKCTRL_SELHF_CRYSTAL_gc = (0x00<<1) ; External Crystal
.equ CLKCTRL_SELHF_EXTCLK_gc = (0x01<<1) ; Extenal Clock on XTALHF1 pin

; Crystal startup time select
.equ CLKCTRL_CSUT_1K_gc = (0x00<<4)      ; 1k cycles
.equ CLKCTRL_CSUT_16K_gc = (0x01<<4)     ; 16k cycles
.equ CLKCTRL_CSUT_32K_gc = (0x02<<4)     ; 32k cycles
.equ CLKCTRL_CSUT_64K_gc = (0x03<<4)     ; 64k cycles


;*************************************************************************
;** CPU - CPU
;*************************************************************************

; CPU_CCP masks
.equ CPU_CCP_gm = 0xFF                   ; CCP signature group mask
.equ CPU_CCP_gp = 0                      ; CCP signature group position
.equ CPU_CCP_0_bm = (1<<0)               ; CCP signature bit 0 mask
.equ CPU_CCP_0_bp = 0                    ; CCP signature bit 0 position
.equ CPU_CCP_1_bm = (1<<1)               ; CCP signature bit 1 mask
.equ CPU_CCP_1_bp = 1                    ; CCP signature bit 1 position
.equ CPU_CCP_2_bm = (1<<2)               ; CCP signature bit 2 mask
.equ CPU_CCP_2_bp = 2                    ; CCP signature bit 2 position
.equ CPU_CCP_3_bm = (1<<3)               ; CCP signature bit 3 mask
.equ CPU_CCP_3_bp = 3                    ; CCP signature bit 3 position
.equ CPU_CCP_4_bm = (1<<4)               ; CCP signature bit 4 mask
.equ CPU_CCP_4_bp = 4                    ; CCP signature bit 4 position
.equ CPU_CCP_5_bm = (1<<5)               ; CCP signature bit 5 mask
.equ CPU_CCP_5_bp = 5                    ; CCP signature bit 5 position
.equ CPU_CCP_6_bm = (1<<6)               ; CCP signature bit 6 mask
.equ CPU_CCP_6_bp = 6                    ; CCP signature bit 6 position
.equ CPU_CCP_7_bm = (1<<7)               ; CCP signature bit 7 mask
.equ CPU_CCP_7_bp = 7                    ; CCP signature bit 7 position

; CPU_SREG masks
.equ CPU_C_bm = 0x01                     ; Carry Flag bit mask
.equ CPU_C_bp = 0                        ; Carry Flag bit position
.equ CPU_Z_bm = 0x02                     ; Zero Flag bit mask
.equ CPU_Z_bp = 1                        ; Zero Flag bit position
.equ CPU_N_bm = 0x04                     ; Negative Flag bit mask
.equ CPU_N_bp = 2                        ; Negative Flag bit position
.equ CPU_V_bm = 0x08                     ; Two's Complement Overflow Flag bit mask
.equ CPU_V_bp = 3                        ; Two's Complement Overflow Flag bit position
.equ CPU_S_bm = 0x10                     ; N Exclusive Or V Flag bit mask
.equ CPU_S_bp = 4                        ; N Exclusive Or V Flag bit position
.equ CPU_H_bm = 0x20                     ; Half Carry Flag bit mask
.equ CPU_H_bp = 5                        ; Half Carry Flag bit position
.equ CPU_T_bm = 0x40                     ; Transfer Bit bit mask
.equ CPU_T_bp = 6                        ; Transfer Bit bit position
.equ CPU_I_bm = 0x80                     ; Global Interrupt Enable Flag bit mask
.equ CPU_I_bp = 7                        ; Global Interrupt Enable Flag bit position

; CCP signature select
.equ CPU_CCP_SPM_gc = (0x9D<<0)          ; SPM Instruction Protection
.equ CPU_CCP_IOREG_gc = (0xD8<<0)        ; IO Register Protection


;*************************************************************************
;** CPUINT - Interrupt Controller
;*************************************************************************

; CPUINT_CTRLA masks
.equ CPUINT_LVL0RR_bm = 0x01             ; Round-robin Scheduling Enable bit mask
.equ CPUINT_LVL0RR_bp = 0                ; Round-robin Scheduling Enable bit position
.equ CPUINT_CVT_bm = 0x20                ; Compact Vector Table bit mask
.equ CPUINT_CVT_bp = 5                   ; Compact Vector Table bit position
.equ CPUINT_IVSEL_bm = 0x40              ; Interrupt Vector Select bit mask
.equ CPUINT_IVSEL_bp = 6                 ; Interrupt Vector Select bit position

; CPUINT_STATUS masks
.equ CPUINT_LVL0EX_bm = 0x01             ; Level 0 Interrupt Executing bit mask
.equ CPUINT_LVL0EX_bp = 0                ; Level 0 Interrupt Executing bit position
.equ CPUINT_LVL1EX_bm = 0x02             ; Level 1 Interrupt Executing bit mask
.equ CPUINT_LVL1EX_bp = 1                ; Level 1 Interrupt Executing bit position
.equ CPUINT_NMIEX_bm = 0x80              ; Non-maskable Interrupt Executing bit mask
.equ CPUINT_NMIEX_bp = 7                 ; Non-maskable Interrupt Executing bit position

; CPUINT_LVL0PRI masks
.equ CPUINT_LVL0PRI_gm = 0xFF            ; Interrupt Level Priority group mask
.equ CPUINT_LVL0PRI_gp = 0               ; Interrupt Level Priority group position
.equ CPUINT_LVL0PRI_0_bm = (1<<0)        ; Interrupt Level Priority bit 0 mask
.equ CPUINT_LVL0PRI_0_bp = 0             ; Interrupt Level Priority bit 0 position
.equ CPUINT_LVL0PRI_1_bm = (1<<1)        ; Interrupt Level Priority bit 1 mask
.equ CPUINT_LVL0PRI_1_bp = 1             ; Interrupt Level Priority bit 1 position
.equ CPUINT_LVL0PRI_2_bm = (1<<2)        ; Interrupt Level Priority bit 2 mask
.equ CPUINT_LVL0PRI_2_bp = 2             ; Interrupt Level Priority bit 2 position
.equ CPUINT_LVL0PRI_3_bm = (1<<3)        ; Interrupt Level Priority bit 3 mask
.equ CPUINT_LVL0PRI_3_bp = 3             ; Interrupt Level Priority bit 3 position
.equ CPUINT_LVL0PRI_4_bm = (1<<4)        ; Interrupt Level Priority bit 4 mask
.equ CPUINT_LVL0PRI_4_bp = 4             ; Interrupt Level Priority bit 4 position
.equ CPUINT_LVL0PRI_5_bm = (1<<5)        ; Interrupt Level Priority bit 5 mask
.equ CPUINT_LVL0PRI_5_bp = 5             ; Interrupt Level Priority bit 5 position
.equ CPUINT_LVL0PRI_6_bm = (1<<6)        ; Interrupt Level Priority bit 6 mask
.equ CPUINT_LVL0PRI_6_bp = 6             ; Interrupt Level Priority bit 6 position
.equ CPUINT_LVL0PRI_7_bm = (1<<7)        ; Interrupt Level Priority bit 7 mask
.equ CPUINT_LVL0PRI_7_bp = 7             ; Interrupt Level Priority bit 7 position

; CPUINT_LVL1VEC masks
.equ CPUINT_LVL1VEC_gm = 0xFF            ; Interrupt Vector with High Priority group mask
.equ CPUINT_LVL1VEC_gp = 0               ; Interrupt Vector with High Priority group position
.equ CPUINT_LVL1VEC_0_bm = (1<<0)        ; Interrupt Vector with High Priority bit 0 mask
.equ CPUINT_LVL1VEC_0_bp = 0             ; Interrupt Vector with High Priority bit 0 position
.equ CPUINT_LVL1VEC_1_bm = (1<<1)        ; Interrupt Vector with High Priority bit 1 mask
.equ CPUINT_LVL1VEC_1_bp = 1             ; Interrupt Vector with High Priority bit 1 position
.equ CPUINT_LVL1VEC_2_bm = (1<<2)        ; Interrupt Vector with High Priority bit 2 mask
.equ CPUINT_LVL1VEC_2_bp = 2             ; Interrupt Vector with High Priority bit 2 position
.equ CPUINT_LVL1VEC_3_bm = (1<<3)        ; Interrupt Vector with High Priority bit 3 mask
.equ CPUINT_LVL1VEC_3_bp = 3             ; Interrupt Vector with High Priority bit 3 position
.equ CPUINT_LVL1VEC_4_bm = (1<<4)        ; Interrupt Vector with High Priority bit 4 mask
.equ CPUINT_LVL1VEC_4_bp = 4             ; Interrupt Vector with High Priority bit 4 position
.equ CPUINT_LVL1VEC_5_bm = (1<<5)        ; Interrupt Vector with High Priority bit 5 mask
.equ CPUINT_LVL1VEC_5_bp = 5             ; Interrupt Vector with High Priority bit 5 position
.equ CPUINT_LVL1VEC_6_bm = (1<<6)        ; Interrupt Vector with High Priority bit 6 mask
.equ CPUINT_LVL1VEC_6_bp = 6             ; Interrupt Vector with High Priority bit 6 position
.equ CPUINT_LVL1VEC_7_bm = (1<<7)        ; Interrupt Vector with High Priority bit 7 mask
.equ CPUINT_LVL1VEC_7_bp = 7             ; Interrupt Vector with High Priority bit 7 position


;*************************************************************************
;** CRCSCAN - CRCSCAN
;*************************************************************************

; CRCSCAN_CTRLA masks
.equ CRCSCAN_ENABLE_bm = 0x01            ; Enable CRC scan bit mask
.equ CRCSCAN_ENABLE_bp = 0               ; Enable CRC scan bit position
.equ CRCSCAN_NMIEN_bm = 0x02             ; Enable NMI Trigger bit mask
.equ CRCSCAN_NMIEN_bp = 1                ; Enable NMI Trigger bit position
.equ CRCSCAN_RESET_bm = 0x80             ; Reset CRC scan bit mask
.equ CRCSCAN_RESET_bp = 7                ; Reset CRC scan bit position

; CRCSCAN_CTRLB masks
.equ CRCSCAN_SRC_gm = 0x03               ; CRC Source group mask
.equ CRCSCAN_SRC_gp = 0                  ; CRC Source group position
.equ CRCSCAN_SRC_0_bm = (1<<0)           ; CRC Source bit 0 mask
.equ CRCSCAN_SRC_0_bp = 0                ; CRC Source bit 0 position
.equ CRCSCAN_SRC_1_bm = (1<<1)           ; CRC Source bit 1 mask
.equ CRCSCAN_SRC_1_bp = 1                ; CRC Source bit 1 position

; CRCSCAN_STATUS masks
.equ CRCSCAN_BUSY_bm = 0x01              ; CRC Busy bit mask
.equ CRCSCAN_BUSY_bp = 0                 ; CRC Busy bit position
.equ CRCSCAN_OK_bm = 0x02                ; CRC Ok bit mask
.equ CRCSCAN_OK_bp = 1                   ; CRC Ok bit position

; CRC Source select
.equ CRCSCAN_SRC_FLASH_gc = (0x00<<0)    ; CRC on entire flash
.equ CRCSCAN_SRC_APPLICATION_gc = (0x01<<0) ; CRC on boot and appl section of flash
.equ CRCSCAN_SRC_BOOT_gc = (0x02<<0)     ; CRC on boot section of flash


;*************************************************************************
;** DAC - Digital to Analog Converter
;*************************************************************************

; DAC_CTRLA masks
.equ DAC_ENABLE_bm = 0x01                ; DAC Enable bit mask
.equ DAC_ENABLE_bp = 0                   ; DAC Enable bit position
.equ DAC_OUTRANGE_gm = 0x30              ; Output Buffer Range group mask
.equ DAC_OUTRANGE_gp = 4                 ; Output Buffer Range group position
.equ DAC_OUTRANGE_0_bm = (1<<4)          ; Output Buffer Range bit 0 mask
.equ DAC_OUTRANGE_0_bp = 4               ; Output Buffer Range bit 0 position
.equ DAC_OUTRANGE_1_bm = (1<<5)          ; Output Buffer Range bit 1 mask
.equ DAC_OUTRANGE_1_bp = 5               ; Output Buffer Range bit 1 position
.equ DAC_OUTEN_bm = 0x40                 ; Output Buffer Enable bit mask
.equ DAC_OUTEN_bp = 6                    ; Output Buffer Enable bit position
.equ DAC_RUNSTDBY_bm = 0x80              ; Run in Standby Mode bit mask
.equ DAC_RUNSTDBY_bp = 7                 ; Run in Standby Mode bit position

; DAC_DATA masks
.equ DAC_DATA_gm = 0xFFC0                ; Data group mask
.equ DAC_DATA_gp = 6                     ; Data group position
.equ DAC_DATA_0_bm = (1<<6)              ; Data bit 0 mask
.equ DAC_DATA_0_bp = 6                   ; Data bit 0 position
.equ DAC_DATA_1_bm = (1<<7)              ; Data bit 1 mask
.equ DAC_DATA_1_bp = 7                   ; Data bit 1 position
.equ DAC_DATA_2_bm = (1<<8)              ; Data bit 2 mask
.equ DAC_DATA_2_bp = 8                   ; Data bit 2 position
.equ DAC_DATA_3_bm = (1<<9)              ; Data bit 3 mask
.equ DAC_DATA_3_bp = 9                   ; Data bit 3 position
.equ DAC_DATA_4_bm = (1<<10)             ; Data bit 4 mask
.equ DAC_DATA_4_bp = 10                  ; Data bit 4 position
.equ DAC_DATA_5_bm = (1<<11)             ; Data bit 5 mask
.equ DAC_DATA_5_bp = 11                  ; Data bit 5 position
.equ DAC_DATA_6_bm = (1<<12)             ; Data bit 6 mask
.equ DAC_DATA_6_bp = 12                  ; Data bit 6 position
.equ DAC_DATA_7_bm = (1<<13)             ; Data bit 7 mask
.equ DAC_DATA_7_bp = 13                  ; Data bit 7 position
.equ DAC_DATA_8_bm = (1<<14)             ; Data bit 8 mask
.equ DAC_DATA_8_bp = 14                  ; Data bit 8 position
.equ DAC_DATA_9_bm = (1<<15)             ; Data bit 9 mask
.equ DAC_DATA_9_bp = 15                  ; Data bit 9 position

; Output Buffer Range select
.equ DAC_OUTRANGE_AUTO_gc = (0x00<<4)    ; Output buffer automatically choose best range
.equ DAC_OUTRANGE_LOW_gc = (0x02<<4)     ; Output buffer configured to low range
.equ DAC_OUTRANGE_HIGH_gc = (0x03<<4)    ; Output buffer configured to high range


;*************************************************************************
;** EVSYS - Event System
;*************************************************************************

; EVSYS_SWEVENTA masks
.equ EVSYS_SWEVENTA_gm = 0xFF            ; Software event on channel select group mask
.equ EVSYS_SWEVENTA_gp = 0               ; Software event on channel select group position
.equ EVSYS_SWEVENTA_0_bm = (1<<0)        ; Software event on channel select bit 0 mask
.equ EVSYS_SWEVENTA_0_bp = 0             ; Software event on channel select bit 0 position
.equ EVSYS_SWEVENTA_1_bm = (1<<1)        ; Software event on channel select bit 1 mask
.equ EVSYS_SWEVENTA_1_bp = 1             ; Software event on channel select bit 1 position
.equ EVSYS_SWEVENTA_2_bm = (1<<2)        ; Software event on channel select bit 2 mask
.equ EVSYS_SWEVENTA_2_bp = 2             ; Software event on channel select bit 2 position
.equ EVSYS_SWEVENTA_3_bm = (1<<3)        ; Software event on channel select bit 3 mask
.equ EVSYS_SWEVENTA_3_bp = 3             ; Software event on channel select bit 3 position
.equ EVSYS_SWEVENTA_4_bm = (1<<4)        ; Software event on channel select bit 4 mask
.equ EVSYS_SWEVENTA_4_bp = 4             ; Software event on channel select bit 4 position
.equ EVSYS_SWEVENTA_5_bm = (1<<5)        ; Software event on channel select bit 5 mask
.equ EVSYS_SWEVENTA_5_bp = 5             ; Software event on channel select bit 5 position
.equ EVSYS_SWEVENTA_6_bm = (1<<6)        ; Software event on channel select bit 6 mask
.equ EVSYS_SWEVENTA_6_bp = 6             ; Software event on channel select bit 6 position
.equ EVSYS_SWEVENTA_7_bm = (1<<7)        ; Software event on channel select bit 7 mask
.equ EVSYS_SWEVENTA_7_bp = 7             ; Software event on channel select bit 7 position

; EVSYS_CHANNEL0 masks
.equ EVSYS_CHANNEL_gm = 0xFF             ; Channel generator select group mask
.equ EVSYS_CHANNEL_gp = 0                ; Channel generator select group position
.equ EVSYS_CHANNEL_0_bm = (1<<0)         ; Channel generator select bit 0 mask
.equ EVSYS_CHANNEL_0_bp = 0              ; Channel generator select bit 0 position
.equ EVSYS_CHANNEL_1_bm = (1<<1)         ; Channel generator select bit 1 mask
.equ EVSYS_CHANNEL_1_bp = 1              ; Channel generator select bit 1 position
.equ EVSYS_CHANNEL_2_bm = (1<<2)         ; Channel generator select bit 2 mask
.equ EVSYS_CHANNEL_2_bp = 2              ; Channel generator select bit 2 position
.equ EVSYS_CHANNEL_3_bm = (1<<3)         ; Channel generator select bit 3 mask
.equ EVSYS_CHANNEL_3_bp = 3              ; Channel generator select bit 3 position
.equ EVSYS_CHANNEL_4_bm = (1<<4)         ; Channel generator select bit 4 mask
.equ EVSYS_CHANNEL_4_bp = 4              ; Channel generator select bit 4 position
.equ EVSYS_CHANNEL_5_bm = (1<<5)         ; Channel generator select bit 5 mask
.equ EVSYS_CHANNEL_5_bp = 5              ; Channel generator select bit 5 position
.equ EVSYS_CHANNEL_6_bm = (1<<6)         ; Channel generator select bit 6 mask
.equ EVSYS_CHANNEL_6_bp = 6              ; Channel generator select bit 6 position
.equ EVSYS_CHANNEL_7_bm = (1<<7)         ; Channel generator select bit 7 mask
.equ EVSYS_CHANNEL_7_bp = 7              ; Channel generator select bit 7 position

; EVSYS_CHANNEL1 masks
; Masks for EVSYS_CHANNEL already defined

; EVSYS_CHANNEL2 masks
; Masks for EVSYS_CHANNEL already defined

; EVSYS_CHANNEL3 masks
; Masks for EVSYS_CHANNEL already defined

; EVSYS_CHANNEL4 masks
; Masks for EVSYS_CHANNEL already defined

; EVSYS_CHANNEL5 masks
; Masks for EVSYS_CHANNEL already defined

; EVSYS_USERCCLLUT0A masks
.equ EVSYS_USER_gm = 0xFF                ; User channel select group mask
.equ EVSYS_USER_gp = 0                   ; User channel select group position
.equ EVSYS_USER_0_bm = (1<<0)            ; User channel select bit 0 mask
.equ EVSYS_USER_0_bp = 0                 ; User channel select bit 0 position
.equ EVSYS_USER_1_bm = (1<<1)            ; User channel select bit 1 mask
.equ EVSYS_USER_1_bp = 1                 ; User channel select bit 1 position
.equ EVSYS_USER_2_bm = (1<<2)            ; User channel select bit 2 mask
.equ EVSYS_USER_2_bp = 2                 ; User channel select bit 2 position
.equ EVSYS_USER_3_bm = (1<<3)            ; User channel select bit 3 mask
.equ EVSYS_USER_3_bp = 3                 ; User channel select bit 3 position
.equ EVSYS_USER_4_bm = (1<<4)            ; User channel select bit 4 mask
.equ EVSYS_USER_4_bp = 4                 ; User channel select bit 4 position
.equ EVSYS_USER_5_bm = (1<<5)            ; User channel select bit 5 mask
.equ EVSYS_USER_5_bp = 5                 ; User channel select bit 5 position
.equ EVSYS_USER_6_bm = (1<<6)            ; User channel select bit 6 mask
.equ EVSYS_USER_6_bp = 6                 ; User channel select bit 6 position
.equ EVSYS_USER_7_bm = (1<<7)            ; User channel select bit 7 mask
.equ EVSYS_USER_7_bp = 7                 ; User channel select bit 7 position

; EVSYS_USERCCLLUT0B masks
; Masks for EVSYS_USER already defined

; EVSYS_USERCCLLUT1A masks
; Masks for EVSYS_USER already defined

; EVSYS_USERCCLLUT1B masks
; Masks for EVSYS_USER already defined

; EVSYS_USERCCLLUT2A masks
; Masks for EVSYS_USER already defined

; EVSYS_USERCCLLUT2B masks
; Masks for EVSYS_USER already defined

; EVSYS_USERCCLLUT3A masks
; Masks for EVSYS_USER already defined

; EVSYS_USERCCLLUT3B masks
; Masks for EVSYS_USER already defined

; EVSYS_USERADC0START masks
; Masks for EVSYS_USER already defined

; EVSYS_USEREVSYSEVOUTA masks
; Masks for EVSYS_USER already defined

; EVSYS_USEREVSYSEVOUTB masks
; Masks for EVSYS_USER already defined

; EVSYS_USEREVSYSEVOUTC masks
; Masks for EVSYS_USER already defined

; EVSYS_USEREVSYSEVOUTD masks
; Masks for EVSYS_USER already defined

; EVSYS_USEREVSYSEVOUTE masks
; Masks for EVSYS_USER already defined

; EVSYS_USEREVSYSEVOUTF masks
; Masks for EVSYS_USER already defined

; EVSYS_USERUSART0IRDA masks
; Masks for EVSYS_USER already defined

; EVSYS_USERUSART1IRDA masks
; Masks for EVSYS_USER already defined

; EVSYS_USERUSART2IRDA masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCA0CNTA masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCA0CNTB masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCA1CNTA masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCA1CNTB masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB0CAPT masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB0COUNT masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB1CAPT masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB1COUNT masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB2CAPT masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB2COUNT masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB3CAPT masks
; Masks for EVSYS_USER already defined

; EVSYS_USERTCB3COUNT masks
; Masks for EVSYS_USER already defined

; Channel generator select
.equ EVSYS_CHANNEL_OFF_gc = (0x00<<0)    ; Off
.equ EVSYS_CHANNEL_UPDI_SYNCH_gc = (0x01<<0) ; UPDI SYNCH character
.equ EVSYS_CHANNEL_RTC_OVF_gc = (0x06<<0) ; Real Time Counter overflow
.equ EVSYS_CHANNEL_RTC_CMP_gc = (0x07<<0) ; Real Time Counter compare
.equ EVSYS_CHANNEL_RTC_PITEV0_gc = (0x08<<0) ; Periodic Interrupt Timer Event 0
.equ EVSYS_CHANNEL_RTC_PITEV1_gc = (0x09<<0) ; Periodic Interrupt Timer Event 1
.equ EVSYS_CHANNEL_CCL_LUT0_gc = (0x10<<0) ; Configurable Custom Logic LUT0
.equ EVSYS_CHANNEL_CCL_LUT1_gc = (0x11<<0) ; Configurable Custom Logic LUT1
.equ EVSYS_CHANNEL_CCL_LUT2_gc = (0x12<<0) ; Configurable Custom Logic LUT2
.equ EVSYS_CHANNEL_CCL_LUT3_gc = (0x13<<0) ; Configurable Custom Logic LUT3
.equ EVSYS_CHANNEL_AC0_OUT_gc = (0x20<<0) ; Analog Comparator 0 out
.equ EVSYS_CHANNEL_AC1_OUT_gc = (0x21<<0) ; Analog Comparator 1 out
.equ EVSYS_CHANNEL_ADC0_RES_gc = (0x24<<0) ; ADC 0 Result Ready
.equ EVSYS_CHANNEL_ADC0_SAMP_gc = (0x25<<0) ; ADC 0 Sample Ready
.equ EVSYS_CHANNEL_ADC0_WCMP_gc = (0x26<<0) ; ADC 0 Window Compare
.equ EVSYS_CHANNEL_PORTA_EV0_gc = (0x40<<0) ; Port A Event 0
.equ EVSYS_CHANNEL_PORTA_EV1_gc = (0x41<<0) ; Port A Event 1
.equ EVSYS_CHANNEL_PORTB_EV0_gc = (0x42<<0) ; Port B Event 0
.equ EVSYS_CHANNEL_PORTB_EV1_gc = (0x43<<0) ; Port B Event 1
.equ EVSYS_CHANNEL_PORTC_EV0_gc = (0x44<<0) ; Port C Event 0
.equ EVSYS_CHANNEL_PORTC_EV1_gc = (0x45<<0) ; Port C Event 1
.equ EVSYS_CHANNEL_PORTD_EV0_gc = (0x46<<0) ; Port D Event 0
.equ EVSYS_CHANNEL_PORTD_EV1_gc = (0x47<<0) ; Port D Event 1
.equ EVSYS_CHANNEL_PORTE_EV0_gc = (0x48<<0) ; Port E Event 0
.equ EVSYS_CHANNEL_PORTE_EV1_gc = (0x49<<0) ; Port E Event 1
.equ EVSYS_CHANNEL_PORTF_EV0_gc = (0x4A<<0) ; Port F Event 0
.equ EVSYS_CHANNEL_PORTF_EV1_gc = (0x4B<<0) ; Port F Event 1
.equ EVSYS_CHANNEL_USART0_XCK_gc = (0x60<<0) ; USART 0 XCK
.equ EVSYS_CHANNEL_USART1_XCK_gc = (0x61<<0) ; USART 1 XCK
.equ EVSYS_CHANNEL_USART2_XCK_gc = (0x62<<0) ; USART 2 XCK
.equ EVSYS_CHANNEL_SPI0_SCK_gc = (0x68<<0) ; SPI 0 SCK
.equ EVSYS_CHANNEL_TCA0_OVF_LUNF_gc = (0x80<<0) ; Timer/Counter A0 overflow / low byte timer underflow
.equ EVSYS_CHANNEL_TCA0_HUNF_gc = (0x81<<0) ; Timer/Counter A0 high byte timer underflow
.equ EVSYS_CHANNEL_TCA0_CMP0_LCMP0_gc = (0x84<<0) ; Timer/Counter A0 compare 0 / low byte timer compare 0
.equ EVSYS_CHANNEL_TCA0_CMP1_LCMP1_gc = (0x85<<0) ; Timer/Counter A0 compare 1 / low byte timer compare 1
.equ EVSYS_CHANNEL_TCA0_CMP2_LCMP2_gc = (0x86<<0) ; Timer/Counter A0 compare 2 / low byte timer compare 2
.equ EVSYS_CHANNEL_TCA1_OVF_LUNF_gc = (0x88<<0) ; Timer/Counter A1 overflow / low byte timer underflow
.equ EVSYS_CHANNEL_TCA1_HUNF_gc = (0x89<<0) ; Timer/Counter A1 high byte timer underflow
.equ EVSYS_CHANNEL_TCA1_CMP0_LCMP0_gc = (0x8C<<0) ; Timer/Counter A1 compare 0 / low byte timer compare 0
.equ EVSYS_CHANNEL_TCA1_CMP1_LCMP1_gc = (0x8D<<0) ; Timer/Counter A1 compare 1 / low byte timer compare 1
.equ EVSYS_CHANNEL_TCA1_CMP2_LCMP2_gc = (0x8E<<0) ; Timer/Counter A1 compare 2 / low byte timer compare 2
.equ EVSYS_CHANNEL_TCB0_CAPT_gc = (0xA0<<0) ; Timer/Counter B0 capture
.equ EVSYS_CHANNEL_TCB0_OVF_gc = (0xA1<<0) ; Timer/Counter B0 overflow
.equ EVSYS_CHANNEL_TCB1_CAPT_gc = (0xA2<<0) ; Timer/Counter B1 capture
.equ EVSYS_CHANNEL_TCB1_OVF_gc = (0xA3<<0) ; Timer/Counter B1 overflow
.equ EVSYS_CHANNEL_TCB2_CAPT_gc = (0xA4<<0) ; Timer/Counter B2 capture
.equ EVSYS_CHANNEL_TCB2_OVF_gc = (0xA5<<0) ; Timer/Counter B2 overflow
.equ EVSYS_CHANNEL_TCB3_CAPT_gc = (0xA6<<0) ; Timer/Counter B3 capture
.equ EVSYS_CHANNEL_TCB3_OVF_gc = (0xA7<<0) ; Timer/Counter B3 overflow

; Software event on channel select
.equ EVSYS_SWEVENTA_CH0_gc = (0x01<<0)   ; Software event on channel 0
.equ EVSYS_SWEVENTA_CH1_gc = (0x02<<0)   ; Software event on channel 1
.equ EVSYS_SWEVENTA_CH2_gc = (0x04<<0)   ; Software event on channel 2
.equ EVSYS_SWEVENTA_CH3_gc = (0x08<<0)   ; Software event on channel 3
.equ EVSYS_SWEVENTA_CH4_gc = (0x10<<0)   ; Software event on channel 4
.equ EVSYS_SWEVENTA_CH5_gc = (0x20<<0)   ; Software event on channel 5
.equ EVSYS_SWEVENTA_CH6_gc = (0x40<<0)   ; Software event on channel 6
.equ EVSYS_SWEVENTA_CH7_gc = (0x80<<0)   ; Software event on channel 7

; User channel select
.equ EVSYS_USER_OFF_gc = (0x00<<0)       ; Off, No Eventsys Channel connected
.equ EVSYS_USER_CHANNEL0_gc = (0x01<<0)  ; Connect user to event channel 0
.equ EVSYS_USER_CHANNEL1_gc = (0x02<<0)  ; Connect user to event channel 1
.equ EVSYS_USER_CHANNEL2_gc = (0x03<<0)  ; Connect user to event channel 2
.equ EVSYS_USER_CHANNEL3_gc = (0x04<<0)  ; Connect user to event channel 3
.equ EVSYS_USER_CHANNEL4_gc = (0x05<<0)  ; Connect user to event channel 4
.equ EVSYS_USER_CHANNEL5_gc = (0x06<<0)  ; Connect user to event channel 5


;*************************************************************************
;** FUSE - Fuses
;*************************************************************************

; FUSE_WDTCFG masks
.equ FUSE_PERIOD_gm = 0x0F               ; Watchdog Timeout Period group mask
.equ FUSE_PERIOD_gp = 0                  ; Watchdog Timeout Period group position
.equ FUSE_PERIOD_0_bm = (1<<0)           ; Watchdog Timeout Period bit 0 mask
.equ FUSE_PERIOD_0_bp = 0                ; Watchdog Timeout Period bit 0 position
.equ FUSE_PERIOD_1_bm = (1<<1)           ; Watchdog Timeout Period bit 1 mask
.equ FUSE_PERIOD_1_bp = 1                ; Watchdog Timeout Period bit 1 position
.equ FUSE_PERIOD_2_bm = (1<<2)           ; Watchdog Timeout Period bit 2 mask
.equ FUSE_PERIOD_2_bp = 2                ; Watchdog Timeout Period bit 2 position
.equ FUSE_PERIOD_3_bm = (1<<3)           ; Watchdog Timeout Period bit 3 mask
.equ FUSE_PERIOD_3_bp = 3                ; Watchdog Timeout Period bit 3 position
.equ FUSE_WINDOW_gm = 0xF0               ; Watchdog Window Timeout Period group mask
.equ FUSE_WINDOW_gp = 4                  ; Watchdog Window Timeout Period group position
.equ FUSE_WINDOW_0_bm = (1<<4)           ; Watchdog Window Timeout Period bit 0 mask
.equ FUSE_WINDOW_0_bp = 4                ; Watchdog Window Timeout Period bit 0 position
.equ FUSE_WINDOW_1_bm = (1<<5)           ; Watchdog Window Timeout Period bit 1 mask
.equ FUSE_WINDOW_1_bp = 5                ; Watchdog Window Timeout Period bit 1 position
.equ FUSE_WINDOW_2_bm = (1<<6)           ; Watchdog Window Timeout Period bit 2 mask
.equ FUSE_WINDOW_2_bp = 6                ; Watchdog Window Timeout Period bit 2 position
.equ FUSE_WINDOW_3_bm = (1<<7)           ; Watchdog Window Timeout Period bit 3 mask
.equ FUSE_WINDOW_3_bp = 7                ; Watchdog Window Timeout Period bit 3 position

; FUSE_BODCFG masks
.equ FUSE_SLEEP_gm = 0x03                ; BOD Operation in Sleep Mode group mask
.equ FUSE_SLEEP_gp = 0                   ; BOD Operation in Sleep Mode group position
.equ FUSE_SLEEP_0_bm = (1<<0)            ; BOD Operation in Sleep Mode bit 0 mask
.equ FUSE_SLEEP_0_bp = 0                 ; BOD Operation in Sleep Mode bit 0 position
.equ FUSE_SLEEP_1_bm = (1<<1)            ; BOD Operation in Sleep Mode bit 1 mask
.equ FUSE_SLEEP_1_bp = 1                 ; BOD Operation in Sleep Mode bit 1 position
.equ FUSE_ACTIVE_gm = 0x0C               ; BOD Operation in Active Mode group mask
.equ FUSE_ACTIVE_gp = 2                  ; BOD Operation in Active Mode group position
.equ FUSE_ACTIVE_0_bm = (1<<2)           ; BOD Operation in Active Mode bit 0 mask
.equ FUSE_ACTIVE_0_bp = 2                ; BOD Operation in Active Mode bit 0 position
.equ FUSE_ACTIVE_1_bm = (1<<3)           ; BOD Operation in Active Mode bit 1 mask
.equ FUSE_ACTIVE_1_bp = 3                ; BOD Operation in Active Mode bit 1 position
.equ FUSE_SAMPFREQ_bm = 0x10             ; BOD Sample Frequency bit mask
.equ FUSE_SAMPFREQ_bp = 4                ; BOD Sample Frequency bit position
.equ FUSE_LVL_gm = 0xE0                  ; BOD Level group mask
.equ FUSE_LVL_gp = 5                     ; BOD Level group position
.equ FUSE_LVL_0_bm = (1<<5)              ; BOD Level bit 0 mask
.equ FUSE_LVL_0_bp = 5                   ; BOD Level bit 0 position
.equ FUSE_LVL_1_bm = (1<<6)              ; BOD Level bit 1 mask
.equ FUSE_LVL_1_bp = 6                   ; BOD Level bit 1 position
.equ FUSE_LVL_2_bm = (1<<7)              ; BOD Level bit 2 mask
.equ FUSE_LVL_2_bp = 7                   ; BOD Level bit 2 position

; FUSE_OSCCFG masks
.equ FUSE_OSCHFFRQ_bm = 0x08             ; High-frequency Oscillator Frequency bit mask
.equ FUSE_OSCHFFRQ_bp = 3                ; High-frequency Oscillator Frequency bit position

; FUSE_SYSCFG0 masks
.equ FUSE_EESAVE_bm = 0x01               ; EEPROM Save bit mask
.equ FUSE_EESAVE_bp = 0                  ; EEPROM Save bit position
.equ FUSE_RSTPINCFG_bm = 0x08            ; Reset Pin Configuration bit mask
.equ FUSE_RSTPINCFG_bp = 3               ; Reset Pin Configuration bit position
.equ FUSE_UPDIPINCFG_bm = 0x10           ; UPDI Pin Configuration bit mask
.equ FUSE_UPDIPINCFG_bp = 4              ; UPDI Pin Configuration bit position
.equ FUSE_CRCSEL_bm = 0x20               ; CRC Select bit mask
.equ FUSE_CRCSEL_bp = 5                  ; CRC Select bit position
.equ FUSE_CRCSRC_gm = 0xC0               ; CRC Source group mask
.equ FUSE_CRCSRC_gp = 6                  ; CRC Source group position
.equ FUSE_CRCSRC_0_bm = (1<<6)           ; CRC Source bit 0 mask
.equ FUSE_CRCSRC_0_bp = 6                ; CRC Source bit 0 position
.equ FUSE_CRCSRC_1_bm = (1<<7)           ; CRC Source bit 1 mask
.equ FUSE_CRCSRC_1_bp = 7                ; CRC Source bit 1 position

; FUSE_SYSCFG1 masks
.equ FUSE_SUT_gm = 0x07                  ; Startup Time group mask
.equ FUSE_SUT_gp = 0                     ; Startup Time group position
.equ FUSE_SUT_0_bm = (1<<0)              ; Startup Time bit 0 mask
.equ FUSE_SUT_0_bp = 0                   ; Startup Time bit 0 position
.equ FUSE_SUT_1_bm = (1<<1)              ; Startup Time bit 1 mask
.equ FUSE_SUT_1_bp = 1                   ; Startup Time bit 1 position
.equ FUSE_SUT_2_bm = (1<<2)              ; Startup Time bit 2 mask
.equ FUSE_SUT_2_bp = 2                   ; Startup Time bit 2 position

; BOD Operation in Active Mode select
.equ FUSE_ACTIVE_DISABLE_gc = (0x00<<2)  ; Disabled
.equ FUSE_ACTIVE_ENABLED_gc = (0x01<<2)  ; Enabled in continuous mode
.equ FUSE_ACTIVE_SAMPLED_gc = (0x02<<2)  ; Enabled in sampled mode
.equ FUSE_ACTIVE_ENABLEWAIT_gc = (0x03<<2) ; Enabled in continuous mode. Execution halted at wake-up until BOD is running

; BOD Level select
.equ FUSE_LVL_BODLEVEL0_gc = (0x00<<5)   ; BOD Disabled during normal operation
.equ FUSE_LVL_BODLEVEL1_gc = (0x01<<5)   ; 1.9 V
.equ FUSE_LVL_BODLEVEL2_gc = (0x02<<5)   ; 2.7 V
.equ FUSE_LVL_BODLEVEL3_gc = (0x03<<5)   ; 4.5 V

; BOD Sample Frequency select
.equ FUSE_SAMPFREQ_128HZ_gc = (0x00<<4)  ; Sampling frequency is 128 Hz
.equ FUSE_SAMPFREQ_32HZ_gc = (0x01<<4)   ; Sample frequency is 32 Hz

; BOD Operation in Sleep Mode select
.equ FUSE_SLEEP_DISABLE_gc = (0x00<<0)   ; Disabled
.equ FUSE_SLEEP_ENABLE_gc = (0x01<<0)    ; Enabled in continuous mode
.equ FUSE_SLEEP_SAMPLE_gc = (0x02<<0)    ; Enabled in sampled mode

; High-frequency Oscillator Frequency select
.equ FUSE_OSCHFFRQ_20M_gc = (0x00<<3)    ; OSCHF running at 20 MHz
.equ FUSE_OSCHFFRQ_16M_gc = (0x01<<3)    ; OSCHF running at 16 MHz

; CRC Select
.equ FUSE_CRCSEL_CRC16_gc = (0x00<<5)    ; Enable CRC16
.equ FUSE_CRCSEL_CRC32_gc = (0x01<<5)    ; Enable CRC32

; CRC Source select
.equ FUSE_CRCSRC_FLASH_gc = (0x00<<6)    ; CRC of full Flash (boot, application code and application data)
.equ FUSE_CRCSRC_BOOT_gc = (0x01<<6)     ; CRC of boot section
.equ FUSE_CRCSRC_BOOTAPP_gc = (0x02<<6)  ; CRC of application code and boot sections
.equ FUSE_CRCSRC_NOCRC_gc = (0x03<<6)    ; No CRC

; EEPROM Save select
.equ FUSE_EESAVE_DISABLE_gc = (0x00<<0)  ; EEPROM content is erased during chip erase
.equ FUSE_EESAVE_ENABLE_gc = (0x01<<0)   ; EEPROM content is preserved during chip erase

; Reset Pin Configuration select
.equ FUSE_RSTPINCFG_NONE_gc = (0x00<<3)  ; No External Reset
.equ FUSE_RSTPINCFG_RESET_gc = (0x01<<3) ; PF6 configured as RESET pin

; UPDI Pin Configuration select
.equ FUSE_UPDIPINCFG_GPIO_gc = (0x00<<4) ; PF7 Configured as GPIO pin
.equ FUSE_UPDIPINCFG_UPDI_gc = (0x01<<4) ; PF7 Configured as UPDI pin

; Startup Time select
.equ FUSE_SUT_0MS_gc = (0x00<<0)         ; 0 ms
.equ FUSE_SUT_1MS_gc = (0x01<<0)         ; 1 ms
.equ FUSE_SUT_2MS_gc = (0x02<<0)         ; 2 ms
.equ FUSE_SUT_4MS_gc = (0x03<<0)         ; 4 ms
.equ FUSE_SUT_8MS_gc = (0x04<<0)         ; 8 ms
.equ FUSE_SUT_16MS_gc = (0x05<<0)        ; 16 ms
.equ FUSE_SUT_32MS_gc = (0x06<<0)        ; 32 ms
.equ FUSE_SUT_64MS_gc = (0x07<<0)        ; 64 ms

; Watchdog Timeout Period select
.equ FUSE_PERIOD_OFF_gc = (0x00<<0)      ; Watch-Dog timer Off
.equ FUSE_PERIOD_8CLK_gc = (0x01<<0)     ; 8 cycles (8ms)
.equ FUSE_PERIOD_16CLK_gc = (0x02<<0)    ; 16 cycles (16ms)
.equ FUSE_PERIOD_32CLK_gc = (0x03<<0)    ; 32 cycles (32ms)
.equ FUSE_PERIOD_64CLK_gc = (0x04<<0)    ; 64 cycles (64ms)
.equ FUSE_PERIOD_128CLK_gc = (0x05<<0)   ; 128 cycles (0.128s)
.equ FUSE_PERIOD_256CLK_gc = (0x06<<0)   ; 256 cycles (0.256s)
.equ FUSE_PERIOD_512CLK_gc = (0x07<<0)   ; 512 cycles (0.512s)
.equ FUSE_PERIOD_1KCLK_gc = (0x08<<0)    ; 1K cycles (1.0s)
.equ FUSE_PERIOD_2KCLK_gc = (0x09<<0)    ; 2K cycles (2.0s)
.equ FUSE_PERIOD_4KCLK_gc = (0x0A<<0)    ; 4K cycles (4.0s)
.equ FUSE_PERIOD_8KCLK_gc = (0x0B<<0)    ; 8K cycles (8.0s)

; Watchdog Window Timeout Period select
.equ FUSE_WINDOW_OFF_gc = (0x00<<4)      ; Window mode off
.equ FUSE_WINDOW_8CLK_gc = (0x01<<4)     ; 8 cycles (8ms)
.equ FUSE_WINDOW_16CLK_gc = (0x02<<4)    ; 16 cycles (16ms)
.equ FUSE_WINDOW_32CLK_gc = (0x03<<4)    ; 32 cycles (32ms)
.equ FUSE_WINDOW_64CLK_gc = (0x04<<4)    ; 64 cycles (64ms)
.equ FUSE_WINDOW_128CLK_gc = (0x05<<4)   ; 128 cycles (0.128s)
.equ FUSE_WINDOW_256CLK_gc = (0x06<<4)   ; 256 cycles (0.256s)
.equ FUSE_WINDOW_512CLK_gc = (0x07<<4)   ; 512 cycles (0.512s)
.equ FUSE_WINDOW_1KCLK_gc = (0x08<<4)    ; 1K cycles (1.0s)
.equ FUSE_WINDOW_2KCLK_gc = (0x09<<4)    ; 2K cycles (2.0s)
.equ FUSE_WINDOW_4KCLK_gc = (0x0A<<4)    ; 4K cycles (4.0s)
.equ FUSE_WINDOW_8KCLK_gc = (0x0B<<4)    ; 8K cycles (8.0s)


;*************************************************************************
;** GPR - General Purpose Registers
;*************************************************************************


;*************************************************************************
;** LOCK - Lockbits
;*************************************************************************

; LOCK_KEY masks
.equ LOCK_KEY_gm = 0xFFFFFFFF            ; Lock Key group mask
.equ LOCK_KEY_gp = 0                     ; Lock Key group position
.equ LOCK_KEY_0_bm = (1<<0)              ; Lock Key bit 0 mask
.equ LOCK_KEY_0_bp = 0                   ; Lock Key bit 0 position
.equ LOCK_KEY_1_bm = (1<<1)              ; Lock Key bit 1 mask
.equ LOCK_KEY_1_bp = 1                   ; Lock Key bit 1 position
.equ LOCK_KEY_2_bm = (1<<2)              ; Lock Key bit 2 mask
.equ LOCK_KEY_2_bp = 2                   ; Lock Key bit 2 position
.equ LOCK_KEY_3_bm = (1<<3)              ; Lock Key bit 3 mask
.equ LOCK_KEY_3_bp = 3                   ; Lock Key bit 3 position
.equ LOCK_KEY_4_bm = (1<<4)              ; Lock Key bit 4 mask
.equ LOCK_KEY_4_bp = 4                   ; Lock Key bit 4 position
.equ LOCK_KEY_5_bm = (1<<5)              ; Lock Key bit 5 mask
.equ LOCK_KEY_5_bp = 5                   ; Lock Key bit 5 position
.equ LOCK_KEY_6_bm = (1<<6)              ; Lock Key bit 6 mask
.equ LOCK_KEY_6_bp = 6                   ; Lock Key bit 6 position
.equ LOCK_KEY_7_bm = (1<<7)              ; Lock Key bit 7 mask
.equ LOCK_KEY_7_bp = 7                   ; Lock Key bit 7 position
.equ LOCK_KEY_8_bm = (1<<8)              ; Lock Key bit 8 mask
.equ LOCK_KEY_8_bp = 8                   ; Lock Key bit 8 position
.equ LOCK_KEY_9_bm = (1<<9)              ; Lock Key bit 9 mask
.equ LOCK_KEY_9_bp = 9                   ; Lock Key bit 9 position
.equ LOCK_KEY_10_bm = (1<<10)            ; Lock Key bit 10 mask
.equ LOCK_KEY_10_bp = 10                 ; Lock Key bit 10 position
.equ LOCK_KEY_11_bm = (1<<11)            ; Lock Key bit 11 mask
.equ LOCK_KEY_11_bp = 11                 ; Lock Key bit 11 position
.equ LOCK_KEY_12_bm = (1<<12)            ; Lock Key bit 12 mask
.equ LOCK_KEY_12_bp = 12                 ; Lock Key bit 12 position
.equ LOCK_KEY_13_bm = (1<<13)            ; Lock Key bit 13 mask
.equ LOCK_KEY_13_bp = 13                 ; Lock Key bit 13 position
.equ LOCK_KEY_14_bm = (1<<14)            ; Lock Key bit 14 mask
.equ LOCK_KEY_14_bp = 14                 ; Lock Key bit 14 position
.equ LOCK_KEY_15_bm = (1<<15)            ; Lock Key bit 15 mask
.equ LOCK_KEY_15_bp = 15                 ; Lock Key bit 15 position
.equ LOCK_KEY_16_bm = (1<<16)            ; Lock Key bit 16 mask
.equ LOCK_KEY_16_bp = 16                 ; Lock Key bit 16 position
.equ LOCK_KEY_17_bm = (1<<17)            ; Lock Key bit 17 mask
.equ LOCK_KEY_17_bp = 17                 ; Lock Key bit 17 position
.equ LOCK_KEY_18_bm = (1<<18)            ; Lock Key bit 18 mask
.equ LOCK_KEY_18_bp = 18                 ; Lock Key bit 18 position
.equ LOCK_KEY_19_bm = (1<<19)            ; Lock Key bit 19 mask
.equ LOCK_KEY_19_bp = 19                 ; Lock Key bit 19 position
.equ LOCK_KEY_20_bm = (1<<20)            ; Lock Key bit 20 mask
.equ LOCK_KEY_20_bp = 20                 ; Lock Key bit 20 position
.equ LOCK_KEY_21_bm = (1<<21)            ; Lock Key bit 21 mask
.equ LOCK_KEY_21_bp = 21                 ; Lock Key bit 21 position
.equ LOCK_KEY_22_bm = (1<<22)            ; Lock Key bit 22 mask
.equ LOCK_KEY_22_bp = 22                 ; Lock Key bit 22 position
.equ LOCK_KEY_23_bm = (1<<23)            ; Lock Key bit 23 mask
.equ LOCK_KEY_23_bp = 23                 ; Lock Key bit 23 position
.equ LOCK_KEY_24_bm = (1<<24)            ; Lock Key bit 24 mask
.equ LOCK_KEY_24_bp = 24                 ; Lock Key bit 24 position
.equ LOCK_KEY_25_bm = (1<<25)            ; Lock Key bit 25 mask
.equ LOCK_KEY_25_bp = 25                 ; Lock Key bit 25 position
.equ LOCK_KEY_26_bm = (1<<26)            ; Lock Key bit 26 mask
.equ LOCK_KEY_26_bp = 26                 ; Lock Key bit 26 position
.equ LOCK_KEY_27_bm = (1<<27)            ; Lock Key bit 27 mask
.equ LOCK_KEY_27_bp = 27                 ; Lock Key bit 27 position
.equ LOCK_KEY_28_bm = (1<<28)            ; Lock Key bit 28 mask
.equ LOCK_KEY_28_bp = 28                 ; Lock Key bit 28 position
.equ LOCK_KEY_29_bm = (1<<29)            ; Lock Key bit 29 mask
.equ LOCK_KEY_29_bp = 29                 ; Lock Key bit 29 position
.equ LOCK_KEY_30_bm = (1<<30)            ; Lock Key bit 30 mask
.equ LOCK_KEY_30_bp = 30                 ; Lock Key bit 30 position
.equ LOCK_KEY_31_bm = (1<<31)            ; Lock Key bit 31 mask
.equ LOCK_KEY_31_bp = 31                 ; Lock Key bit 31 position

; Lock Key select
.equ LOCK_KEY_NOLOCK_gc = (0x5CC5C55C<<0) ; No locks
.equ LOCK_KEY_RWLOCK_gc = (0xA33A3AA3<<0) ; Read and write lock


;*************************************************************************
;** NVMCTRL - Non-volatile Memory Controller
;*************************************************************************

; NVMCTRL_CTRLA masks
.equ NVMCTRL_CMD_gm = 0x7F               ; Command group mask
.equ NVMCTRL_CMD_gp = 0                  ; Command group position
.equ NVMCTRL_CMD_0_bm = (1<<0)           ; Command bit 0 mask
.equ NVMCTRL_CMD_0_bp = 0                ; Command bit 0 position
.equ NVMCTRL_CMD_1_bm = (1<<1)           ; Command bit 1 mask
.equ NVMCTRL_CMD_1_bp = 1                ; Command bit 1 position
.equ NVMCTRL_CMD_2_bm = (1<<2)           ; Command bit 2 mask
.equ NVMCTRL_CMD_2_bp = 2                ; Command bit 2 position
.equ NVMCTRL_CMD_3_bm = (1<<3)           ; Command bit 3 mask
.equ NVMCTRL_CMD_3_bp = 3                ; Command bit 3 position
.equ NVMCTRL_CMD_4_bm = (1<<4)           ; Command bit 4 mask
.equ NVMCTRL_CMD_4_bp = 4                ; Command bit 4 position
.equ NVMCTRL_CMD_5_bm = (1<<5)           ; Command bit 5 mask
.equ NVMCTRL_CMD_5_bp = 5                ; Command bit 5 position
.equ NVMCTRL_CMD_6_bm = (1<<6)           ; Command bit 6 mask
.equ NVMCTRL_CMD_6_bp = 6                ; Command bit 6 position

; NVMCTRL_CTRLB masks
.equ NVMCTRL_APPCODEWP_bm = 0x01         ; Application Code Write Protect bit mask
.equ NVMCTRL_APPCODEWP_bp = 0            ; Application Code Write Protect bit position
.equ NVMCTRL_BOOTRP_bm = 0x02            ; Boot Read Protect bit mask
.equ NVMCTRL_BOOTRP_bp = 1               ; Boot Read Protect bit position
.equ NVMCTRL_APPDATAWP_bm = 0x04         ; Application Data Write Protect bit mask
.equ NVMCTRL_APPDATAWP_bp = 2            ; Application Data Write Protect bit position
.equ NVMCTRL_EEWP_bm = 0x08              ; EEPROM Write Protect bit mask
.equ NVMCTRL_EEWP_bp = 3                 ; EEPROM Write Protect bit position
.equ NVMCTRL_FLMAP_gm = 0x30             ; Flash Mapping in Data space group mask
.equ NVMCTRL_FLMAP_gp = 4                ; Flash Mapping in Data space group position
.equ NVMCTRL_FLMAP_0_bm = (1<<4)         ; Flash Mapping in Data space bit 0 mask
.equ NVMCTRL_FLMAP_0_bp = 4              ; Flash Mapping in Data space bit 0 position
.equ NVMCTRL_FLMAP_1_bm = (1<<5)         ; Flash Mapping in Data space bit 1 mask
.equ NVMCTRL_FLMAP_1_bp = 5              ; Flash Mapping in Data space bit 1 position
.equ NVMCTRL_FLMAPLOCK_bm = 0x80         ; Flash Mapping Lock bit mask
.equ NVMCTRL_FLMAPLOCK_bp = 7            ; Flash Mapping Lock bit position

; NVMCTRL_INTCTRL masks
.equ NVMCTRL_EEREADY_bm = 0x01           ; EEPROM Ready bit mask
.equ NVMCTRL_EEREADY_bp = 0              ; EEPROM Ready bit position
.equ NVMCTRL_FLREADY_bm = 0x02           ; Flash Ready bit mask
.equ NVMCTRL_FLREADY_bp = 1              ; Flash Ready bit position

; NVMCTRL_INTFLAGS masks
; Masks for NVMCTRL_EEREADY already defined
; Masks for NVMCTRL_FLREADY already defined

; NVMCTRL_STATUS masks
.equ NVMCTRL_EEBUSY_bm = 0x01            ; EEPROM busy bit mask
.equ NVMCTRL_EEBUSY_bp = 0               ; EEPROM busy bit position
.equ NVMCTRL_FLBUSY_bm = 0x02            ; Flash busy bit mask
.equ NVMCTRL_FLBUSY_bp = 1               ; Flash busy bit position
.equ NVMCTRL_ERROR_gm = 0x70             ; Write error group mask
.equ NVMCTRL_ERROR_gp = 4                ; Write error group position
.equ NVMCTRL_ERROR_0_bm = (1<<4)         ; Write error bit 0 mask
.equ NVMCTRL_ERROR_0_bp = 4              ; Write error bit 0 position
.equ NVMCTRL_ERROR_1_bm = (1<<5)         ; Write error bit 1 mask
.equ NVMCTRL_ERROR_1_bp = 5              ; Write error bit 1 position
.equ NVMCTRL_ERROR_2_bm = (1<<6)         ; Write error bit 2 mask
.equ NVMCTRL_ERROR_2_bp = 6              ; Write error bit 2 position

; Command select
.equ NVMCTRL_CMD_NOCMD_gc = (0x00<<0)    ; No Command
.equ NVMCTRL_CMD_NOOP_gc = (0x01<<0)     ; No Operation
.equ NVMCTRL_CMD_FLPW_gc = (0x04<<0)     ; Flash Page Write
.equ NVMCTRL_CMD_FLPERW_gc = (0x05<<0)   ; Flash Page Erase and Write
.equ NVMCTRL_CMD_FLPER_gc = (0x08<<0)    ; Flash Page Erase
.equ NVMCTRL_CMD_FLMPER2_gc = (0x09<<0)  ; Flash 2-page erase enable
.equ NVMCTRL_CMD_FLMPER4_gc = (0x0A<<0)  ; Flash 4-page erase enable
.equ NVMCTRL_CMD_FLMPER8_gc = (0x0B<<0)  ; Flash 8-page erase enable
.equ NVMCTRL_CMD_FLMPER16_gc = (0x0C<<0) ; Flash 16-page erase enable
.equ NVMCTRL_CMD_FLMPER32_gc = (0x0D<<0) ; Flash 32-page erase enable
.equ NVMCTRL_CMD_FLPBCLR_gc = (0x0F<<0)  ; Flash Page Buffer Clear
.equ NVMCTRL_CMD_EEPW_gc = (0x14<<0)     ; EEPROM Page Write
.equ NVMCTRL_CMD_EEPERW_gc = (0x15<<0)   ; EEPROM Page Erase and Write
.equ NVMCTRL_CMD_EEPER_gc = (0x17<<0)    ; EEPROM Page Erase
.equ NVMCTRL_CMD_EEPBCLR_gc = (0x1F<<0)  ; EEPROM Page Buffer Clear
.equ NVMCTRL_CMD_CHER_gc = (0x20<<0)     ; Chip Erase Command (UPDI only)
.equ NVMCTRL_CMD_EECHER_gc = (0x30<<0)   ; EEPROM Erase Command (UPDI only)

; Flash Mapping in Data space select
.equ NVMCTRL_FLMAP_SECTION0_gc = (0x00<<4) ; Flash section 0, 0 - 32KB
.equ NVMCTRL_FLMAP_SECTION1_gc = (0x01<<4) ; Flash section 1, 32 - 64KB
.equ NVMCTRL_FLMAP_SECTION2_gc = (0x02<<4) ; Flash section 2, 64 - 96KB
.equ NVMCTRL_FLMAP_SECTION3_gc = (0x03<<4) ; Flash section 3, 96 - 128KB

; Write error select
.equ NVMCTRL_ERROR_NOERROR_gc = (0x00<<4) ; No Error
.equ NVMCTRL_ERROR_WRITEPROTECT_gc = (0x02<<4) ; Attempt to program write protected area
.equ NVMCTRL_ERROR_CMDCOLLISION_gc = (0x03<<4) ; Selecting new write command while write command already seleted
.equ NVMCTRL_ERROR_WRONGSECTION_gc = (0x04<<4) ; Address cannot be written with selected command


;*************************************************************************
;** PORT - I/O Ports
;*************************************************************************

; PORT_INTFLAGS masks
.equ PORT_INT_gm = 0xFF                  ; Pin Interrupt Flag group mask
.equ PORT_INT_gp = 0                     ; Pin Interrupt Flag group position
.equ PORT_INT_0_bm = (1<<0)              ; Pin Interrupt Flag bit 0 mask
.equ PORT_INT_0_bp = 0                   ; Pin Interrupt Flag bit 0 position
.equ PORT_INT_1_bm = (1<<1)              ; Pin Interrupt Flag bit 1 mask
.equ PORT_INT_1_bp = 1                   ; Pin Interrupt Flag bit 1 position
.equ PORT_INT_2_bm = (1<<2)              ; Pin Interrupt Flag bit 2 mask
.equ PORT_INT_2_bp = 2                   ; Pin Interrupt Flag bit 2 position
.equ PORT_INT_3_bm = (1<<3)              ; Pin Interrupt Flag bit 3 mask
.equ PORT_INT_3_bp = 3                   ; Pin Interrupt Flag bit 3 position
.equ PORT_INT_4_bm = (1<<4)              ; Pin Interrupt Flag bit 4 mask
.equ PORT_INT_4_bp = 4                   ; Pin Interrupt Flag bit 4 position
.equ PORT_INT_5_bm = (1<<5)              ; Pin Interrupt Flag bit 5 mask
.equ PORT_INT_5_bp = 5                   ; Pin Interrupt Flag bit 5 position
.equ PORT_INT_6_bm = (1<<6)              ; Pin Interrupt Flag bit 6 mask
.equ PORT_INT_6_bp = 6                   ; Pin Interrupt Flag bit 6 position
.equ PORT_INT_7_bm = (1<<7)              ; Pin Interrupt Flag bit 7 mask
.equ PORT_INT_7_bp = 7                   ; Pin Interrupt Flag bit 7 position

; PORT_PORTCTRL masks
.equ PORT_SRL_bm = 0x01                  ; Slew Rate Limit Enable bit mask
.equ PORT_SRL_bp = 0                     ; Slew Rate Limit Enable bit position

; PORT_PINCONFIG masks
.equ PORT_ISC_gm = 0x07                  ; Input/Sense Configuration group mask
.equ PORT_ISC_gp = 0                     ; Input/Sense Configuration group position
.equ PORT_ISC_0_bm = (1<<0)              ; Input/Sense Configuration bit 0 mask
.equ PORT_ISC_0_bp = 0                   ; Input/Sense Configuration bit 0 position
.equ PORT_ISC_1_bm = (1<<1)              ; Input/Sense Configuration bit 1 mask
.equ PORT_ISC_1_bp = 1                   ; Input/Sense Configuration bit 1 position
.equ PORT_ISC_2_bm = (1<<2)              ; Input/Sense Configuration bit 2 mask
.equ PORT_ISC_2_bp = 2                   ; Input/Sense Configuration bit 2 position
.equ PORT_PULLUPEN_bm = 0x08             ; Pullup enable bit mask
.equ PORT_PULLUPEN_bp = 3                ; Pullup enable bit position
.equ PORT_INLVL_bm = 0x40                ; Input Level Select bit mask
.equ PORT_INLVL_bp = 6                   ; Input Level Select bit position
.equ PORT_INVEN_bm = 0x80                ; Inverted I/O Enable bit mask
.equ PORT_INVEN_bp = 7                   ; Inverted I/O Enable bit position

; PORT_PIN0CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_PIN1CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_PIN2CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_PIN3CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_PIN4CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_PIN5CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_PIN6CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_PIN7CTRL masks
; Masks for PORT_ISC already defined
; Masks for PORT_PULLUPEN already defined
; Masks for PORT_INLVL already defined
; Masks for PORT_INVEN already defined

; PORT_EVGENCTRL masks
.equ PORT_EVGEN0SEL_gm = 0x07            ; Event Generator 0 Select group mask
.equ PORT_EVGEN0SEL_gp = 0               ; Event Generator 0 Select group position
.equ PORT_EVGEN0SEL_0_bm = (1<<0)        ; Event Generator 0 Select bit 0 mask
.equ PORT_EVGEN0SEL_0_bp = 0             ; Event Generator 0 Select bit 0 position
.equ PORT_EVGEN0SEL_1_bm = (1<<1)        ; Event Generator 0 Select bit 1 mask
.equ PORT_EVGEN0SEL_1_bp = 1             ; Event Generator 0 Select bit 1 position
.equ PORT_EVGEN0SEL_2_bm = (1<<2)        ; Event Generator 0 Select bit 2 mask
.equ PORT_EVGEN0SEL_2_bp = 2             ; Event Generator 0 Select bit 2 position
.equ PORT_EVGEN1SEL_gm = 0x70            ; Event Generator 1 Select group mask
.equ PORT_EVGEN1SEL_gp = 4               ; Event Generator 1 Select group position
.equ PORT_EVGEN1SEL_0_bm = (1<<4)        ; Event Generator 1 Select bit 0 mask
.equ PORT_EVGEN1SEL_0_bp = 4             ; Event Generator 1 Select bit 0 position
.equ PORT_EVGEN1SEL_1_bm = (1<<5)        ; Event Generator 1 Select bit 1 mask
.equ PORT_EVGEN1SEL_1_bp = 5             ; Event Generator 1 Select bit 1 position
.equ PORT_EVGEN1SEL_2_bm = (1<<6)        ; Event Generator 1 Select bit 2 mask
.equ PORT_EVGEN1SEL_2_bp = 6             ; Event Generator 1 Select bit 2 position

; Event Generator 0 Select
.equ PORT_EVGEN0SEL_PIN0_gc = (0x00<<0)  ; Pin 0 used as event generator
.equ PORT_EVGEN0SEL_PIN1_gc = (0x01<<0)  ; Pin 1 used as event generator
.equ PORT_EVGEN0SEL_PIN2_gc = (0x02<<0)  ; Pin 2 used as event generator
.equ PORT_EVGEN0SEL_PIN3_gc = (0x03<<0)  ; Pin 3 used as event generator
.equ PORT_EVGEN0SEL_PIN4_gc = (0x04<<0)  ; Pin 4 used as event generator
.equ PORT_EVGEN0SEL_PIN5_gc = (0x05<<0)  ; Pin 5 used as event generator
.equ PORT_EVGEN0SEL_PIN6_gc = (0x06<<0)  ; Pin 6 used as event generator
.equ PORT_EVGEN0SEL_PIN7_gc = (0x07<<0)  ; Pin 7 used as event generator

; Event Generator 1 Select
.equ PORT_EVGEN1SEL_PIN0_gc = (0x00<<4)  ; Pin 0 used as event generator
.equ PORT_EVGEN1SEL_PIN1_gc = (0x01<<4)  ; Pin 1 used as event generator
.equ PORT_EVGEN1SEL_PIN2_gc = (0x02<<4)  ; Pin 2 used as event generator
.equ PORT_EVGEN1SEL_PIN3_gc = (0x03<<4)  ; Pin 3 used as event generator
.equ PORT_EVGEN1SEL_PIN4_gc = (0x04<<4)  ; Pin 4 used as event generator
.equ PORT_EVGEN1SEL_PIN5_gc = (0x05<<4)  ; Pin 5 used as event generator
.equ PORT_EVGEN1SEL_PIN6_gc = (0x06<<4)  ; Pin 6 used as event generator
.equ PORT_EVGEN1SEL_PIN7_gc = (0x07<<4)  ; Pin 7 used as event generator

; Input Level Select
.equ PORT_INLVL_ST_gc = (0x00<<6)        ; Schmitt-Trigger input level
.equ PORT_INLVL_TTL_gc = (0x01<<6)       ; TTL Input level

; Input/Sense Configuration select
.equ PORT_ISC_INTDISABLE_gc = (0x00<<0)  ; Interrupt disabled but input buffer enabled
.equ PORT_ISC_BOTHEDGES_gc = (0x01<<0)   ; Sense Both Edges
.equ PORT_ISC_RISING_gc = (0x02<<0)      ; Sense Rising Edge
.equ PORT_ISC_FALLING_gc = (0x03<<0)     ; Sense Falling Edge
.equ PORT_ISC_INPUT_DISABLE_gc = (0x04<<0) ; Digital Input Buffer disabled
.equ PORT_ISC_LEVEL_gc = (0x05<<0)       ; Sense low Level


;*************************************************************************
;** PORTMUX - Port Multiplexer
;*************************************************************************

; PORTMUX_EVSYSROUTEA masks
.equ PORTMUX_EVOUTA_bm = 0x01            ; Event Output A bit mask
.equ PORTMUX_EVOUTA_bp = 0               ; Event Output A bit position
.equ PORTMUX_EVOUTC_bm = 0x04            ; Event Output C bit mask
.equ PORTMUX_EVOUTC_bp = 2               ; Event Output C bit position
.equ PORTMUX_EVOUTD_bm = 0x08            ; Event Output D bit mask
.equ PORTMUX_EVOUTD_bp = 3               ; Event Output D bit position
.equ PORTMUX_EVOUTF_bm = 0x20            ; Event Output F bit mask
.equ PORTMUX_EVOUTF_bp = 5               ; Event Output F bit position

; PORTMUX_CCLROUTEA masks
.equ PORTMUX_LUT0_bm = 0x01              ; CCL Look-Up Table 0 Signals bit mask
.equ PORTMUX_LUT0_bp = 0                 ; CCL Look-Up Table 0 Signals bit position
.equ PORTMUX_LUT1_bm = 0x02              ; CCL Look-Up Table 1 Signals bit mask
.equ PORTMUX_LUT1_bp = 1                 ; CCL Look-Up Table 1 Signals bit position
.equ PORTMUX_LUT2_bm = 0x04              ; CCL Look-Up Table 2 Signals bit mask
.equ PORTMUX_LUT2_bp = 2                 ; CCL Look-Up Table 2 Signals bit position

; PORTMUX_USARTROUTEA masks
.equ PORTMUX_USART0_gm = 0x07            ; USART0 Routing group mask
.equ PORTMUX_USART0_gp = 0               ; USART0 Routing group position
.equ PORTMUX_USART0_0_bm = (1<<0)        ; USART0 Routing bit 0 mask
.equ PORTMUX_USART0_0_bp = 0             ; USART0 Routing bit 0 position
.equ PORTMUX_USART0_1_bm = (1<<1)        ; USART0 Routing bit 1 mask
.equ PORTMUX_USART0_1_bp = 1             ; USART0 Routing bit 1 position
.equ PORTMUX_USART0_2_bm = (1<<2)        ; USART0 Routing bit 2 mask
.equ PORTMUX_USART0_2_bp = 2             ; USART0 Routing bit 2 position
.equ PORTMUX_USART1_gm = 0x18            ; USART1 Routing group mask
.equ PORTMUX_USART1_gp = 3               ; USART1 Routing group position
.equ PORTMUX_USART1_0_bm = (1<<3)        ; USART1 Routing bit 0 mask
.equ PORTMUX_USART1_0_bp = 3             ; USART1 Routing bit 0 position
.equ PORTMUX_USART1_1_bm = (1<<4)        ; USART1 Routing bit 1 mask
.equ PORTMUX_USART1_1_bp = 4             ; USART1 Routing bit 1 position

; PORTMUX_USARTROUTEB masks
.equ PORTMUX_USART2_gm = 0x30            ; USART2 Routing group mask
.equ PORTMUX_USART2_gp = 4               ; USART2 Routing group position
.equ PORTMUX_USART2_0_bm = (1<<4)        ; USART2 Routing bit 0 mask
.equ PORTMUX_USART2_0_bp = 4             ; USART2 Routing bit 0 position
.equ PORTMUX_USART2_1_bm = (1<<5)        ; USART2 Routing bit 1 mask
.equ PORTMUX_USART2_1_bp = 5             ; USART2 Routing bit 1 position

; PORTMUX_SPIROUTEA masks
.equ PORTMUX_SPI0_gm = 0x07              ; SPI0 Signals group mask
.equ PORTMUX_SPI0_gp = 0                 ; SPI0 Signals group position
.equ PORTMUX_SPI0_0_bm = (1<<0)          ; SPI0 Signals bit 0 mask
.equ PORTMUX_SPI0_0_bp = 0               ; SPI0 Signals bit 0 position
.equ PORTMUX_SPI0_1_bm = (1<<1)          ; SPI0 Signals bit 1 mask
.equ PORTMUX_SPI0_1_bp = 1               ; SPI0 Signals bit 1 position
.equ PORTMUX_SPI0_2_bm = (1<<2)          ; SPI0 Signals bit 2 mask
.equ PORTMUX_SPI0_2_bp = 2               ; SPI0 Signals bit 2 position

; PORTMUX_TWIROUTEA masks
.equ PORTMUX_TWI0_gm = 0x03              ; TWI0 Signals group mask
.equ PORTMUX_TWI0_gp = 0                 ; TWI0 Signals group position
.equ PORTMUX_TWI0_0_bm = (1<<0)          ; TWI0 Signals bit 0 mask
.equ PORTMUX_TWI0_0_bp = 0               ; TWI0 Signals bit 0 position
.equ PORTMUX_TWI0_1_bm = (1<<1)          ; TWI0 Signals bit 1 mask
.equ PORTMUX_TWI0_1_bp = 1               ; TWI0 Signals bit 1 position

; PORTMUX_TCAROUTEA masks
.equ PORTMUX_TCA0_gm = 0x07              ; TCA0 Signals group mask
.equ PORTMUX_TCA0_gp = 0                 ; TCA0 Signals group position
.equ PORTMUX_TCA0_0_bm = (1<<0)          ; TCA0 Signals bit 0 mask
.equ PORTMUX_TCA0_0_bp = 0               ; TCA0 Signals bit 0 position
.equ PORTMUX_TCA0_1_bm = (1<<1)          ; TCA0 Signals bit 1 mask
.equ PORTMUX_TCA0_1_bp = 1               ; TCA0 Signals bit 1 position
.equ PORTMUX_TCA0_2_bm = (1<<2)          ; TCA0 Signals bit 2 mask
.equ PORTMUX_TCA0_2_bp = 2               ; TCA0 Signals bit 2 position
.equ PORTMUX_TCA1_gm = 0x38              ; TCA1 Signals group mask
.equ PORTMUX_TCA1_gp = 3                 ; TCA1 Signals group position
.equ PORTMUX_TCA1_0_bm = (1<<3)          ; TCA1 Signals bit 0 mask
.equ PORTMUX_TCA1_0_bp = 3               ; TCA1 Signals bit 0 position
.equ PORTMUX_TCA1_1_bm = (1<<4)          ; TCA1 Signals bit 1 mask
.equ PORTMUX_TCA1_1_bp = 4               ; TCA1 Signals bit 1 position
.equ PORTMUX_TCA1_2_bm = (1<<5)          ; TCA1 Signals bit 2 mask
.equ PORTMUX_TCA1_2_bp = 5               ; TCA1 Signals bit 2 position

; PORTMUX_TCBROUTEA masks
.equ PORTMUX_TCB0_bm = 0x01              ; TCB0 Output bit mask
.equ PORTMUX_TCB0_bp = 0                 ; TCB0 Output bit position
.equ PORTMUX_TCB1_bm = 0x02              ; TCB1 Output bit mask
.equ PORTMUX_TCB1_bp = 1                 ; TCB1 Output bit position
.equ PORTMUX_TCB2_bm = 0x04              ; TCB2 Output bit mask
.equ PORTMUX_TCB2_bp = 2                 ; TCB2 Output bit position
.equ PORTMUX_TCB3_bm = 0x08              ; TCB3 Output bit mask
.equ PORTMUX_TCB3_bp = 3                 ; TCB3 Output bit position

; PORTMUX_ACROUTEA masks
.equ PORTMUX_AC0_bm = 0x01               ; Analog Comparator 0 Output bit mask
.equ PORTMUX_AC0_bp = 0                  ; Analog Comparator 0 Output bit position
.equ PORTMUX_AC1_bm = 0x02               ; Analog Comparator 1 Output bit mask
.equ PORTMUX_AC1_bp = 1                  ; Analog Comparator 1 Output bit position

; Analog Comparator 0 Output select
.equ PORTMUX_AC0_DEFAULT_gc = (0x00<<0)  ; OUT on PA7
.equ PORTMUX_AC0_ALT1_gc = (0x01<<0)     ; OUT on PC6

; Analog Comparator 1 Output select
.equ PORTMUX_AC1_DEFAULT_gc = (0x00<<1)  ; OUT on PA7
.equ PORTMUX_AC1_ALT1_gc = (0x01<<1)     ; OUT on PC6

; CCL Look-Up Table 0 Signals select
.equ PORTMUX_LUT0_DEFAULT_gc = (0x00<<0) ; Out: PA3 In: PA0, PA1, PA2
.equ PORTMUX_LUT0_ALT1_gc = (0x01<<0)    ; Out: PA6 In: PA0, PA1, PA2

; CCL Look-Up Table 1 Signals select
.equ PORTMUX_LUT1_DEFAULT_gc = (0x00<<1) ; Out: PC3 In: PC0, PC1, PC2
.equ PORTMUX_LUT1_ALT1_gc = (0x01<<1)    ; Out: PC6 In: PC0, PC1, PC2

; CCL Look-Up Table 2 Signals select
.equ PORTMUX_LUT2_DEFAULT_gc = (0x00<<2) ; Out: PD3 In: PD0, PD1, PD2
.equ PORTMUX_LUT2_ALT1_gc = (0x01<<2)    ; Out: PD6 In: PD0, PD1, PD2

; Event Output A select
.equ PORTMUX_EVOUTA_DEFAULT_gc = (0x00<<0) ; EVOUT on PA2
.equ PORTMUX_EVOUTA_ALT1_gc = (0x01<<0)  ; EVOUT on PA7

; Event Output C select
.equ PORTMUX_EVOUTC_DEFAULT_gc = (0x00<<2) ; EVOUT on PC2
.equ PORTMUX_EVOUTC_ALT1_gc = (0x01<<2)  ; EVOUT on PC7

; Event Output D select
.equ PORTMUX_EVOUTD_DEFAULT_gc = (0x00<<3) ; EVOUT on PD2
.equ PORTMUX_EVOUTD_ALT1_gc = (0x01<<3)  ; EVOUT on PD7

; Event Output F select
.equ PORTMUX_EVOUTF_DEFAULT_gc = (0x00<<5) ; EVOUT on PF2
.equ PORTMUX_EVOUTF_ALT1_gc = (0x01<<5)  ; EVOUT on PF7

; SPI0 Signals select
.equ PORTMUX_SPI0_DEFAULT_gc = (0x00<<0) ; PA4, PA5, PA6, PA7
.equ PORTMUX_SPI0_ALT1_gc = (0x01<<0)    ; PE0, PE1, PE2, PE3
.equ PORTMUX_SPI0_NONE_gc = (0x03<<0)    ; Not connected to any pins

; TCA0 Signals select
.equ PORTMUX_TCA0_PORTA_gc = (0x00<<0)   ; PA0, PA1, PA2, PA3, PA4, PA5
.equ PORTMUX_TCA0_PORTB_gc = (0x01<<0)   ; PB0, PB1, PB2, PB3, PB4, PB5
.equ PORTMUX_TCA0_PORTC_gc = (0x02<<0)   ; PC0, PC1, PC2, PC3, PC4, PC5
.equ PORTMUX_TCA0_PORTD_gc = (0x03<<0)   ; PD0, PD1, PD2, PD3, PD4, PD5
.equ PORTMUX_TCA0_PORTE_gc = (0x04<<0)   ; PE0, PE1, PE2, PE3, PE4, PE5
.equ PORTMUX_TCA0_PORTF_gc = (0x05<<0)   ; PF0, PF1, PF2, PF3, PF4, PF5

; TCA1 Signals select
.equ PORTMUX_TCA1_PORTB_gc = (0x00<<3)   ; PB0, PB1, PB2, PB3, PB4, PB5
.equ PORTMUX_TCA1_PORTC_gc = (0x01<<3)   ; PC4, PC5, PC6
.equ PORTMUX_TCA1_PORTA_gc = (0x04<<3)   ; PA4, PA5, PA6
.equ PORTMUX_TCA1_PORTD_gc = (0x05<<3)   ; PD4, PD5, PD6

; TCB0 Output select
.equ PORTMUX_TCB0_DEFAULT_gc = (0x00<<0) ; PA2
.equ PORTMUX_TCB0_ALT1_gc = (0x01<<0)    ; PF4

; TCB1 Output select
.equ PORTMUX_TCB1_DEFAULT_gc = (0x00<<1) ; PA3
.equ PORTMUX_TCB1_ALT1_gc = (0x01<<1)    ; PF5

; TCB2 Output select
.equ PORTMUX_TCB2_DEFAULT_gc = (0x00<<2) ; PC0
.equ PORTMUX_TCB2_ALT1_gc = (0x01<<2)    ; PB4

; TCB3 Output select
.equ PORTMUX_TCB3_DEFAULT_gc = (0x00<<3) ; PB5
.equ PORTMUX_TCB3_ALT1_gc = (0x01<<3)    ; PC1

; TWI0 Signals select
.equ PORTMUX_TWI0_DEFAULT_gc = (0x00<<0) ; {PA2, PA3}, {PC2, PC3}
.equ PORTMUX_TWI0_ALT1_gc = (0x01<<0)    ; {PA2, PA3}, {PC6, PC7}
.equ PORTMUX_TWI0_ALT2_gc = (0x02<<0)    ; {PC2, PC3}, {PC6, PC7}
.equ PORTMUX_TWI0_ALT3_gc = (0x03<<0)    ; {PA0, PA1}, {PC2, PC3}

; USART0 Routing select
.equ PORTMUX_USART0_DEFAULT_gc = (0x00<<0) ; {PA0, PA1, PA2, PA3}
.equ PORTMUX_USART0_ALT1_gc = (0x01<<0)  ; {PA4, PA5, PA6, PA7}
.equ PORTMUX_USART0_ALT2_gc = (0x02<<0)  ; {PA2,PA3, -, -}
.equ PORTMUX_USART0_ALT3_gc = (0x03<<0)  ; {PD4, PD5, PD6, PD7}
.equ PORTMUX_USART0_ALT4_gc = (0x04<<0)  ; {PC1, PC2, PC3, -}
.equ PORTMUX_USART0_NONE_gc = (0x05<<0)  ; Not connected to any pins

; USART1 Routing select
.equ PORTMUX_USART1_DEFAULT_gc = (0x00<<3) ; {PC0, PC1, PC2, PC3}
.equ PORTMUX_USART1_ALT1_gc = (0x01<<3)  ; {PC4, PC5, PC6, PC7}
.equ PORTMUX_USART1_ALT2_gc = (0x02<<3)  ; {PD6, PD7, -, -}
.equ PORTMUX_USART1_NONE_gc = (0x03<<3)  ; Not connected to any pins

; USART2 Routing select
.equ PORTMUX_USART2_DEFAULT_gc = (0x00<<4) ; {PF0, PF1, PF2, PF3}
.equ PORTMUX_USART2_ALT1_gc = (0x01<<4)  ; {PF4, PF5,- , -}
.equ PORTMUX_USART2_NONE_gc = (0x03<<4)  ; Not connected to any pins


;*************************************************************************
;** RSTCTRL - Reset controller
;*************************************************************************

; RSTCTRL_RSTFR masks
.equ RSTCTRL_PORF_bm = 0x01              ; Power on Reset flag bit mask
.equ RSTCTRL_PORF_bp = 0                 ; Power on Reset flag bit position
.equ RSTCTRL_BORF_bm = 0x02              ; Brown out detector Reset flag bit mask
.equ RSTCTRL_BORF_bp = 1                 ; Brown out detector Reset flag bit position
.equ RSTCTRL_EXTRF_bm = 0x04             ; External Reset flag bit mask
.equ RSTCTRL_EXTRF_bp = 2                ; External Reset flag bit position
.equ RSTCTRL_WDRF_bm = 0x08              ; Watch dog Reset flag bit mask
.equ RSTCTRL_WDRF_bp = 3                 ; Watch dog Reset flag bit position
.equ RSTCTRL_SWRF_bm = 0x10              ; Software Reset flag bit mask
.equ RSTCTRL_SWRF_bp = 4                 ; Software Reset flag bit position
.equ RSTCTRL_UPDIRF_bm = 0x20            ; UPDI Reset flag bit mask
.equ RSTCTRL_UPDIRF_bp = 5               ; UPDI Reset flag bit position

; RSTCTRL_SWRR masks
.equ RSTCTRL_SWRE_bm = 0x01              ; Software Reset Enable bit mask
.equ RSTCTRL_SWRE_bp = 0                 ; Software Reset Enable bit position


;*************************************************************************
;** RTC - Real-Time Counter
;*************************************************************************

; RTC_CTRLA masks
.equ RTC_RTCEN_bm = 0x01                 ; Enable bit mask
.equ RTC_RTCEN_bp = 0                    ; Enable bit position
.equ RTC_CORREN_bm = 0x04                ; Correction enable bit mask
.equ RTC_CORREN_bp = 2                   ; Correction enable bit position
.equ RTC_PRESCALER_gm = 0x78             ; Prescaling Factor group mask
.equ RTC_PRESCALER_gp = 3                ; Prescaling Factor group position
.equ RTC_PRESCALER_0_bm = (1<<3)         ; Prescaling Factor bit 0 mask
.equ RTC_PRESCALER_0_bp = 3              ; Prescaling Factor bit 0 position
.equ RTC_PRESCALER_1_bm = (1<<4)         ; Prescaling Factor bit 1 mask
.equ RTC_PRESCALER_1_bp = 4              ; Prescaling Factor bit 1 position
.equ RTC_PRESCALER_2_bm = (1<<5)         ; Prescaling Factor bit 2 mask
.equ RTC_PRESCALER_2_bp = 5              ; Prescaling Factor bit 2 position
.equ RTC_PRESCALER_3_bm = (1<<6)         ; Prescaling Factor bit 3 mask
.equ RTC_PRESCALER_3_bp = 6              ; Prescaling Factor bit 3 position
.equ RTC_RUNSTDBY_bm = 0x80              ; Run In Standby bit mask
.equ RTC_RUNSTDBY_bp = 7                 ; Run In Standby bit position

; RTC_STATUS masks
.equ RTC_CTRLABUSY_bm = 0x01             ; CTRLA Synchronization Busy Flag bit mask
.equ RTC_CTRLABUSY_bp = 0                ; CTRLA Synchronization Busy Flag bit position
.equ RTC_CNTBUSY_bm = 0x02               ; Count Synchronization Busy Flag bit mask
.equ RTC_CNTBUSY_bp = 1                  ; Count Synchronization Busy Flag bit position
.equ RTC_PERBUSY_bm = 0x04               ; Period Synchronization Busy Flag bit mask
.equ RTC_PERBUSY_bp = 2                  ; Period Synchronization Busy Flag bit position
.equ RTC_CMPBUSY_bm = 0x08               ; Comparator Synchronization Busy Flag bit mask
.equ RTC_CMPBUSY_bp = 3                  ; Comparator Synchronization Busy Flag bit position

; RTC_INTCTRL masks
.equ RTC_OVF_bm = 0x01                   ; Overflow Interrupt enable bit mask
.equ RTC_OVF_bp = 0                      ; Overflow Interrupt enable bit position
.equ RTC_CMP_bm = 0x02                   ; Compare Match Interrupt enable bit mask
.equ RTC_CMP_bp = 1                      ; Compare Match Interrupt enable bit position

; RTC_INTFLAGS masks
; Masks for RTC_OVF already defined
; Masks for RTC_CMP already defined

; RTC_DBGCTRL masks
.equ RTC_DBGRUN_bm = 0x01                ; Run in debug bit mask
.equ RTC_DBGRUN_bp = 0                   ; Run in debug bit position

; RTC_CALIB masks
.equ RTC_ERROR_gm = 0x7F                 ; Error Correction Value group mask
.equ RTC_ERROR_gp = 0                    ; Error Correction Value group position
.equ RTC_ERROR_0_bm = (1<<0)             ; Error Correction Value bit 0 mask
.equ RTC_ERROR_0_bp = 0                  ; Error Correction Value bit 0 position
.equ RTC_ERROR_1_bm = (1<<1)             ; Error Correction Value bit 1 mask
.equ RTC_ERROR_1_bp = 1                  ; Error Correction Value bit 1 position
.equ RTC_ERROR_2_bm = (1<<2)             ; Error Correction Value bit 2 mask
.equ RTC_ERROR_2_bp = 2                  ; Error Correction Value bit 2 position
.equ RTC_ERROR_3_bm = (1<<3)             ; Error Correction Value bit 3 mask
.equ RTC_ERROR_3_bp = 3                  ; Error Correction Value bit 3 position
.equ RTC_ERROR_4_bm = (1<<4)             ; Error Correction Value bit 4 mask
.equ RTC_ERROR_4_bp = 4                  ; Error Correction Value bit 4 position
.equ RTC_ERROR_5_bm = (1<<5)             ; Error Correction Value bit 5 mask
.equ RTC_ERROR_5_bp = 5                  ; Error Correction Value bit 5 position
.equ RTC_ERROR_6_bm = (1<<6)             ; Error Correction Value bit 6 mask
.equ RTC_ERROR_6_bp = 6                  ; Error Correction Value bit 6 position
.equ RTC_SIGN_bm = 0x80                  ; Error Correction Sign Bit bit mask
.equ RTC_SIGN_bp = 7                     ; Error Correction Sign Bit bit position

; RTC_CLKSEL masks
.equ RTC_CLKSEL_gm = 0x03                ; Clock Select group mask
.equ RTC_CLKSEL_gp = 0                   ; Clock Select group position
.equ RTC_CLKSEL_0_bm = (1<<0)            ; Clock Select bit 0 mask
.equ RTC_CLKSEL_0_bp = 0                 ; Clock Select bit 0 position
.equ RTC_CLKSEL_1_bm = (1<<1)            ; Clock Select bit 1 mask
.equ RTC_CLKSEL_1_bp = 1                 ; Clock Select bit 1 position

; RTC_PITCTRLA masks
.equ RTC_PITEN_bm = 0x01                 ; Enable bit mask
.equ RTC_PITEN_bp = 0                    ; Enable bit position
.equ RTC_PERIOD_gm = 0x78                ; Period group mask
.equ RTC_PERIOD_gp = 3                   ; Period group position
.equ RTC_PERIOD_0_bm = (1<<3)            ; Period bit 0 mask
.equ RTC_PERIOD_0_bp = 3                 ; Period bit 0 position
.equ RTC_PERIOD_1_bm = (1<<4)            ; Period bit 1 mask
.equ RTC_PERIOD_1_bp = 4                 ; Period bit 1 position
.equ RTC_PERIOD_2_bm = (1<<5)            ; Period bit 2 mask
.equ RTC_PERIOD_2_bp = 5                 ; Period bit 2 position
.equ RTC_PERIOD_3_bm = (1<<6)            ; Period bit 3 mask
.equ RTC_PERIOD_3_bp = 6                 ; Period bit 3 position

; RTC_PITSTATUS masks
.equ RTC_CTRLBUSY_bm = 0x01              ; CTRLA Synchronization Busy Flag bit mask
.equ RTC_CTRLBUSY_bp = 0                 ; CTRLA Synchronization Busy Flag bit position

; RTC_PITINTCTRL masks
.equ RTC_PI_bm = 0x01                    ; Periodic Interrupt bit mask
.equ RTC_PI_bp = 0                       ; Periodic Interrupt bit position

; RTC_PITINTFLAGS masks
; Masks for RTC_PI already defined

; RTC_PITDBGCTRL masks
; Masks for RTC_DBGRUN already defined

; RTC_PITEVGENCTRLA masks
.equ RTC_EVGEN0SEL_gm = 0x0F             ; Event Generation 0 Select group mask
.equ RTC_EVGEN0SEL_gp = 0                ; Event Generation 0 Select group position
.equ RTC_EVGEN0SEL_0_bm = (1<<0)         ; Event Generation 0 Select bit 0 mask
.equ RTC_EVGEN0SEL_0_bp = 0              ; Event Generation 0 Select bit 0 position
.equ RTC_EVGEN0SEL_1_bm = (1<<1)         ; Event Generation 0 Select bit 1 mask
.equ RTC_EVGEN0SEL_1_bp = 1              ; Event Generation 0 Select bit 1 position
.equ RTC_EVGEN0SEL_2_bm = (1<<2)         ; Event Generation 0 Select bit 2 mask
.equ RTC_EVGEN0SEL_2_bp = 2              ; Event Generation 0 Select bit 2 position
.equ RTC_EVGEN0SEL_3_bm = (1<<3)         ; Event Generation 0 Select bit 3 mask
.equ RTC_EVGEN0SEL_3_bp = 3              ; Event Generation 0 Select bit 3 position
.equ RTC_EVGEN1SEL_gm = 0xF0             ; Event Generation 1 Select group mask
.equ RTC_EVGEN1SEL_gp = 4                ; Event Generation 1 Select group position
.equ RTC_EVGEN1SEL_0_bm = (1<<4)         ; Event Generation 1 Select bit 0 mask
.equ RTC_EVGEN1SEL_0_bp = 4              ; Event Generation 1 Select bit 0 position
.equ RTC_EVGEN1SEL_1_bm = (1<<5)         ; Event Generation 1 Select bit 1 mask
.equ RTC_EVGEN1SEL_1_bp = 5              ; Event Generation 1 Select bit 1 position
.equ RTC_EVGEN1SEL_2_bm = (1<<6)         ; Event Generation 1 Select bit 2 mask
.equ RTC_EVGEN1SEL_2_bp = 6              ; Event Generation 1 Select bit 2 position
.equ RTC_EVGEN1SEL_3_bm = (1<<7)         ; Event Generation 1 Select bit 3 mask
.equ RTC_EVGEN1SEL_3_bp = 7              ; Event Generation 1 Select bit 3 position

; Clock Select
.equ RTC_CLKSEL_OSC32K_gc = (0x00<<0)    ; Internal 32.768 kHz Oscillator
.equ RTC_CLKSEL_OSC1K_gc = (0x01<<0)     ; Internal 32.768 kHz Oscillator Divided by 32
.equ RTC_CLKSEL_XOSC32K_gc = (0x02<<0)   ; 32.768 kHz Crystal Oscillator
.equ RTC_CLKSEL_EXTCLK_gc = (0x03<<0)    ; External Clock

; Prescaling Factor select
.equ RTC_PRESCALER_DIV1_gc = (0x00<<3)   ; RTC Clock / 1
.equ RTC_PRESCALER_DIV2_gc = (0x01<<3)   ; RTC Clock / 2
.equ RTC_PRESCALER_DIV4_gc = (0x02<<3)   ; RTC Clock / 4
.equ RTC_PRESCALER_DIV8_gc = (0x03<<3)   ; RTC Clock / 8
.equ RTC_PRESCALER_DIV16_gc = (0x04<<3)  ; RTC Clock / 16
.equ RTC_PRESCALER_DIV32_gc = (0x05<<3)  ; RTC Clock / 32
.equ RTC_PRESCALER_DIV64_gc = (0x06<<3)  ; RTC Clock / 64
.equ RTC_PRESCALER_DIV128_gc = (0x07<<3) ; RTC Clock / 128
.equ RTC_PRESCALER_DIV256_gc = (0x08<<3) ; RTC Clock / 256
.equ RTC_PRESCALER_DIV512_gc = (0x09<<3) ; RTC Clock / 512
.equ RTC_PRESCALER_DIV1024_gc = (0x0A<<3) ; RTC Clock / 1024
.equ RTC_PRESCALER_DIV2048_gc = (0x0B<<3) ; RTC Clock / 2048
.equ RTC_PRESCALER_DIV4096_gc = (0x0C<<3) ; RTC Clock / 4096
.equ RTC_PRESCALER_DIV8192_gc = (0x0D<<3) ; RTC Clock / 8192
.equ RTC_PRESCALER_DIV16384_gc = (0x0E<<3) ; RTC Clock / 16384
.equ RTC_PRESCALER_DIV32768_gc = (0x0F<<3) ; RTC Clock / 32768

; Period select
.equ RTC_PERIOD_OFF_gc = (0x00<<3)       ; Off
.equ RTC_PERIOD_CYC4_gc = (0x01<<3)      ; RTC Clock Cycles 4
.equ RTC_PERIOD_CYC8_gc = (0x02<<3)      ; RTC Clock Cycles 8
.equ RTC_PERIOD_CYC16_gc = (0x03<<3)     ; RTC Clock Cycles 16
.equ RTC_PERIOD_CYC32_gc = (0x04<<3)     ; RTC Clock Cycles 32
.equ RTC_PERIOD_CYC64_gc = (0x05<<3)     ; RTC Clock Cycles 64
.equ RTC_PERIOD_CYC128_gc = (0x06<<3)    ; RTC Clock Cycles 128
.equ RTC_PERIOD_CYC256_gc = (0x07<<3)    ; RTC Clock Cycles 256
.equ RTC_PERIOD_CYC512_gc = (0x08<<3)    ; RTC Clock Cycles 512
.equ RTC_PERIOD_CYC1024_gc = (0x09<<3)   ; RTC Clock Cycles 1024
.equ RTC_PERIOD_CYC2048_gc = (0x0A<<3)   ; RTC Clock Cycles 2048
.equ RTC_PERIOD_CYC4096_gc = (0x0B<<3)   ; RTC Clock Cycles 4096
.equ RTC_PERIOD_CYC8192_gc = (0x0C<<3)   ; RTC Clock Cycles 8192
.equ RTC_PERIOD_CYC16384_gc = (0x0D<<3)  ; RTC Clock Cycles 16384
.equ RTC_PERIOD_CYC32768_gc = (0x0E<<3)  ; RTC Clock Cycles 32768

; Event Generation 0 Select
.equ RTC_EVGEN0SEL_OFF_gc = (0x00<<0)    ; No Event Generated
.equ RTC_EVGEN0SEL_DIV4_gc = (0x01<<0)   ; CLK_RTC divided by 4
.equ RTC_EVGEN0SEL_DIV8_gc = (0x02<<0)   ; CLK_RTC divided by 8
.equ RTC_EVGEN0SEL_DIV16_gc = (0x03<<0)  ; CLK_RTC divided by 16
.equ RTC_EVGEN0SEL_DIV32_gc = (0x04<<0)  ; CLK_RTC divided by 32
.equ RTC_EVGEN0SEL_DIV64_gc = (0x05<<0)  ; CLK_RTC divided by 64
.equ RTC_EVGEN0SEL_DIV128_gc = (0x06<<0) ; CLK_RTC divided by 128
.equ RTC_EVGEN0SEL_DIV256_gc = (0x07<<0) ; CLK_RTC divided by 256
.equ RTC_EVGEN0SEL_DIV512_gc = (0x08<<0) ; CLK_RTC divided by 512
.equ RTC_EVGEN0SEL_DIV1024_gc = (0x09<<0) ; CLK_RTC divided by 1024
.equ RTC_EVGEN0SEL_DIV2048_gc = (0x0A<<0) ; CLK_RTC divided by 2048
.equ RTC_EVGEN0SEL_DIV4096_gc = (0x0B<<0) ; CLK_RTC divided by 4096
.equ RTC_EVGEN0SEL_DIV8192_gc = (0x0C<<0) ; CLK_RTC divided by 8192
.equ RTC_EVGEN0SEL_DIV16384_gc = (0x0D<<0) ; CLK_RTC divided by 16384
.equ RTC_EVGEN0SEL_DIV32768_gc = (0x0E<<0) ; CLK_RTC divided by 32768

; Event Generation 1 Select
.equ RTC_EVGEN1SEL_OFF_gc = (0x00<<4)    ; No Event Generated
.equ RTC_EVGEN1SEL_DIV4_gc = (0x01<<4)   ; CLK_RTC divided by 4
.equ RTC_EVGEN1SEL_DIV8_gc = (0x02<<4)   ; CLK_RTC divided by 8
.equ RTC_EVGEN1SEL_DIV16_gc = (0x03<<4)  ; CLK_RTC divided by 16
.equ RTC_EVGEN1SEL_DIV32_gc = (0x04<<4)  ; CLK_RTC divided by 32
.equ RTC_EVGEN1SEL_DIV64_gc = (0x05<<4)  ; CLK_RTC divided by 64
.equ RTC_EVGEN1SEL_DIV128_gc = (0x06<<4) ; CLK_RTC divided by 128
.equ RTC_EVGEN1SEL_DIV256_gc = (0x07<<4) ; CLK_RTC divided by 256
.equ RTC_EVGEN1SEL_DIV512_gc = (0x08<<4) ; CLK_RTC divided by 512
.equ RTC_EVGEN1SEL_DIV1024_gc = (0x09<<4) ; CLK_RTC divided by 1024
.equ RTC_EVGEN1SEL_DIV2048_gc = (0x0A<<4) ; CLK_RTC divided by 2048
.equ RTC_EVGEN1SEL_DIV4096_gc = (0x0B<<4) ; CLK_RTC divided by 4096
.equ RTC_EVGEN1SEL_DIV8192_gc = (0x0C<<4) ; CLK_RTC divided by 8192
.equ RTC_EVGEN1SEL_DIV16384_gc = (0x0D<<4) ; CLK_RTC divided by 16384
.equ RTC_EVGEN1SEL_DIV32768_gc = (0x0E<<4) ; CLK_RTC divided by 32768


;*************************************************************************
;** SIGROW - Signature row
;*************************************************************************


;*************************************************************************
;** SLPCTRL - Sleep Controller
;*************************************************************************

; SLPCTRL_CTRLA masks
.equ SLPCTRL_SEN_bm = 0x01               ; Sleep enable bit mask
.equ SLPCTRL_SEN_bp = 0                  ; Sleep enable bit position
.equ SLPCTRL_SMODE_gm = 0x06             ; Sleep mode group mask
.equ SLPCTRL_SMODE_gp = 1                ; Sleep mode group position
.equ SLPCTRL_SMODE_0_bm = (1<<1)         ; Sleep mode bit 0 mask
.equ SLPCTRL_SMODE_0_bp = 1              ; Sleep mode bit 0 position
.equ SLPCTRL_SMODE_1_bm = (1<<2)         ; Sleep mode bit 1 mask
.equ SLPCTRL_SMODE_1_bp = 2              ; Sleep mode bit 1 position

; Sleep mode select
.equ SLPCTRL_SMODE_IDLE_gc = (0x00<<1)   ; Idle mode
.equ SLPCTRL_SMODE_STDBY_gc = (0x01<<1)  ; Standby Mode
.equ SLPCTRL_SMODE_PDOWN_gc = (0x02<<1)  ; Power-down Mode


;*************************************************************************
;** SPI - Serial Peripheral Interface
;*************************************************************************

; SPI_CTRLA masks
.equ SPI_ENABLE_bm = 0x01                ; Enable Module bit mask
.equ SPI_ENABLE_bp = 0                   ; Enable Module bit position
.equ SPI_PRESC_gm = 0x06                 ; Prescaler group mask
.equ SPI_PRESC_gp = 1                    ; Prescaler group position
.equ SPI_PRESC_0_bm = (1<<1)             ; Prescaler bit 0 mask
.equ SPI_PRESC_0_bp = 1                  ; Prescaler bit 0 position
.equ SPI_PRESC_1_bm = (1<<2)             ; Prescaler bit 1 mask
.equ SPI_PRESC_1_bp = 2                  ; Prescaler bit 1 position
.equ SPI_CLK2X_bm = 0x10                 ; Enable Double Speed bit mask
.equ SPI_CLK2X_bp = 4                    ; Enable Double Speed bit position
.equ SPI_MASTER_bm = 0x20                ; Host Operation Enable bit mask
.equ SPI_MASTER_bp = 5                   ; Host Operation Enable bit position
.equ SPI_DORD_bm = 0x40                  ; Data Order Setting bit mask
.equ SPI_DORD_bp = 6                     ; Data Order Setting bit position

; SPI_CTRLB masks
.equ SPI_MODE_gm = 0x03                  ; SPI Mode group mask
.equ SPI_MODE_gp = 0                     ; SPI Mode group position
.equ SPI_MODE_0_bm = (1<<0)              ; SPI Mode bit 0 mask
.equ SPI_MODE_0_bp = 0                   ; SPI Mode bit 0 position
.equ SPI_MODE_1_bm = (1<<1)              ; SPI Mode bit 1 mask
.equ SPI_MODE_1_bp = 1                   ; SPI Mode bit 1 position
.equ SPI_SSD_bm = 0x04                   ; SPI Select Disable bit mask
.equ SPI_SSD_bp = 2                      ; SPI Select Disable bit position
.equ SPI_BUFWR_bm = 0x40                 ; Buffer Mode Wait for Receive bit mask
.equ SPI_BUFWR_bp = 6                    ; Buffer Mode Wait for Receive bit position
.equ SPI_BUFEN_bm = 0x80                 ; Buffer Mode Enable bit mask
.equ SPI_BUFEN_bp = 7                    ; Buffer Mode Enable bit position

; SPI_INTCTRL masks
.equ SPI_IE_bm = 0x01                    ; Interrupt Enable bit mask
.equ SPI_IE_bp = 0                       ; Interrupt Enable bit position
.equ SPI_SSIE_bm = 0x10                  ; SPI Select Trigger Interrupt Enable bit mask
.equ SPI_SSIE_bp = 4                     ; SPI Select Trigger Interrupt Enable bit position
.equ SPI_DREIE_bm = 0x20                 ; Data Register Empty Interrupt Enable bit mask
.equ SPI_DREIE_bp = 5                    ; Data Register Empty Interrupt Enable bit position
.equ SPI_TXCIE_bm = 0x40                 ; Transfer Complete Interrupt Enable bit mask
.equ SPI_TXCIE_bp = 6                    ; Transfer Complete Interrupt Enable bit position
.equ SPI_RXCIE_bm = 0x80                 ; Receive Complete Interrupt Enable bit mask
.equ SPI_RXCIE_bp = 7                    ; Receive Complete Interrupt Enable bit position

; SPI_INTFLAGS masks
.equ SPI_BUFOVF_bm = 0x01                ; Buffer Overflow bit mask
.equ SPI_BUFOVF_bp = 0                   ; Buffer Overflow bit position
.equ SPI_SSIF_bm = 0x10                  ; SPI Select Trigger Interrupt Flag bit mask
.equ SPI_SSIF_bp = 4                     ; SPI Select Trigger Interrupt Flag bit position
.equ SPI_DREIF_bm = 0x20                 ; Data Register Empty Interrupt Flag bit mask
.equ SPI_DREIF_bp = 5                    ; Data Register Empty Interrupt Flag bit position
.equ SPI_TXCIF_bm = 0x40                 ; Transfer Complete Interrupt Flag bit mask
.equ SPI_TXCIF_bp = 6                    ; Transfer Complete Interrupt Flag bit position
.equ SPI_WRCOL_bm = 0x40                 ; Write Collision bit mask
.equ SPI_WRCOL_bp = 6                    ; Write Collision bit position
.equ SPI_RXCIF_bm = 0x80                 ; Receive Complete Interrupt Flag bit mask
.equ SPI_RXCIF_bp = 7                    ; Receive Complete Interrupt Flag bit position
.equ SPI_IF_bm = 0x80                    ; Interrupt Flag bit mask
.equ SPI_IF_bp = 7                       ; Interrupt Flag bit position

; Prescaler select
.equ SPI_PRESC_DIV4_gc = (0x00<<1)       ; CLK_PER / 4
.equ SPI_PRESC_DIV16_gc = (0x01<<1)      ; CLK_PER / 16
.equ SPI_PRESC_DIV64_gc = (0x02<<1)      ; CLK_PER / 64
.equ SPI_PRESC_DIV128_gc = (0x03<<1)     ; CLK_PER / 128

; SPI Mode select
.equ SPI_MODE_0_gc = (0x00<<0)           ; SPI Mode 0
.equ SPI_MODE_1_gc = (0x01<<0)           ; SPI Mode 1
.equ SPI_MODE_2_gc = (0x02<<0)           ; SPI Mode 2
.equ SPI_MODE_3_gc = (0x03<<0)           ; SPI Mode 3


;*************************************************************************
;** SYSCFG - System Configuration Registers
;*************************************************************************

; SYSCFG_REVID masks
.equ SYSCFG_MINOR_gm = 0x0F              ; Minor Revision group mask
.equ SYSCFG_MINOR_gp = 0                 ; Minor Revision group position
.equ SYSCFG_MINOR_0_bm = (1<<0)          ; Minor Revision bit 0 mask
.equ SYSCFG_MINOR_0_bp = 0               ; Minor Revision bit 0 position
.equ SYSCFG_MINOR_1_bm = (1<<1)          ; Minor Revision bit 1 mask
.equ SYSCFG_MINOR_1_bp = 1               ; Minor Revision bit 1 position
.equ SYSCFG_MINOR_2_bm = (1<<2)          ; Minor Revision bit 2 mask
.equ SYSCFG_MINOR_2_bp = 2               ; Minor Revision bit 2 position
.equ SYSCFG_MINOR_3_bm = (1<<3)          ; Minor Revision bit 3 mask
.equ SYSCFG_MINOR_3_bp = 3               ; Minor Revision bit 3 position
.equ SYSCFG_MAJOR_gm = 0xF0              ; Major Revision group mask
.equ SYSCFG_MAJOR_gp = 4                 ; Major Revision group position
.equ SYSCFG_MAJOR_0_bm = (1<<4)          ; Major Revision bit 0 mask
.equ SYSCFG_MAJOR_0_bp = 4               ; Major Revision bit 0 position
.equ SYSCFG_MAJOR_1_bm = (1<<5)          ; Major Revision bit 1 mask
.equ SYSCFG_MAJOR_1_bp = 5               ; Major Revision bit 1 position
.equ SYSCFG_MAJOR_2_bm = (1<<6)          ; Major Revision bit 2 mask
.equ SYSCFG_MAJOR_2_bp = 6               ; Major Revision bit 2 position
.equ SYSCFG_MAJOR_3_bm = (1<<7)          ; Major Revision bit 3 mask
.equ SYSCFG_MAJOR_3_bp = 7               ; Major Revision bit 3 position

; SYSCFG_OCDMCTRL masks
.equ SYSCFG_OCDM_gm = 0xFF               ; OCD Message group mask
.equ SYSCFG_OCDM_gp = 0                  ; OCD Message group position
.equ SYSCFG_OCDM_0_bm = (1<<0)           ; OCD Message bit 0 mask
.equ SYSCFG_OCDM_0_bp = 0                ; OCD Message bit 0 position
.equ SYSCFG_OCDM_1_bm = (1<<1)           ; OCD Message bit 1 mask
.equ SYSCFG_OCDM_1_bp = 1                ; OCD Message bit 1 position
.equ SYSCFG_OCDM_2_bm = (1<<2)           ; OCD Message bit 2 mask
.equ SYSCFG_OCDM_2_bp = 2                ; OCD Message bit 2 position
.equ SYSCFG_OCDM_3_bm = (1<<3)           ; OCD Message bit 3 mask
.equ SYSCFG_OCDM_3_bp = 3                ; OCD Message bit 3 position
.equ SYSCFG_OCDM_4_bm = (1<<4)           ; OCD Message bit 4 mask
.equ SYSCFG_OCDM_4_bp = 4                ; OCD Message bit 4 position
.equ SYSCFG_OCDM_5_bm = (1<<5)           ; OCD Message bit 5 mask
.equ SYSCFG_OCDM_5_bp = 5                ; OCD Message bit 5 position
.equ SYSCFG_OCDM_6_bm = (1<<6)           ; OCD Message bit 6 mask
.equ SYSCFG_OCDM_6_bp = 6                ; OCD Message bit 6 position
.equ SYSCFG_OCDM_7_bm = (1<<7)           ; OCD Message bit 7 mask
.equ SYSCFG_OCDM_7_bp = 7                ; OCD Message bit 7 position

; SYSCFG_OCDMSTATUS masks
.equ SYSCFG_VALID_bm = 0x01              ; OCD Message Valid bit mask
.equ SYSCFG_VALID_bp = 0                 ; OCD Message Valid bit position


;*************************************************************************
;** TCA - 16-bit Timer/Counter Type A
;*************************************************************************

; TCA_SINGLE_CTRLA masks
.equ TCA_SINGLE_ENABLE_bm = 0x01         ; Module Enable bit mask
.equ TCA_SINGLE_ENABLE_bp = 0            ; Module Enable bit position
.equ TCA_SINGLE_CLKSEL_gm = 0x0E         ; Clock Selection group mask
.equ TCA_SINGLE_CLKSEL_gp = 1            ; Clock Selection group position
.equ TCA_SINGLE_CLKSEL_0_bm = (1<<1)     ; Clock Selection bit 0 mask
.equ TCA_SINGLE_CLKSEL_0_bp = 1          ; Clock Selection bit 0 position
.equ TCA_SINGLE_CLKSEL_1_bm = (1<<2)     ; Clock Selection bit 1 mask
.equ TCA_SINGLE_CLKSEL_1_bp = 2          ; Clock Selection bit 1 position
.equ TCA_SINGLE_CLKSEL_2_bm = (1<<3)     ; Clock Selection bit 2 mask
.equ TCA_SINGLE_CLKSEL_2_bp = 3          ; Clock Selection bit 2 position
.equ TCA_SINGLE_RUNSTDBY_bm = 0x80       ; Run in Standby bit mask
.equ TCA_SINGLE_RUNSTDBY_bp = 7          ; Run in Standby bit position

; TCA_SINGLE_CTRLB masks
.equ TCA_SINGLE_WGMODE_gm = 0x07         ; Waveform generation mode group mask
.equ TCA_SINGLE_WGMODE_gp = 0            ; Waveform generation mode group position
.equ TCA_SINGLE_WGMODE_0_bm = (1<<0)     ; Waveform generation mode bit 0 mask
.equ TCA_SINGLE_WGMODE_0_bp = 0          ; Waveform generation mode bit 0 position
.equ TCA_SINGLE_WGMODE_1_bm = (1<<1)     ; Waveform generation mode bit 1 mask
.equ TCA_SINGLE_WGMODE_1_bp = 1          ; Waveform generation mode bit 1 position
.equ TCA_SINGLE_WGMODE_2_bm = (1<<2)     ; Waveform generation mode bit 2 mask
.equ TCA_SINGLE_WGMODE_2_bp = 2          ; Waveform generation mode bit 2 position
.equ TCA_SINGLE_ALUPD_bm = 0x08          ; Auto Lock Update bit mask
.equ TCA_SINGLE_ALUPD_bp = 3             ; Auto Lock Update bit position
.equ TCA_SINGLE_CMP0EN_bm = 0x10         ; Compare 0 Enable bit mask
.equ TCA_SINGLE_CMP0EN_bp = 4            ; Compare 0 Enable bit position
.equ TCA_SINGLE_CMP1EN_bm = 0x20         ; Compare 1 Enable bit mask
.equ TCA_SINGLE_CMP1EN_bp = 5            ; Compare 1 Enable bit position
.equ TCA_SINGLE_CMP2EN_bm = 0x40         ; Compare 2 Enable bit mask
.equ TCA_SINGLE_CMP2EN_bp = 6            ; Compare 2 Enable bit position

; TCA_SINGLE_CTRLC masks
.equ TCA_SINGLE_CMP0OV_bm = 0x01         ; Compare 0 Waveform Output Value bit mask
.equ TCA_SINGLE_CMP0OV_bp = 0            ; Compare 0 Waveform Output Value bit position
.equ TCA_SINGLE_CMP1OV_bm = 0x02         ; Compare 1 Waveform Output Value bit mask
.equ TCA_SINGLE_CMP1OV_bp = 1            ; Compare 1 Waveform Output Value bit position
.equ TCA_SINGLE_CMP2OV_bm = 0x04         ; Compare 2 Waveform Output Value bit mask
.equ TCA_SINGLE_CMP2OV_bp = 2            ; Compare 2 Waveform Output Value bit position

; TCA_SINGLE_CTRLD masks
.equ TCA_SINGLE_SPLITM_bm = 0x01         ; Split Mode Enable bit mask
.equ TCA_SINGLE_SPLITM_bp = 0            ; Split Mode Enable bit position

; TCA_SINGLE_CTRLECLR masks
.equ TCA_SINGLE_DIR_bm = 0x01            ; Direction bit mask
.equ TCA_SINGLE_DIR_bp = 0               ; Direction bit position
.equ TCA_SINGLE_LUPD_bm = 0x02           ; Lock Update bit mask
.equ TCA_SINGLE_LUPD_bp = 1              ; Lock Update bit position
.equ TCA_SINGLE_CMD_gm = 0x0C            ; Command group mask
.equ TCA_SINGLE_CMD_gp = 2               ; Command group position
.equ TCA_SINGLE_CMD_0_bm = (1<<2)        ; Command bit 0 mask
.equ TCA_SINGLE_CMD_0_bp = 2             ; Command bit 0 position
.equ TCA_SINGLE_CMD_1_bm = (1<<3)        ; Command bit 1 mask
.equ TCA_SINGLE_CMD_1_bp = 3             ; Command bit 1 position

; TCA_SINGLE_CTRLESET masks
; Masks for TCA_SINGLE_DIR already defined
; Masks for TCA_SINGLE_LUPD already defined
; Masks for TCA_SINGLE_CMD already defined

; TCA_SINGLE_CTRLFCLR masks
.equ TCA_SINGLE_PERBV_bm = 0x01          ; Period Buffer Valid bit mask
.equ TCA_SINGLE_PERBV_bp = 0             ; Period Buffer Valid bit position
.equ TCA_SINGLE_CMP0BV_bm = 0x02         ; Compare 0 Buffer Valid bit mask
.equ TCA_SINGLE_CMP0BV_bp = 1            ; Compare 0 Buffer Valid bit position
.equ TCA_SINGLE_CMP1BV_bm = 0x04         ; Compare 1 Buffer Valid bit mask
.equ TCA_SINGLE_CMP1BV_bp = 2            ; Compare 1 Buffer Valid bit position
.equ TCA_SINGLE_CMP2BV_bm = 0x08         ; Compare 2 Buffer Valid bit mask
.equ TCA_SINGLE_CMP2BV_bp = 3            ; Compare 2 Buffer Valid bit position

; TCA_SINGLE_CTRLFSET masks
; Masks for TCA_SINGLE_PERBV already defined
; Masks for TCA_SINGLE_CMP0BV already defined
; Masks for TCA_SINGLE_CMP1BV already defined
; Masks for TCA_SINGLE_CMP2BV already defined

; TCA_SINGLE_EVCTRL masks
.equ TCA_SINGLE_CNTAEI_bm = 0x01         ; Count on Event Input A bit mask
.equ TCA_SINGLE_CNTAEI_bp = 0            ; Count on Event Input A bit position
.equ TCA_SINGLE_EVACTA_gm = 0x0E         ; Event Action A group mask
.equ TCA_SINGLE_EVACTA_gp = 1            ; Event Action A group position
.equ TCA_SINGLE_EVACTA_0_bm = (1<<1)     ; Event Action A bit 0 mask
.equ TCA_SINGLE_EVACTA_0_bp = 1          ; Event Action A bit 0 position
.equ TCA_SINGLE_EVACTA_1_bm = (1<<2)     ; Event Action A bit 1 mask
.equ TCA_SINGLE_EVACTA_1_bp = 2          ; Event Action A bit 1 position
.equ TCA_SINGLE_EVACTA_2_bm = (1<<3)     ; Event Action A bit 2 mask
.equ TCA_SINGLE_EVACTA_2_bp = 3          ; Event Action A bit 2 position
.equ TCA_SINGLE_CNTBEI_bm = 0x10         ; Count on Event Input B bit mask
.equ TCA_SINGLE_CNTBEI_bp = 4            ; Count on Event Input B bit position
.equ TCA_SINGLE_EVACTB_gm = 0xE0         ; Event Action B group mask
.equ TCA_SINGLE_EVACTB_gp = 5            ; Event Action B group position
.equ TCA_SINGLE_EVACTB_0_bm = (1<<5)     ; Event Action B bit 0 mask
.equ TCA_SINGLE_EVACTB_0_bp = 5          ; Event Action B bit 0 position
.equ TCA_SINGLE_EVACTB_1_bm = (1<<6)     ; Event Action B bit 1 mask
.equ TCA_SINGLE_EVACTB_1_bp = 6          ; Event Action B bit 1 position
.equ TCA_SINGLE_EVACTB_2_bm = (1<<7)     ; Event Action B bit 2 mask
.equ TCA_SINGLE_EVACTB_2_bp = 7          ; Event Action B bit 2 position

; TCA_SINGLE_INTCTRL masks
.equ TCA_SINGLE_OVF_bm = 0x01            ; Overflow Interrupt bit mask
.equ TCA_SINGLE_OVF_bp = 0               ; Overflow Interrupt bit position
.equ TCA_SINGLE_CMP0_bm = 0x10           ; Compare 0 Interrupt bit mask
.equ TCA_SINGLE_CMP0_bp = 4              ; Compare 0 Interrupt bit position
.equ TCA_SINGLE_CMP1_bm = 0x20           ; Compare 1 Interrupt bit mask
.equ TCA_SINGLE_CMP1_bp = 5              ; Compare 1 Interrupt bit position
.equ TCA_SINGLE_CMP2_bm = 0x40           ; Compare 2 Interrupt bit mask
.equ TCA_SINGLE_CMP2_bp = 6              ; Compare 2 Interrupt bit position

; TCA_SINGLE_INTFLAGS masks
; Masks for TCA_SINGLE_OVF already defined
; Masks for TCA_SINGLE_CMP0 already defined
; Masks for TCA_SINGLE_CMP1 already defined
; Masks for TCA_SINGLE_CMP2 already defined

; TCA_SINGLE_DBGCTRL masks
.equ TCA_SINGLE_DBGRUN_bm = 0x01         ; Debug Run bit mask
.equ TCA_SINGLE_DBGRUN_bp = 0            ; Debug Run bit position

; Masks for CTRLA already defined
; Masks for CTRLB already defined
; Masks for CTRLC already defined
; Masks for CTRLD already defined
; Masks for CTRLECLR already defined
; Masks for CTRLESET already defined
; Masks for INTCTRL already defined
; Masks for INTFLAGS already defined
; Masks for DBGCTRL already defined
; TCA_SPLIT_CTRLA masks
.equ TCA_SPLIT_ENABLE_bm = 0x01          ; Module Enable bit mask
.equ TCA_SPLIT_ENABLE_bp = 0             ; Module Enable bit position
.equ TCA_SPLIT_CLKSEL_gm = 0x0E          ; Clock Selection group mask
.equ TCA_SPLIT_CLKSEL_gp = 1             ; Clock Selection group position
.equ TCA_SPLIT_CLKSEL_0_bm = (1<<1)      ; Clock Selection bit 0 mask
.equ TCA_SPLIT_CLKSEL_0_bp = 1           ; Clock Selection bit 0 position
.equ TCA_SPLIT_CLKSEL_1_bm = (1<<2)      ; Clock Selection bit 1 mask
.equ TCA_SPLIT_CLKSEL_1_bp = 2           ; Clock Selection bit 1 position
.equ TCA_SPLIT_CLKSEL_2_bm = (1<<3)      ; Clock Selection bit 2 mask
.equ TCA_SPLIT_CLKSEL_2_bp = 3           ; Clock Selection bit 2 position
.equ TCA_SPLIT_RUNSTDBY_bm = 0x80        ; Run in Standby bit mask
.equ TCA_SPLIT_RUNSTDBY_bp = 7           ; Run in Standby bit position

; TCA_SPLIT_CTRLB masks
.equ TCA_SPLIT_LCMP0EN_bm = 0x01         ; Low Compare 0 Enable bit mask
.equ TCA_SPLIT_LCMP0EN_bp = 0            ; Low Compare 0 Enable bit position
.equ TCA_SPLIT_LCMP1EN_bm = 0x02         ; Low Compare 1 Enable bit mask
.equ TCA_SPLIT_LCMP1EN_bp = 1            ; Low Compare 1 Enable bit position
.equ TCA_SPLIT_LCMP2EN_bm = 0x04         ; Low Compare 2 Enable bit mask
.equ TCA_SPLIT_LCMP2EN_bp = 2            ; Low Compare 2 Enable bit position
.equ TCA_SPLIT_HCMP0EN_bm = 0x10         ; High Compare 0 Enable bit mask
.equ TCA_SPLIT_HCMP0EN_bp = 4            ; High Compare 0 Enable bit position
.equ TCA_SPLIT_HCMP1EN_bm = 0x20         ; High Compare 1 Enable bit mask
.equ TCA_SPLIT_HCMP1EN_bp = 5            ; High Compare 1 Enable bit position
.equ TCA_SPLIT_HCMP2EN_bm = 0x40         ; High Compare 2 Enable bit mask
.equ TCA_SPLIT_HCMP2EN_bp = 6            ; High Compare 2 Enable bit position

; TCA_SPLIT_CTRLC masks
.equ TCA_SPLIT_LCMP0OV_bm = 0x01         ; Low Compare 0 Output Value bit mask
.equ TCA_SPLIT_LCMP0OV_bp = 0            ; Low Compare 0 Output Value bit position
.equ TCA_SPLIT_LCMP1OV_bm = 0x02         ; Low Compare 1 Output Value bit mask
.equ TCA_SPLIT_LCMP1OV_bp = 1            ; Low Compare 1 Output Value bit position
.equ TCA_SPLIT_LCMP2OV_bm = 0x04         ; Low Compare 2 Output Value bit mask
.equ TCA_SPLIT_LCMP2OV_bp = 2            ; Low Compare 2 Output Value bit position
.equ TCA_SPLIT_HCMP0OV_bm = 0x10         ; High Compare 0 Output Value bit mask
.equ TCA_SPLIT_HCMP0OV_bp = 4            ; High Compare 0 Output Value bit position
.equ TCA_SPLIT_HCMP1OV_bm = 0x20         ; High Compare 1 Output Value bit mask
.equ TCA_SPLIT_HCMP1OV_bp = 5            ; High Compare 1 Output Value bit position
.equ TCA_SPLIT_HCMP2OV_bm = 0x40         ; High Compare 2 Output Value bit mask
.equ TCA_SPLIT_HCMP2OV_bp = 6            ; High Compare 2 Output Value bit position

; TCA_SPLIT_CTRLD masks
.equ TCA_SPLIT_SPLITM_bm = 0x01          ; Split Mode Enable bit mask
.equ TCA_SPLIT_SPLITM_bp = 0             ; Split Mode Enable bit position

; TCA_SPLIT_CTRLECLR masks
.equ TCA_SPLIT_CMDEN_gm = 0x03           ; Command Enable group mask
.equ TCA_SPLIT_CMDEN_gp = 0              ; Command Enable group position
.equ TCA_SPLIT_CMDEN_0_bm = (1<<0)       ; Command Enable bit 0 mask
.equ TCA_SPLIT_CMDEN_0_bp = 0            ; Command Enable bit 0 position
.equ TCA_SPLIT_CMDEN_1_bm = (1<<1)       ; Command Enable bit 1 mask
.equ TCA_SPLIT_CMDEN_1_bp = 1            ; Command Enable bit 1 position
.equ TCA_SPLIT_CMD_gm = 0x0C             ; Command group mask
.equ TCA_SPLIT_CMD_gp = 2                ; Command group position
.equ TCA_SPLIT_CMD_0_bm = (1<<2)         ; Command bit 0 mask
.equ TCA_SPLIT_CMD_0_bp = 2              ; Command bit 0 position
.equ TCA_SPLIT_CMD_1_bm = (1<<3)         ; Command bit 1 mask
.equ TCA_SPLIT_CMD_1_bp = 3              ; Command bit 1 position

; TCA_SPLIT_CTRLESET masks
; Masks for TCA_SPLIT_CMDEN already defined
; Masks for TCA_SPLIT_CMD already defined

; TCA_SPLIT_INTCTRL masks
.equ TCA_SPLIT_LUNF_bm = 0x01            ; Low Underflow Interrupt Enable bit mask
.equ TCA_SPLIT_LUNF_bp = 0               ; Low Underflow Interrupt Enable bit position
.equ TCA_SPLIT_HUNF_bm = 0x02            ; High Underflow Interrupt Enable bit mask
.equ TCA_SPLIT_HUNF_bp = 1               ; High Underflow Interrupt Enable bit position
.equ TCA_SPLIT_LCMP0_bm = 0x10           ; Low Compare 0 Interrupt Enable bit mask
.equ TCA_SPLIT_LCMP0_bp = 4              ; Low Compare 0 Interrupt Enable bit position
.equ TCA_SPLIT_LCMP1_bm = 0x20           ; Low Compare 1 Interrupt Enable bit mask
.equ TCA_SPLIT_LCMP1_bp = 5              ; Low Compare 1 Interrupt Enable bit position
.equ TCA_SPLIT_LCMP2_bm = 0x40           ; Low Compare 2 Interrupt Enable bit mask
.equ TCA_SPLIT_LCMP2_bp = 6              ; Low Compare 2 Interrupt Enable bit position

; TCA_SPLIT_INTFLAGS masks
; Masks for TCA_SPLIT_LUNF already defined
; Masks for TCA_SPLIT_HUNF already defined
; Masks for TCA_SPLIT_LCMP0 already defined
; Masks for TCA_SPLIT_LCMP1 already defined
; Masks for TCA_SPLIT_LCMP2 already defined

; TCA_SPLIT_DBGCTRL masks
.equ TCA_SPLIT_DBGRUN_bm = 0x01          ; Debug Run bit mask
.equ TCA_SPLIT_DBGRUN_bp = 0             ; Debug Run bit position

; Clock Selection
.equ TCA_SINGLE_CLKSEL_DIV1_gc = (0x00<<1) ; CLK_PER
.equ TCA_SINGLE_CLKSEL_DIV2_gc = (0x01<<1) ; CLK_PER / 2
.equ TCA_SINGLE_CLKSEL_DIV4_gc = (0x02<<1) ; CLK_PER / 4
.equ TCA_SINGLE_CLKSEL_DIV8_gc = (0x03<<1) ; CLK_PER / 8
.equ TCA_SINGLE_CLKSEL_DIV16_gc = (0x04<<1) ; CLK_PER / 16
.equ TCA_SINGLE_CLKSEL_DIV64_gc = (0x05<<1) ; CLK_PER / 64
.equ TCA_SINGLE_CLKSEL_DIV256_gc = (0x06<<1) ; CLK_PER / 256
.equ TCA_SINGLE_CLKSEL_DIV1024_gc = (0x07<<1) ; CLK_PER / 1024

; Waveform generation mode select
.equ TCA_SINGLE_WGMODE_NORMAL_gc = (0x00<<0) ; Normal Mode
.equ TCA_SINGLE_WGMODE_FRQ_gc = (0x01<<0) ; Frequency Generation Mode
.equ TCA_SINGLE_WGMODE_SINGLESLOPE_gc = (0x03<<0) ; Single Slope PWM
.equ TCA_SINGLE_WGMODE_DSTOP_gc = (0x05<<0) ; Dual Slope PWM, overflow on TOP
.equ TCA_SINGLE_WGMODE_DSBOTH_gc = (0x06<<0) ; Dual Slope PWM, overflow on TOP and BOTTOM
.equ TCA_SINGLE_WGMODE_DSBOTTOM_gc = (0x07<<0) ; Dual Slope PWM, overflow on BOTTOM

; Command select
.equ TCA_SINGLE_CMD_NONE_gc = (0x00<<2)  ; No Command
.equ TCA_SINGLE_CMD_UPDATE_gc = (0x01<<2) ; Force Update
.equ TCA_SINGLE_CMD_RESTART_gc = (0x02<<2) ; Force Restart
.equ TCA_SINGLE_CMD_RESET_gc = (0x03<<2) ; Force Hard Reset

; Direction select
.equ TCA_SINGLE_DIR_UP_gc = (0x00<<0)    ; Count up
.equ TCA_SINGLE_DIR_DOWN_gc = (0x01<<0)  ; Count down

; Event Action A select
.equ TCA_SINGLE_EVACTA_CNT_POSEDGE_gc = (0x00<<1) ; Count on positive edge event
.equ TCA_SINGLE_EVACTA_CNT_ANYEDGE_gc = (0x01<<1) ; Count on any edge event
.equ TCA_SINGLE_EVACTA_CNT_HIGHLVL_gc = (0x02<<1) ; Count on prescaled clock while event line is 1.
.equ TCA_SINGLE_EVACTA_UPDOWN_gc = (0x03<<1) ; Count on prescaled clock. Event controls count direction. Up-count when event line is 0, down-count when event line is 1.

; Event Action B select
.equ TCA_SINGLE_EVACTB_NONE_gc = (0x00<<5) ; No Action
.equ TCA_SINGLE_EVACTB_UPDOWN_gc = (0x03<<5) ; Count on prescaled clock. Event controls count direction. Up-count when event line is 0, down-count when event line is 1.
.equ TCA_SINGLE_EVACTB_RESTART_POSEDGE_gc = (0x04<<5) ; Restart counter at positive edge event
.equ TCA_SINGLE_EVACTB_RESTART_ANYEDGE_gc = (0x05<<5) ; Restart counter on any edge event
.equ TCA_SINGLE_EVACTB_RESTART_HIGHLVL_gc = (0x06<<5) ; Restart counter while event line is 1.

; Clock Selection
.equ TCA_SPLIT_CLKSEL_DIV1_gc = (0x00<<1) ; CLK_PER
.equ TCA_SPLIT_CLKSEL_DIV2_gc = (0x01<<1) ; CLK_PER / 2
.equ TCA_SPLIT_CLKSEL_DIV4_gc = (0x02<<1) ; CLK_PER / 4
.equ TCA_SPLIT_CLKSEL_DIV8_gc = (0x03<<1) ; CLK_PER / 8
.equ TCA_SPLIT_CLKSEL_DIV16_gc = (0x04<<1) ; CLK_PER / 16
.equ TCA_SPLIT_CLKSEL_DIV64_gc = (0x05<<1) ; CLK_PER / 64
.equ TCA_SPLIT_CLKSEL_DIV256_gc = (0x06<<1) ; CLK_PER / 256
.equ TCA_SPLIT_CLKSEL_DIV1024_gc = (0x07<<1) ; CLK_PER / 1024

; Command select
.equ TCA_SPLIT_CMD_NONE_gc = (0x00<<2)   ; No Command
.equ TCA_SPLIT_CMD_UPDATE_gc = (0x01<<2) ; Force Update
.equ TCA_SPLIT_CMD_RESTART_gc = (0x02<<2) ; Force Restart
.equ TCA_SPLIT_CMD_RESET_gc = (0x03<<2)  ; Force Hard Reset

; Command Enable select
.equ TCA_SPLIT_CMDEN_NONE_gc = (0x00<<0) ; None
.equ TCA_SPLIT_CMDEN_BOTH_gc = (0x03<<0) ; Both low byte and high byte counter


;*************************************************************************
;** TCB - 16-bit Timer Type B
;*************************************************************************

; TCB_CTRLA masks
.equ TCB_ENABLE_bm = 0x01                ; Enable bit mask
.equ TCB_ENABLE_bp = 0                   ; Enable bit position
.equ TCB_CLKSEL_gm = 0x0E                ; Clock Select group mask
.equ TCB_CLKSEL_gp = 1                   ; Clock Select group position
.equ TCB_CLKSEL_0_bm = (1<<1)            ; Clock Select bit 0 mask
.equ TCB_CLKSEL_0_bp = 1                 ; Clock Select bit 0 position
.equ TCB_CLKSEL_1_bm = (1<<2)            ; Clock Select bit 1 mask
.equ TCB_CLKSEL_1_bp = 2                 ; Clock Select bit 1 position
.equ TCB_CLKSEL_2_bm = (1<<3)            ; Clock Select bit 2 mask
.equ TCB_CLKSEL_2_bp = 3                 ; Clock Select bit 2 position
.equ TCB_SYNCUPD_bm = 0x10               ; Synchronize Update bit mask
.equ TCB_SYNCUPD_bp = 4                  ; Synchronize Update bit position
.equ TCB_CASCADE_bm = 0x20               ; Cascade two timers bit mask
.equ TCB_CASCADE_bp = 5                  ; Cascade two timers bit position
.equ TCB_RUNSTDBY_bm = 0x40              ; Run Standby bit mask
.equ TCB_RUNSTDBY_bp = 6                 ; Run Standby bit position

; TCB_CTRLB masks
.equ TCB_CNTMODE_gm = 0x07               ; Timer Mode group mask
.equ TCB_CNTMODE_gp = 0                  ; Timer Mode group position
.equ TCB_CNTMODE_0_bm = (1<<0)           ; Timer Mode bit 0 mask
.equ TCB_CNTMODE_0_bp = 0                ; Timer Mode bit 0 position
.equ TCB_CNTMODE_1_bm = (1<<1)           ; Timer Mode bit 1 mask
.equ TCB_CNTMODE_1_bp = 1                ; Timer Mode bit 1 position
.equ TCB_CNTMODE_2_bm = (1<<2)           ; Timer Mode bit 2 mask
.equ TCB_CNTMODE_2_bp = 2                ; Timer Mode bit 2 position
.equ TCB_CCMPEN_bm = 0x10                ; Pin Output Enable bit mask
.equ TCB_CCMPEN_bp = 4                   ; Pin Output Enable bit position
.equ TCB_CCMPINIT_bm = 0x20              ; Pin Initial State bit mask
.equ TCB_CCMPINIT_bp = 5                 ; Pin Initial State bit position
.equ TCB_ASYNC_bm = 0x40                 ; Asynchronous Enable bit mask
.equ TCB_ASYNC_bp = 6                    ; Asynchronous Enable bit position

; TCB_EVCTRL masks
.equ TCB_CAPTEI_bm = 0x01                ; Event Input Enable bit mask
.equ TCB_CAPTEI_bp = 0                   ; Event Input Enable bit position
.equ TCB_EDGE_bm = 0x10                  ; Event Edge bit mask
.equ TCB_EDGE_bp = 4                     ; Event Edge bit position
.equ TCB_FILTER_bm = 0x40                ; Input Capture Noise Cancellation Filter bit mask
.equ TCB_FILTER_bp = 6                   ; Input Capture Noise Cancellation Filter bit position

; TCB_INTCTRL masks
.equ TCB_CAPT_bm = 0x01                  ; Capture or Timeout bit mask
.equ TCB_CAPT_bp = 0                     ; Capture or Timeout bit position
.equ TCB_OVF_bm = 0x02                   ; Overflow bit mask
.equ TCB_OVF_bp = 1                      ; Overflow bit position

; TCB_INTFLAGS masks
; Masks for TCB_CAPT already defined
; Masks for TCB_OVF already defined

; TCB_STATUS masks
.equ TCB_RUN_bm = 0x01                   ; Run bit mask
.equ TCB_RUN_bp = 0                      ; Run bit position

; TCB_DBGCTRL masks
.equ TCB_DBGRUN_bm = 0x01                ; Debug Run bit mask
.equ TCB_DBGRUN_bp = 0                   ; Debug Run bit position

; Clock Select
.equ TCB_CLKSEL_DIV1_gc = (0x00<<1)      ; CLK_PER
.equ TCB_CLKSEL_DIV2_gc = (0x01<<1)      ; CLK_PER/2
.equ TCB_CLKSEL_TCA0_gc = (0x02<<1)      ; Use CLK_TCA from TCA0
.equ TCB_CLKSEL_TCA1_gc = (0x03<<1)      ; Use CLK_TCA from TCA1
.equ TCB_CLKSEL_EVENT_gc = (0x07<<1)     ; Count on event edge

; Timer Mode select
.equ TCB_CNTMODE_INT_gc = (0x00<<0)      ; Periodic Interrupt
.equ TCB_CNTMODE_TIMEOUT_gc = (0x01<<0)  ; Periodic Timeout
.equ TCB_CNTMODE_CAPT_gc = (0x02<<0)     ; Input Capture Event
.equ TCB_CNTMODE_FRQ_gc = (0x03<<0)      ; Input Capture Frequency measurement
.equ TCB_CNTMODE_PW_gc = (0x04<<0)       ; Input Capture Pulse-Width measurement
.equ TCB_CNTMODE_FRQPW_gc = (0x05<<0)    ; Input Capture Frequency and Pulse-Width measurement
.equ TCB_CNTMODE_SINGLE_gc = (0x06<<0)   ; Single Shot
.equ TCB_CNTMODE_PWM8_gc = (0x07<<0)     ; 8-bit PWM


;*************************************************************************
;** TWI - Two-Wire Interface
;*************************************************************************

; TWI_CTRLA masks
.equ TWI_FMEN_bm = 0x01                  ; Fast-mode Enable bit mask
.equ TWI_FMEN_bp = 0                     ; Fast-mode Enable bit position
.equ TWI_FMPEN_bm = 0x02                 ; Fast-mode Plus Enable bit mask
.equ TWI_FMPEN_bp = 1                    ; Fast-mode Plus Enable bit position
.equ TWI_SDAHOLD_gm = 0x0C               ; SDA Hold Time group mask
.equ TWI_SDAHOLD_gp = 2                  ; SDA Hold Time group position
.equ TWI_SDAHOLD_0_bm = (1<<2)           ; SDA Hold Time bit 0 mask
.equ TWI_SDAHOLD_0_bp = 2                ; SDA Hold Time bit 0 position
.equ TWI_SDAHOLD_1_bm = (1<<3)           ; SDA Hold Time bit 1 mask
.equ TWI_SDAHOLD_1_bp = 3                ; SDA Hold Time bit 1 position
.equ TWI_SDASETUP_bm = 0x10              ; SDA Setup Time bit mask
.equ TWI_SDASETUP_bp = 4                 ; SDA Setup Time bit position
.equ TWI_INPUTLVL_bm = 0x40              ; Input voltage transition level bit mask
.equ TWI_INPUTLVL_bp = 6                 ; Input voltage transition level bit position

; TWI_DUALCTRL masks
.equ TWI_ENABLE_bm = 0x01                ; Enable bit mask
.equ TWI_ENABLE_bp = 0                   ; Enable bit position
; Masks for TWI_FMPEN already defined
; Masks for TWI_SDAHOLD already defined
; Masks for TWI_INPUTLVL already defined

; TWI_DBGCTRL masks
.equ TWI_DBGRUN_bm = 0x01                ; Debug Run bit mask
.equ TWI_DBGRUN_bp = 0                   ; Debug Run bit position

; TWI_MCTRLA masks
; Masks for TWI_ENABLE already defined
.equ TWI_SMEN_bm = 0x02                  ; Smart Mode Enable bit mask
.equ TWI_SMEN_bp = 1                     ; Smart Mode Enable bit position
.equ TWI_TIMEOUT_gm = 0x0C               ; Inactive Bus Time-Out group mask
.equ TWI_TIMEOUT_gp = 2                  ; Inactive Bus Time-Out group position
.equ TWI_TIMEOUT_0_bm = (1<<2)           ; Inactive Bus Time-Out bit 0 mask
.equ TWI_TIMEOUT_0_bp = 2                ; Inactive Bus Time-Out bit 0 position
.equ TWI_TIMEOUT_1_bm = (1<<3)           ; Inactive Bus Time-Out bit 1 mask
.equ TWI_TIMEOUT_1_bp = 3                ; Inactive Bus Time-Out bit 1 position
.equ TWI_QCEN_bm = 0x10                  ; Quick Command Enable bit mask
.equ TWI_QCEN_bp = 4                     ; Quick Command Enable bit position
.equ TWI_WIEN_bm = 0x40                  ; Write Interrupt Enable bit mask
.equ TWI_WIEN_bp = 6                     ; Write Interrupt Enable bit position
.equ TWI_RIEN_bm = 0x80                  ; Read Interrupt Enable bit mask
.equ TWI_RIEN_bp = 7                     ; Read Interrupt Enable bit position

; TWI_MCTRLB masks
.equ TWI_MCMD_gm = 0x03                  ; Command group mask
.equ TWI_MCMD_gp = 0                     ; Command group position
.equ TWI_MCMD_0_bm = (1<<0)              ; Command bit 0 mask
.equ TWI_MCMD_0_bp = 0                   ; Command bit 0 position
.equ TWI_MCMD_1_bm = (1<<1)              ; Command bit 1 mask
.equ TWI_MCMD_1_bp = 1                   ; Command bit 1 position
.equ TWI_ACKACT_bm = 0x04                ; Acknowledge Action bit mask
.equ TWI_ACKACT_bp = 2                   ; Acknowledge Action bit position
.equ TWI_FLUSH_bm = 0x08                 ; Flush bit mask
.equ TWI_FLUSH_bp = 3                    ; Flush bit position

; TWI_MSTATUS masks
.equ TWI_BUSSTATE_gm = 0x03              ; Bus State group mask
.equ TWI_BUSSTATE_gp = 0                 ; Bus State group position
.equ TWI_BUSSTATE_0_bm = (1<<0)          ; Bus State bit 0 mask
.equ TWI_BUSSTATE_0_bp = 0               ; Bus State bit 0 position
.equ TWI_BUSSTATE_1_bm = (1<<1)          ; Bus State bit 1 mask
.equ TWI_BUSSTATE_1_bp = 1               ; Bus State bit 1 position
.equ TWI_BUSERR_bm = 0x04                ; Bus Error bit mask
.equ TWI_BUSERR_bp = 2                   ; Bus Error bit position
.equ TWI_ARBLOST_bm = 0x08               ; Arbitration Lost bit mask
.equ TWI_ARBLOST_bp = 3                  ; Arbitration Lost bit position
.equ TWI_RXACK_bm = 0x10                 ; Received Acknowledge bit mask
.equ TWI_RXACK_bp = 4                    ; Received Acknowledge bit position
.equ TWI_CLKHOLD_bm = 0x20               ; Clock Hold bit mask
.equ TWI_CLKHOLD_bp = 5                  ; Clock Hold bit position
.equ TWI_WIF_bm = 0x40                   ; Write Interrupt Flag bit mask
.equ TWI_WIF_bp = 6                      ; Write Interrupt Flag bit position
.equ TWI_RIF_bm = 0x80                   ; Read Interrupt Flag bit mask
.equ TWI_RIF_bp = 7                      ; Read Interrupt Flag bit position

; TWI_MBAUD masks
.equ TWI_BAUD_gm = 0xFF                  ; Baud Rate group mask
.equ TWI_BAUD_gp = 0                     ; Baud Rate group position
.equ TWI_BAUD_0_bm = (1<<0)              ; Baud Rate bit 0 mask
.equ TWI_BAUD_0_bp = 0                   ; Baud Rate bit 0 position
.equ TWI_BAUD_1_bm = (1<<1)              ; Baud Rate bit 1 mask
.equ TWI_BAUD_1_bp = 1                   ; Baud Rate bit 1 position
.equ TWI_BAUD_2_bm = (1<<2)              ; Baud Rate bit 2 mask
.equ TWI_BAUD_2_bp = 2                   ; Baud Rate bit 2 position
.equ TWI_BAUD_3_bm = (1<<3)              ; Baud Rate bit 3 mask
.equ TWI_BAUD_3_bp = 3                   ; Baud Rate bit 3 position
.equ TWI_BAUD_4_bm = (1<<4)              ; Baud Rate bit 4 mask
.equ TWI_BAUD_4_bp = 4                   ; Baud Rate bit 4 position
.equ TWI_BAUD_5_bm = (1<<5)              ; Baud Rate bit 5 mask
.equ TWI_BAUD_5_bp = 5                   ; Baud Rate bit 5 position
.equ TWI_BAUD_6_bm = (1<<6)              ; Baud Rate bit 6 mask
.equ TWI_BAUD_6_bp = 6                   ; Baud Rate bit 6 position
.equ TWI_BAUD_7_bm = (1<<7)              ; Baud Rate bit 7 mask
.equ TWI_BAUD_7_bp = 7                   ; Baud Rate bit 7 position

; TWI_MADDR masks
.equ TWI_ADDR_gm = 0xFF                  ; Address group mask
.equ TWI_ADDR_gp = 0                     ; Address group position
.equ TWI_ADDR_0_bm = (1<<0)              ; Address bit 0 mask
.equ TWI_ADDR_0_bp = 0                   ; Address bit 0 position
.equ TWI_ADDR_1_bm = (1<<1)              ; Address bit 1 mask
.equ TWI_ADDR_1_bp = 1                   ; Address bit 1 position
.equ TWI_ADDR_2_bm = (1<<2)              ; Address bit 2 mask
.equ TWI_ADDR_2_bp = 2                   ; Address bit 2 position
.equ TWI_ADDR_3_bm = (1<<3)              ; Address bit 3 mask
.equ TWI_ADDR_3_bp = 3                   ; Address bit 3 position
.equ TWI_ADDR_4_bm = (1<<4)              ; Address bit 4 mask
.equ TWI_ADDR_4_bp = 4                   ; Address bit 4 position
.equ TWI_ADDR_5_bm = (1<<5)              ; Address bit 5 mask
.equ TWI_ADDR_5_bp = 5                   ; Address bit 5 position
.equ TWI_ADDR_6_bm = (1<<6)              ; Address bit 6 mask
.equ TWI_ADDR_6_bp = 6                   ; Address bit 6 position
.equ TWI_ADDR_7_bm = (1<<7)              ; Address bit 7 mask
.equ TWI_ADDR_7_bp = 7                   ; Address bit 7 position

; TWI_MDATA masks
.equ TWI_DATA_gm = 0xFF                  ; Data group mask
.equ TWI_DATA_gp = 0                     ; Data group position
.equ TWI_DATA_0_bm = (1<<0)              ; Data bit 0 mask
.equ TWI_DATA_0_bp = 0                   ; Data bit 0 position
.equ TWI_DATA_1_bm = (1<<1)              ; Data bit 1 mask
.equ TWI_DATA_1_bp = 1                   ; Data bit 1 position
.equ TWI_DATA_2_bm = (1<<2)              ; Data bit 2 mask
.equ TWI_DATA_2_bp = 2                   ; Data bit 2 position
.equ TWI_DATA_3_bm = (1<<3)              ; Data bit 3 mask
.equ TWI_DATA_3_bp = 3                   ; Data bit 3 position
.equ TWI_DATA_4_bm = (1<<4)              ; Data bit 4 mask
.equ TWI_DATA_4_bp = 4                   ; Data bit 4 position
.equ TWI_DATA_5_bm = (1<<5)              ; Data bit 5 mask
.equ TWI_DATA_5_bp = 5                   ; Data bit 5 position
.equ TWI_DATA_6_bm = (1<<6)              ; Data bit 6 mask
.equ TWI_DATA_6_bp = 6                   ; Data bit 6 position
.equ TWI_DATA_7_bm = (1<<7)              ; Data bit 7 mask
.equ TWI_DATA_7_bp = 7                   ; Data bit 7 position

; TWI_SCTRLA masks
; Masks for TWI_ENABLE already defined
; Masks for TWI_SMEN already defined
.equ TWI_PMEN_bm = 0x04                  ; Address Recognition Mode bit mask
.equ TWI_PMEN_bp = 2                     ; Address Recognition Mode bit position
.equ TWI_PIEN_bm = 0x20                  ; Stop Interrupt Enable bit mask
.equ TWI_PIEN_bp = 5                     ; Stop Interrupt Enable bit position
.equ TWI_APIEN_bm = 0x40                 ; Address or Stop Interrupt Enable bit mask
.equ TWI_APIEN_bp = 6                    ; Address or Stop Interrupt Enable bit position
.equ TWI_DIEN_bm = 0x80                  ; Data Interrupt Enable bit mask
.equ TWI_DIEN_bp = 7                     ; Data Interrupt Enable bit position

; TWI_SCTRLB masks
.equ TWI_SCMD_gm = 0x03                  ; Command group mask
.equ TWI_SCMD_gp = 0                     ; Command group position
.equ TWI_SCMD_0_bm = (1<<0)              ; Command bit 0 mask
.equ TWI_SCMD_0_bp = 0                   ; Command bit 0 position
.equ TWI_SCMD_1_bm = (1<<1)              ; Command bit 1 mask
.equ TWI_SCMD_1_bp = 1                   ; Command bit 1 position
; Masks for TWI_ACKACT already defined

; TWI_SSTATUS masks
.equ TWI_AP_bm = 0x01                    ; Address or Stop bit mask
.equ TWI_AP_bp = 0                       ; Address or Stop bit position
.equ TWI_DIR_bm = 0x02                   ; Read/Write Direction bit mask
.equ TWI_DIR_bp = 1                      ; Read/Write Direction bit position
; Masks for TWI_BUSERR already defined
.equ TWI_COLL_bm = 0x08                  ; Collision bit mask
.equ TWI_COLL_bp = 3                     ; Collision bit position
; Masks for TWI_RXACK already defined
; Masks for TWI_CLKHOLD already defined
.equ TWI_APIF_bm = 0x40                  ; Address or Stop Interrupt Flag bit mask
.equ TWI_APIF_bp = 6                     ; Address or Stop Interrupt Flag bit position
.equ TWI_DIF_bm = 0x80                   ; Data Interrupt Flag bit mask
.equ TWI_DIF_bp = 7                      ; Data Interrupt Flag bit position

; TWI_SADDR masks
; Masks for TWI_ADDR already defined

; TWI_SDATA masks
; Masks for TWI_DATA already defined

; TWI_SADDRMASK masks
.equ TWI_ADDREN_bm = 0x01                ; Address Mask Enable bit mask
.equ TWI_ADDREN_bp = 0                   ; Address Mask Enable bit position
.equ TWI_ADDRMASK_gm = 0xFE              ; Address Mask group mask
.equ TWI_ADDRMASK_gp = 1                 ; Address Mask group position
.equ TWI_ADDRMASK_0_bm = (1<<1)          ; Address Mask bit 0 mask
.equ TWI_ADDRMASK_0_bp = 1               ; Address Mask bit 0 position
.equ TWI_ADDRMASK_1_bm = (1<<2)          ; Address Mask bit 1 mask
.equ TWI_ADDRMASK_1_bp = 2               ; Address Mask bit 1 position
.equ TWI_ADDRMASK_2_bm = (1<<3)          ; Address Mask bit 2 mask
.equ TWI_ADDRMASK_2_bp = 3               ; Address Mask bit 2 position
.equ TWI_ADDRMASK_3_bm = (1<<4)          ; Address Mask bit 3 mask
.equ TWI_ADDRMASK_3_bp = 4               ; Address Mask bit 3 position
.equ TWI_ADDRMASK_4_bm = (1<<5)          ; Address Mask bit 4 mask
.equ TWI_ADDRMASK_4_bp = 5               ; Address Mask bit 4 position
.equ TWI_ADDRMASK_5_bm = (1<<6)          ; Address Mask bit 5 mask
.equ TWI_ADDRMASK_5_bp = 6               ; Address Mask bit 5 position
.equ TWI_ADDRMASK_6_bm = (1<<7)          ; Address Mask bit 6 mask
.equ TWI_ADDRMASK_6_bp = 7               ; Address Mask bit 6 position

; Fast-mode Enable select
.equ TWI_FMEN_OFF_gc = (0x00<<0)         ; SCL duty cycle operating according to Sm specification
.equ TWI_FMEN_ON_gc = (0x01<<0)          ; SCL duty cycle operating according to Fm specification

; Fast-mode Plus Enable select
.equ TWI_FMPEN_OFF_gc = (0x00<<1)        ; Operating in Standard-mode or Fast-mode
.equ TWI_FMPEN_ON_gc = (0x01<<1)         ; Operating in Fast-mode Plus

; Input voltage transition level select
.equ TWI_INPUTLVL_I2C_gc = (0x00<<6)     ; I2C input voltage transition level
.equ TWI_INPUTLVL_SMBUS_gc = (0x01<<6)   ; SMBus 3.0 input voltage transition level

; SDA Hold Time select
.equ TWI_SDAHOLD_OFF_gc = (0x00<<2)      ; No SDA Hold Delay
.equ TWI_SDAHOLD_50NS_gc = (0x01<<2)     ; Short SDA hold time
.equ TWI_SDAHOLD_300NS_gc = (0x02<<2)    ; Meets SMBUS 2.0 specification under typical conditions
.equ TWI_SDAHOLD_500NS_gc = (0x03<<2)    ; Meets SMBUS 2.0 specificaiton across all corners

; SDA Setup Time select
.equ TWI_SDASETUP_4CYC_gc = (0x00<<4)    ; SDA setup time is four clock cycles
.equ TWI_SDASETUP_8CYC_gc = (0x01<<4)    ; SDA setup time is eight clock cycle

; Debug Run select
.equ TWI_DBGRUN_HALT_gc = (0x00<<0)      ; The peripheral is halted in Break Debug mode and ignores events
.equ TWI_DBGRUN_RUN_gc = (0x01<<0)       ; The peripheral will continue to run in Break Debug mode when the CPU is halted

; Inactive Bus Time-Out select
.equ TWI_TIMEOUT_DISABLED_gc = (0x00<<2) ; Bus time-out disabled. I2C.
.equ TWI_TIMEOUT_50US_gc = (0x01<<2)     ; 50us - SMBus
.equ TWI_TIMEOUT_100US_gc = (0x02<<2)    ; 100us
.equ TWI_TIMEOUT_200US_gc = (0x03<<2)    ; 200us

; Acknowledge Action select
.equ TWI_ACKACT_ACK_gc = (0x00<<2)       ; Send ACK
.equ TWI_ACKACT_NACK_gc = (0x01<<2)      ; Send NACK

; Command select
.equ TWI_MCMD_NOACT_gc = (0x00<<0)       ; No action
.equ TWI_MCMD_REPSTART_gc = (0x01<<0)    ; Execute Acknowledge Action followed by repeated Start.
.equ TWI_MCMD_RECVTRANS_gc = (0x02<<0)   ; Execute Acknowledge Action followed by a byte read/write operation. Read/write is defined by DIR.
.equ TWI_MCMD_STOP_gc = (0x03<<0)        ; Execute Acknowledge Action followed by issuing a Stop condition.

; Bus State select
.equ TWI_BUSSTATE_UNKNOWN_gc = (0x00<<0) ; Unknown bus state
.equ TWI_BUSSTATE_IDLE_gc = (0x01<<0)    ; Bus is idle
.equ TWI_BUSSTATE_OWNER_gc = (0x02<<0)   ; This TWI controls the bus
.equ TWI_BUSSTATE_BUSY_gc = (0x03<<0)    ; The bus is busy

; Command select
.equ TWI_SCMD_NOACT_gc = (0x00<<0)       ; No Action
.equ TWI_SCMD_COMPTRANS_gc = (0x02<<0)   ; Complete transaction
.equ TWI_SCMD_RESPONSE_gc = (0x03<<0)    ; Used in response to an interrupt

; Address or Stop select
.equ TWI_AP_STOP_gc = (0x00<<0)          ; A Stop condition generated the interrupt on APIF flag
.equ TWI_AP_ADR_gc = (0x01<<0)           ; Address detection generated the interrupt on APIF flag


;*************************************************************************
;** USART - Universal Synchronous and Asynchronous Receiver and Transmitter
;*************************************************************************

; USART_RXDATAL masks
.equ USART_DATA_gm = 0xFF                ; RX Data group mask
.equ USART_DATA_gp = 0                   ; RX Data group position
.equ USART_DATA_0_bm = (1<<0)            ; RX Data bit 0 mask
.equ USART_DATA_0_bp = 0                 ; RX Data bit 0 position
.equ USART_DATA_1_bm = (1<<1)            ; RX Data bit 1 mask
.equ USART_DATA_1_bp = 1                 ; RX Data bit 1 position
.equ USART_DATA_2_bm = (1<<2)            ; RX Data bit 2 mask
.equ USART_DATA_2_bp = 2                 ; RX Data bit 2 position
.equ USART_DATA_3_bm = (1<<3)            ; RX Data bit 3 mask
.equ USART_DATA_3_bp = 3                 ; RX Data bit 3 position
.equ USART_DATA_4_bm = (1<<4)            ; RX Data bit 4 mask
.equ USART_DATA_4_bp = 4                 ; RX Data bit 4 position
.equ USART_DATA_5_bm = (1<<5)            ; RX Data bit 5 mask
.equ USART_DATA_5_bp = 5                 ; RX Data bit 5 position
.equ USART_DATA_6_bm = (1<<6)            ; RX Data bit 6 mask
.equ USART_DATA_6_bp = 6                 ; RX Data bit 6 position
.equ USART_DATA_7_bm = (1<<7)            ; RX Data bit 7 mask
.equ USART_DATA_7_bp = 7                 ; RX Data bit 7 position

; USART_RXDATAH masks
.equ USART_DATA8_bm = 0x01               ; Receiver Data Register bit mask
.equ USART_DATA8_bp = 0                  ; Receiver Data Register bit position
.equ USART_PERR_bm = 0x02                ; Parity Error bit mask
.equ USART_PERR_bp = 1                   ; Parity Error bit position
.equ USART_FERR_bm = 0x04                ; Frame Error bit mask
.equ USART_FERR_bp = 2                   ; Frame Error bit position
.equ USART_BUFOVF_bm = 0x40              ; Buffer Overflow bit mask
.equ USART_BUFOVF_bp = 6                 ; Buffer Overflow bit position
.equ USART_RXCIF_bm = 0x80               ; Receive Complete Interrupt Flag bit mask
.equ USART_RXCIF_bp = 7                  ; Receive Complete Interrupt Flag bit position

; USART_TXDATAL masks
; Masks for USART_DATA already defined

; USART_TXDATAH masks
; Masks for USART_DATA8 already defined

; USART_STATUS masks
.equ USART_WFB_bm = 0x01                 ; Wait For Break bit mask
.equ USART_WFB_bp = 0                    ; Wait For Break bit position
.equ USART_BDF_bm = 0x02                 ; Break Detected Flag bit mask
.equ USART_BDF_bp = 1                    ; Break Detected Flag bit position
.equ USART_ISFIF_bm = 0x08               ; Inconsistent Sync Field Interrupt Flag bit mask
.equ USART_ISFIF_bp = 3                  ; Inconsistent Sync Field Interrupt Flag bit position
.equ USART_RXSIF_bm = 0x10               ; Receive Start Interrupt bit mask
.equ USART_RXSIF_bp = 4                  ; Receive Start Interrupt bit position
.equ USART_DREIF_bm = 0x20               ; Data Register Empty Flag bit mask
.equ USART_DREIF_bp = 5                  ; Data Register Empty Flag bit position
.equ USART_TXCIF_bm = 0x40               ; Transmit Interrupt Flag bit mask
.equ USART_TXCIF_bp = 6                  ; Transmit Interrupt Flag bit position
; Masks for USART_RXCIF already defined

; USART_CTRLA masks
.equ USART_RS485_bm = 0x01               ; RS485 Mode internal transmitter bit mask
.equ USART_RS485_bp = 0                  ; RS485 Mode internal transmitter bit position
.equ USART_ABEIE_bm = 0x04               ; Auto-baud Error Interrupt Enable bit mask
.equ USART_ABEIE_bp = 2                  ; Auto-baud Error Interrupt Enable bit position
.equ USART_LBME_bm = 0x08                ; Loop-back Mode Enable bit mask
.equ USART_LBME_bp = 3                   ; Loop-back Mode Enable bit position
.equ USART_RXSIE_bm = 0x10               ; Receiver Start Frame Interrupt Enable bit mask
.equ USART_RXSIE_bp = 4                  ; Receiver Start Frame Interrupt Enable bit position
.equ USART_DREIE_bm = 0x20               ; Data Register Empty Interrupt Enable bit mask
.equ USART_DREIE_bp = 5                  ; Data Register Empty Interrupt Enable bit position
.equ USART_TXCIE_bm = 0x40               ; Transmit Complete Interrupt Enable bit mask
.equ USART_TXCIE_bp = 6                  ; Transmit Complete Interrupt Enable bit position
.equ USART_RXCIE_bm = 0x80               ; Receive Complete Interrupt Enable bit mask
.equ USART_RXCIE_bp = 7                  ; Receive Complete Interrupt Enable bit position

; USART_CTRLB masks
.equ USART_MPCM_bm = 0x01                ; Multi-processor Communication Mode bit mask
.equ USART_MPCM_bp = 0                   ; Multi-processor Communication Mode bit position
.equ USART_RXMODE_gm = 0x06              ; Receiver Mode group mask
.equ USART_RXMODE_gp = 1                 ; Receiver Mode group position
.equ USART_RXMODE_0_bm = (1<<1)          ; Receiver Mode bit 0 mask
.equ USART_RXMODE_0_bp = 1               ; Receiver Mode bit 0 position
.equ USART_RXMODE_1_bm = (1<<2)          ; Receiver Mode bit 1 mask
.equ USART_RXMODE_1_bp = 2               ; Receiver Mode bit 1 position
.equ USART_ODME_bm = 0x08                ; Open Drain Mode Enable bit mask
.equ USART_ODME_bp = 3                   ; Open Drain Mode Enable bit position
.equ USART_SFDEN_bm = 0x10               ; Start Frame Detection Enable bit mask
.equ USART_SFDEN_bp = 4                  ; Start Frame Detection Enable bit position
.equ USART_TXEN_bm = 0x40                ; Transmitter Enable bit mask
.equ USART_TXEN_bp = 6                   ; Transmitter Enable bit position
.equ USART_RXEN_bm = 0x80                ; Reciever enable bit mask
.equ USART_RXEN_bp = 7                   ; Reciever enable bit position

; USART_CTRLC masks
.equ USART_UCPHA_bm = 0x02               ; SPI Host Mode, Clock Phase bit mask
.equ USART_UCPHA_bp = 1                  ; SPI Host Mode, Clock Phase bit position
.equ USART_UDORD_bm = 0x04               ; SPI Host Mode, Data Order bit mask
.equ USART_UDORD_bp = 2                  ; SPI Host Mode, Data Order bit position
.equ USART_CHSIZE_gm = 0x07              ; Character Size group mask
.equ USART_CHSIZE_gp = 0                 ; Character Size group position
.equ USART_CHSIZE_0_bm = (1<<0)          ; Character Size bit 0 mask
.equ USART_CHSIZE_0_bp = 0               ; Character Size bit 0 position
.equ USART_CHSIZE_1_bm = (1<<1)          ; Character Size bit 1 mask
.equ USART_CHSIZE_1_bp = 1               ; Character Size bit 1 position
.equ USART_CHSIZE_2_bm = (1<<2)          ; Character Size bit 2 mask
.equ USART_CHSIZE_2_bp = 2               ; Character Size bit 2 position
.equ USART_SBMODE_bm = 0x08              ; Stop Bit Mode bit mask
.equ USART_SBMODE_bp = 3                 ; Stop Bit Mode bit position
.equ USART_PMODE_gm = 0x30               ; Parity Mode group mask
.equ USART_PMODE_gp = 4                  ; Parity Mode group position
.equ USART_PMODE_0_bm = (1<<4)           ; Parity Mode bit 0 mask
.equ USART_PMODE_0_bp = 4                ; Parity Mode bit 0 position
.equ USART_PMODE_1_bm = (1<<5)           ; Parity Mode bit 1 mask
.equ USART_PMODE_1_bp = 5                ; Parity Mode bit 1 position
.equ USART_CMODE_gm = 0xC0               ; Communication Mode group mask
.equ USART_CMODE_gp = 6                  ; Communication Mode group position
.equ USART_CMODE_0_bm = (1<<6)           ; Communication Mode bit 0 mask
.equ USART_CMODE_0_bp = 6                ; Communication Mode bit 0 position
.equ USART_CMODE_1_bm = (1<<7)           ; Communication Mode bit 1 mask
.equ USART_CMODE_1_bp = 7                ; Communication Mode bit 1 position

; USART_CTRLD masks
.equ USART_ABW_gm = 0xC0                 ; Auto Baud Window group mask
.equ USART_ABW_gp = 6                    ; Auto Baud Window group position
.equ USART_ABW_0_bm = (1<<6)             ; Auto Baud Window bit 0 mask
.equ USART_ABW_0_bp = 6                  ; Auto Baud Window bit 0 position
.equ USART_ABW_1_bm = (1<<7)             ; Auto Baud Window bit 1 mask
.equ USART_ABW_1_bp = 7                  ; Auto Baud Window bit 1 position

; USART_DBGCTRL masks
.equ USART_DBGRUN_bm = 0x01              ; Debug Run bit mask
.equ USART_DBGRUN_bp = 0                 ; Debug Run bit position

; USART_EVCTRL masks
.equ USART_IREI_bm = 0x01                ; IrDA Event Input Enable bit mask
.equ USART_IREI_bp = 0                   ; IrDA Event Input Enable bit position

; USART_TXPLCTRL masks
.equ USART_TXPL_gm = 0xFF                ; Transmit pulse length group mask
.equ USART_TXPL_gp = 0                   ; Transmit pulse length group position
.equ USART_TXPL_0_bm = (1<<0)            ; Transmit pulse length bit 0 mask
.equ USART_TXPL_0_bp = 0                 ; Transmit pulse length bit 0 position
.equ USART_TXPL_1_bm = (1<<1)            ; Transmit pulse length bit 1 mask
.equ USART_TXPL_1_bp = 1                 ; Transmit pulse length bit 1 position
.equ USART_TXPL_2_bm = (1<<2)            ; Transmit pulse length bit 2 mask
.equ USART_TXPL_2_bp = 2                 ; Transmit pulse length bit 2 position
.equ USART_TXPL_3_bm = (1<<3)            ; Transmit pulse length bit 3 mask
.equ USART_TXPL_3_bp = 3                 ; Transmit pulse length bit 3 position
.equ USART_TXPL_4_bm = (1<<4)            ; Transmit pulse length bit 4 mask
.equ USART_TXPL_4_bp = 4                 ; Transmit pulse length bit 4 position
.equ USART_TXPL_5_bm = (1<<5)            ; Transmit pulse length bit 5 mask
.equ USART_TXPL_5_bp = 5                 ; Transmit pulse length bit 5 position
.equ USART_TXPL_6_bm = (1<<6)            ; Transmit pulse length bit 6 mask
.equ USART_TXPL_6_bp = 6                 ; Transmit pulse length bit 6 position
.equ USART_TXPL_7_bm = (1<<7)            ; Transmit pulse length bit 7 mask
.equ USART_TXPL_7_bp = 7                 ; Transmit pulse length bit 7 position

; USART_RXPLCTRL masks
.equ USART_RXPL_gm = 0x7F                ; Receiver Pulse Lenght group mask
.equ USART_RXPL_gp = 0                   ; Receiver Pulse Lenght group position
.equ USART_RXPL_0_bm = (1<<0)            ; Receiver Pulse Lenght bit 0 mask
.equ USART_RXPL_0_bp = 0                 ; Receiver Pulse Lenght bit 0 position
.equ USART_RXPL_1_bm = (1<<1)            ; Receiver Pulse Lenght bit 1 mask
.equ USART_RXPL_1_bp = 1                 ; Receiver Pulse Lenght bit 1 position
.equ USART_RXPL_2_bm = (1<<2)            ; Receiver Pulse Lenght bit 2 mask
.equ USART_RXPL_2_bp = 2                 ; Receiver Pulse Lenght bit 2 position
.equ USART_RXPL_3_bm = (1<<3)            ; Receiver Pulse Lenght bit 3 mask
.equ USART_RXPL_3_bp = 3                 ; Receiver Pulse Lenght bit 3 position
.equ USART_RXPL_4_bm = (1<<4)            ; Receiver Pulse Lenght bit 4 mask
.equ USART_RXPL_4_bp = 4                 ; Receiver Pulse Lenght bit 4 position
.equ USART_RXPL_5_bm = (1<<5)            ; Receiver Pulse Lenght bit 5 mask
.equ USART_RXPL_5_bp = 5                 ; Receiver Pulse Lenght bit 5 position
.equ USART_RXPL_6_bm = (1<<6)            ; Receiver Pulse Lenght bit 6 mask
.equ USART_RXPL_6_bp = 6                 ; Receiver Pulse Lenght bit 6 position

; RS485 Mode internal transmitter select
.equ USART_RS485_DISABLE_gc = (0x00<<0)  ; RS485 Mode disabled
.equ USART_RS485_ENABLE_gc = (0x01<<0)   ; RS485 Mode enabled

; Receiver Mode select
.equ USART_RXMODE_NORMAL_gc = (0x00<<1)  ; Normal mode
.equ USART_RXMODE_CLK2X_gc = (0x01<<1)   ; CLK2x mode
.equ USART_RXMODE_GENAUTO_gc = (0x02<<1) ; Generic autobaud mode
.equ USART_RXMODE_LINAUTO_gc = (0x03<<1) ; LIN constrained autobaud mode

; Communication Mode select
.equ USART_CMODE_ASYNCHRONOUS_gc = (0x00<<6) ; Asynchronous Mode
.equ USART_CMODE_SYNCHRONOUS_gc = (0x01<<6) ; Synchronous Mode
.equ USART_CMODE_IRCOM_gc = (0x02<<6)    ; Infrared Communication
.equ USART_CMODE_MSPI_gc = (0x03<<6)     ; SPI Host Mode

; Character Size select
.equ USART_NORMAL_CHSIZE_5BIT_gc = (0x00<<0) ; Character size: 5 bit
.equ USART_NORMAL_CHSIZE_6BIT_gc = (0x01<<0) ; Character size: 6 bit
.equ USART_NORMAL_CHSIZE_7BIT_gc = (0x02<<0) ; Character size: 7 bit
.equ USART_NORMAL_CHSIZE_8BIT_gc = (0x03<<0) ; Character size: 8 bit
.equ USART_NORMAL_CHSIZE_9BITL_gc = (0x06<<0) ; Character size: 9 bit read low byte first
.equ USART_NORMAL_CHSIZE_9BITH_gc = (0x07<<0) ; Character size: 9 bit read high byte first

; Parity Mode select
.equ USART_NORMAL_PMODE_DISABLED_gc = (0x00<<4) ; No Parity
.equ USART_NORMAL_PMODE_EVEN_gc = (0x02<<4) ; Even Parity
.equ USART_NORMAL_PMODE_ODD_gc = (0x03<<4) ; Odd Parity

; Stop Bit Mode select
.equ USART_NORMAL_SBMODE_1BIT_gc = (0x00<<3) ; 1 stop bit
.equ USART_NORMAL_SBMODE_2BIT_gc = (0x01<<3) ; 2 stop bits

; Auto Baud Window select
.equ USART_ABW_WDW0_gc = (0x00<<6)       ; 18% tolerance
.equ USART_ABW_WDW1_gc = (0x01<<6)       ; 15% tolerance
.equ USART_ABW_WDW2_gc = (0x02<<6)       ; 21% tolerance
.equ USART_ABW_WDW3_gc = (0x03<<6)       ; 25% tolerance


;*************************************************************************
;** USERROW - User Row
;*************************************************************************


;*************************************************************************
;** VPORT - Virtual Ports
;*************************************************************************

; VPORT_INTFLAGS masks
.equ VPORT_INT_gm = 0xFF                 ; Pin Interrupt Flag group mask
.equ VPORT_INT_gp = 0                    ; Pin Interrupt Flag group position
.equ VPORT_INT_0_bm = (1<<0)             ; Pin Interrupt Flag bit 0 mask
.equ VPORT_INT_0_bp = 0                  ; Pin Interrupt Flag bit 0 position
.equ VPORT_INT_1_bm = (1<<1)             ; Pin Interrupt Flag bit 1 mask
.equ VPORT_INT_1_bp = 1                  ; Pin Interrupt Flag bit 1 position
.equ VPORT_INT_2_bm = (1<<2)             ; Pin Interrupt Flag bit 2 mask
.equ VPORT_INT_2_bp = 2                  ; Pin Interrupt Flag bit 2 position
.equ VPORT_INT_3_bm = (1<<3)             ; Pin Interrupt Flag bit 3 mask
.equ VPORT_INT_3_bp = 3                  ; Pin Interrupt Flag bit 3 position
.equ VPORT_INT_4_bm = (1<<4)             ; Pin Interrupt Flag bit 4 mask
.equ VPORT_INT_4_bp = 4                  ; Pin Interrupt Flag bit 4 position
.equ VPORT_INT_5_bm = (1<<5)             ; Pin Interrupt Flag bit 5 mask
.equ VPORT_INT_5_bp = 5                  ; Pin Interrupt Flag bit 5 position
.equ VPORT_INT_6_bm = (1<<6)             ; Pin Interrupt Flag bit 6 mask
.equ VPORT_INT_6_bp = 6                  ; Pin Interrupt Flag bit 6 position
.equ VPORT_INT_7_bm = (1<<7)             ; Pin Interrupt Flag bit 7 mask
.equ VPORT_INT_7_bp = 7                  ; Pin Interrupt Flag bit 7 position


;*************************************************************************
;** VREF - Voltage reference
;*************************************************************************

; VREF_DAC0REF masks
.equ VREF_REFSEL_gm = 0x07               ; Reference select group mask
.equ VREF_REFSEL_gp = 0                  ; Reference select group position
.equ VREF_REFSEL_0_bm = (1<<0)           ; Reference select bit 0 mask
.equ VREF_REFSEL_0_bp = 0                ; Reference select bit 0 position
.equ VREF_REFSEL_1_bm = (1<<1)           ; Reference select bit 1 mask
.equ VREF_REFSEL_1_bp = 1                ; Reference select bit 1 position
.equ VREF_REFSEL_2_bm = (1<<2)           ; Reference select bit 2 mask
.equ VREF_REFSEL_2_bp = 2                ; Reference select bit 2 position
.equ VREF_ALWAYSON_bm = 0x80             ; Always on bit mask
.equ VREF_ALWAYSON_bp = 7                ; Always on bit position

; VREF_ACREF masks
; Masks for VREF_REFSEL already defined
; Masks for VREF_ALWAYSON already defined

; Reference select
.equ VREF_REFSEL_1V024_gc = (0x00<<0)    ; Internal 1.024V reference
.equ VREF_REFSEL_2V048_gc = (0x01<<0)    ; Internal 2.048V reference
.equ VREF_REFSEL_4V096_gc = (0x02<<0)    ; Internal 4.096V reference
.equ VREF_REFSEL_2V500_gc = (0x03<<0)    ; Internal 2.500V reference
.equ VREF_REFSEL_VDD_gc = (0x05<<0)      ; VDD as reference
.equ VREF_REFSEL_VREFA_gc = (0x06<<0)    ; External reference on VREFA pin


;*************************************************************************
;** WDT - Watch-Dog Timer
;*************************************************************************

; WDT_CTRLA masks
.equ WDT_PERIOD_gm = 0x0F                ; Period group mask
.equ WDT_PERIOD_gp = 0                   ; Period group position
.equ WDT_PERIOD_0_bm = (1<<0)            ; Period bit 0 mask
.equ WDT_PERIOD_0_bp = 0                 ; Period bit 0 position
.equ WDT_PERIOD_1_bm = (1<<1)            ; Period bit 1 mask
.equ WDT_PERIOD_1_bp = 1                 ; Period bit 1 position
.equ WDT_PERIOD_2_bm = (1<<2)            ; Period bit 2 mask
.equ WDT_PERIOD_2_bp = 2                 ; Period bit 2 position
.equ WDT_PERIOD_3_bm = (1<<3)            ; Period bit 3 mask
.equ WDT_PERIOD_3_bp = 3                 ; Period bit 3 position
.equ WDT_WINDOW_gm = 0xF0                ; Window group mask
.equ WDT_WINDOW_gp = 4                   ; Window group position
.equ WDT_WINDOW_0_bm = (1<<4)            ; Window bit 0 mask
.equ WDT_WINDOW_0_bp = 4                 ; Window bit 0 position
.equ WDT_WINDOW_1_bm = (1<<5)            ; Window bit 1 mask
.equ WDT_WINDOW_1_bp = 5                 ; Window bit 1 position
.equ WDT_WINDOW_2_bm = (1<<6)            ; Window bit 2 mask
.equ WDT_WINDOW_2_bp = 6                 ; Window bit 2 position
.equ WDT_WINDOW_3_bm = (1<<7)            ; Window bit 3 mask
.equ WDT_WINDOW_3_bp = 7                 ; Window bit 3 position

; WDT_STATUS masks
.equ WDT_SYNCBUSY_bm = 0x01              ; Syncronization busy bit mask
.equ WDT_SYNCBUSY_bp = 0                 ; Syncronization busy bit position
.equ WDT_LOCK_bm = 0x80                  ; Lock enable bit mask
.equ WDT_LOCK_bp = 7                     ; Lock enable bit position

; Period select
.equ WDT_PERIOD_OFF_gc = (0x00<<0)       ; Off
.equ WDT_PERIOD_8CLK_gc = (0x01<<0)      ; 8 cycles (8ms)
.equ WDT_PERIOD_16CLK_gc = (0x02<<0)     ; 16 cycles (16ms)
.equ WDT_PERIOD_32CLK_gc = (0x03<<0)     ; 32 cycles (32ms)
.equ WDT_PERIOD_64CLK_gc = (0x04<<0)     ; 64 cycles (64ms)
.equ WDT_PERIOD_128CLK_gc = (0x05<<0)    ; 128 cycles (0.128s)
.equ WDT_PERIOD_256CLK_gc = (0x06<<0)    ; 256 cycles (0.256s)
.equ WDT_PERIOD_512CLK_gc = (0x07<<0)    ; 512 cycles (0.512s)
.equ WDT_PERIOD_1KCLK_gc = (0x08<<0)     ; 1K cycles (1.0s)
.equ WDT_PERIOD_2KCLK_gc = (0x09<<0)     ; 2K cycles (2.0s)
.equ WDT_PERIOD_4KCLK_gc = (0x0A<<0)     ; 4K cycles (4.1s)
.equ WDT_PERIOD_8KCLK_gc = (0x0B<<0)     ; 8K cycles (8.2s)

; Window select
.equ WDT_WINDOW_OFF_gc = (0x00<<4)       ; Off
.equ WDT_WINDOW_8CLK_gc = (0x01<<4)      ; 8 cycles (8ms)
.equ WDT_WINDOW_16CLK_gc = (0x02<<4)     ; 16 cycles (16ms)
.equ WDT_WINDOW_32CLK_gc = (0x03<<4)     ; 32 cycles (32ms)
.equ WDT_WINDOW_64CLK_gc = (0x04<<4)     ; 64 cycles (64ms)
.equ WDT_WINDOW_128CLK_gc = (0x05<<4)    ; 128 cycles (0.128s)
.equ WDT_WINDOW_256CLK_gc = (0x06<<4)    ; 256 cycles (0.256s)
.equ WDT_WINDOW_512CLK_gc = (0x07<<4)    ; 512 cycles (0.512s)
.equ WDT_WINDOW_1KCLK_gc = (0x08<<4)     ; 1K cycles (1.0s)
.equ WDT_WINDOW_2KCLK_gc = (0x09<<4)     ; 2K cycles (2.0s)
.equ WDT_WINDOW_4KCLK_gc = (0x0A<<4)     ; 4K cycles (4.1s)
.equ WDT_WINDOW_8KCLK_gc = (0x0B<<4)     ; 8K cycles (8.2s)




; ***** CPU REGISTER DEFINITIONS *****************************************
.def	XH	= r27
.def	XL	= r26
.def	YH	= r29
.def	YL	= r28
.def	ZH	= r31
.def	ZL	= r30


; ***** DATA MEMORY DECLARATIONS *****************************************


#define DATAMEM_START 0x0000
#define DATAMEM_SIZE 0x10000
#define DATAMEM_END (0x0000 + 0x10000 - 1)

#define IO_START 0x0000
#define IO_SIZE 0x103F
#define IO_END (0x0000 + 0x103F - 1)

#define LOCKBITS_START 0x1040
#define LOCKBITS_SIZE 0x0004
#define LOCKBITS_END (0x1040 + 0x0004 - 1)
#define LOCKBITS_PAGE_SIZE 0x04

#define FUSES_START 0x1050
#define FUSES_SIZE 0x0010
#define FUSES_END (0x1050 + 0x0010 - 1)
#define FUSES_PAGE_SIZE 0x08

#define USER_SIGNATURES_START 0x1080
#define USER_SIGNATURES_SIZE 0x0040
#define USER_SIGNATURES_END (0x1080 + 0x0040 - 1)
#define USER_SIGNATURES_PAGE_SIZE 0x40

#define SIGNATURES_START 0x1100
#define SIGNATURES_SIZE 0x0003
#define SIGNATURES_END (0x1100 + 0x0003 - 1)
#define SIGNATURES_PAGE_SIZE 0x80

#define PROD_SIGNATURES_START 0x1103
#define PROD_SIGNATURES_SIZE 0x007D
#define PROD_SIGNATURES_END (0x1103 + 0x007D - 1)
#define PROD_SIGNATURES_PAGE_SIZE 0x80

#define EEPROM_START 0x1400
#define EEPROM_SIZE 0x0200
#define EEPROM_END (0x1400 + 0x0200 - 1)
#define EEPROM_PAGE_SIZE 0x08

#define INTERNAL_SRAM_START 0x7000
#define INTERNAL_SRAM_SIZE 0x1000
#define INTERNAL_SRAM_END (0x7000 + 0x1000 - 1)

#define MAPPED_PROGMEM_START 0x8000
#define MAPPED_PROGMEM_SIZE 0x8000
#define MAPPED_PROGMEM_END (0x8000 + 0x8000 - 1)
#define MAPPED_PROGMEM_PAGE_SIZE 0x40

#define PROGMEM_START 0x0000
#define PROGMEM_SIZE 0x8000
#define PROGMEM_END (0x0000 + 0x8000 - 1)
#define PROGMEM_PAGE_SIZE 0x40

#define PROGMEM_NRWW_START 0x0000
#define PROGMEM_NRWW_SIZE 0x1000
#define PROGMEM_NRWW_END (0x0000 + 0x1000 - 1)
#define PROGMEM_NRWW_PAGE_SIZE 0x40

#define PROGMEM_RWW_START 0x1000
#define PROGMEM_RWW_SIZE 0x7000
#define PROGMEM_RWW_END (0x1000 + 0x7000 - 1)
#define PROGMEM_RWW_PAGE_SIZE 0x40


; Legacy definitions
.equ    FLASHSTART    = (PROGMEM_START / 2) ; Note: Word address
.equ    FLASHEND      = (PROGMEM_END / 2) ; Note: Word address
.equ    IOEND         = IO_END
.equ    SRAM_START    = INTERNAL_SRAM_START
.equ    SRAM_SIZE     = INTERNAL_SRAM_SIZE
.equ    RAMEND        = INTERNAL_SRAM_END
.equ    E2END         = EEPROM_END
.equ    EEPROMEND     = EEPROM_END


; Definitions used by the assembler
#pragma AVRPART MEMORY PROG_FLASH 0x8000
#pragma AVRPART MEMORY EEPROM 0x0200
#pragma AVRPART MEMORY INT_SRAM SIZE 0x1000
#pragma AVRPART MEMORY INT_SRAM START_ADDR 0x7000

; ***** INTERRUPT VECTORS, ABSOLUTE ADDRESSES ****************************
; NMI interrupt vectors
.equ NMI_vect = 0x0002                   ; 

; BOD interrupt vectors
.equ BOD_VLM_vect = 0x0004               ; 

; CLKCTRL interrupt vectors
.equ CLKCTRL_CFD_vect = 0x0006           ; 

; RTC interrupt vectors
.equ RTC_CNT_vect = 0x0008               ; 
.equ RTC_PIT_vect = 0x000A               ; 

; CCL interrupt vectors
.equ CCL_CCL_vect = 0x000C               ; 

; PORTA interrupt vectors
.equ PORTA_PORT_vect = 0x000E            ; 

; TCA0 interrupt vectors
.equ TCA0_LUNF_vect = 0x0010             ; 
.equ TCA0_OVF_vect = 0x0010              ; 
.equ TCA0_HUNF_vect = 0x0012             ; 
.equ TCA0_CMP0_vect = 0x0014             ; 
.equ TCA0_LCMP0_vect = 0x0014            ; 
.equ TCA0_CMP1_vect = 0x0016             ; 
.equ TCA0_LCMP1_vect = 0x0016            ; 
.equ TCA0_CMP2_vect = 0x0018             ; 
.equ TCA0_LCMP2_vect = 0x0018            ; 

; TCB0 interrupt vectors
.equ TCB0_INT_vect = 0x001A              ; 

; TCB1 interrupt vectors
.equ TCB1_INT_vect = 0x001C              ; 

; TWI0 interrupt vectors
.equ TWI0_TWIS_vect = 0x001E             ; 
.equ TWI0_TWIM_vect = 0x0020             ; 

; SPI0 interrupt vectors
.equ SPI0_INT_vect = 0x0022              ; 

; USART0 interrupt vectors
.equ USART0_RXC_vect = 0x0024            ; 
.equ USART0_DRE_vect = 0x0026            ; 
.equ USART0_TXC_vect = 0x0028            ; 

; PORTD interrupt vectors
.equ PORTD_PORT_vect = 0x002A            ; 

; AC0 interrupt vectors
.equ AC0_AC_vect = 0x002C                ; 

; ADC0 interrupt vectors
.equ ADC0_ERROR_vect = 0x002E            ; 
.equ ADC0_RESRDY_vect = 0x0030           ; 
.equ ADC0_SAMPRDY_vect = 0x0032          ; 

; AC1 interrupt vectors
.equ AC1_AC_vect = 0x0034                ; 

; PORTC interrupt vectors
.equ PORTC_PORT_vect = 0x0036            ; 

; TCB2 interrupt vectors
.equ TCB2_INT_vect = 0x0038              ; 

; USART1 interrupt vectors
.equ USART1_RXC_vect = 0x003A            ; 
.equ USART1_DRE_vect = 0x003C            ; 
.equ USART1_TXC_vect = 0x003E            ; 

; PORTF interrupt vectors
.equ PORTF_PORT_vect = 0x0040            ; 

; NVMCTRL interrupt vectors
.equ NVMCTRL_EEREADY_vect = 0x0042       ; 
.equ NVMCTRL_FLREADY_vect = 0x0042       ; 
.equ NVMCTRL_NVMREADY_vect = 0x0042      ; 

; USART2 interrupt vectors
.equ USART2_RXC_vect = 0x0044            ; 
.equ USART2_DRE_vect = 0x0046            ; 
.equ USART2_TXC_vect = 0x0048            ; 

; TCB3 interrupt vectors
.equ TCB3_INT_vect = 0x004A              ; 

; TCA1 interrupt vectors
.equ TCA1_LUNF_vect = 0x004C             ; 
.equ TCA1_OVF_vect = 0x004C              ; 
.equ TCA1_HUNF_vect = 0x004E             ; 
.equ TCA1_CMP0_vect = 0x0050             ; 
.equ TCA1_LCMP0_vect = 0x0050            ; 
.equ TCA1_CMP1_vect = 0x0052             ; 
.equ TCA1_LCMP1_vect = 0x0052            ; 
.equ TCA1_CMP2_vect = 0x0054             ; 
.equ TCA1_LCMP2_vect = 0x0054            ; 

; PORTE interrupt vectors
.equ PORTE_PORT_vect = 0x0056            ; 

; PORTB interrupt vectors
.equ PORTB_PORT_vect = 0x0058            ; 



; ***** INTERRUPT VECTORS, MODULE BASES **********************************

.equ NMI_vbase = 0x0002
.equ BOD_vbase = 0x0004
.equ CLKCTRL_vbase = 0x0006
.equ RTC_vbase = 0x0008
.equ CCL_vbase = 0x000C
.equ PORTA_vbase = 0x000E
.equ TCA0_vbase = 0x0010
.equ TCB0_vbase = 0x001A
.equ TCB1_vbase = 0x001C
.equ TWI0_vbase = 0x001E
.equ SPI0_vbase = 0x0022
.equ USART0_vbase = 0x0024
.equ PORTD_vbase = 0x002A
.equ AC0_vbase = 0x002C
.equ ADC0_vbase = 0x002E
.equ AC1_vbase = 0x0034
.equ PORTC_vbase = 0x0036
.equ TCB2_vbase = 0x0038
.equ USART1_vbase = 0x003A
.equ PORTF_vbase = 0x0040
.equ NVMCTRL_vbase = 0x0042
.equ USART2_vbase = 0x0044
.equ TCB3_vbase = 0x004A
.equ TCA1_vbase = 0x004C
.equ PORTE_vbase = 0x0056
.equ PORTB_vbase = 0x0058


; ***** INTERRUPT VECTORS, VECTOR OFFSETS ********************************

; NMI interrupt vector offsets

.equ NMI_voffset = 0

; BOD interrupt vector offsets

.equ BOD_VLM_voffset = 0

; CLKCTRL interrupt vector offsets

.equ CLKCTRL_CFD_voffset = 0

; RTC interrupt vector offsets

.equ RTC_CNT_voffset = 0
.equ RTC_PIT_voffset = 2

; CCL interrupt vector offsets

.equ CCL_CCL_voffset = 0

; PORTA interrupt vector offsets

.equ PORTA_PORT_voffset = 0

; TCA0 interrupt vector offsets

.equ TCA0_LUNF_voffset = 0
.equ TCA0_OVF_voffset = 0
.equ TCA0_HUNF_voffset = 2
.equ TCA0_CMP0_voffset = 4
.equ TCA0_LCMP0_voffset = 4
.equ TCA0_CMP1_voffset = 6
.equ TCA0_LCMP1_voffset = 6
.equ TCA0_CMP2_voffset = 8
.equ TCA0_LCMP2_voffset = 8

; TCB0 interrupt vector offsets

.equ TCB0_INT_voffset = 0

; TCB1 interrupt vector offsets

.equ TCB1_INT_voffset = 0

; TWI0 interrupt vector offsets

.equ TWI0_TWIS_voffset = 0
.equ TWI0_TWIM_voffset = 2

; SPI0 interrupt vector offsets

.equ SPI0_INT_voffset = 0

; USART0 interrupt vector offsets

.equ USART0_RXC_voffset = 0
.equ USART0_DRE_voffset = 2
.equ USART0_TXC_voffset = 4

; PORTD interrupt vector offsets

.equ PORTD_PORT_voffset = 0

; AC0 interrupt vector offsets

.equ AC0_AC_voffset = 0

; ADC0 interrupt vector offsets

.equ ADC0_ERROR_voffset = 0
.equ ADC0_RESRDY_voffset = 2
.equ ADC0_SAMPRDY_voffset = 4

; AC1 interrupt vector offsets

.equ AC1_AC_voffset = 0

; PORTC interrupt vector offsets

.equ PORTC_PORT_voffset = 0

; TCB2 interrupt vector offsets

.equ TCB2_INT_voffset = 0

; USART1 interrupt vector offsets

.equ USART1_RXC_voffset = 0
.equ USART1_DRE_voffset = 2
.equ USART1_TXC_voffset = 4

; PORTF interrupt vector offsets

.equ PORTF_PORT_voffset = 0

; NVMCTRL interrupt vector offsets

.equ NVMCTRL_EEREADY_voffset = 0
.equ NVMCTRL_FLREADY_voffset = 0
.equ NVMCTRL_NVMREADY_voffset = 0

; USART2 interrupt vector offsets

.equ USART2_RXC_voffset = 0
.equ USART2_DRE_voffset = 2
.equ USART2_TXC_voffset = 4

; TCB3 interrupt vector offsets

.equ TCB3_INT_voffset = 0

; TCA1 interrupt vector offsets

.equ TCA1_LUNF_voffset = 0
.equ TCA1_OVF_voffset = 0
.equ TCA1_HUNF_voffset = 2
.equ TCA1_CMP0_voffset = 4
.equ TCA1_LCMP0_voffset = 4
.equ TCA1_CMP1_voffset = 6
.equ TCA1_LCMP1_voffset = 6
.equ TCA1_CMP2_voffset = 8
.equ TCA1_LCMP2_voffset = 8

; PORTE interrupt vector offsets

.equ PORTE_PORT_voffset = 0

; PORTB interrupt vector offsets

.equ PORTB_PORT_voffset = 0



.equ INT_VECTORS_SIZE = 90 ; size in words


#endif /* _AVR32EA48DEF_INC_ */

; ***** END OF FILE ******************************************************



