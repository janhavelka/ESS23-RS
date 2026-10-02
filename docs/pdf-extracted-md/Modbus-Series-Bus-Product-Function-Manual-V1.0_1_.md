# Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf

Source: [Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf](../vendor/Modbus-Series-Bus-Product-Function-Manual-V1.0_1_.pdf)

Raw text extracted with PyMuPDF. Tables, figures and symbols may be
misordered or missing; verify against the original PDF. Page numbers
below are physical PDF pages, starting at 1.


## PDF page 1

```text
  Modbus Series
Bus type driver product Features Manual





                ©2025 All Rights Reserved
      Address: 15-4, #799 Hushan Road, Jiangning, Nanjing, China
                           Tel: 0086-2587156578
             Web: www.omc-stepperonline.com
                     Sales: sales@stepperonline.com
               Support: technical@stepperonline.com
```


## PDF page 2

```text
                                                         Modbus Series Bus Driver Function Manual

                     Table of contents
Preface ................................................................................................................................1
1 Modbus/RTU Protocol Definition...................................................................................2
2 Modbus/RTU Hardware Interface.................................................................................. 3
  2.1 Wiring method...................................................................................................................................... 3
  2.2 Twenty two Node Settings................................................................................................................... 3
  2.3 Twenty three Communication parameter settings...............................................................................4
3 Modbus/RTU protocol.................................................................................................... 4
  3.1 Message Format...................................................................................................................................4
  3.2 Function code introduction...................................................................................................................5
     3.2.1 Read Holding Register Command 03.................................................................................................5
     3.2.2 Write Single Register Command 06...................................................................................................5
     3.2.3 Write multiple registers command 10.............................................................................................. 6
  3.3 CRC Checksum Algorithm ................................................................................................................... 6
  3.4 Communication error code...................................................................................................................8
4 Motion control function................................................................................................11
  4.1 Positioning movement........................................................................................................................12
  4.2 Speed Movement............................................................................................................................... 15
  4.3 Return to zero function .......................................................................................................................17
  4.4 Multi-stage position control................................................................................................................ 18
  4.5 Multi- speed modes............................................................................................................................21
  4.6 Stop and emergency stop commands............................................................................................... 23
5 Other functions.............................................................................................................24
  5.1 Auxiliary control instructions.............................................................................................................. 24
  5.2 Input and output terminals..................................................................................................................25
  5.3 Software limit function ........................................................................................................................ 26
  5.4 Interrupt fixed length function.............................................................................................................27
  5.5 Setting the endian mode of high and low registers........................................................................... 27
6 Revision History........................................................................................................... 29
Appendix 1: Introduction to the method of returning to the origin.............................30
Appendix 2: Modbus Register Parameter Table - ESS-RS Series...............................66
Appendix 3: Modbus register parameter table – DM-PR series...................................78
```


## PDF page 3

```text
                                                         Modbus Series Bus Driver Function Manual

Preface

Thank you for using our Modbus series bus type driver .

Before using this product, please be sure to read this manual carefully to understand the necessary
safety information , precautions, and operating methods.

Incorrect operation may lead to extremely serious consequences.

statement

The design and manufacture of this product are not capable of protecting personal safety from threats
from mechanical systems. Please consider safety protection measures during the design and
manufacture of mechanical systems to prevent accidents caused by improper operation or product
abnormalities.

Due to product improvements, the manual contents may be changed without prior notice.

Our company will not be responsible for any modification of the product by the user.

When reading, please pay attention to the following signs in the manual:


           NOTE: Draws your attention to important points in the text.


           CAUTION: Indicates that incorrect operation may result in personal injury and
  equipment damage.

The contents described in this user manual are only applicable to the
following models:

                                      Model
                                ESS17-RSx1 Series
                                ESS23-RSx1 Series
                             DM556PR Series
                             DM860PR Series





                                                 1
```


## PDF page 4

```text
                                                         Modbus Series Bus Driver Function Manual
1 Modbus/RTU Protocol Definition

   Modbus is a serial communication protocol that was published by Modicon (now Schneider
Electric) in 1979 for communication with programmable logic controllers (PLCs). Modbus has
become the de facto industry standard for communication protocols and is now a common way to
connect industrial electronic devices. The main reasons why Modbus is more widely used than other
communication protocols are: it is publicly published and has no copyright requirements; it is easy to
deploy and maintain; there are not many restrictions on suppliers to modify local bits or bytes;

   The Modbus protocol is a master/slave protocol. There is one node that is the master node, and
the other nodes that use the Modbus protocol to participate in the communication are slave nodes.
Each slave device has a unique address. In the serial and MB+ network, only the node designated
as the master node can initiate a command.





                                                 2
```


## PDF page 5

```text
                                                         Modbus Series Bus Driver Function Manual
2 Modbus/RTU Hardware Interface

2.1 Wiring method

   The Modbus series products communicate as Modbus devices through the serial RS485
physical layer, allowing multiple devices to be connected on the same network.
    RS485 uses differential signals to complete communication transmission. Ordinary twisted pair
cables can be used in low-speed, short-distance, and interference-free situations. In high-speed,
long-line transmission, RS485 dedicated cables with impedance matching (generally 120Ω) must be
used. In environments with harsh interference, armored twisted pair shielded cables should also be
used.
   When using the RS485 interface, for a specific transmission line, the maximum cable length
allowed for data signal transmission from the RS485 interface to the load is inversely proportional to
the baud rate of the signal transmission. This length data is mainly affected by factors such as signal
distortion and noise. During the transmission process, the signal can be amplified by adding relays,
and up to eight relays can be added.
   RS485 has two interface types: two-wire and four-wire. This series of products all use two-wire
interface.
    The two-wire half-duplex connection method is shown in the figure:
            Host GND

            Host R-(B-)
                                              120Ω
            Host R+(A+)




                               A+ B- GND       A+ B- GND       A+ B- GND

                                Driver1         Driver2        Driver3

   When working in high-speed, long-line, and multi-node conditions, a 120-ohm impedance matching
resistor must be connected to the differential communication line of the last slave in the network. The
Modbus series driver products all have integrated 120-ohm resistors, and the validity can be selected by the
dial code .

2.2 Twenty two Node Settings

    Each slave node in the Modbus network needs to be set to a different address so that the master
node can complete device addressing.
   Modbus series products generally have 4-bit (up to 15 node addresses) or 5-bit (up to 31 node
addresses) address dip switches specifically used for node settings.
    At the same time, you can also customize the slave node configuration through Modbus: 0013h register.
Note that 0013h is only effective when all address dip switches are OFF.





                                                 3
```


## PDF page 6

```text
                                                         Modbus Series Bus Driver Function Manual

※ Modbus: 0013h register
   Register               name                                        illustrate   Address
                           0 ~ 255 : custom slave address;
             Custom drive
   0x0013                     This register takes effect when all the drive address DIP
              node number
                               switches are OFF;

2.3 Twenty three Communication parameter settings
   Modbus network communication parameters mainly include: baud rate, data bit, stop bit, and check
bit.
   Modbus network can set different communication parameters for communication. All nodes in
the same network must be set to the same baud rate, data bit, stop bit, and check bit.
   Modbus series products generally use the baud rate DIP switch to set the baud rate of the node,
and the default configuration is 8 data bits, 1 stop bit, and no parity bit.
    For products without baud rate dial, you can also customize the slave node communication
parameters through Modbus: 0014h~0015h registers. After modifying the customized communication
baud rate, it will take effect after powering on again.

※ Modbus: 0014h~0015h register
   Register
               name                                        illustrate   Address

                              0:115200
                            1 : 38400
                 Customize                            2 : 19200   0x0014     communication
                baud rate    3 : 9600
                                 Note: After modification, power must be turned on again to take
                                        effect;
                            0 : 8 -bit data, no parity, 1 stop bit;
                            1 : 8 -bit data, no parity, 2 stop bits;
                  Serial port data  2 : 8 -bit data, even parity, 1 stop bit;   0x0015                    format                            3 : 8 -bit data, odd parity, 1 stop bit;
                                 Note: After modification, power must be turned on again to take
                                        effect;




3 Modbus/RTU protocol
3.1 Message Format

Modbus/RTU message is a data frame, and its format is shown in the following table:

                                                 4
```


## PDF page 7

```text
                                                         Modbus Series Bus Driver Function Manual

    Device Address         Function code         Data Format          CRC Check
        8 bits               8 bits           N * 8 bits              16 bit

For detailed message formats of each function code, see Section 3.2.
3.2 Function code introduction
3.2.1 Read Holding Register Command 03

➢  Master->Slave Data

     01   03  00 23  00 01  75 C0
        Device  Function  Register  Read register CRC verify
        address  code    address     count

    The host sends a command to query the positioning motion speed register to the slave.

    Slave->Master data:

      01   03    02    00 3C   B8 55
           Device   Function  Return bytes   Register value  CRC verify
           address   code

    The slave returns the maximum speed register value of 60.

➢  read holding register commands are as follows :

    Query the current position (0x00 0A~0x000B ) and current speed (0x00 0C).

    Master->Slave data: 01 03 00 0A 00 0 3 25 C9

    Slave->Host data: 01 03 0 6 00 0 0 13 88 00 0 0 A5 D B ( current position 5000,
     current speed 0 )




              Note: The maximum number of queries must not exceed 16 registers.

3.2.2 Write Single Register Command 06
➢  Master->Slave Data

      01    06    00 23   00 3C   78 11
           Device   Function       Register       Write data    CRC verify

                                                 5
```


## PDF page 8

```text
                                                         Modbus Series Bus Driver Function Manual

           address    code        address

    The host writes a value of 60 to the positioning motion speed register of the slave .
After receiving the command, the slave returns the same command for confirmation.
    Slave->Master data:
       01    06   00 23   00 3C   78 11
              Device   Function    Register      Write data    CRC verify
              address    code      address
➢  Other examples of writing a single register command are as follows :

    Set the acceleration time register to 500ms:

    Master->Slave data: 01 06 00 21 01 F4 D9 D7

    Slave->Host data: 01 06 00 21 01 F4 D9 D7
3.2.3 Write multiple registers command 10


 01       10      00   24     00   02      04       00   00    13   88     B9  56

address   Function         Origin      Number of     Number of        Write            Write       CRC
           code         address          writes        bytes             content         content         check

   An example of a write multiple registers command is as follows:
    The host writes two registers to the slave, setting the pulse number high register and the pulse
number low register respectively.

    Master->Slave data: 01 10 00 24 00 02 04 00 00 13 88 FD 12

    Slave->Host data: 01 10 00 24 00 02 01 C3
3.3 CRC Checksum Algorithm

    The CRC area is 2 bytes, containing a 16-bit binary data. The sending device calculates the
CRC value and attaches the calculated value to the information. When receiving the information, the
receiving device recalculates the CRC value and compares the calculated value with the actual value
received in the CRC area. If the two are different, an error occurs.

    At the beginning of CRC, all 16 bits of the register are set to "1", and then the data of two adjacent
8-bit bytes are placed in the current register. Only the 8-bit data of each character is used to generate
CRC, and the start bit, stop bit and parity bit are not added to the CRC.





                                                 6
```


## PDF page 9

```text
                                                         Modbus Series Bus Driver Function Manual

    During CRC generation, every 8 bits of data are XORed with the value in the register, the result
is shifted one position to the right (towards the LSB), and the MSB is filled with "0". The LSB is tested.
If the LSB is "1", it is XORed with the preset fixed value . If the LSB is "0", no XOR operation is
performed.
    Repeat the above process until it shifts 8 times. After the 8th shift is completed, the next 8 bits of
data are XORed with the current value of the register . After all information is processed, the final
value in the register is the CRC value.

※ CRC check routine


 Uint16 CRC ( Uint16 *tempMsg, Uint16 tempLength)
 {
    //Here CRC check is a query table
    Uint16 i=0;
     Uint16 tempCrcHigh = 0xFF;
     Uint16 tempCrcLow = 0xFF;
     Uint16 tempIndex=0;
      for (i = 0; i < tempLength; i++)
 {
 tempIndex = tempCrcLow ^ (tempMsg[i]); tempCrcLow =
 tempCrcHigh ^ (CRCVALUE[tempIndex] >>8); tempCrcHigh
 =CRCVALUE[tempIndex]&0xff;
 }
 return (tempCrcHigh | (tempCrcLow<<8));
 }
 const Uint16 CRCVALUE[]=

 {
  0x0000,0xC1C0,0x81C1,0x4001,0x01C3,0xC003,0x8002,0x41C2,0x01C6,0xC
 006,0x8007,0x41C7,
  0x0005,0xC1C5,0x81C4,0x4004,0x01CC,0xC00C,0x800D,0x41CD,0x000F,0xC
 1CF,0x81CE,0x400E,
  0x000A,0xC1CA,0x81CB,0x400B,0x01C9,0xC009,0x8008,0x41C8,0x01D8,0xC
 018,0x8019,0x41D9,
  0x001B,0xC1DB,0x81DA,0x401A,0x001E,0xC1DE,0x81DF,0x401F,0x01DD,0xC
 01D,0x801C,0x41DC,
  0x0014,0xC1D4,0x81D5,0x4015,0x01D7,0xC017,0x8016,0x41D6,0x01D2,0xC
 012,0x8013,0x41D3,
  0x0011,0xC1D1,0x81D0,0x4010,0x01F0,0xC030,0x8031,0x41F1,0x0033,0xC
 1F3,0x81F2,0x4032,
  0x0036,0xC1F6,0x81F7,0x4037,0x01F5,0xC035,0x8034,0x41F4,0x003C,0xC


                                                 7
```


## PDF page 10

```text
                                                         Modbus Series Bus Driver Function Manual

 1FC,0x81FD,0x403D,
  0x01FF,0xC03F,0x803E,0x41FE,0x01FA,0xC03A,0x803B,0x41FB,0x0039,0xC
 1F9,0x81F8,0x4038,
  0x0028,0xC1E8,0x81E9,0x4029,0x01EB,0xC02B,0x802A,0x41EA,0x01EE,0xC
 02E,0x802F,0x41EF,
  0x002D,0xC1ED,0x81EC,0x402C,0x01E4,0xC024,0x8025,0x41E5,0x0027,0xC
 1E7,0x81E6,0x4026,
  0x0022,0xC1E2,0x81E3,0x4023,0x01E1,0xC021,0x8020,0x41E0,0x01A0,0xC
 060,0x8061,0x41A1,
  0x0063,0xC1A3,0x81A2,0x4062,0x0066,0xC1A6,0x81A7,0x4067,0x01A5,0xC
 065,0x8064,0x41A4,
  0x006C,0xC1AC,0x81AD,0x406D,0x01AF,0xC06F,0x806E,0x41AE,0x01AA,0xC
 06A,0x806B,0x41AB,
  0x0069,0xC1A9,0x81A8,0x4068,0x0078,0xC1B8,0x81B9,0x4079,0x01BB,0xC
 07B,0x807A,0x41BA,
  0x01BE,0xC07E,0x807F,0x41BF,0x007D,0xC1BD,0x81BC,0x407C,0x01B4,0xC
 074,0x8075,0x41B5,
  0x0077,0xC1B7,0x81B6,0x4076,0x0072,0xC1B2,0x81B3,0x4073,0x01B1,0xC
 071,0x8070,0x41B0,
  0x0050,0xC190,0x8191,0x4051,0x0193,0xC053,0x8052,0x4192,0x0196,0xC
 056,0x8057,0x4197,
  0x0055,0xC195,0x8194,0x4054,0x019C,0xC05C,0x805D,0x419D,0x005F,0xC
 19F,0x819E,0x405E,
  0x005A,0xC19A,0x819B,0x405B,0x0199,0xC059,0x8058,0x4198,0x0188,0xC
 048,0x8049,0x4189,
  0x004B,0xC18B,0x818A,0x404A,0x004E,0xC18E,0x818F,0x404F,0x018D,0xC
 04D,0x804C,0x418C,
  0x0044,0xC184,0x8185,0x4045,0x0187,0xC047,0x8046,0x4186,0x0182,0xC
 042,0x8043,0x4183,
  0x0041,0xC181,0x8180,0x4040 };

3.4 Communication error code

➢ CRC check error

       If an error occurs during data transmission, and the CRC check value calculated by the slave
device for a frame of data is not 85 C0 , the slave device discards the frame of data and does not
return any data.

    Master->Slave data: 01 03 00 20 00 01 85 C1

    Slave->Master data: 01 83 01 80 F0





                                                 8
```


## PDF page 11

```text
                                                         Modbus Series Bus Driver Function Manual


➢  Instruction code error

       If the function code requested by the host is not 03 , 06 or 10 , the device returns exception code
02 .

    Master->Slave data: 01 02 00 00 00 04 79 C9

    Slave->Master data: 01 82 02 61 C1

➢  Invalid data address

        If the data address requested by the host is illegal, the device returns exception code
03.

    Host->Slave data: 01 03 00 1 C 00 01 4 5 C C

    Slave->Master data: 01 83 03 01 31

    Register address 0x001 C is empty and the device returns exception code 03.

➢  Out of address range

    Host- > Slave data: 01 06 FF 00 0B 580B

    Slave- > Master data: 01 86 04 43 A3
    Register address 0 Xff 00 is beyond the register address definition range. The device returns
exception code 04.

➢  Read address overflow

       If the data requested by the host exceeds the range of one read, the device returns the exception
code 05. For details of the exception code 05, please refer to the table ※
MODBUS exception code .

    Master->Slave data: 01 03 00 20 00 20 45 D8

    Slave->Master data: 01 83 05 81 33

    Reading 32 data at a time exceeds the range and returns exception code 05

➢   Illegal read and write errors

    Function code read and write attributes are divided into three types: read-only, writeonly, and read-
write. For operations that do not conform to the function code attributes, exception code 06 will be
reported.

    Master->Slave data: 01 03 00 27 00 01 34 01

    Slave->Host data: 01 83 06 C1 32
    Function code 0x27 is a write-only function code, and its read operation reports exception code 06.

➢  Error writing content

    The content of the written function code exceeds its specified range.


                                                 9
```


## PDF page 12

```text
                                                         Modbus Series Bus Driver Function Manual

    Master->Slave data: 01 06 00 2 7 FF FF 38 71
    Slave->Master data: 01 86 07 03 A2

If the write function code is out of range, the exception code 07 is returned.

※ MODBUS abnormal code

  Code       name                         meaning

    01   CRC check error     CRC check error.

                            The slave receives a function code other than 03 and
    02    Instruction code error    06.

          Function code address
    03    error                The received data address is not allowed by the slave.

         Exceeded function     The received data address exceeds the function code
    04   code address           range.

        Read function code    A maximum of 16 function codes can be read at one
    05   number overflow         time.

                                  Function code read and write attributes are divided into
             Illegal function code     three types: read-only, write-only, and read-write.
    06    reading and writing      Operations that do not conform to the function code
            error                      attributes will be abnormally incorrect.

          Function code writing   Data outside the specified range is written to the
    07    error                     function code.





                                                10
```


## PDF page 13

```text
                                                         Modbus Series Bus Driver Function Manual
4 Motion control function

   Modbus series bus drivers can realize 5 single-axis motion control functions, including:

    ➢Positioning movement

   ➢Speed Movement

   ➢Zero return function

    ➢Multi-stage position control
    ➢Multi-speed control
     there are two control modes for the three functions of positioning motion, speed control
and zero return:
     1) Control is achieved through Modbus: 0027h motion control command register;

     2) Control is achieved through external input signals;
    The multi-stage position control and multi-stage speed control functions can only be controlled by
external input signals.
    The driver integrates several input signals, and its functions can be configured through Modbus:
0041h~0047h registers.

    This section describes in detail how the above five functions work.

※ Modbus: 0027h motion control command register
         Register Address                         name                                illustrate

                                                  Bit0: Position mode start command bit;
                                                      0; invalid;
                                                      1: Valid;
                                                  Bit1: Speed mode start command bit;
                                                      0; invalid;
                                                      1: Valid;
                                                  Bit2: Position mode positioning method;
                                                      0: relative positioning;
                            Motion Control    1: absolute positioning;
            0x0027        Command     Bit3: Sports mode switching method;
                                Register       0: Ignore motion commands;
                                                      1: interrupt the current motion and execute it
                                                immediately;
                                                  Bit4: Return to origin start command bit;
                                                      0; invalid;
                                                      1: Valid;
                                                  Bit8: stop command bit;
                                                      0; invalid;
                                                      1: Valid;


                                                11
```


## PDF page 14

```text
                                                         Modbus Series Bus Driver Function Manual

                                                  Bit9: Emergency stop command bit;
                                                      0; invalid;
                                                      1: Valid;

※ Modbus: 0041h~0047h input terminal function configuration register
          Register
                    name                                    illustrate         Address

                                  0 : undefined;
                                  1 : origin signal;
                                  2 : Positive limit signal;
                                  3 : Anti-limit signal;
                                  4 : Motor MF signal;
                                  5 : Stop signal;
                                  6 : Emergency stop signal;
                                  7 : Position mode movement;
                         Input terminal          0x004                          8: Speed mode movement;
                             function
        1~0x0047                  9 : JOG+ point motion;                            selection
                                  10 : JOG -point movement;
                                  11 : Return to origin enable signal;
                                  12 : PT trigger signal ;
                                         13: PV trigger signal;
                                  14 : PIN0;
                                  15 : PIN1;
                                  16 : PIN2;
                                  17 : PIN3;
4.1 Positioning movement

    The positioning motion mode is implemented using a trapezoidal acceleration and deceleration
curve. Users can achieve precise position control by setting several parameters, including
acceleration time, deceleration time, running speed, and target total pulse number.

    Note: The driver determines the rotation direction of the motor by judging the positive or negative
of the total number of target pulses . When the total number of pulses is positive, the motor is defined
as forward rotation, and when the total number of pulses is negative, the motor is defined as reverse
rotation. The trapezoidal acceleration and deceleration curve is shown in the figure below.





                                                12
```


## PDF page 15

```text
                                                         Modbus Series Bus Driver Function Manual

                                                     Max speed(r/min)
                Speed(r/min)




                      initial
             velocityr/min)


                                      Acceleration                        Deceleration
                                    time(ms)                          time(ms)
                                       Total pulse count（A）               time(S)

                     Position mode acceleration and deceleration curve

   When the total number of pulses set by the user is small, the motor may need to decelerate
before accelerating to the maximum speed (that is, the motor does not accelerate to the maximum
speed set by the user during actual operation). The speed curve is shown in the figure below. The
solid line in the figure shows the actual operation curve of the motor, and the dotted line is the curve
required to accelerate to the set maximum speed. The theoretical total number of pulses is the
minimum total number of pulses calculated according to the user-set parameters (maximum speed,
acceleration time, deceleration time). When the total number of pulses set by the user is less than
the theoretical total number of pulses, the motor will run according to the solid line in the figure below.
                                                                    Set maximum
                                                            speed(r/min)                       Speed(r/mi
                              n)
                                                                           Actual maximum
                                                                          speed(r/min)

                            Initial
                   velocity(r/min)

                                           Acceleration               Deceleration
                                           time                     time

                                                                                                   time(S)                                          Set the total number of pulses（a）

                                             Total number of theoretical pulses（a）

         Position mode acceleration and deceleration curve (not accelerated to the set

                            maximum speed)

    The relevant motion control parameter registers are:

           Register
                           name                                  illustrate          Address

           0x0021              Acceleration time         Acceleration time , unit: ms ;

           0x0022              Deceleration time         Deceleration time , unit: ms ;

                                               Movement speed during positioning
           0x0023         Positioning movement speed   movement, unit: r/min;



                                                13
```


## PDF page 16

```text
                                                         Modbus Series Bus Driver Function Manual

                                                         Target pulse value, unit: pulse; When
           0x0024           Target pulse value high                                                       the value is positive, the motor
                                           moves in the positive

           0x0025           Target pulse value low       direction, and when the value is
                                                         negative, the motor moves in the
                                                      negative direction .

  ※ Control is completed through Modbus: 0027h motion control command register

    Control is completed through 0027h : Bit0, Bit2, Bit3 .
         0027h                 Order                                     illustrate

                        Position mode start command bit   When this bit is written as 1, a start
                                          command is generated and the drive             Bit0       0; invalid;                                                                 starts moving according to the given
                         1: Start;                         motion parameters;

                        Position mode positioning method
                                                        This bit indicates whether the given
             Bit2       0: relative positioning;               target position is a relative position
                                                            or an absolute position;
                         1: absolute positioning;

                   The current exercise is not
                                              The current drive is in motion.                      completed, how to switch the
                                                        This bit is used to set to ignore the                       exercise mode
                                           new given motion command or
             Bit3       0: Ignore the new given motion      interrupt the current motion and
                                                      immediately execute the new given                 command;
                                                     motion command. This bit is only
                         1: interrupt the current motion and  valid in positioning mode ;
                    execute it immediately;

    Example:
   move forward relative to the specified parameters for 1000 pulses (starting speed 10r/min,
acceleration time 100ms, deceleration time 100ms, maximum speed 60 /min) .
    Step 1: Write parameters: acceleration time 100ms , deceleration time 100ms , running speed 60
/min , set total pulse number 1000.

    Host->Slave: 01 10 00 2 1 00 0 5 0 A 00 64 00 64 00 3C 00 00 00 03 E8 98 EA

    Slave->Host: 01 10 00 2 1 00 0 5 50 00

    Step 2 : Relative position mode start command

    Host -> Slave: 01 06 00 27 00 01 F8 01

    Slave->Host: 01 06 00 27 00 01 F8 01

  ※ Control is completed through external input terminals



                                                14
```


## PDF page 17

```text
                                                         Modbus Series Bus Driver Function Manual

    Any input terminal of the driver and set its function to 7: position motion mode through the
corresponding function setting register . When a valid level signal is given to this terminal, the
driver completes the action according to the given motion parameters.

            Register
                          name                                  illustrate          Address

            0x004          Input terminal function
                                     selection            7: Position mode movement;          1~0x0047
4.2 Speed Movement

    The speed motion mode is shown in the figure below. Unlike the positioning motion mode, the
speed mode only requires the setting of three parameters: running speed, acceleration time, and
deceleration time. After the motor accelerates to the maximum speed according to these three
parameter settings, it runs at a constant speed at the maximum speed.
    Note: The positive and negative values of the running speed register are absolutely related to
the forward and reverse rotation of the motor. When the maximum speed register is positive, the
motor is defined as forward rotation. When the maximum speed register is negative, the motor is
defined as reverse rotation.


          speed(r/min)                                            Maximum speed(r/min)



            Initial
     velocity(r/min)


                                Acceleration
                              time(ms)

                                                                                            time(S)

                     Speed mode acceleration curve

  The relevant motion control parameter registers are:

          Register
                      name                                    illustrate         Address

                                          Jog mode running speed, unit: r/min;
                                 When the value is positive, the motor moves
                      Jog mode operation    in the positive direction, and when the value         0x001D
                           speed            is negative, the motor moves in the negative
                                                direction .

                     Jog mode acceleration
         0x001E              time          Jog mode acceleration time, in ms;

                     Jog mode deceleration
         0x001F              time          Jog mode deceleration time, unit: ms;


                                                15
```


## PDF page 18

```text
                                                         Modbus Series Bus Driver Function Manual

  ※ Control is completed through Modbus: 0027h motion control command register
Through 0027h :Bit1 .

         0027h            Order                                    illustrate

                     Speed mode start
                     command bit      When this bit is written as 1, a start command
             Bit1                                           is generated and the drive starts moving
                                0; invalid;                                               according to the given motion parameters;
                                 1: Start;

    Example:
    Accelerate in the reverse direction according to the parameters ( operating speed 60r/min,
acceleration/ deceleration time 100ms) to 500r/min, and then run at a constant speed.
    Before starting this example, be sure to set the driver device address to 1, that is, turn the DIP
switches SW5-SW2 to OFF and SW1 to ON.
    Step 1: Set the running speed to 60 r/min and the acceleration/deceleration time to 100ms.

    Host->Slave: 01 10 00 1D 00 03 06 00 3C 00 64 00 64 66 DE

    Slave->Host: 01 10 00 1D 00 03 10 0E

    Step 2 : Speed Mode Start Command

    Host -> Slave: 01 06 00 27 00 02 B8 00

    Slave->Host: 01 06 00 27 00 02 B8 00

  ※ Control is completed through external input terminals
    Any input terminal of the driver , and set its function to 8: speed mode motion, 9: JOG+ point
motion, 10: JOG- point motion through the corresponding function setting register . When the
terminal is given an effective level signal, the driver completes the action according to the given
motion parameters.

            Register
                          name                                  illustrate          Address

                                                             8: Speed mode movement;
            0x004          Input terminal function                                                             9: JOG+point motion;
          1~0x0047               selection
                                                       10: JOG-point movement;

    The difference between setting it to 8, 9, and 10 is:
   When set to mode 8, the forward and reverse directions of the motor are determined by the positive
and negative values of the 001Dh register;
   When set to mode 9, the drive always moves in the positive direction regardless of the positive or
     negative value of the 001Dh register parameter;



                                                16
```


## PDF page 19

```text
                                                         Modbus Series Bus Driver Function Manual


   When set to mode 10, the drive always moves in the negative direction regardless of whether the
001Dh register parameter value is positive or negative.

4.3 Return to zero function

    The selection of the homing mode is set by the Modubs: 0031h register. When the limit signal or
origin signal is needed in the homing process , you need to select the limit signal or origin signal
function of the input terminal according to the mode before using the homing function . At the same
time, the homing function can be triggered by external I/O or Modbus: 0027h register . If an external
I/O trigger is used, a certain input terminal function needs to be enabled as the "homing enable"
function.

    Before triggering the homing action, the relevant motion parameter registers need to be set,
including the homing mode, homing speed, homing query speed, homing acceleration and
deceleration time. If the origin needs to be offset, the origin offset value needs to be set.

    The relevant motion control parameter registers are:
          Register                      name                                    illustrate         Address

          0x0031      Return to origin mode             1~14, 17~30, 33~35 modes;

                                              Running speed when querying the origin          0x0032     Return to origin speed                                                                            position;

                       Return to origin query          0x0033                           The return speed after querying the origin;                           speed

                           Acceleration and                                         The acceleration and deceleration time when          0x0034      deceleration time when                                                         querying the origin position;                            returning to origin

          0x0035      Origin offset value high
                                                   Origin offset value: after finding the origin sensor,
                                                                                             it moves to the correct position according to the


          0x0036      Origin offset value low               value set in this register .

    The driver supports 1~14, 17~30, 33, 34, and 35 return to origin methods. Among them, 1~14,
33, and 34 return to zero modes require the use of a closed-loop stepper motor with a Z signal. For
detailed description, see Appendix 1.
  ※ Control is completed through Modbus: 0027h motion control command register
through 0027h :Bit4 .

          0027h                 Order                                   illustrate

                         Return to origin start command     When this bit is written as 1, a              Bit4                                         position                   start command is generated and

                                       0; invalid;                                                           the drive starts moving according
                                                                  to the given motion parameters;                                        1: Start;

                                                17
```


## PDF page 20

```text
                                                         Modbus Series Bus Driver Function Manual

    Example:
    Complete the homing action according to the parameters ( homing mode 24 , homing speed
60r/min, homing query speed 30r/min , homing acceleration/ deceleration time 100ms, origin offset
0 ) .
   Assume that before returning to the origin, the functions of the relevant input terminals have been
set to the origin, positive limit, and negative limit;
    Step 1: Write parameters: homing mode 24 , homing speed 60r/min, homing query speed 30r/min ,
homing acceleration/ deceleration time 100ms, origin offset 0.

    Host->Slave: 01 10 00 31 00 0 6 0 C 00 18 00 3C 00 1E 00 64 00 0 0 00 00 2F 3B

    Slave->Host: 01 10 00 31 00 06 11 C4

    Step 2 : Return to origin and start command

    Host -> Slave: 01 06 00 27 00 10 38 0D

    Slave->Host: 01 06 00 27 00 10 38 0D

  ※ Control is completed through external input terminals
    Any input terminal of the driver , and set its function to 11 through the corresponding function
setting register: homing enable signal . When a valid level signal is given to this terminal, the
driver completes the action according to the given motion parameters.

            Register
                          name                                  illustrate          Address

            0x004          Input terminal function
                                     selection         1 1 : Return to origin enable signal;          1~0x0047

4.4 Multi-stage position control

    Multi-segment position control is a working mode that combines multiple position segments in a
certain order, triggers motion through external IO signals, and completes a series of position
segment actions . This function can also be regarded as a multi-segment combination of the
positioning motion mode described in 4.1 Positioning motion . The difference is that the user can
store the motion parameters of several position segments such as acceleration and deceleration time,
target pulse number , acceleration and deceleration in eeprom or local flash in advance . When these
position segments need to be enabled, only a trigger signal is needed to complete the work. The
working process is described as shown in the figure below.





                                                18
```


## PDF page 21

```text
                                                      Modbus Series Bus Driver Function Manual


Speed(r/min
     )                  Position           Position                   Position
                  segment 1          segment 2                  segment n





                                             19
```


## PDF page 22

```text
                                                         Modbus Series Bus Driver Function Manual


                                 Multi-position working mode
  Currently, a maximum of 16 position segments are supported. Here are the relevant motion
parameters describing the first position segment:

         Register
                      name                                     illustrate        Address

                  The first segment motion  The first segment is the motion pulse instruction,
        0x00 6 0        pulse command       the unit is pulse;
                               high         When the value is positive, the motor moves
                                                   in the positive direction, and when the value                  The first segment motion
                                                      is negative, the motor moves in the negative         0x0061       pulse command low                                                direction .

                        Positioning speed of the         0x00 62                               the first position positioning, unit: r/min;                                          first stage
                         1st stage positioning    The first segment position positioning motion
         0x0063                       motion acceleration      acceleration, in ms;

                           1st stage position
                                       The first stage of the positioning motion         0x0064        positioning motion                                                   deceleration, unit: ms;                             deceleration
    For the addresses of motion parameters related to the remaining position segments, please refer
to the Appendix: Modbus Register Parameter Table .
     Multi-stage position control uses external input terminals to select position segments and start
motion, so the input terminal functions need to be set.
   Among them, 1 2 : The PT trigger signal function is the trigger signal, and PIN0 ~PIN3 are the
position segment selection signals .

            Register
                          name                                  illustrate          Address
                                              1 2 : PT trigger signal ;
                                              1 4 : PIN0 ;
            0x004          Input terminal function                                              1 5 : PIN1 ;
          1~0x0047               selection
                                              1 6 : PIN2 ;
                                              1 7 : PIN3 ;
The position segment selection is completed according to the binary number composed of PIN0~PIN3.
The corresponding relationship is as follows:
※ Input terminal selection position segment
                                                                     Location            PIN3          PIN2         PIN1          PIN0                                                             segment
             0             0            0             0              1
             0             0            0             1              2
             0             0            1             0              3
             0             0            1             1              4
             0             1            0             0              5

                                                20
```


## PDF page 23

```text
                                                         Modbus Series Bus Driver Function Manual

             0             1            0             1              6
             0             1            1             0              7
             0             1            1             1              8
             1             0            0             0              9
             1             0            0             1             10
             1             0            1             0             11
             1             0            1             1             12
             1             1            0             0             13
             1             1            0             1             14
             1             1            1             0             15
             1             1            1             1             16



               Notice:
        ⚫  using the PIN terminal for segment selection , it must remain valid 5ms before
              and after the " PT trigger signal " .
        ⚫  In multi-segment mode, the position segment is controlled by the
                  relative/absolute position register 0x00 50 to determine whether it is relative
                  position movement or absolute position movement.
        ⚫ Some driver peripherals only support 4 input signals from X0 to X3, so the
                  position segment selection supports a maximum of 8 segments;
4.5 Multi- speed modes

    The multi- speed mode function is a working method that stores multiple speed segments in
advance, triggers movement through external IO signals, and completes a series of actions at
different speeds .
     Currently, a maximum of 16 speed segments are supported. Here are the relevant motion
parameters describing the first speed segment:
          Register
                      name                                    illustrate         Address

                                               running speed of the first speed segment is in
                                                   r/min ;
                           1st speed segment         0x00 C0                    When the value is positive, the motor moves                           running speed                                                   in the positive direction, and when the value
                                                      is negative, the motor moves in the negative
                                                direction .
                           1st speed segment    The first speed segment motion acceleration,
         0x00C1        acceleration time       unit: ms;

                            1st speed stage     The first speed segment is the deceleration of
         0x00C2        deceleration time      the movement, in ms;



                                                21
```


## PDF page 24

```text
                                                         Modbus Series Bus Driver Function Manual

    For the addresses of motion parameters related to other speed segments, please refer to the
Appendix: Modbus Register Parameter Table .
    Multi-speed control uses external input terminals to select speed segments and start motion, so the
input terminal function needs to be set.
   Among them, 1 3 : PV trigger signal function is trigger signal, PIN0 ~ PIN3 are speed segment
selection signals .
            Register
                          name                                  illustrate          Address

                                              1 3 : PV trigger signal ;
                                              1 4 : PIN0 ;
            0x004          Input terminal function                                              1 5 : PIN1 ;
          1~0x0047               selection
                                              1 6 : PIN2 ;
                                              1 7 : PIN3 ;
    Speed segment selection is completed according to the binary number composed of PIN0~PIN3.
The corresponding relationship is shown in the following table:
※ Input terminal selects speed segment
            PIN3          PIN2         PIN1          PIN0       Speed range
             0             0            0             0              1
             0             0            0             1              2
             0             0            1             0              3
             0             0            1             1              4
             0             1            0             0              5
             0             1            0             1              6
             0             1            1             0              7
             0             1            1             1              8
             1             0            0             0              9
             1             0            0             1             10
             1             0            1             0             11
             1             0            1             1             12
             1             1            0             0             13
             1             1            0             1             14
             1             1            1             0             15
             1             1            1             1             16



               Notice:
        ⚫  Some driver peripherals only support 4 input signals from X0 to X3, so the
                  position segment selection supports a maximum of 8 segments;

                                                22
```


## PDF page 25

```text
                                                         Modbus Series Bus Driver Function Manual

4.6 Stop and emergency stop commands

   When the drive needs to stop the current moving state, it can be completed through Modbus:
0027h motion control command register.

    through 0027 h: Bit8 and Bit9 .

          0027h                 Order                                   illustrate


                                         When this bit is written as 1, a                      Stop command bit                                                                      start command is generated and              Bit8       0; invalid;                                                           the drive starts moving according                          1: Start;                                                                 to the given motion parameters;

                    Emergency stop command position   This bit indicates whether the
              Bit9       0: relative positioning;              given target position is a relative
                          1: absolute positioning;              position or an absolute position;

    Example:

    Send a stop command to the slave:

    Host -> Slave: 01 06 00 27 01 00 38 51

    Slave->Host: 01 06 00 27 01 00 38 51

    Send an emergency stop command to the slave:

    Master->Slave: 01 06 00 27 02 00 38 A1 Slave->Host: 01 06 00

    27 02 00 38 A1

    the motor is running in positioning mode or speed mode, if it receives a normal stop command,
the motor will decelerate and stop according to the set deceleration time. If it receives an emergency
stop command, it will stop directly without deceleration.


          Notice:
    ⚫  The deceleration time parameter needs to be set before the motor runs. If the
            driver receives the command after the motor starts running, it will execute the
        command according to the deceleration time set before the motor runs.
                                              Maximum                                                                           Slow down stop command                                                      speed ( r/min )                        Speed(r/mi                                                received0x0027
                                n)


                                                     Normal stop
                                         scram


                                                                                Deceleration
                                                                           time(ms)

                                                                                                       time(S)
                         Normal stop and emergency stop

                                                23
```


## PDF page 26

```text
                                                         Modbus Series Bus Driver Function Manual
5 Other functions

     In addition to the main motion control function, the driver also has a series of other functions to
achieve functions such as register parameter saving, factory reset, terminal function configuration,
etc., for the convenience of customers.
5.1 Auxiliary control instructions

Modbus : 002Dh auxiliary control command register is used to realize a series of functions such as
motor enable release, alarm clearing, parameter recovery and storage.
※ Modbus: 002Dh auxiliary control register
          Register Address     name                              illustrate

                                          0x0000: invalid;
                                          0x0011: Motor release;
                                          0x0012: Motor enable;
                                          0x0021: drive alarm cleared;
                                     Auxiliary   0x0031: Clear the current position of the motor;
             0x002D            control    0x0041: Restore parameters to factory settings;
                                    instructions  0x0042: save all parameters;
                                            Note: When performing factory recovery and
                                          parameter saving operations on the drive, it is
                                          necessary to ensure that the motor is in the
                                          stopped state, otherwise the relevant instructions
                                                             will be ignored;

Example:1) Driver controls the motor to release:

    Host->Slave: 01 06 00 2D 00 11 D9 CF

    Slave->Host: 01 06 00 2D 00 11 D9 CF

     2) Driver controls the motor to enable:

    Host->Slave: 01 06 00 2D 00 12 99 CE

    Slave->Host: 01 06 00 2D 00 12 99 CE

     3) When the driver generates a resettable alarm, reset the alarm:

    Host->Slave: 01 06 00 2D 00 21 D9 DB

    Slave->Host: 01 06 00 2D 00 21 D9 DB

     4) Restore the drive factory parameters

    Host->Slave: 01 06 00 2D 00 41 D9 F3

    Slave->Host: 01 06 00 2D 00 41 D9 F3

     5) Save the current parameters of the drive
    Host- >Slave: 01 06 00 2D 00 42 99 F2

    Slave->Host: 01 06 00 2D 00 42 99 F2

                                                24
```


## PDF page 27

```text
                                                         Modbus Series Bus Driver Function Manual
5.2 Input and output terminals

  The driver integrates several input and output terminals, and its functions can be configured
according to the actual use function. If the return to origin function is required, several terminals can
be selected and their functions can be set to origin, positive and negative limit; if multi-stage
position/multi-stage speed control is required, several terminals can be selected and their functions
can be set to PT enable, PV enable, PIN0~3, etc.
      All functions of the input and output terminals are as follows:
※ Input terminal function table

         Input terminal function  illustrate

       0 : undefined;         The terminal has no function;
       1 : origin signal;        The input signal is the origin sensor signal;
       2 : Positive limit signal;  The input signal is the positive limit sensor signal;
       3 : Anti-limit signal;     The input signal is the negative limit sensor signal;
       4 : Motor MF signal;    The motor is released when a valid level is given;

                       When a valid level is given, the motor movement is forced to       5 : Stop signal;                                   stop;

       6 : Emergency stop                       When a valid level is given, the motor is forced to stop urgently;          signal;

       7 : Position mode     When a valid level is given, the movement is completed
       movement;             according to the movement parameters and the edge is valid;

          8: Speed mode       When a valid level is given, the movement is completed
       movement;             according to the movement parameters and the level is valid;

                       When a valid level is given, the movement is completed       9 : JOG+ point motion;                                according to the movement parameters and the level is valid;

       10 : JOG -point       When a valid level is given, the movement is completed
       movement;             according to the movement parameters and the level is valid;

       1 1 : Return to origin   When a valid level is given, the movement is completed
        enable signal;           according to the movement parameters and the edge is valid;

                       When a valid level is given, the movement is completed       1 2: PT trigger signal ;                                according to the movement parameters and the edge is valid;

                       When a valid level is given, the movement is completed         13: PV trigger signal;                                according to the movement parameters and the level is valid;

       1 4 : PIN0 ;
       1 5 : PIN1 ;                                    Multiple position/speed segment binary selection bits, level
                                      effective;       1 6 : PIN2 ;
       1 7 : PIN3 ;





                                                25
```


## PDF page 28

```text
                                                         Modbus Series Bus Driver Function Manual


※ Output terminal function table
        Output terminal function          illustrate
       0 : undefined;               The terminal has no function;
          1: Alarm signal;            When the driver is in alarm state, it outputs a valid level;
          2: Driver status signal;        When the driver is in motion, it outputs a valid level;
          3: Return to origin completion  When the driver returns to the origin,  it outputs a valid
          signal;                               level;

                             When the driver starts and reaches the correct position, it          4: Arrival signal;                                        outputs a valid level.

                             When the driver locks the motor, it outputs a valid level;
          5: Braking signal;           When the driver releases the motor,  it outputs an invalid
                                                 level;
          9: User defined 0;
                                 The driver outputs a valid or invalid level according to the
         10: User defined 1;                                 004F h register setting;
         11: User defined 2;

※ Modbus: 0040h~004Fh input and output terminal function configuration register

          Register                    name                                    illustrate         Address
                          Input terminal
                                Used to set the input terminal voltage to be normally         0x0040       effective level                                  open or normally closed;                            selection
                          Input terminal          0x004
                             function     Used to set the input terminal function
        1~0x0047        selection

                      Output terminal
                                Used to set the output terminal voltage to be normally         0x004B       effective level                                  open or normally closed;                            selection

                      Output terminal        0x004C~0x
                             function     Used to set the output terminal function
          004E          selection
                   Y group    When the output terminal function is set to custom, this
                        terminal custom  register is used to control the corresponding output         0x004F
                            output       terminal to output a valid level or an invalid level;

5.3 Software limit function

     In some special applications, the limit sensor cannot be installed in the stroke due to structural
space limitations, and at the same time it is necessary to ensure that the stroke is limited within a
safe range. In this case, the internal soft limit function of the driver can be used.
 The soft limit function can be started through Modbus: 0019h register, and the soft limit range can be
set through Modbus: 0037h~003Ah register.



                                                26
```


## PDF page 29

```text
                                                         Modbus Series Bus Driver Function Manual

※ Modbus: 0018h, 0037h~003Ah soft limit configuration register
          Register
                    name                                    illustrate         Address

                              Internal
                                              0: invalid;                        software limit         0x0018
                          switches       1: Take effect after returning to zero;

                            Soft limit
                           positive high         0x0037
                             position
                                      Software positive limit setting point, this limit point will
                            Soft limit      take effect only after zero return is completed ;
                           positive low         0x0038
                             position

                            Soft limit
                        negative high         0x0039
                             position
                                      Software negative limit setting point, this limit point will
                            Soft limit      take effect only after zero return is completed ;
                        negative low         0x003A
                             position

    Note: The soft limit function will only take effect after the origin position is determined.
5.4 Interrupt fixed length function

The interrupt fixed length function means that when the motor is currently running in speed mode,
the running mode can be switched to positioning mode by triggering an external signal, and the
positioning length can be set in advance.
    The interrupt fixed length function can be implemented in many ways.
Method 1 :
 Start the motor into speed operation mode through Modbus: 0027h motion control command register;
 Select any input signal and set its function to 7 : Position mode motion function. When the input
signal receives a valid level, the motor enters fixed-length positioning. The fixed-length pulse value can be
set in Modbus: 0024h~0025h; Method 2:
 This function is achieved by mixing the multi-speed control mode and the multiposition mode. Note
that the multi-speed mode and multi-position mode functions can only be mixed after the Modbus:
0051h register is set to 1.

5.5 Setting the endian mode of high and low registers

 In the Modbus register parameter table, some parameters extend the register working range by high
and low bits to meet the actual application requirements. The following table lists all high and low bit
parameters:
※ All high and low register tables



                                                27
```


## PDF page 30

```text
                                                         Modbus Series Bus Driver Function Manual

              Register Address                        name

                0x000A                               Current position high

                0x000B                               Current position low

                 0x0024                                 Total pulse count high

                 0x0025                                 Total pulse count low

                 0x0037                            Soft limit positive high position

                 0x0038                             Soft limit positive low position

                 0x0039                            Soft limit negative high position

                0x003A                            Soft limit negative low position

                0x00 6 0               The first segment motion pulse command high

                 0x0061                The first segment motion pulse command low

          …                    …

               0x00BA                    16th segment motion pulse command high

               0x00BB                     16th segment motion pulse command low
    The Modbus series bus driver can adapt to mainstream touch screens, PLCs, control cards and
other control devices that support the Modbus protocol. However, there are differences in the big-
endian and small -endian modes for 32-bit variables in different devices. In the big-endian mode, the
high byte of the data is stored in the low address of the memory, and the low byte of the data is
stored in the high address of the memory; in the little-endian mode , the high byte of the data is
stored in the high address of the memory, and the low byte of the data is stored in the low address of
the memory. For different control devices, the Modbus: 0019h register can be set to adapt the big-
endian and small-endian modes of the control device.

※ Modbus: 0019h big and small end configuration register

          Register
                    name                                    illustrate         Address

                                              0: high position first, low position last;                           32-bit register
                                              1: High position at the back, low position at the front;
         0x0019      endianness
                          configuration
                                    Adapt to different PLC or touch screen usage habits;





                                                28
```


## PDF page 31

```text
                                                         Modbus Series Bus Driver Function Manual
6 Revision History

   Version             describe                time           Remark

     V1.0             First edition released        2025/03/05





                                                29
```


## PDF page 32

```text
                                                         Modbus Series Bus Driver Function Manual

Appendix 1: Introduction to the method of returning to the origin

※ Method 1 (0031h = 1)
Origin: Motor Z signal
    Deceleration point: negative limit
     a) The deceleration point signal is invalid when returning to zero : when running in the reverse
direction at high speed, it will decelerate and stop when encountering the rising edge of the
deceleration point; when running in the forward direction at low speed, it will stop when encountering
the first Z signal after the falling edge of the deceleration point ;





     b) The deceleration point signal is valid when starting from zero : the machine runs at a low
speed in the forward direction and stops when it encounters the first Z signal after the falling edge of
the deceleration point ;





                                                30
```


## PDF page 33

```text
                                                         Modbus Series Bus Driver Function Manual

※ Method 2 (0031h = 2)
Origin: Motor Z signal
    Deceleration point: positive limit
     a) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the rising edge of
the deceleration point; when running at low speed in the reverse direction, it will stop when
encountering the first Z signal after the falling edge of the deceleration point ;





     b) The deceleration point signal is valid when starting from zero : the machine runs in the
reverse direction at a low speed and stops when encountering the first Z signal after the falling edge
of the deceleration point ;





                                                31
```


## PDF page 34

```text
                                                         Modbus Series Bus Driver Function Manual

※ Method 3 (0031h = 3)
Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop after encountering the rising edge of
the deceleration point; when running at low speed in the reverse direction, it will stop after
encountering the first Z signal after the falling edge of the deceleration point ;





     b) The deceleration point signal is valid when starting from zero : the machine runs in the
reverse direction at a low speed and stops when encountering the first Z signal after the falling edge
of the deceleration point ;





※ Method 4 (0031h = 4)
Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when starting from zero : the machine runs at a low
speed in the forward direction and stops when it encounters the first Z signal after the rising edge of
the deceleration point;

                                                32
```


## PDF page 35

```text
                                                        Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is valid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop after encountering the falling edge of the
deceleration point; when running in the forward direction at low speed, it will stop after encountering
the first Z signal after the rising edge of the deceleration point;





※ Method 5 (0031h = 5)
Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop after encountering the rising edge of the
deceleration point; when running in the forward direction at low speed, it will stop after encountering the
first Z signal after the falling edge of the deceleration point ;




                                                33
```


## PDF page 36

```text
                                                        Modbus Series Bus Driver Function Manual





     Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is valid when starting from zero : the machine runs at a low speed
in the forward direction and stops when it encounters the first Z signal after the falling edge of the
deceleration point ;





※ Method 6 (0031h = 6)
  Origin: Motor Z signal
  Deceleration point: origin signal
     a) The deceleration point signal is invalid when starting from zero return : the machine runs in the
reverse direction at a low speed and stops when encountering the first Z signal after the rising edge of
the deceleration point;





                                                34
```


## PDF page 37

```text
                                                        Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is valid when returning to zero and starting : when running at high
speed in the forward direction , it will decelerate and stop after encountering the falling edge of the
deceleration point; when running at low speed in the reverse direction, it will stop after encountering the
first Z signal after the rising edge of the deceleration point;





※ Method 7 (0031h = 7)
Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop after encountering the rising edge of the
deceleration point; when running at low speed in the reverse direction, it will stop after encountering the
first Z signal after the falling edge of the deceleration point ;





                                                35
```


## PDF page 38

```text
                                                        Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the positive limit
signal; when running at high speed in the reverse direction, it will run at low speed when encountering
the rising edge of the deceleration point, and stop when encountering the first Z signal after the falling
edge of the deceleration point ;





     c) The deceleration point signal is valid when starting from zero : the machine runs in the reverse
direction at a low speed and stops when encountering the first Z signal after the falling edge of the
deceleration point ;




                                                36
```


## PDF page 39

```text
                                                        Modbus Series Bus Driver Function Manual





※ Method 8 (0031h = 8)
Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running at high
speed in the forward direction, it will decelerate and stop after encountering the rising edge of the
deceleration point; when running at low speed in the reverse direction, it will decelerate and stop after
encountering the falling edge of the deceleration point; when running at low speed in the forward
direction, it will stop after encountering the first
Z signal after the rising edge of the deceleration point;





     b) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the positive limit
signal; when running at high speed in the reverse direction, it will run at low speed when encountering
the rising edge of the deceleration point, and decelerate and stop when encountering the falling edge of

                                                37
```


## PDF page 40

```text
                                                        Modbus Series Bus Driver Function Manual

the deceleration point ; when running at low speed in the forward direction, it will stop when
encountering the first Z signal after the rising edge of the deceleration point;





     c) The deceleration point signal is valid when returning to zero and starting : when running in the
reverse direction at a low speed , it will decelerate and stop when it encounters the falling edge of the
deceleration point; when running in the forward direction at a low speed, it will stop when it encounters
the first Z signal after the rising edge of the deceleration point;





※ Method 9 (0031h = 9)
Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the forward direction, runs at low speed when encountering the rising edge of the deceleration
point, and decelerates and stops when encountering the falling edge of the deceleration point ; it runs at

                                                38
```


## PDF page 41

```text
                                                        Modbus Series Bus Driver Function Manual

low speed in the reverse direction, and stops when encountering the first Z signal after the rising edge
of the deceleration point;





     b) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the positive limit
signal; when running at high speed in the reverse direction, it will decelerate and stop when
encountering the rising edge of the deceleration point; when running at low speed in the forward
direction, it will decelerate and stop when encountering the falling edge of the deceleration point ; when
running at low speed in the reverse direction, it will stop when encountering the first Z signal after the
rising edge of the deceleration point;





                                                39
```


## PDF page 42

```text
                                                        Modbus Series Bus Driver Function Manual

     c) The deceleration point signal is valid when returning to zero and starting : when running at low
speed in the forward direction , it will decelerate and stop when it encounters the falling edge of the
deceleration point; when running at low speed in the reverse direction, it will stop when it encounters
the first Z signal after the rising edge of the deceleration point;





※ Method 10 (0031h = 10)
     Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when starting from zero : it runs at high speed in the
forward direction, runs at low speed when encountering the rising edge of the deceleration point, and
stops when encountering the first Z signal after the falling edge of the deceleration point ;





                                                40
```


## PDF page 43

```text
                                                        Modbus Series Bus Driver Function Manual

     b) The deceleration point signal is invalid when returning to zero : when running at high speed in
the forward direction, it will decelerate and stop when encountering the positive limit signal; when
running at high speed in the reverse direction, it will decelerate and stop when encountering the rising
edge of the deceleration point; when running at low speed in the forward direction, it will stop when
encountering the first Z signal after the falling edge of the deceleration point ;





     c) The deceleration point signal is invalid when starting from zero :  it runs at a low speed in the
forward direction and stops at the first Z signal after the falling edge of the deceleration point ;





                                                41
```


## PDF page 44

```text
                                                        Modbus Series Bus Driver Function Manual

※ Method 11 (0031h = 11)
     Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero : when running in the reverse
direction at high speed, it will decelerate and stop when encountering the rising edge of the
deceleration point; when running in the forward direction at low speed, it will stop when encountering
the first Z signal after the falling edge of the deceleration point ;





     b) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop when encountering the negative limit; when
running in the forward direction at high speed, it will run at low speed when encountering the rising
edge of the deceleration point, and stop when encountering the first Z signal after the falling edge of the
deceleration point





                                                42
```


## PDF page 45

```text
                                                        Modbus Series Bus Driver Function Manual

     c) The deceleration point signal is valid when starting from zero : the machine runs at a low speed
in the forward direction and stops when it encounters the first Z signal after the falling edge of the
deceleration point ;





※ Method 12 (0031h = 12)
     Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero : when running in the reverse
direction at high speed, it will decelerate and stop when encountering the rising edge of the
deceleration point; when running in the forward direction at low speed, it will decelerate and stop when
encountering the falling edge of the deceleration point; when running in the reverse direction at low
speed, it will stop when encountering the first Z signal after the rising edge of the deceleration point;





                                                43
```


## PDF page 46

```text
                                                        Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop when encountering the negative limit ; when
running in the forward direction at high speed, it will run at low speed when encountering the rising
edge of the deceleration point, and decelerate and stop when encountering the falling edge of the
deceleration point ; when running in the reverse direction at low speed, it will stop when encountering
the first Z signal after the rising edge of the deceleration point;





     c) The deceleration point signal is valid when returning to zero and starting : when running at low
speed in the forward direction , it will decelerate and stop when it encounters the falling edge of the
deceleration point; when running at low speed in the reverse direction, it will stop when it encounters
the first Z signal after the rising edge of the deceleration point;
                                                44
```


## PDF page 47

```text
                                                        Modbus Series Bus Driver Function Manual





※ Method 13 (0031h = 13)
     Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero : it runs at high speed in the
reverse direction, runs at low speed when encountering the rising edge of the deceleration point, and
decelerates and stops when encountering the falling edge of the deceleration point; it runs at low speed
in the forward direction, and stops when encountering the first Z signal after the rising edge of the
deceleration point;





                                                45
```


## PDF page 48

```text
                                                        Modbus Series Bus Driver Function Manual

     b) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop when encountering the negative limit; when
running in the forward direction at high speed, it will decelerate and stop when encountering the rising
edge of the deceleration point; when running in the reverse direction at low speed, it will decelerate and
stop when encountering the falling edge of the deceleration point ; when running in the forward direction
at low speed, it will stop when encountering the first Z signal after the rising edge of the deceleration
point;





     c) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at a low speed , it will decelerate and stop when it encounters the falling edge of the
deceleration point; when running in the forward direction at a low speed, it will stop when it encounters
the first Z signal after the rising edge of the deceleration point;





                                                46
```


## PDF page 49

```text
                                                        Modbus Series Bus Driver Function Manual

※ Method 14 (0031h = 14)
     Origin: Motor Z signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the reverse direction, runs at low speed when encountering the rising edge of the deceleration
point, and stops when encountering the first Z signal after the falling edge of the deceleration point ;





     b) The deceleration point signal is invalid when returning to zero : when running in the reverse
direction at high speed, it will decelerate and stop when encountering the negative limit; when running
in the forward direction at high speed, it will decelerate and stop when encountering the rising edge of
the deceleration point; when running in the reverse direction at low speed, it will stop when
encountering the first Z signal after the falling edge of the deceleration point ;





                                                47
```


## PDF page 50

```text
                                                        Modbus Series Bus Driver Function Manual

     c) The deceleration point signal is invalid when returning to zero : the machine runs in the reverse
direction at a low speed and stops when encountering the first Z signal after the falling edge of the
deceleration point ;





※ Method 17 (0031h = 17)
     Origin: Negative limit
    Deceleration point: negative limit
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the reverse direction, and stops when it encounters the rising edge of the deceleration point; it
runs at low speed in the forward direction, and stops when it encounters the falling edge of the
deceleration point ;

                         Negative limit signal





     b) The deceleration point signal is valid when returning to zero and starting : the machine runs at a
low speed in the forward direction and stops after encountering the falling edge of the deceleration
point ;



                                                48
```


## PDF page 51

```text
                                                        Modbus Series Bus Driver Function Manual





※ Method 18 (0031h = 18)
     Origin: Positive limit
    Deceleration point: positive limit
     a) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when it encounters the rising edge of the
deceleration point; when running at low speed in the reverse direction, it will stop when it encounters
the falling edge of the deceleration point ;





     b) The deceleration point signal is valid when returning to zero and starting : the machine will run
in the reverse direction at a low speed and stop when it encounters the falling edge of the deceleration
point ;





                                                49
```


## PDF page 52

```text
                                                        Modbus Series Bus Driver Function Manual

※ Method 19 (0031h = 19)
     Origin: Origin signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop after encountering the rising edge of the
deceleration point; when running at low speed in the reverse direction, it will stop after encountering the
falling edge of the deceleration point ;





     b) The deceleration point signal is valid when returning to zero and starting : the machine will run
in the reverse direction at a low speed and stop when it encounters the falling edge of the deceleration
point ;





※ Method 20 (0031h = 20)
     Origin: Origin signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : the machine runs at
a low speed in the forward direction and stops after encountering the rising edge of the deceleration
point;



                                                50
```


## PDF page 53

```text
                                                        Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is valid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop after encountering the falling edge of the
deceleration point; when running in the forward direction at low speed, it will stop after encountering the
rising edge of the deceleration point;





※ Method 21 (0031h = 21)
     Origin: Origin signal

    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : the machine runs at
high speed in the reverse direction, and stops after encountering the rising edge of the deceleration
point; the machine runs at low speed in the forward direction, and stops after encountering the falling
edge of the deceleration point ;





                                                51
```


## PDF page 54

```text
                                                        Modbus Series Bus Driver Function Manual





     Origin: Origin signal

    Deceleration point: origin signal
     a) The deceleration point signal is valid when starting from zero : the machine runs at a low speed
in the forward direction and stops after encountering the falling edge of the deceleration point ;





※ Method 22 (0031h = 22)

     Origin: Origin signal

    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : the machine will run
in the reverse direction at a low speed and stop after encountering the rising edge of the deceleration
point;





                                                52
```


## PDF page 55

```text
                                                         Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is valid when returning to zero and starting : when running at high
speed in the forward direction , it will decelerate and stop after encountering the falling edge of the
deceleration point; when running at low speed in the reverse direction, it will stop after encountering the
rising edge of the deceleration point;





※ Method 23 (0031h = 23)

     Origin: Origin signal

    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop after encountering the rising edge of the
deceleration point; when running at low speed in the reverse direction, it will stop after encountering the
falling edge of the deceleration point ;





                                                53
```


## PDF page 56

```text
                                                         Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the forward direction, and decelerates and stops when encountering the positive limit signal; it
runs at high speed in the reverse direction, and runs at low speed when encountering the rising edge of
the deceleration point, and stops when encountering the falling edge of the deceleration point ;





     c) The deceleration point signal is valid when returning to zero and starting : the machine will run
in the reverse direction at a low speed and stop after encountering the falling edge of the deceleration
point ;





                                                54
```


## PDF page 57

```text
                                                         Modbus Series Bus Driver Function Manual





※ Method 24 (0031h = 24)
     Origin: Origin signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the rising edge of the
deceleration point; when running at low speed in the reverse direction, it will decelerate and stop when
encountering the falling edge of the deceleration point; when running at low speed in the forward
direction, it will stop when encountering the rising edge of the deceleration point;





     b) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the positive limit
signal; when running at high speed in the reverse direction, it will run at low speed when encountering
the rising edge of the deceleration point, and decelerate and stop when encountering the falling edge of
the deceleration point ; when running at low speed in the forward direction, it will stop after
encountering the rising edge of the deceleration point;

                                                55
```


## PDF page 58

```text
                                                         Modbus Series Bus Driver Function Manual





     c) The deceleration point signal is valid when returning to zero and starting : when running in the
reverse direction at a low speed , the machine will decelerate and stop when encountering the falling
edge of the deceleration point; when running in the forward direction at a low speed, the machine will
stop when encountering the rising edge of the deceleration point;





※ Method 25 (0031h = 25)
     Origin: Origin signal

    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the forward direction, runs at low speed when encountering the rising edge of the deceleration
point, and decelerates and stops when encountering the falling edge of the deceleration point ; it runs at
low speed in the reverse direction, and stops when encountering the rising edge of the deceleration
point;



                                                56
```


## PDF page 59

```text
                                                         Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the positive limit
signal; when running at high speed in the reverse direction, it will decelerate and stop when
encountering the rising edge of the deceleration point; when running at low speed in the forward
direction, it will decelerate and stop when encountering the falling edge of the deceleration point ; when
running at low speed in the reverse direction, it will stop after encountering the rising edge of the
deceleration point;





     c) The deceleration point signal is valid when returning to zero and starting : when running at low
speed in the forward direction , the machine will decelerate and stop when encountering the falling
edge of the deceleration point; when running at low speed in the reverse direction, the machine will stop
when encountering the rising edge of the deceleration point;





                                                57
```


## PDF page 60

```text
                                                         Modbus Series Bus Driver Function Manual





※ Method 26 (0031h = 26)
     Origin: Origin signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the forward direction, runs at low speed when encountering the rising edge of the deceleration
point, and stops when encountering the falling edge of the deceleration point ;





     b) The deceleration point signal is invalid when returning to zero and starting : when running at
high speed in the forward direction, it will decelerate and stop when encountering the positive limit
signal; when running at high speed in the reverse direction, it will decelerate and stop when
encountering the rising edge of the deceleration point; when running at low speed in the forward
direction, it will stop after encountering the falling edge of the deceleration point ;





                                                58
```


## PDF page 61

```text
                                                         Modbus Series Bus Driver Function Manual





     c) The deceleration point signal is invalid when returning to zero and starting : the machine runs at
a low speed in the forward direction and stops after encountering the falling edge of the deceleration
point ;





※ Method 27 (0031h = 27)
     Origin: Origin signal

    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the reverse direction, and stops when it encounters the rising edge of the deceleration point; it
runs at low speed in the forward direction, and stops when it encounters the falling edge of the
deceleration point ;





                                                59
```


## PDF page 62

```text
                                                         Modbus Series Bus Driver Function Manual





     b) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the reverse direction, and stops when it encounters the negative limit ; it runs at high speed in
the forward direction, and runs at low speed when it encounters the rising edge of the deceleration point,
and stops when it encounters the falling edge of the deceleration point ;





     c) The deceleration point signal is valid when returning to zero and starting : the machine runs at a
low speed in the forward direction and stops after encountering the falling edge of the deceleration
point ;





                                                60
```


## PDF page 63

```text
                                                        Modbus Series Bus Driver Function Manual





※ Method 28 (0031h = 28)
     Origin: Origin signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop when encountering the rising edge of the
deceleration point; when running in the forward direction at low speed, it will decelerate and stop when
encountering the falling edge of the deceleration point; when running in the reverse direction at low
speed, it will stop after encountering the rising edge of the deceleration point;





     b) The deceleration point signal is invalid when returning to zero and starting: when running in the
reverse direction at high speed, it will decelerate and stop when encountering the negative limit; when
running in the forward direction at high speed, it will run at low speed when encountering the rising
edge of the deceleration point, and decelerate and stop when encountering the falling edge of the
deceleration point; when running in the reverse direction at low speed, it will stop after encountering the
rising edge of the deceleration point;
                                                61
```


## PDF page 64

```text
                                                        Modbus Series Bus Driver Function Manual





     c) The deceleration point signal is valid when returning to zero and starting : when running at low
speed in the forward direction , the machine will decelerate and stop when encountering the falling
edge of the deceleration point; when running at low speed in the reverse direction, the machine will stop
when encountering the rising edge of the deceleration point;





※ Method 29 (0031h = 29)
     Origin: Origin signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the reverse direction, runs at low speed when encountering the rising edge of the deceleration
point, and decelerates and stops when encountering the falling edge of the deceleration point ; it runs at



                                                62
```


## PDF page 65

```text
                                                        Modbus Series Bus Driver Function Manual

low speed in the forward direction, and stops after encountering the rising edge of the deceleration
point;





     b) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at high speed, it will decelerate and stop when encountering the negative limit; when
running in the forward direction at high speed, it will decelerate and stop when encountering the rising
edge of the deceleration point; when running in the reverse direction at low speed, it will decelerate and
stop when encountering the falling edge of the deceleration point ; when running in the forward direction
at low speed, it will stop after encountering the rising edge of the deceleration point;





     c) The deceleration point signal is invalid when returning to zero and starting : it runs at a low
speed in the reverse direction , and stops when it encounters the falling edge of the deceleration point;
it runs at a low speed in the forward direction, and stops when it encounters the rising edge of the
deceleration point;
                                                63
```


## PDF page 66

```text
                                                         Modbus Series Bus Driver Function Manual





※ Method 30 (0031h = 30)
     Origin: Origin signal
    Deceleration point: origin signal
     a) The deceleration point signal is invalid when returning to zero and starting : it runs at high
speed in the reverse direction, runs at low speed when encountering the rising edge of the deceleration
point, and stops when encountering the falling edge of the deceleration point ;





     b) The deceleration point signal is invalid when returning to zero and starting : when running in the
reverse direction at high speed , it will decelerate and stop when encountering the negative limit; when
running in the forward direction at high speed, it will decelerate and stop when encountering the rising
edge of the deceleration point; when running in the reverse direction at low speed, it will stop after
encountering the falling edge of the deceleration point ;

                                                64
```


## PDF page 67

```text
                                                        Modbus Series Bus Driver Function Manual





     c) The deceleration point signal is invalid when returning to zero and starting : the machine will run
in the reverse direction at a low speed and stop after encountering the falling edge of the deceleration
point ;





※ Method 33 and Method 34 (0031h = 33 and 34)
Origin: Motor Z signal
Slowdown point: None





※ Method 35 (0031h = 35)
Return to zero mode 35, with the current position as the mechanical origin;

                                                65
```


## PDF page 68

```text
                                                        Modbus Series Bus Driver Function Manual
Appendix 2: Modbus Register Parameter Table - ESS-RS Series
                                                                      Setting range
  Register                                                        Note: Other     default                    project                       illustrate
  Address                                                        values are       value
                                                                                   invalid.

                           Status parameter group (read-only)

  0x0000       Driver Model     Drive model code                                 ( RO )         0x0305

  0x0001      Driver version    Driver version                                      ( RO )        0x010 0

                 Drive Node    MODBUS current communication
  0x0002                       slave node number                               ( RO )                   -               Number
                                    Bit0~Bit6: SW1~SW7 status;
  0x0003    DIP status code    0: OFF;                                              ( RO )                   -
                              1:ON;
                Current error    0 : Normal;
  0x0006         code                                                                    ( RO )                   -                           1~5 : Error;
                                   Bit0: In place flag;
                                     0: not in place, 1: in place;
                                   Bit1: Return to origin
                            completed bit;
                                     0: not completed, 1: completed;
                                   Bit2: Motor running position;
                                     0: stationary, 1: running;
                                   Bit3: alarm bit;
  0x0007    Motion status bit                                                             ( RO )                   -                                     0: normal, 1: alarm;
                                   Bit4: Motor enable bit;
                                     0: enable, 1: release;
                                   Bit5: Forward soft limit
                                overtravel flag ;
                                     0: invalid; 1: valid;
                                   Bit6: Negative soft
                                      limit overtravel flag ; 0:
                                        invalid; 1: valid;
                                        Bit 0 : X0 terminal input status;
                                        Bit 1 : X1 terminal input status;
                 Input terminal     Bit 2 : X2 terminal input status;  0x0008                                                                                         ( RO )                   -
                   status flag       Bit 3 : X3 terminal input status;
                                        Bit 4 ~Bit15 : Reserved;




                                                66
```


## PDF page 69

```text
                                                       Modbus Series Bus Driver Function Manual


                          0 : Input level is invalid;
                          1 : Input level is valid;

                                    Bit0: Y0 terminal output status;
                                    Bit1: Y1 terminal output status;
            Output terminal    Bit 2 ~Bit15: reserved;0x0009                                                                                          ( RO )             -                status flag                          0 : Output level is invalid;
                                  1: Output level is valid;

            Current position  The current position value of the
0x000A           high        motor. For an open-loop motor, this          ( RO )             -
                                  register value is the position given
                                value. For a closed-loop motor, this
                                  register value is the subdivision            Current position
0x000B                        equivalent of the encoder feedback          ( RO )             -                 low
                                value.

0x000C     Current speed    Current motor running speed                   ( RO )             -

                       Driver basic control parameter group 1

                 Default
                          0 : default direction ;                  0~ 1
0x0010      movement                                                    0
                  direction      1 : Reverse direction ;                            ( RW )

                           Address - Segment                 400~51200
0x0011   Segment settings                                               1000
                           400~51200;                                         ( RW )
                          0 ~ 255 : custom slave address; This
           Custom drive     register takes effect when all the        0~ 2550x0013                                                                   0
           node number     drive address DIP switches are OFF;        ( RW )

                           0:115200
                          1 : 38400
             Customize                          2 : 19200                           0~ 3
0x0014     communication                                                  0
                          3 : 9600                                              ( RW )             baud rate
                              Note: After modification, power must
                          be turned on again to take effect;

                          0 : 8 -bit data, no parity, 1 stop bit; 1 :
                          8 -bit data, no parity, 2 stop bits;
                          2 : 8 -bit data, even parity, 1 stop
              Serial port data     bit;                                0~ 30x0015                                                                   0
                 format      3 : 8 -bit data, odd parity, 1 stop               ( RW )
                                           bit;
                              Note: After modification, power must
                          be turned on again to take effect;



                                              67
```


## PDF page 70

```text
                                                       Modbus Series Bus Driver Function Manual

             Over-limit parking  0 : free parking; 1 :                    0~ 1
0x0017        method      Emergency stop;                                   0                                                                                                     ( RW )
                                  0: invalid;                           0~ 1             Internal software0x0018                                                                    0
                  limit switches     1: Take effect after returning to zero;         ( RW )
                                  0: high position first, low position last;
                                  1: High position at the back, low               32-bit register                                                            0~ 1
0x0019       endianness      position at the front;                                 0
                                                                                                     ( RW )               configuration                           Adapt to different PLC or touch
                             screen usage habits;

                  Basic motion control parameters of the drive

                                                               -3000 -300 0
             Jog mode                                                     1200x001D                    Jog mode running speed, unit: r/min;       r/min            operation speed                                                              (5r/min)
                                                                                                     ( RW )
             Jog mode     Jog mode acceleration time, in ms;     0-2000ms       50
0x001E     acceleration time                                                                                                     ( RW )      (100ms)
             Jog mode     Jog mode deceleration time, unit:      0-2000ms       50
0x001F     deceleration time  ms;                                                                                                     ( RW )      (100ms)
                         The starting speed of the positioning   0 ~3000r/min     30
0x0020      Starting speed   movement , in r/min ;                                                                                                     ( RW )       (60r/min)
                                                           0-2000ms       50
0x0021    Acceleration time   Acceleration time , in ms;
                                                                                                     ( RW )      (100ms)
                                                           0-2000ms       50
0x0022    Deceleration time  Deceleration time , in ms;
                                                                                                     ( RW )      (100ms)
                        Movement speed during positioning
               Positioning                                     0 ~3000r/min     600x0023                   movement , unit:
         movement speed                                                             ( RW )       (60r/min)
                                r/min ;
            Total pulse count  The total number of pulses for
0x0024           high                                 positioning motion operation is a 32-
                                                                                                - 0xFFFFFFF                                         bit register. If 100000 pulses are set,
                                                            ~                               the high bit is 0x0001 , the low bit is                                                                        5000
                                                      0xFFFFFFFF            Total pulse count  0x86A0 , and the value written to the0x0025                 low          register is                                             ( RW )
                           0x000186A0;

                                Bit0: Position mode start
                     command bit;
                                  0; invalid;
            Movement       1: Valid;                           0~65535
0x0027                                                                                                                  -                                Bit1: Speed mode start command             Control Order                                                                ( WO )
                                     bit;
                                  0; invalid;
                                  1: Valid;

                                              68
```


## PDF page 71

```text
                                                       Modbus Series Bus Driver Function Manual


                                Bit2: Position mode positioning
                         method;
                                  0: relative positioning;
                                  1: absolute positioning; Bit3:
                           Sports mode switching
                         method;
                                  0: Ignore motion commands; 1:
                                   interrupt the current motion and
                             execute it immediately;
                                Bit4: Return to origin start
                     command bit;
                                  0; invalid;
                                  1: Valid;
                                Bit8: stop command bit;
                                  0; invalid;
                                  1: Valid;
                                Bit9: Emergency stop
                     command bit;
                                  0; invalid;
                                  1: Valid;
                            0x0000: invalid;

                            0x0011: Motor release;
                            0x0012: Motor enable;

                            0x0021: drive alarm cleared;

                            0x0031: Clear the current position
                                  of the motor;
              Auxiliary control                                  0~ 65535
0x002D                     0x0041:  Restore  parameters  to                                -                instructions                                                              ( WO )
                                 factory settings;
                            0x0042: save all parameters;

                              Note: When performing factory
                              recovery and parameter saving
                               operations on the drive, it is
                            necessary to ensure that the
                            motor is in the stopped state,
                              otherwise the relevant instructions
                                         will be ignored;

                  Drive return to zero motion control parameters

                                              69
```


## PDF page 72

```text
                                                      Modbus Series Bus Driver Function Manual


                                  0: Run the offset, and after
                               completion, the current position is
                               the offset value;
                                  1: Run the offset, and the current
                                 position is 0 after completion; 2 :
                       Run the offset, and after
                             completion the current position is
                          a negative offset value; 3: Run
                               the offset, and after completion,
                               the current position is the actual
                                value; 4: Run the offset. After
                               completion, the current position
                                        is the actual value plus the offset
                                value.
                                  5: Run the offset. After
                               completion, the current position is
                               the actual value minus the offset
                                value.
             Zero return      6: Do not run the offset, and the        0 ~110x0030                                                                   0
              auxiliary setting                                  (RW)                                current position after completion is
                               the offset value;
                          7 : Do not run the offset, the
                                current position is 0 after
                               completion;
                                  8: Do not run the offset, and the
                                current position will be a negative
                                   offset value after completion; 9:
                      Do not run the offset, and the
                                current position is the actual
                              value after completion;
                          1 0 : Do not run the offset. After
                               completion, the current position is
                               the actual value plus the offset
                                value.
                          1 1 : Do not run the offset. After
                               completion, the current position is
                               the actual value minus the offset
                                value.

            Return to origin   Support -1~-4 , 1~14, 17~30,        0~ 65535      twenty
0x0031       mode                         33~35 modes;                                    ( RW )          four

            Return to origin   Running speed when querying the    5-3000r/min      120
0x0032        speed                                                               (60r/min)                                   origin position;                                   ( RW )

                                             70
```


## PDF page 73

```text
                                                       Modbus Series Bus Driver Function Manual


            Return to origin   The return speed after querying      5-300 r/min      60
0x0033      query speed     the origin;                                                                                                  ( RW )       (60r/min)

            Acceleration and
            deceleration time  The acceleration and deceleration    30-2000ms      50
0x0034                       time when querying the origin
         when returning to                                                           ( RW )      (100ms)                                   position;
                    origin
               Origin offset
0x0035        value high      Origin offset value: after finding     -0xFFFFFFF ~
                               the origin sensor, it moves to the                                                     0xFFFFFFF       0                                 correct position according to the               Origin offset0x0036                       value set in this register .                      ( RW )               value low

             Soft limit positive
0x0037       high position    Software positive limit setting      -0xFFFFFFF ~
                                   point, this limit point will take                                                     0xFFFFFFF       0
             Soft limit positive   effect only after zero return is
0x0038       low position     completed ;                                       ( RW )

            Soft limit negative
0x0039       high position    Software negative limit setting     -0xFFFFFFF ~
                                   point, this limit point will take                                                     0xFFFFFFF       0                                   effect only after zero return is
            Soft limit negative0x003A                     completed ;                                       ( RW )              low position
                               the zero return mode is set to -1 ~-
               Collision return   4 , if the position error of the stall0x00 3B                                                   5 0~4000       200                threshold        is greater than this value, it is
                             judged as a valid collision;
                               the zero return mode is set to -1 ~-
               Collision return                          4 and the zero return motion is
0x 003C         current                                      2 0~100        50                                   started, the motion current will be
              percentage                            reduced to the set percentage;

                     Input and output terminal parameter group

                                     Bit 0 : Input terminal X0 control
                                            bit;
                                     Bit 1 : Input terminal X1 control
                                            bit;
                                     Bit 2 : Input terminal X2 control
              Input terminal      bit;                            0~655350x0040                                                               0
                effective level     Bit 3 : Input terminal X3 control              ( RW )
                                            bit;
                                     Bit 4 ~Bit15 : Reserved;
                          0 : default;
                          1 : Level inversion;

             Input terminal X0  0 : undefined;                     0~17
0x004 1     terminal function                                               1                          1 : origin signal;                                  ( RW )


                                              71
```


## PDF page 74

```text
                                                       Modbus Series Bus Driver Function Manual

                 selection      2 : Positive limit signal;
                          3 : Anti-limit signal;
                          4 : Motor MF signal;            Input terminal X1
             terminal function  5 : Stop signal;                    0~170x004 2                                                               2
                 selection                          6 : Emergency stop signal;                   ( RW )
                          7 : Position mode movement;            Input terminal X2
             terminal function   8: Speed mode movement;           0~170x004 3                                                               3
                 selection                          9 : JOG+ point motion;                         ( RW )
                          10 : JOG -point movement;
                          1 1 : Return to origin enable
                                   signal;
                          1 2 : PT trigger signal ;
            Input terminal X3                                                        0~17                               13: PV trigger signal;0x004 4     terminal function                                               0
                                                                                                  ( RW )                 selection     1 4 : PIN0 ;
                          1 5 : PIN1 ;
                          1 6 : PIN2 ;
                          1 7 : PIN3 ;
                                 Bit0 : Output terminal Y0 control
                                           bit;
                                 Bit1 : Output terminal Y1 control
            Output terminal                                 0~65535                                           bit;0x004B                                                               0
               effective level                                                             ( RW )                                     Bit 2 ~Bit15 : Reserved;
                          0 : default;
                          1 : Level inversion;

            Output terminal    0: undefined ;
                                                        0~11
0x004C     Y0 terminal      1: Alarm signal;                                 0
            function selection                                                          ( RW )                                   2: Driver status signal;
                                   3: Return to origin completion
                                   signal;
                                   4: Arrival signal;            Output terminal                                                        0~11
0x004D     Y1 terminal      5: Braking signal;                                0
                                                                                                  ( RW )            function selection   9: User defined 0;
                               10: User defined 1;
                               11: User defined 2;
                                 Bit0 : Y0 terminal output status;
                                 Bit1 : Y1 terminal output status;
         Y group terminal                                0~655350x004F                            Bit 2 ~Bit15 : Reserved;                           0
            custom output                                                            ( RW )
                          0 : Output is invalid;
                          1 : Output is valid;

               Multi-stage positioning/speed control parameter group

                                              72
```


## PDF page 75

```text
                                                        Modbus Series Bus Driver Function Manual


                       When using IO to control
                              positioning motion /
             Relative position /  multisegment position control ,       0~1
 0x0050                                                                   0
             absolute position   this bit is valid:                                   ( RW )
                           0 : relative position;
                           1 : absolute position;
         PV trigger signal   0: level is valid;                     0~1
 0x0051                                                                   0                 level selection                                    1: rising edge is valid;                          ( RW )
           The first segment  The first section of motion pulse
 0x00 6 0      motion pulse     instruction is a 32-bit register. If                      0                                                       -0xFFFFFFF ~           command high   100000 pulses are set, the high
                                           bit is 0x0001 , the low bit is        0xFFFFFFF
           The first segment  0x86A0 , and the value written                                                                                                    ( RW ) 0x0061      motion pulse     into the register is 0x000186A0.                    500 0
           command low
                                                           0 -3000
             Positioning speed  the first position positioning, unit:                    120 0x00 62                                                                  r/min               of the first stage   r/min;                                                  (0/min)
                                                                                                  ( RW/S )
                  1st stage                          The first segment position                 positioning                                    0 -2000ms
 0x0063                         positioning motion acceleration, in                 5 0 (0ms)
                 motion                                                                ( RW/S )                            ms;                acceleration
              1st stage position
                  positioning    The first stage of the positioning      0 -2000ms 0x0064                                                                5 0 (0ms)
                 motion       motion deceleration, unit: ms;              ( RW/S )
                deceleration
                                                              0
 0x0065         reserve       reserve                                         0                                                                                                  ( RW/S )

 0x0066     Parameters of
                              Refer to 0x0060~0x0065;                         -                   -
 ~0x006B       Section 2
  0x00
             Parameters of
6C~0x007                     Refer to 0x0060~0x0065;                         -                   -
                Section 3
    1
0x0072~0    Parameters of
  x0077                      Refer to 0x0060~0x0065;                         -                   -                Section 4

0x0078~0    Parameters of
  x007D                      Refer to 0x0060~0x0065;                         -                   -                Section 5
0x007E~0    Parameters of
                              Refer to 0x0060~0x0065;                         -                   -  x0083                Section 6
0x0084~0    Parameters of
                              Refer to 0x0060~0x0065;                         -                   -  x0089                Section 7
0x008A~0    Parameters of
  x008F                      Refer to 0x0060~0x0065;                         -                   -                Section 8
                                               73
```


## PDF page 76

```text
                                                        Modbus Series Bus Driver Function Manual


0x0090~0    Parameters of
                              Refer to 0x0060~0x0065;                         -                   -  x0095                Section 9
 0x0096     Parameters of
                              Refer to 0x0060~0x0065;                         -                   -
 ~0x009B       Section 10
 0x009C     Parameters of
                              Refer to 0x0060~0x0065;                         -                   -
 ~0x00A1       Section 11

0x00A2~0    Parameters of
                              Refer to 0x0060~0x0065;                         -                   -  x00A7               Section 12
0x00A8~0    Parameters of
 x00AD                      Refer to 0x0060~0x0065;                         -                   -               Section 13
0x00AE~0    Parameters of
  x00B3                       Refer to 0x0060~0x0065;                         -                   -               Section 14

0x00B4~0    Parameters of
  x00B9                       Refer to 0x0060~0x0065;                         -                   -               Section 15

0x00BA~0    Parameters of
  x00BF                       Refer to 0x0060~0x0065;                         -                   -               Section 16

                 1st speed                                       -3000 -3000
           segment running   running speed of the first speed                    10 0 0x00 C0                                                                  r/min
               speed       segment is in r/min ;                                    (0/min)
                                                                                                  ( RW/S )
                 1st speed
                          The first speed segment motion      0 -2000ms 0x00C1       segment                                                  5 0 (0ms)
                                  acceleration, unit: ms;                        ( RW/S )
             acceleration time
                          The first speed segment is the
              1st speed stage   deceleration of the movement, in     0 -2000ms 0x00C2                                                               5 0 (0ms)
             deceleration time  ms;                                                 ( RW/S )

0x00C3~0    Parameters of
  x00C5                     Reference 0x00C0 ~0x00C2                     -                   -                Section 2

0x00C6~0    Parameters of
                             Reference 0x00C0 ~0x00C2                     -                   -  x00C8                Section 3
0x00C9~0    Parameters of
 x00CB                     Reference 0x00C0 ~0x00C2                     -                   -                Section 4

0x00CC~    Parameters of
                             Reference 0x00C0 ~0x00C2                     -                   -
 0x00CE        Section 5
0x00CF~0    Parameters of
                             Reference 0x00C0 ~0x00C2                     -                   -  x00D1                Section 6
0x00D2~0    Parameters of
  x00D4                     Reference 0x00C0 ~0x00C2                     -                   -                Section 7
0x00D5~0    Parameters of
  x00D7                     Reference 0x00C0 ~0x00C2                     -                   -                Section 8
                                               74
```


## PDF page 77

```text
                                                       Modbus Series Bus Driver Function Manual


0x00D8~0    Parameters of
 x00DA                     Reference 0x00C0 ~0x00C2                     -                   -                Section 9

0x00DB~0    Parameters of
 x00DD                     Reference 0x00C0 ~0x00C2                     -                   -               Section 10

0x00DE~0    Parameters of
  x00E0                     Reference 0x00C0 ~0x00C2                     -                   -               Section 11

0x00E1~0    Parameters of
  x00E3                     Reference 0x00C0 ~0x00C2                     -                   -               Section 12

0x00E4~0    Parameters of
  x00E6                     Reference 0x00C0 ~0x00C2                     -                   -               Section 13

0x00E7~0    Parameters of
  x00E9                     Reference 0x00C0 ~0x00C2                     -                   -               Section 14
0x00EA~0    Parameters of
 x00EC                     Reference 0x00C0 ~0x00C2                     -                   -               Section 15
0x00ED~0    Parameters of
  x00EF                     Reference 0x00C0 ~0x00C2                     -                   -               Section 16
              Multi-segment   The starting speed of each
 0x0130                                                    -180~180 rpm             motion starting                           segment of movement (shared by                       0r pm
~0x013F        speed                                                                ( RW /S)
                       PT mode and PV mode)

                        Performance parameter group

                                Drive operating mode;
               Closed-loop                                      1 ~ 2 0x0100                          1: open loop;                                     2
                 algorithm                                                              ( RW/S )
                           2 : Algorithm 1;
                            Encoder resolution, which is four
               Encoder                                    0~ 65535 0x0101                       times the encoder value, and the                  4000                  resolution                                  default is a 1000-line encoder;             ( RW/S )

                        The maximum current value
                              output by the driver. The register           Maximum                                    0~ 5600     5600/220
 0x0102                      value is given according to the
              effective current   rated current of the motor, in              ( RW/S )        0
                        mA.

                        The maximum current
                            percentage in closed-loop
                      mode , in % , when the driver is
                                    in closed-loop mode, the             Closed loop
                       maximum current that can be        0~150 0x0103   maximum current                                                100
                              output under rated load is the            ( RW/S )              percentage
                             product of the 2042h register
                             value and this register value;



                                               75
```


## PDF page 78

```text
                                                       Modbus Series Bus Driver Function Manual

                            Registered current percentage ,
                                  unit % , when the driver is
                            running at no load, the output           Base current                                    0~ 750x0104                       current value is the product of                     40
             percentage                                                             ( RW/S )                              the 2042h register value and this
                                 register value;

                        The maximum open-loop current
                            percentage, in % , when the
                                 driver is in open-loop mode, the           Open loop                                                          0~1000x0105   maximum current maximum output current during                    100                                                                                                ( RW/S )
             percentage     operation is the product of the
                        2042h register value and this
                                 register value;
                          Lock current percentage,
                                  unit % . When the driver is in
                              the locked state , the lock            Lock current                                    0 - 1000x0106                       current value is the product of                     100
             percentage                                                             ( RW/S )                              the 2042h register value and the
                                 register value.

                                   for the motor to switch to the lock
                                                         0~20000                               state after it stops moving , in0x0107      Lock time                                                   4000
                         ms;                                                ( RW/S )

          IO terminal filter                                  0 ~ 65535
0x0108        coefficient     Terminal filter coefficient;                          2                                                                                                ( RW/S )

                           Pulse control command filter
                                  coefficient;
           Pulse low-pass                                   0~1024
0x0109                   Low-pass filter coefficient, the                      5                  filter coefficient                                                          ( RW/S )
                              smaller the value, the more
                            obvious the filtering effect;

                Position
            deviation alarm   Position deviation alarm           1~ 655350x010A                                                                 4000
               threshold      threshold ;                                       ( RW/S )

            Positioning error                                   1~256
0x010B        range        Positioning error range ;                    ( RW/S )        5

                                                          0~200
0x010C      End time     End time of arrival ;                          ( RW/S )       1 0

                      Mean filter coefficient, the larger
          Pulse mean filter                                  0~ 5120x010D                      the value, the more obvious the                    512
                coefficient                                                             ( RW/S )
                                      filtering effect;
           Current loop Kp                                 0~ 65535
0x010E      gain multiple    Current loop Kp gain multiple ;                      4096                                                                                                ( RW/S )

                                              76
```


## PDF page 79

```text
                                                      Modbus Series Bus Driver Function Manual

           Current loop Kp                                 0~ 65535
0x010F          gain        Current loop Kp gain ;                              1024                                                                                                ( RW/S )

           Current loop Ki                                  0~ 65535
0x0110          gain        Current loop Ki gain ;                                28                                                                                                ( RW/S )

           Current loop Kc                                 0~ 65535
0x0111          gain        Current loop Kc gain ;                              1228                                                                                                ( RW/S )

                                                         0~65535
0x0112    LA speed Kp1   LA speed Kp1 ;                                     10                                                                                                ( RW/S )

                                                        0~ 65535
0x0113    LA Speed Kv1   LA speed Kv1 ;                                     32                                                                                                ( RW/S )

         LA Speed Node                                 0~ 65535
0x0114          1       LA speed node 1 ;                                  320                                                                                                ( RW/S )

                                                         0~65535
0x0115    LA speed Kp2   LA speed Kp2 ;                                     15                                                                                                ( RW/S )

                                                         0~65535
0x0116    LA Speed Kv2   LA speed Kv2 ;                                     33
                                                                                                ( RW/S )
         LA Speed Node                                 0~ 65535
0x0117                 LA speed node 2 ;                                  320               2                                                                                                ( RW/S )
           LA   speed
                                                         0~65535           feedforward Kvf0x0118                 LA speed feedforward Kvf gain ;                      20
           gain                                                                          ( RW/S )

         LA position loop                                  0~65535
0x0119                 LA position loop Ki gain ;                            35                Ki gain                                                                                                ( RW/S )
             Collision return   Position error value after           200~4000
0x0122     to zero threshold   collision;                                           2 00                                                           (RW/S)
                        The ratio of the current
             Collision return   magnitude when the collision        2 0~100
0x0123      to zero current   returns to zero to that during                         50
                                                (RW /S )             percentage    normal motion;





                                              77
```


## PDF page 80

```text
                                                         Modbus Series Bus Driver Function Manual
Appendix 3: Modbus register parameter table – DM-PR series

                                                                     Setting range
  Register                                                      Note: Other      default              project               illustrate
  Address                                                     values are      value
                                                                              invalid.

 Status parameter group (read-only)

 0x0000      Driver Model       Drive model code                          ( RO )         0x0305

 0x0001      Driver version       Driver version                               ( RO )         0x0100

              Drive Node      MODBUS current communication 0x0002                                                                                    ( RO )               -
           Number            slave node number
                                    Bit0~Bit6: SW1~SW7 status;
 0x0003     DIP status code     0: OFF;                                       ( RO )               -
                              1:ON;
              Current error      0 : Normal; 0x0006                                                                                    ( RO )               -
            code            1~5 : Error;
                                   Bit0: In place flag;
                                     0: not in place, 1: in place;
                                   Bit1: Return to origin
                            completed bit;
                                     0: not completed, 1: completed;
                                   Bit2: Motor running position;
                                     0: stationary, 1: running;
                                   Bit3: alarm bit;
 0x0007     Motion status bit                                                       ( RO )               -                                     0: normal, 1: alarm;
                                   Bit4: Motor enable bit;
                                     0: enable, 1: release;
                                   Bit5: Forward soft limit
                                overtravel flag ;
                                     0: invalid; 1: valid;
                                   Bit6: Negative soft
                                      limit overtravel flag ; 0:
                                        invalid; 1: valid;
                                        Bit 0 : X0 terminal input status;
                                        Bit 1 : X1 terminal input status;
               Input terminal        Bit 2 : X2 terminal input status; 0x0008                                                                                    ( RO )               -
               status flag            Bit 3 : X3 terminal input status;
                                        Bit 4 : X4 terminal input status;
                                        Bit 5 : X5 terminal input status;

                                                78
```


## PDF page 81

```text
                                                       Modbus Series Bus Driver Function Manual

                                     Bit 6 : X6 terminal input status;
                                     Bit 7 ~Bit15 : Reserved;

                          0 : Input level is invalid;
                          1 : Input level is valid;
                                    Bit0: Y0 terminal output status;
                                    Bit1: Y1 terminal output status;
                                     Bit 2 : Y 2 terminal output status;            Output terminal0x0009                                                                                        ( RO )               -
                status flag       Bit 2 ~Bit15: reserved;
                                  0: output level is invalid;
                                  1: Output level is valid;

            Current position  The current position value of the
0x000A           high        motor. For an open-loop motor,         ( RO )               -
                                     this register value is the position
                              given  value.  For a  closed-loop
            Current position   motor,  this register value  is the0x000B                                                                                        ( RO )               -                 low         subdivision   equivalent   of   the
                            encoder feedback value.

0x000C     Current speed    Current motor running speed                 ( RO )               -

                      Driver basic control parameter group 1
                 Default
                          0 : default direction ;                 0~ 1
0x0010      movement                                                    0
                  direction      1 : Reverse direction ;                          ( RW )

                           Address - Segment               400~51200
0x0011   Segment settings                                               1000
                           400~51200;                                       ( RW )
                          0 ~ 255 : custom slave address;
           Custom drive    This register takes effect when all      0~ 2550x0013                                                                   0                               the drive address DIP switches            node number                                                             ( RW )
                              are OFF;

                           0:115200
                          1 : 38400
             Customize     2 : 19200                                                          0~ 3
0x0014     communication                                                  0                          3 : 9600                                                                                                  ( RW )             baud rate
                              Note: After modification, power
                          must be turned on again to take
                                     effect;
                          0 : 8 -bit data, no parity, 1 stop bit;
                          1 : 8 -bit data, no parity, 2 stop
              Serial port data    bits;                              0~ 30x0015                                                                   0
                 format      2 : 8 -bit data, even parity, 1 stop           ( RW )
                                           bit;
                          3 : 8 -bit data, odd parity, 1 stop


                                              79
```


## PDF page 82

```text
                                                       Modbus Series Bus Driver Function Manual

                                           bit;
                              Note: After modification, power
                          must be turned on again to take
                                     effect;
             Over-limit parking  0 : free parking; 1 :                  0~ 1
0x0017        method      Emergency stop;                                 0                                                                                                  ( RW )
                                  0: invalid;
             Internal software                                   0~ 10x0018                          1: Take effect after returning to                      0                  limit switches                                                             ( RW )                                zero;
                                  0: high position first, low position
                                       last;
               32-bit register     1: High position at the back, low        0~ 1
0x0019       endianness                                                   0
                                 position at the front;                            ( RW )               configuration
                           Adapt to different PLC or touch
                             screen usage habits;
                    motion control parameters of the drive
                                                             -3000 -300 0
             Jog mode     Jog mode running speed, unit:                      1200x001D                                                                  r/min            operation speed   r/min;                                                    (5r/min)                                                                                                  ( RW )

             Jog mode     Jog mode acceleration time, in       0-2000ms       50
0x001E     acceleration time  ms;                                                                                                  ( RW )      (100ms)
             Jog mode     Jog mode deceleration time, unit:     0-2000ms       50
0x001F     deceleration time  ms;                                                                                                  ( RW )      (100ms)
                         The starting speed of the          0 ~3000r/min      30
0x0020      Starting speed    positioning movement , in r/min ;                                                                                                  ( RW )       (60r/min)
                                                         0-2000ms       50
0x0021    Acceleration time   Acceleration time , in ms;
                                                                                                  ( RW )      (100ms)
                                                         0-2000ms       50
0x0022    Deceleration time  Deceleration time , in ms;
                                                                                                  ( RW )      (100ms)
                        Movement speed during
               Positioning                                   0 ~3000r/min      600x0023                         positioning movement , unit:
         movement speed                                                          ( RW )       (60r/min)
                                r/min ;
            Total pulse count  The total number of pulses for
0x0024           high                                 positioning motion operation is a
                                                                                              - 0xFFFFFFF                                  32-bit register. If 100000 pulses
                                                          ~
                              are set, the high bit is 0x0001 ,                     5000
                                                    0xFFFFFFFF            Total pulse count   the low bit is 0x86A0 , and the0x0025                 low         value written to the register is                ( RW )
                           0x000186A0;
                                Bit0: Position mode start
            Movement                                    0~6 5535
0x0027                command bit;                                                           -
             Control Order                                                             ( WO )
                                  0; invalid;

                                              80
```


## PDF page 83

```text
                                                       Modbus Series Bus Driver Function Manual

                                  1: Valid;
                                Bit1: Speed mode start
                     command bit;
                                  0; invalid;
                                  1: Valid;
                                Bit2: Position mode positioning
                         method;
                                  0: relative positioning;
                                  1: absolute positioning; Bit3:
                           Sports mode switching
                         method;
                                  0: Ignore motion commands; 1:
                                   interrupt the current motion and
                             execute it immediately;
                                Bit4: Return to origin start
                     command bit;
                                  0; invalid;
                                  1: Valid;
                                Bit8: stop command bit;
                                  0; invalid;
                                  1: Valid;
                                Bit9: Emergency stop
                     command bit;
                                  0; invalid;
                                  1: Valid;
                            0x0000: invalid;

                            0x0011: Motor release;

                            0x0012: Motor enable;

                            0x0021: drive alarm cleared;

                            0x0031: Clear the current position
                                  of the motor;
              Auxiliary control   0x0041:  Restore  parameters  to    0~ 655350x002D                                                                                                               -
                instructions                                                              ( WO )                                 factory settings;
                            0x0042: save all parameters;

                              Note: When performing factory
                              recovery and parameter saving
                               operations on the drive, it is
                            necessary to ensure that the
                            motor is in the stopped state,



                                              81
```


## PDF page 84

```text
                                                      Modbus Series Bus Driver Function Manual

                              otherwise the relevant instructions
                                         will be ignored;


                    Drive return to zero motion control parameters

                                  0: Run the offset, and after
                               completion, the current position is
                               the offset value;
                                  1: Run the offset, and the current
                                 position is 0 after completion; 2 :
                       Run the offset, and after
                             completion the current position is
                          a negative offset value; 3: Run
                               the offset, and after completion,
                               the current position is the actual
                                value; 4: Run the offset. After
                               completion, the current position
                                        is the actual value plus the offset
                                value.
                                  5: Run the offset. After
                               completion, the current position is
                               the actual value minus the offset
                                value.
             Zero return      6: Do not run the offset, and the        0 ~110x0030                                                                   0
              auxiliary setting                                  (RW)                                current position after completion is
                               the offset value;
                          7 : Do not run the offset, the
                                current position is 0 after
                               completion;
                                  8: Do not run the offset, and the
                                current position will be a negative
                                   offset value after completion; 9:
                      Do not run the offset, and the
                                current position is the actual
                              value after completion;
                          1 0 : Do not run the offset. After
                               completion, the current position is
                               the actual value plus the offset
                                value.
                          1 1 : Do not run the offset. After
                               completion, the current position is
                               the actual value minus the offset
                                value.


                                             82
```


## PDF page 85

```text
                                                       Modbus Series Bus Driver Function Manual

            Return to origin                                  0~ 65535      twenty
0x0031       mode       Support 17~30, 35 modes;                                                                                                  ( RW )          four

            Return to origin   Running speed when querying the    5-3000r/min      120
0x0032        speed                                                               (60r/min)                                   origin position;                                   ( RW )

            Return to origin   The return speed after querying      5-300 r/min      60
0x0033             query speed     the origin;                                                                                                  ( RW )       (60r/min)
            Acceleration and
                         The acceleration and deceleration            deceleration time                                 30-2000ms      50
0x0034                       time when querying the origin
         when returning to                                                           ( RW )      (100ms)                                  position;
                   origin
               Origin offset
0x0035                         Origin offset value: after finding     -0xFFFFFFF ~               value high                               the origin sensor, it moves to the                                                     0xFFFFFFF       0
               Origin offset     correct position according to the
0x0036               value low      value set in this register .                      ( RW )

            Soft limit positive
0x0037       high position    Software positive limit setting      -0xFFFFFFF ~
                                   point, this limit point will take                                                     0xFFFFFFF       0                                   effect only after zero return is
            Soft limit positive0x0038                     completed ;                                       ( RW )             low position
            Soft limit negative
0x0039                      Software negative limit setting     -0xFFFFFFF ~              high position
                                   point, this limit point will take                                                     0xFFFFFFF       0
            Soft limit negative  effect only after zero return is
0x003A             low position     completed ;                                       ( RW )

                    Input and output terminal parameter group
                                     Bit 0 : Input terminal X0 control
                                           bit;
                                     Bit 1 : Input terminal X1 control
                                           bit;
                                     Bit 2 : Input terminal X2 control
                                           bit;
                                     Bit 3 : Input terminal X3 control
                                           bit;
              Input terminal                                 0~655350x0040                            Bit 4 : Input terminal X 4 control                     0
               effective level                                                             ( RW )
                                           bit;
                                     Bit 5 : Input terminal X 5 control
                                           bit;
                                     Bit 6 : Input terminal X 6 control
                                           bit;
                                     Bit 7 ~Bit15 : Reserved;
                          0 : default;
                          1 : Level inversion;

                                              83
```


## PDF page 86

```text
                                                       Modbus Series Bus Driver Function Manual


            Input terminal X0                          0 : undefined;                     0~17
0x0041     terminal function                                               1
                          1 : origin signal;                                  ( RW )                 selection
                          2 : Positive limit signal;            Input terminal X1                                                       0~17
0x0042     terminal function  3 : Anti-limit signal;                              2
                                                                                                  ( RW )                 selection     4 : Motor MF signal;
            Input terminal X2  5 : Stop signal;                                                       0~17
0x0043     terminal function                          6 : Emergency stop signal;                        3                 selection                                                                ( RW )
                          7 : Position mode movement;
            Input terminal X3                                  8: Speed mode movement;           0~17
0x0044     terminal function  9 : JOG+ point motion;                           0
                 selection                                                                ( RW )                          10 : JOG -point movement;
                          1 1 : Return to origin enable            Input terminal X 4                                   signal;                          0~17
0x0045     terminal function                                               0                          1 2 : PT trigger signal ;                 selection                                                                ( RW )
                               13: PV trigger signal;
            Input terminal X 5                          1 4 : PIN0 ;                       0~17             terminal function0x0046                                                               0
                          1 5 : PIN1 ;                                        ( RW )                 selection
                          1 6 : PIN2 ;
            Input terminal X 6                          1 7 : PIN3 ;                       0~1 8
0x0047     terminal function                                               0
                 selection     1 8 : PV direction signal;                      ( RW )
                                 Bit0 : Output terminal Y0 control
                                           bit;
                                 Bit1 : Output terminal Y1 control
                                           bit;
            Output terminal                                 0~655350x004B                            Bit 2 : Output terminal Y 2 control                   0
               effective level                                                             ( RW )
                                           bit;
                                     Bit 2 ~Bit15 : Reserved;
                          0 : default;
                          1 : Level inversion;
           Output terminal    0: undefined ;
                                                       0~11
0x004C     Y0 terminal      1: Alarm signal;                                 0
            function selection                                                          ( RW )                                  2: Driver status signal;
           Output terminal    3: Return to origin completion
                                                       0~11
0x004D     Y1 terminal      signal;                                        0
            function selection                                                          ( RW )                                  4: Arrival signal;
                                  5: Braking signal;
           Output terminal                                  9: User defined 0;                  0~11
0x004E     Y2 terminal                                                 0                               10: User defined 1;                             ( RW )            function selection
                               11: User defined 2;
                                 Bit0 : Y0 terminal output status;
        Y group terminal                                0~655350x004F                         Bit1 : Y1 terminal output status;                     0
           custom output                                                            ( RW )
                                     Bit 2 ~Bit15 : Reserved;

                                              84
```


## PDF page 87

```text
                                                        Modbus Series Bus Driver Function Manual



                           0 : Output is invalid;
                           1 : Output is valid;
                Multi-stage positioning/speed control parameter group
                       When using IO to control
                              positioning motion , this bit is
             Relative position /                                   0~1
 0x0050                            valid:                                           0
             absolute position                                                           ( RW )
                           0 : relative position;
                           1 : absolute position;
         PV trigger signal   0: level is valid;                     0~1
 0x0051       level selection                                                  0                                    1: rising edge is valid;                          ( RW )

          The first segment  The first section of motion pulse
              motion pulse     instruction is a 32-bit register. If 0x0060                                                                   0                                                       -0xFFFFFFF ~           command high   100000 pulses are set, the high
                                           bit is 0x0001 , the low bit is        0xFFFFFFF
          The first segment  0x86A0 , and the value written                                                                                                    ( RW )
 0x0061      motion pulse     into the register is 0x000186A0.                    5000
           command low
                                                           0 -3000
             Positioning speed  the first position positioning, unit:                    120 0x0062                                                                   r/min               of the first stage   r/min;                                                  (0/min)
                                                                                                  ( RW/S )
                  1st stage                          The first segment position                 positioning                                    0 -2000ms       50 0x0063                         positioning motion acceleration, in
                 motion                                                                ( RW/S )       (0ms)                            ms;                acceleration
             1st stage position
                  positioning                          The first stage of the positioning      0 -2000ms       50 0x0064         motion
                              motion deceleration, unit: ms;              ( RW/S )       (0ms)                deceleration

                       When using IO to control
                           multisegment position motion ,                  1st stage                                                            0 ~1
 0x0065        positioning       this bit is valid:                                   0
                                                                                                  ( RW/S )              motion mode                                    0: relative position;
                                    1: absolute position;
 0x0066     Parameters of
                             Reference 0x0060~0x0065;                      -                   -
 ~0x006B       Section 2
  0x00
             Parameters of
6C~0x007                     Refer to 0x0060~0x0065;                         -                   -
                Section 3    1
0x0072~0    Parameters of
  x0077                      Reference 0x0060~0x0065;                      -                   -                Section 4

0x0078~0    Parameters of
  x007D                     Reference 0x0060~0x0065;                      -                   -                Section 5

                                               85
```


## PDF page 88

```text
                                                        Modbus Series Bus Driver Function Manual


0x007E~0    Parameters of
  x0083                      Reference 0x0060~0x0065;                      -                   -                Section 6
0x0084~0    Parameters of
  x0089                      Reference 0x0060~0x0065;                      -                   -                Section 7

0x008A~0    Parameters of
  x008F                      Reference 0x0060~0x0065;                      -                   -                Section 8
0x0090~0    Parameters of
  x0095                      Reference 0x0060~0x0065;                      -                   -                Section 9
 0x0096     Parameters of
                             Reference 0x0060~0x0065;                      -                   -
 ~0x009B       Section 10
 0x009C     Parameters of
                             Reference 0x0060~0x0065;                      -                   -
 ~0x00A1       Section 11
0x00A2~0    Parameters of
  x00A7                      Reference 0x0060~0x0065;                      -                   -               Section 12
0x00A8~0    Parameters of
                              Refer to 0x0060~0x0065;                         -                   - x00AD               Section 13
0x00AE~0    Parameters of
                             Reference 0x0060~0x0065;                      -                   -  x00B3               Section 14
0x00B4~0    Parameters of
                             Reference 0x0060~0x0065;                      -                   -  x00B9               Section 15
0x00BA~0    Parameters of
  x00BF                       Refer to 0x0060~0x0065;                         -                   -               Section 16
                 1st speed                                       -3000 -3000
           segment running   running speed of the first speed                     100 0x00C0                                                                  r/min
               speed       segment is in r/min ;                                    (0/min)
                                                                                                  ( RW/S )
                 1st speed
                          The first speed segment motion      0 -2000ms       50 0x00C1       segment
                                  acceleration, unit: ms;                        ( RW/S )       (0ms)
             acceleration time
                          The first speed segment is the
              1st speed stage                                  0 -2000ms       50 0x00C2                        deceleration of the movement, in             deceleration time                            ms;                                                 ( RW/S )       (0ms)

0x00C3~0    Parameters of
  x00C5                     Reference 0x00C0 ~0x00C2                     -                   -                Section 2

0x00C6~0    Parameters of
                             Reference 0x00C0 ~0x00C2                     -                   -  x00C8                Section 3
0x00C9~0    Parameters of
 x00CB                     Reference 0x00C0 ~0x00C2                     -                   -                Section 4

0x00CC~    Parameters of
                             Reference 0x00C0 ~0x00C2                     -                   -
 0x00CE        Section 5
                                               86
```


## PDF page 89

```text
                                                        Modbus Series Bus Driver Function Manual


0x00CF~0    Parameters of
  x00D1                     Reference 0x00C0 ~0x00C2                     -                   -                Section 6

0x00D2~0    Parameters of
  x00D4                     Reference 0x00C0 ~0x00C2                     -                   -                Section 7

0x00D5~0    Parameters of
  x00D7                     Reference 0x00C0 ~0x00C2                     -                   -                Section 8

0x00D8~0    Parameters of
 x00DA                     Reference 0x00C0 ~0x00C2                     -                   -                Section 9

0x00DB~0    Parameters of
 x00DD                     Reference 0x00C0 ~0x00C2                     -                   -               Section 10

0x00DE~0    Parameters of
  x00E0                      Reference 0x00C0 ~0x00C2                     -                   -               Section 11

0x00E1~0    Parameters of
  x00E3                      Reference 0x00C0 ~0x00C2                     -                   -               Section 12
0x00E4~0    Parameters of
                             Reference 0x00C0 ~0x00C2                     -                   -  x00E6               Section 13
0x00E7~0    Parameters of
                             Reference 0x00C0 ~0x00C2                     -                   -  x00E9               Section 14
0x00EA~0    Parameters of
 x00EC                     Reference 0x00C0 ~0x00C2                     -                   -               Section 15
0x00ED~0    Parameters of
  x00EF                     Reference 0x00C0 ~0x00C2                     -                   -               Section 16
              Multi-segment   The starting speed of each
 0x0130                                                    -180~180 rpm             motion starting                           segment of movement (shared by                  0rpm
 ~0x013F        speed                                                                ( RW /S)
                       PT mode and PV mode)
                        Performance parameter group
                         The maximum current value
                              output by the driver. The register           Maximum                                    0~ 7700     5600/770
 0x0102                      value is given according to the
              effective current   rated current of the motor, in              ( RW/S )        0
                        mA.

                         The maximum current
                            percentage in closed-loop
                       mode , in % , when the driver is             Closed loop
                                    in closed-loop mode, the            0~1 0 0            maximum
 0x0103                 maximum current that can be                     100                  current                                                                ( RW/S )                              output under rated load is the              percentage
                              product of the 2042h register
                              value and this register value;


                                               87
```


## PDF page 90

```text
                                                       Modbus Series Bus Driver Function Manual

                            Registered current percentage ,
                                  unit % , when the driver is
           Base current    running at no load, the output        0~ 1000x0104                                                                  40
             percentage     current value is the product of            ( RW/S )
                              the 2042h register value and this
                                 register value;
                        The maximum open-loop current
                            percentage, in % , when the
                                 driver is in open-loop mode, the           Open loop                                                          0~1000x0105   maximum current maximum output current during                    100                                                                                                ( RW/S )
             percentage     operation is the product of the
                        2042h register value and this
                                 register value;
                          Lock current percentage,
                                  unit % . When the driver is in
            Lock current    the locked state , the lock           0 - 1 000x0106                                                                  100
             percentage     current value is the product of            ( RW/S )
                              the 2042h register value and the
                                 register value.
                                   for the motor to switch to the lock
                                                          0~20000x0107      Lock time      state after it stops moving , in                     200                                                                                                 ( RW/S )                         ms;
          IO terminal filter                                  0 ~ 65535
0x0108                     Terminal filter coefficient;                          2                coefficient                                                                                                ( RW/S )
                           Pulse control command filter
                                  coefficient;
           Pulse low-pass                                   0~10240x0109                                                                   5                  filter coefficient   Low-pass filter coefficient, the            ( RW/S )
                              smaller the value, the more
                            obvious the filtering effect;
                                                          0~200
0x010C      End time     End time of arrival ;                              10                                                                                                ( RW/S )

                      Mean filter coefficient, the larger
          Pulse mean filter                                  0~ 5120x010D                      the value, the more obvious the                    512                coefficient                                                             ( RW/S )
                                      filtering effect;
           Current loop Kp                                 0~ 65535
0x010E      gain multiple    Current loop Kp gain multiple ;           ( RW/S )       4096

           Current loop Kp                                 0~ 65535
0x010F          gain        Current loop Kp gain ;                       ( RW/S )       1024

            Current loop Ki                                  0~ 65535
0x0110          gain        Current loop Ki gain ;                        ( RW/S )        28

           Current loop Kc                                 0~ 65535
0x0111          gain        Current loop Kc gain ;                       ( RW/S )       1228



                                              88
```
