# ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf

Source: [ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf](../vendor/ESS23-RS1020_Series_Bus_Integrated_Motor_Hardware_Manual.pdf)

Raw text extracted with PyMuPDF. Tables, figures and symbols may be
misordered or missing; verify against the original PDF. Page numbers
below are physical PDF pages, starting at 1.


## PDF page 1

```text
ESS23-RS Series
   Bus Type Integrated Stepper Motor
             User Manual
                       Version: V1.0





            ©2025 All Rights Reserved
Address: 15-4, #799 Hushan Road, Jiangning, Nanjing, China
                      Tel: 0086-2587156578
         Web: www.omc-stepperonline.com
               Sales: sales@stepperonline.com
          Support: technical@stepperonline.com
```


## PDF page 2

```text
                                                          ESS23-RS Series User Manual

                    Contents
Foreword..............................................................................................................................1
1 Overview ...........................................................................................................................2
  1.1 Product Description.................................................................................................... 2
  1.2 Feature........................................................................................................................2
  1.3 Applications.................................................................................................................2
2 Performance Indicators.................................................................................................. 3
  2.1 Electrical Features......................................................................................................3
  2.2 Working Environment................................................................................................. 3
3 Installation........................................................................................................................4
  3.1 Installation Dimensions...............................................................................................4
  3.2 Installation Requirements...........................................................................................4
4 Drive Port and Wiring......................................................................................................5
  4.1 Wiring Diagram ........................................................................................................... 5
  4.2 Port Definition............................................................................................................. 6
     4.2.1 Status light........................................................................................................... 6
     4.2.2 Input/output Port.................................................................................................. 6
     4.2.3 DIP Switch............................................................................................................6
     4.2.4 Power / Communication Terminal....................................................................... 7
  4.3 Input/Output Terminal Operation................................................................................7
  4.4 DIP Switch Setting ......................................................................................................8
5 Alarm Diagnosis............................................................................................................ 10
6 Version Histroy.............................................................................................................. 10
```


## PDF page 3

```text
                                                   ESS17-RS Series User Manual

Foreword

    Thank you for using our Bus type stepper motor drive.

    Before using this product, be sure to read the manual to learn the necessary safety information,
precautions, and operating methods.

Incorrect handling may lead to extremely serious consequences.

Statement

    This product is designed and manufactured without the ability to protect personal safety from
mechanical system threats. Users are advised to consider safety precautions during use to prevent
accidents caused by improper operation or product abnormalities.

   Due to product improvements, the contents of this manual are subject to change without notice.

    Our company will not be responsible for any modification of the product by the user.

When reading, please pay attention to the following signs in the manual:


         Notice: Remind you to pay attention to the main points in the text.


         Caution: Indicates that incorrect operation may result in personal injury and
               equipment damage.


The contents described in this user manual are only applicable to the following models:

              Model                            Motor length L(mm)

           ESS23-RS10                                 56

           ESS23-RS20                                 80





                                                    1
```


## PDF page 4

```text
                                                                 ESS23-RS Series User Manual

1 Overview

1.1 Product Description
   ESS23-RS series bus-type integrated stepper motor adds bus communication and single-axis
controller functions based on digital stepper drives and traditional digital close-loop stepper drives. The
bus communication adopts the MODBUS interface.
   The current control technology based on load can effectively reduce the heat of the motor and
prolong the service life of the motor. The driver's built-in alarm output signal is convenient for the upper
computer to monitor and control. The out-of-position alarm function ensures the safe operation of the
processing equipment.
1.2 Feature
      A new generation of 32-bit DSP technology, cost-effective, smooth, low noise and low vibration
      Bus-type drive can achieve long-distance reliable control, which effectively solve the problem
           of pulse loss in interference environment.
      Users can set the current through the bus, subdivision, lock current, control motor start and
          stop and inquire real-time status of the motor.
      Support position control, speed control and multi-position mode.
      4 photoelectric isolation programmable high-speed differential input interface, external signal
         can be used to control the motor start and stop.
      2 photoelectric isolated programmable output interface, output drive status and control signals.
      Smooth and precise current control, small motor heat.
      Excellent smoothness in low frequency and small microstep.
      Voltage: DC24-50V.
      Over-voltage, under-voltage, over-current protection.
1.3 Applications

    Mainly used in wire stripping machines, marking machines, cutting machines, plotters, medical
equipment and automation equipment and instruments.





                                                    2
```


## PDF page 5

```text
                                                                 ESS23-RS Series User Manual

2 Performance Indicators

2.1 Electrical Features

                                      ESS23-RS Series
        Spec.
                     Minimum value     Typical value    Maximum value       Unit
     Output current             1.0                       -                 4.0          A
      Input voltage            24              36              48           Vdc
   Logic input current         10              10              50        mA
   Logic input voltage                 -              24              24          V
    Pulse frequency           0                         -              200          kHz
  Insulation resistance        100                       -                         -        MΩ
2.2 Working Environment

     Cooling                                Cooling fin
                               Keep away from other heating equipment as far as
                    Environment     possible. Avoid dust, oil mist, corrosive gas, strong
                                            vibration, prohibit combustible gas and conductive dust     Working
   environment      Temperature   0℃~50℃
                       Humidity     40－90%RH(No dew)
                          Vibration     10~55Hz/0.15mm
     Storage
               -20℃~+80℃
   temperature





                                                    3
```


## PDF page 6

```text
                                                                 ESS23-RS Series User Manual

3 Installation

3.1 Installation Dimensions





                 Model              The length of motor L(mm)
              ESS23-RS10                       56
              ESS23-RS20                       80
                                     Installation dimensions(unit:mm)
3.2 Installation Requirements

   The ESS23-RS series bus type integrated stepper motor needs to be installed on a stable base, and
the cold air is circulated, which is beneficial to the heat dissipation of the motor. If the installation is not
smooth, the internal parts will vibrate and damage.

   The center axis of the motor rotation is required to be centered and cannot exceed the allowable
error range..

   The reliable operating temperature of the drive is usually 50℃, the motor operating temperature ≤
80 ℃.

       If necessary, install a fan near the drive to force the heat dissipation to ensure the drive work in a
reliable working temperature.





                                                    4
```


## PDF page 7

```text
                                                                 ESS23-RS Series User Manual

4 Drive Port and Wiring

4.1 Wiring Diagram





                                  Drive side wiring diagram


         Caution:

       The personnel involved in the wiring must have professional ability.
      Do not wire with electricity power on.
         Wiring after the installation is firmly finished.
      Do not wrongly connect + and – of power, input voltage should not exceed 50V.





                                                    5
```


## PDF page 8

```text
                                                                 ESS23-RS Series User Manual

4.2 Port Definition
4.2.1 Status light
     Color    Symbol    Name                         Function
                      Power supply
     Green   PWR              When powered on, keep the indicator light on;
                               indication
                                         Current is overcurrent, the indicator flashes once a
                                             cycle;
                           Alarm       In case of overvoltage, the indicator light will blink twice.     Red     ALM
                               indication    Under voltage, the indicator light flashes three times;
                              When the error is exceeded, the indicator light blinks
                                                  five times.

4.2.2 Input/output Port

         Port        Pin    Mark        Name                  Function
                    1      X0                                                                   Input terminal, signal power
                    2      X1      Single-ended input   supply 24V driver, support NPN
                                                     and PNP two wiring modes, port
                    3      X2               port           function support software
                                                                  modification                    4      X3
                                        Single-ended input   Single-ended input port Public
                    5    XCOM
                                  common       end
          1                    6      Y0                       The output terminal supports
          2                              Single-ended output  NPN and PNP connection
          3            7      Y1        common       modes, and the port function
          4                                                    supports software modification
          5
          6
          7                              Single-ended output                    8    YCOM                         Single-ended output public end
          8                           common


4.2.3 DIP Switch

       Terminal      Pin   Symbol       Name                  Function
                                                SW1: 120 terminal resistor
          55             1    SW1                                                                        effective bit
          44
          33             2    SW2
          22                                DIP switch          11             3    SW3
                                                     SW2-5: Drive address setting
                    4    SW4

                    5    SW5





                                                    6
```


## PDF page 9

```text
                                                                 ESS23-RS Series User Manual

4.2.4 Power / Communication Terminal

   Terminal      Pin    Symbol       Name                  Function
                1         B-
       1
                2       A+       2                             Communication
                                              Modbus communication cable
       3                                          port                3     RGND       4
       5           4      NC
       6
                5     GND
                                 Power terminal   DC: 24-50V
                6      +DC

4.3 Input/Output Terminal Operation

  Terminal hardware description

   ESS23-RS series drives provide 4 opto-isolated programmable input interfaces, compatible with
NPN wiring and PNP wiring.
    4 (X0-X3) programmable input signal and external control interface are isolated through optocoupler.
The drive is compatible with common cathode and common anode connection, as shown below. In order
to ensure that the drive optocoupler conduction is reliable, the controller requires to provide drive current
at least 10mA. The drive has been inserted with optocoupler current limiting resistor, Standard input
signal voltage is 24V.
   The level pulse width of the X0-X3 input needs to be greater than 10ms, otherwise the drive may not
respond properly. The X0-X3 timing diagram is shown below:





                                X0-X3 timing diagram

    Each time the drive is powered on, X0-X3 defaults to the unspecified state. At this time, the input
signal is invalid. The user can configure the X0-X3 input function through the bus.
   ESS23-RS series drives provide 2 optocoupler isolated output terminals, support NPN wiring and
PNP wiring, and can support high level and low level effective controllers.

   Signal interface wiring diagram





                                                    7
```


## PDF page 10

```text
                                                                 ESS23-RS Series User Manual





                               Input signal interface wiring diagram





                             Output signal electrical schematic

4.4 DIP Switch Setting

   The ESS23-RS series bus type stepper motor drives uses a 5-bit DIP switch to set the drive address
and terminating resistor. The details are as follows:





                                    DIP switch chart



                                                    8
```


## PDF page 11

```text
                                                                 ESS23-RS Series User Manual

   Drive Address Setting

   The user can use MODBUS bus to control up to 15 ESS23-RS series drives at the same time. The
drive communication address setting adopts 4-bit DIP switch, the address setting range is 0~15, where
address 0 is Reservedd for the system, when the drive address is set greater than 15. It needs to be set
and saved using the upper debugging software, and the SW2~SW5 switches must be all set to OFF. As
shown in the following table:

      SW5         SW4         SW3          SW2            Address
       OFF          OFF          OFF           OFF            Customize
       OFF          OFF          OFF          ON                1
       OFF          OFF         ON           OFF                2
                       ……
      ON         ON          ON           OFF               14
      ON         ON          ON          ON            factory data reset
Note 1: When all 4-bit DIP switches are set to ON, the drive is restored to factory defaults.
Note 2: When all 4-bit dip switches are turned OFF, the driver node address can be customized by the
host computer.

  Terminal Resistance Setting

   The user can use this bit to select whether the communication end is incorporated into the 120
terminating resistor. According to the use situation, in general, only the master station and the last slave
station need to connect 120Ω of terminating resistor. As shown in the following table:

             SW1                        120 terminal resistance selection bit
              OFF                                                 Invalid
             ON                                                 Valid





                                                    9
```


## PDF page 12

```text
                                                                 ESS23-RS Series User Manual

5 Alarm Diagnosis

   ESS23-RS series drive has 4 kinds of alarm information, the alarm indicator flashing several times
according to the alarm code, the specific alarm code and treatment as shown in the following table.
    Alarm code     Alarm message                    Indicator                  Reset
                                                                            Lock motor /
                     Overcurrent or short
      Err1: 0x01                                                                   re-power to
                         circuit between phases
                                                                                              reset
                                                                            Lock motor /
                 Power supply voltage
      Err2: 0x02                                                                              reset
                            high
                                                                                         automatically
                                                                            Lock motor /
                 Power supply voltage
      Err3: 0x03                                                                              reset
                           low
                                                                                         automatically
                                                                          Re-power to
      Err5: 0x05       Out of tolerance
                                                                                              reset

6 Version Histroy

     Version                Descrition               Time             Remark
       V1.0                     First edition              2025/04/15





                                                    10
```
