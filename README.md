# stick_man

An editor / animation system for 2D skeletal animation. The intention here is for animation to be driven by inverse kinematics rather than layering IK as a kind of constraint on top of a fundamentally FK-based animation system, the latter being how I would characterize applications like Spine 2D.

Not done, experimental, seriously a work in progress. Code is Qt + C++23 (It's C++ greater than 17 because I use std::ranges; it's greater than 20 because I use std::ranges::to<T>() which finally makes std::ranges actually useful)
The project is CMake-based but I've only built the code on Windows so YMMV on Linux or Mac. The code currently just has a dependency on Qt6 and Eigen3. I also include Boost in the CMakeLists file because I anticipated needing Boost.Geometry but may drop the boost dependency as QGraphicsView having a built-in BSP-tree means I didn't need to manage my own spatial indeing structure.

https://github.com/jwezorek/stick_man/assets/14944901/9a1b1ebf-104f-4953-973f-b9a0779b111d

