# DXR Raytracing with OptiX 6.0 DNN Denoiser
Project made with DirectX Raytracing to try and understand the API and ray tracing in general.

The project uses 1 sample per pixel to handle soft shadows and GI.
The noisy output is denoised by the neural network denoiser in Optix 6.0.

A release build running the Sponza Scene can be downloaded [here](https://www.dropbox.com/s/twcvux9zhrxpl28/ReleaseBuildV1.rar?dl=0)

**Prerequisites to compile/run**
-  Windows 10 October 2018 update version 1809 (or newer)
-  Windows 10 SDK version 10.0.17763.0 (or newer)
-  NVIDIA Turing or Volta GPU (or newer)
-  Tested on Visual Studio 2017 v15.9.6 (or newer)
-  [Dependencies](https://www.dropbox.com/s/8wi8zmqokgzgeo3/DependenciesRtDemo.rar?dl=0)
-  CUDA version 10.1 installed and added to environment variables (or newer)

![Rungholt Scene](http://brentopdebeeck.com/images/RTRunholt.png)
![Sponza Scene](http://brentopdebeeck.com/images/RTSponza.png)
![Cornell Scene](http://brentopdebeeck.com/images/RTCornell.png)

Note that this is still a work in progress. Fixes and new features will be added in the future.
