#pragma once
enum usedAPI
{
	DXR, //use DXR API if driver/GPU supports it
	FALLBACK, //if not supported used fallback layers

	//Could be possible but won't support this for the time being
	FALLBACKwDXR,
};

