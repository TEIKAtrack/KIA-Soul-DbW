<img src="https://raw.githubusercontent.com/wiki/PolySync/OSCC/images/oscc_logo_title.png">

Joystick commander is an example application designed to show how the Open Source Car Control API can be used to recieve reports from and send commands to a drive by-wire enabled vehicle.

Using an SDL2 supported game controller, inputs are normalized and converted to relative torque, throttle, and brake commands. This application also demonstrates registering callback functions to recieve and parse OSCC reports as well as vehicle state reports from the car's OBD-II CAN network.

For more information about OSCC, check out our [github](https://github.com/PolySync/oscc).


# Pre-requisites:

- OSCC's API and firmware modules are both required, and the modules must be installed on the vehicle
- The socketcan driver for USB and PCIe CAN interfaces is required, and is pre-installed on most Linux systems
- An SDL2 supported game controller is also required, and the SDL2 library must be pre-installed
- A CAN interface adapter, such as the [Kvaser Leaf Light](https://www.kvaser.com), is also necessary in order to connect the API to the OSCC control CAN network via USB

This application has been tested with a Logitech F310 gamepad and a wired Xbox 360 controller, but should work with any SDL2 supported game controller. Controllers with rumble capabilities will provide feedback when OSCC is enabled or disabled.

[Xbox 360 Wired Controller](https://www.amazon.com/dp/B004QRKWLA)

[Logitech Gamepad F310](http://a.co/3GoUlkN)

Install the SDL2 library with the command below.

```
sudo apt install libsdl2-dev
```


# Getting OSCC & Joystick Commander

If you haven't already, install the OSCC hardware modules onto the target vehicle.

Once the hardware is installed and the firmware is flashed, clone and enter the joystick commander repo:

```
git clone git@github.com:PolySync/oscc-joystick-commander.git
cd oscc-joystick-commander
```

From within the joystick commander directory, clone the OSCC repo:

```
git clone git@github.com:PolySync/oscc.git --branch master
```

This will clone into a directory called `oscc` where CMake will look for the OSCC API when it builds joystick commander.


# Building Joystick Commander

From the joystick commander directory, run the following sequence to build it:

```
mkdir build
cd build
cmake .. -DKIA_SOUL=ON
make
```

The KIA_SOUL CMake option enables the API to swap between different vehicle specifications, allowing the firmware and API to remain car agnostic.

After initializing the CAN interface, use the channel number to start joystick commander and begin sending commands to the OSCC modules.

For example with a Kvaser Leaf Light attached, using a bitrate of 500000 and a CAN channel of 0:

```
 sudo ip link set can0 type can bitrate 500000
 sudo ip link set up can0
```

You would then run:

```
./oscc-joystick-commander 0
```

For more information on setting up a socketcan interface, check out [this guide](http://elinux.org/Bringing_CAN_interface_up).


# Controlling the Vehicle with the Joystick Gamepad

Once the joystick commander is up and running you can use it to send commands to the Arduino modules.
Use the left trigger to brake, the right trigger for throttle, and the left gamepad axis to control steering.

The vehicle will only respond to commands if control is enabled with the start button. The back button disables control.


# Application Details


### main

Entry point of joystick commander. Initializes OSCC interface, checks for controller updates in 50 ms intervals, and closes the interface when the program terminates. This contains the applications main loop.


### joystick

`joystick.c` contains the functionality necessary to initialize and interact with the game controller.


### commander

The commander files contain the joystick commander's interactivity with the OSCC API. It demonstrates opening and closing the CAN channel communications with OSCC's control CAN network, sending enable/disable commands to the modules through the API, retrieving OSCC reports through callback functions, and sending commands through the OSCC `publish` functions.


# Using OSCC API

To use the OSCC API in your applications, you need to include any relevant header files.

* The can message protocols are located in `oscc/api/include/can_protocols/`
    * These specify the structs we use for steering, throttle, brake, and fault reports
* Vehicle specific macros and values are located in `oscc/api/include/vehicles/`
	* You only need to include `vehicles.h`, which will include the relevant vehicle-specific header depending on the option provided to CMake (e.g., `-DKIA_SOUL=ON` will include `kia_soul.h`)
* `oscc/api/include/oscc.h` includes the functionality to interface with the OSCC API


# License Information

MIT License

Copyright (c) 2017 PolySync Technologies

Please see [LICENSE.md](LICENSE.md) for more details.


# Contact Information

Please direct questions regarding OSCC and/or licensing to help@polysync.io.

*Distributed as-is; no warranty is given.*

Copyright (c) 2017 PolySync Technologies, Inc.  All Rights Reserved.
