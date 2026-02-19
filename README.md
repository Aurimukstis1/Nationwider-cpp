# Nationwider C++
Rewritten Nationwider tool in C++, using SDL3 as a graphics lib

With additional SDL3_image library.
With additional Dear ImGui library.

Integrates **Amazon AWS S3** for savefile sharing between Nationwide hosts and players.  
*Note: Will most likely create several different modules to use with different file hosting providers, such as: Hetzner.*

---
Building:
```
cmake -B build
```

```
cmake --build build
```

`SDL3.dll` and `SDL3_image.dll` are required for the executable.
