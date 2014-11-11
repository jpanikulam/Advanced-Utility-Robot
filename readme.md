MIL Advanced Utility Robot
========================

This is the core codebase for the MIL advanced utility robot. It uses the jOS.h microcontroller framework and the unified IEEE ROS-uC communication package. 

# Requires
* ROS hokuyo package (somebody add how to install this)
* This repository
* ROS Indigo, Ubuntu 14.04
* Python 2.7

# How to install
```git clone git@github.com:ufieeehw/Advanced-Utility-Robot.git```

# How to use

```
  rosrun MILAUR_xmega_driver communication.py

  rosrun MILAUR_controller vehicle_controller.py

  rosrun MILAUR_navigation navigation.py

  rosrun hokuyo_node hokuyo_node

```
Note: navigator.py may not yet be implemented

# TODO
* Add a roslaunch
* Implement navigation
* Add installation instructions for hokuyo node
* Add installation instructions for misc required drivers
* Implement a unified UFIEEE XMega Comms library for rospy


Show Alan how to git fork and pull
