#pragma once
#define USE_PIX
#include <pix3.h>

#include "optix_world.h"
#include "stdafx.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>
#include <array>
#include <chrono>
#include "DXRHelpers.h"
#include "Externals/GLM/glm/glm.hpp"
#include <DDSTextureLoader.h>
#include "DirectXHelpers.h"

//int WIDTH = 640;
//int HEIGHT = 480; //480p res

//Custom Res
//const int WIDTH = 760;
//const int HEIGHT = 520; //480p res

const int WIDTH = 1280;
const int HEIGHT = 720; //720p res

//const int WIDTH = 1920;
//const int HEIGHT = 1080; //1080p res

//const int WIDTH = 3840;
//const int HEIGHT = 2160; //4K res

using namespace Microsoft::WRL;

enum Scene
{
	SPONZA,
	CORNELL,
	MINECRAFT,
	RUNGHOLT,
	MIGUEL,
};