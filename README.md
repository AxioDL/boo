<p align="center">
  <a href="http://axiodl.github.io/libBoo/mascot_big.png">
    <img src="http://axiodl.github.io/libBoo/mascot.png" alt="Boo Mascot" width="256" height="256"/><br><em>Charlie</em>
  </a>
</p>

### libBoo

**libBoo** is a cross-platform windowing and event manager similar to
SDL and SFML, with additional 3D rendering functionality. 

The exclusive focus of libBoo is 2D/3D rendering using polygon-rasterization
APIs like OpenGL or Direct3D. It exposes a unified command-queue API for 
calling the underlying graphics API.

The only per-platform responsibility of the client code is providing the 
shaders' source. Drawing, resource-management and state-switching are
performed using the unified API; these may be written once for all platforms.

Client code is entered via the `appMain` method supplied in a callback object.
This code executes on a dedicated thread with graphics command context available.
The API may be used to synchronize loops on the client thread with the display
refresh-rate.

#### Supported Backends

* OpenGL 3.3+
* Direct3D 11/12
* Metal 1.1 (OS X 10.11 only for now, iOS coming soon)
* **[Coming soon]** OpenGL ES 3.0
* **[Coming soon]** Vulkan
