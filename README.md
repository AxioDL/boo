### libBoo

**libBoo** is a cross-platform windowing and event manager similar to
SDL and SFML, with additional 3D rendering functionality. 

The exclusive focus of libBoo is 3D rendering using polygon-rasterization
APIs like OpenGL or Direct3D. It exposes a unified command-queue API for 
calling the underlying graphics API.

The only per-platform responsibility of the client code is providing the 
shaders' source. Drawing, resource-management and state-switching are
performed using the unified API; these may be written once for all platforms.

#### Supported Backends

* OpenGL 3.3+
* Direct3D 11/12
* Apple Metal 1.1 (OS X 10.11 only for now)
* **[Coming soon]** Vulkan
