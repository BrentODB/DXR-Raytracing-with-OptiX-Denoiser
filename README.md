# DXR-Raytracing-with-OptiX-Denoiser
Project made with DirectX Raytracing to try and understand the API and ray tracing in general.

The project uses 1 sample per pixel to handle soft shadows and GI.
The noisy output is denoised by the neural network denoiser in Optix 6.0.

![Rungholt Scene](http://brentopdebeeck.com/images/RTRunholt.png)
![Sponza Scene](http://brentopdebeeck.com/images/RTSponza.png)
![Cornell Scene](http://brentopdebeeck.com/images/RTCornell.png)

Note that this is still a work in progress. Fixes and new features will be added in the near future.
