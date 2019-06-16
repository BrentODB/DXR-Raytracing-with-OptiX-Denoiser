//#include "Random.hlsl" USE INCLUDE HANDLER

// Source
// https://gamedev.stackexchange.com/questions/32681/random-number-hlsl
// http://www.gamedev.net/topic/592001-random-number-generation-based-on-time-in-hlsl/
// Supposebly from the NVidia Direct3D10 SDK
// Slightly modified for my purposes
#define RANDOM_IA 16807
#define RANDOM_IM 2147483647
#define RANDOM_AM (1.0f/float(RANDOM_IM))
#define RANDOM_IQ 127773u
#define RANDOM_IR 2836
#define RANDOM_MASK 123459876

struct NumberGenerator {
	int seed; // Used to generate values.

	// Returns the current random float.
	float GetCurrentFloat() {
		Cycle();
		return RANDOM_AM * seed;
	}

	// Returns the current random int.
	int GetCurrentInt() {
		Cycle();
		return seed;
	}

	// Generates the next number in the sequence.
	void Cycle() {
		seed ^= RANDOM_MASK;
		int k = seed / RANDOM_IQ;
		seed = RANDOM_IA * (seed - k * RANDOM_IQ) - RANDOM_IR * k;

		if (seed < 0)
			seed += RANDOM_IM;

		seed ^= RANDOM_MASK;
	}

	// Cycles the generator based on the input count. Useful for generating a thread unique seed.
	// PERFORMANCE - O(N)
	void Cycle(const uint _count) {
		for (uint i = 0; i < _count; ++i)
			Cycle();
	}

	// Returns a random float within the input range.
	float GetRandomFloat(const float low, const float high) {
		float v = GetCurrentFloat();
		return low * (1.0f - v) + high * v;
	}

	// Sets the seed
	void SetSeed(uint value) {
		seed = int(value);
		//value += 1024;
		Cycle();
	}
};

struct SceneConstantBuffer
{
	float4x4 cameraToWorld;
	float4 cameraPosition;
	float3 lightPosition;
	float3 lightDirection;
	uint amountOfLights;
	uint sampleCount;
	uint frameCount;
	float2 windowSize;
	//XMVECTOR lightAmbientColor;
	//XMVECTOR lightDiffuseColor;
};

struct Vertex
{
	float3 pos;
	float3 normal;
	float2 uv;
	float4 tangent;
};

struct PointLight
{
	float3 position;
	float radius;
	float3 emissiveColor;
	float intensity;
	float size;
};

struct MeshInfo
{
	uint vertexStride;
	uint vertexDataByteOffset;
	uint hasTexture;
	uint indexStride;

	uint indexDataByteOffset;
	uint isReflective;
	uint indexOffset;
	uint materialIndex;

	float3 diffuseColor;
	uint isRefractive;

	uint emissiveStrength;
	float3 filler;
};

struct colorData
{
	float4 color;
};

RWTexture2D<float4> gOutput : register(u0);
RWBuffer<float4> gBufferOutput : register(u1);
RWBuffer<float4> gAlbedoBufferOutput : register(u2);
RWBuffer<float4> gNormalBufferOutput : register(u3);
RaytracingAccelerationStructure gRtScene : register(t0, space0);
ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
StructuredBuffer<Vertex> g_Vertices : register(t1, space0);
StructuredBuffer<uint32_t> g_Indices : register(t2, space0);
StructuredBuffer<MeshInfo> g_MeshInfo : register(t3, space0);
RWStructuredBuffer<PointLight> g_PointLights : register(u4);
RWStructuredBuffer<colorData> g_ColorData : register(u5);
Texture2D<float4> g_TextureArray[] : register(t6, space0);
SamplerState      g_s0 : register(s0, space0);

static NumberGenerator rndGen;
static uint counter = 0;
static uint MAX_DEPTH = 6;

float4 linearToSrgb(float4 c)
{
	// Based on http://chilliant.blogspot.com/2012/08/srgb-approximations-for-hlsl.html
	float4 sq1 = sqrt(c);
	float4 sq2 = sqrt(sq1);
	float4 sq3 = sqrt(sq2);
	float4 srgb = 0.662002687 * sq1 + 0.684122060 * sq2 - 0.323583601 * sq3 - 0.0225411470 * c;
	return srgb;
}

struct RayPayload
{
	float4 color;
	bool skipShading;
	float3 normal;
	float RayHitT;
	float shadowValue;
	uint depth;
	bool gi;
};

inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
	float2 xy = index + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;

	// Unproject the pixel coordinate into a ray.
	float4 unprojected = mul(g_sceneCB.cameraToWorld, float4(screenPos, 0, 1));
	//float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);
	float3 world = unprojected.xyz / unprojected.w;

	origin = g_sceneCB.cameraPosition.xyz;
	direction = normalize(world - origin);
}

// http://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
uint wang_hash(uint seed)
{
	seed = (seed ^ 61) ^ (seed >> 16);
	seed *= 9;
	seed = seed ^ (seed >> 4);
	seed *= 0x27d4eb2d;
	seed = seed ^ (seed >> 15);
	return seed;
}

float nrand(float2 uv)
{
	return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

// https://www.scratchapixel.com/lessons/3d-basic-rendering/introduction-to-shading/reflection-refraction-fresnel
float3 refractionFunc(float3 I, float3 N, float ior)
{
	float cosi = dot(I, N);
	float etai;
	float etat;
	float3 normal;
	if (cosi < 0.0f)
	{
		etai = 1.0f;
		etat = ior;
		cosi = -cosi;
		normal = N;
	}
	else
	{
		etai = ior;
		etat = 1.0f;
		normal = -N;
	}
	float eta = etai / etat;
	float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
	if (k < 0)
	{
		return float3(0, 0, 0);
	}
	else
	{
		return eta * (I + (eta * cosi - sqrt(k)) * normal);
	}
	return float3(0, 0, 0);
}

// https://gist.github.com/keijiro/ee439d5e7388f3aafc5296005c8c3f33
float3x3 AngleAxis3x3(float angle, float3 axis)
{
	float c, s;
	sincos(angle, s, c);

	float t = 1 - c;
	float x = axis.x;
	float y = axis.y;
	float z = axis.z;

	return float3x3(
		t * x * x + c, t * x * y - s * z, t * x * z + s * y,
		t * x * y + s * z, t * y * y + c, t * y * z - s * x,
		t * x * z - s * y, t * y * z + s * x, t * z * z + c
		);
}

// http://intro-to-dxr.cwyman.org/
float3 GetPerpendicularVector(float3 u)
{
	float3 a = abs(u);
	uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
	uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
	uint zm = 1 ^ (xm | ym);
	return cross(u, float3(xm, ym, zm));
}

// http://intro-to-dxr.cwyman.org/
float3 CosineWeightedHemisphereSample(float3 normal)
{
	float2 random = float2(rndGen.GetCurrentFloat(), rndGen.GetCurrentFloat());

	float3 bitangent = GetPerpendicularVector(normal);
	float3 tangent = cross(bitangent, normal);
	float r = sqrt(random.x);
	float phi = 2.0f * 3.14159265f * (random.x / random.y);

	return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + normal.xyz * sqrt(1 - random.x);
}

[shader("raygeneration")]
void rayGen()
{
	//Set sunlight
	g_PointLights[0].position = g_sceneCB.lightPosition;

	if (DispatchRaysIndex().x == 0 && DispatchRaysIndex().y == 0)
	{
		g_PointLights[0].size += 1;
	}

	if (g_sceneCB.sampleCount == 1)
	{
		//gBufferOutput[DispatchRaysIndex().xy] = float4(0, 0, 0, 1);
	}

	float3 rayDir;
	float3 origin;

	// Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
	GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

	uint2 launchIndex = DispatchRaysIndex();
	uint2 launchDim = DispatchRaysDimensions();
	int idx = launchIndex.y * g_sceneCB.windowSize.x + launchIndex.x; //adjust width

	float2 crd = float2(launchIndex);
	float2 dims = float2(launchDim);

	float2 d = ((crd / dims) * 2.f - 1.f);
	float aspectRatio = dims.x / dims.y;

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = rayDir;

	ray.TMin = 0.001f;
	ray.TMax = 100000;

	float4 ogColor = gOutput[launchIndex.xy];
	float4 ogAlbedo = gAlbedoBufferOutput[idx];
	float4 ogNormal = gNormalBufferOutput[idx];

	RayPayload payload;
	payload.skipShading = false;
	payload.depth = 0;
	payload.shadowValue = 0.0f;
	payload.color = ogAlbedo;
	payload.gi = false;

	TraceRay(gRtScene, 0, ~0, 0 /* ray index*/, 0, 0, ray, payload);

	float4 albedoColor = payload.color;
	float3 normalColor = payload.normal;

	payload.color.xyz = clamp(payload.color.xyz, 0.0f, 1.0f);
	payload.shadowValue = clamp(payload.shadowValue, 0.0f, 1.0f);

	payload.color.r = (payload.shadowValue) * payload.color.r;
	payload.color.g = (payload.shadowValue) * payload.color.g;
	payload.color.b = (payload.shadowValue) * payload.color.b;

	const float lerpFactor = g_sceneCB.sampleCount / (g_sceneCB.sampleCount + 1.0f);
	float3 newSample = payload.color.xyz;
	float3 currValue = ogColor.xyz;
	float3 newValue = lerp(newSample, currValue, lerpFactor);

	// Update the Albedo result with the new color sample
	float3 newAlbedoSample = albedoColor.xyz;
	float3 currAlbedoValue = ogAlbedo.xyz;
	float3 newAlbedoValue = lerp(newAlbedoSample, currAlbedoValue, lerpFactor);

	// Update the Normal result with the new color sample
	float3 newNormalSample = normalColor.xyz;
	float3 currNormalValue = ogNormal.xyz;
	float3 newNormalValue = lerp(newNormalSample, currNormalValue, lerpFactor);

	float4 colTest = float4(0, 0, 0, 1);
	payload.color.r = payload.shadowValue * payload.color.r;
	payload.color.g = payload.shadowValue * payload.color.g;
	payload.color.b = payload.shadowValue * payload.color.b;

	gOutput[launchIndex.xy] = float4(newValue, 1.0f);
	gBufferOutput[idx] = gOutput[launchIndex.xy];
	gAlbedoBufferOutput[idx] = float4(newAlbedoValue, 1.0f);
	gNormalBufferOutput[idx] = float4(newNormalValue, 1.0f);
}

[shader("miss")]
void miss(inout RayPayload payload)
{
	payload.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
	payload.shadowValue = 1.0f;
}

struct IntersectionAttribs
{
	float2 baryCrd;
};

[shader("closesthit")]
void chs(inout RayPayload payload, in IntersectionAttribs attribs)
{
	//Seed
	counter = g_sceneCB.frameCount;
	float rand = nrand(float2(1.0f / (DispatchRaysIndex().x), 1.0f / (DispatchRaysIndex().y) * counter));
	uint seed = wang_hash(uint((DispatchRaysIndex().x * counter * DispatchRaysIndex().y * counter) / (g_sceneCB.sampleCount + 1)));
	rndGen.SetSeed(wang_hash(seed));

	float4 color = float4(0,0,0,1);
	//float4 color = payload.color;
	payload.RayHitT = RayTCurrent();

	if (payload.skipShading)
	{
		return;
	}

	//Mesh Values
	int materialID = InstanceID();
	MeshInfo info = g_MeshInfo[materialID];
	uint primitive = PrimitiveIndex() * 3;
	uint3 ii = uint3(g_Indices[primitive + info.indexOffset], g_Indices[primitive + info.indexOffset + 1], g_Indices[primitive + info.indexOffset + 2]);

	float2 uvTemp1 = g_Vertices[ii[0] + info.vertexDataByteOffset / info.vertexStride].uv;
	float2 uvTemp2 = g_Vertices[ii[1] + info.vertexDataByteOffset / info.vertexStride].uv;
	float2 uvTemp3 = g_Vertices[ii[2] + info.vertexDataByteOffset / info.vertexStride].uv;

	float3 barycentrics = float3(1.0 - attribs.baryCrd.x - attribs.baryCrd.y, attribs.baryCrd.x, attribs.baryCrd.y);
	float2 uvActual = barycentrics.x * uvTemp1 + barycentrics.y * uvTemp2 + barycentrics.z * uvTemp3;

	float3 normToUse = float3(0, 0, 0);
	float3 normActual = float3(0, 0, 0);

	float3 normTemp1 = g_Vertices[ii[0] + info.vertexDataByteOffset / info.vertexStride].normal;
	float3 normTemp2 = g_Vertices[ii[1] + info.vertexDataByteOffset / info.vertexStride].normal;
	float3 normTemp3 = g_Vertices[ii[2] + info.vertexDataByteOffset / info.vertexStride].normal;

	normActual = normalize(normTemp1 + normTemp2 + normTemp3);
	normToUse = normalize(barycentrics.x * normTemp1 + barycentrics.y * normTemp2 + barycentrics.z * normTemp3);
	//#Mesh Values

	float3 worldPosition = (WorldRayOrigin() + WorldRayDirection() * (RayTCurrent()-0.1f)) /*SelfHitting*/;

	if (payload.depth > MAX_DEPTH)
	{
		//payload.color = float4(0.0f, 0.0f, 0.0f, 1.0f);
		//payload.shadowValue = 1.0f;
		return;
	}

	//Diffuse textures
	int id = info.materialIndex;
	if (info.hasTexture == 1)
	{
		color = g_TextureArray[id].SampleLevel(g_s0, uvActual, 0);
	}
	else if (info.hasTexture == 0)
	{
		color.rgb = info.diffuseColor;
	}

	float radiance = payload.shadowValue;
	if (info.emissiveStrength != 0)
	{
		radiance = info.emissiveStrength;
	}

	//Opaque Surface
	if (info.isReflective == 0 && info.isRefractive == 0)
	{
		normToUse = CosineWeightedHemisphereSample(normToUse);
	}
	
	float3 rayDirection = normToUse;
	float3 rayOrigin = worldPosition;

	RayDesc rayDesc;
	rayDesc.Origin = rayOrigin;
	rayDesc.Direction = rayDirection;
	rayDesc.TMin = 0.1f;
	rayDesc.TMax = 100000;

	//Refractions & Reflections
	if (info.isRefractive == 1)
	{
		rayDesc.Direction = normalize(refractionFunc(WorldRayDirection(), normToUse, 1.05f));
	}

	if (info.isReflective == 1)
	{
		rayDesc.Direction = normalize(reflect(WorldRayDirection(), normToUse));
	}

	RayPayload lightPayload;
	lightPayload.skipShading = false;
	lightPayload.gi = true;
	lightPayload.depth = payload.depth + 1;
	TraceRay(gRtScene, 0, ~0, 0, 0, 0, rayDesc, lightPayload);
	if (lightPayload.RayHitT < 100000) //Reflections
	{
		if (info.isReflective == 1)
		{
			if (info.hasTexture == 0)
			{
				color.xyz = lightPayload.color.xyz;
			}
			else
			{
				color.xyz += lightPayload.color.xyz * 0.3f;
			}
		}
		else if (info.isRefractive == 1)
		{
			color.xyz = lightPayload.color.xyz;
			//color.xyz /= 2;
		}
		else
		{
			color.xyz += lightPayload.color.xyz * 0.1f;
			//color.xyz /= 2;
		}
		
	}

	//Transparency
	if (color.w < 0.5f && info.isRefractive == 0)
	{
		RayDesc transRayDesc;
		float3 transWP = (WorldRayOrigin() + WorldRayDirection() * (RayTCurrent() + 0.01f));
		transRayDesc.Origin = transWP;
		transRayDesc.Direction = WorldRayDirection();
		transRayDesc.TMin = 0.001f;
		transRayDesc.TMax = 100000;

		RayPayload transPayload;
		transPayload.skipShading = false;
		transPayload.gi = true;
		transPayload.depth = payload.depth + (MAX_DEPTH/2);
		TraceRay(gRtScene, 0, ~0, 0, 0, 0, transRayDesc, transPayload);
		if (transPayload.RayHitT < 100000)
		{
			color.xyz = transPayload.color.xyz;
			radiance += transPayload.shadowValue;
			//radiance /= 4;
		}
	}
	
	//Shadows
	if (lightPayload.RayHitT < 100000)
	{
		if (info.isReflective == 1)
		{
			if (info.hasTexture == 0)
			{
				radiance = lightPayload.shadowValue;
			}
			else
			{
				radiance += lightPayload.shadowValue;
			}
			
			//radiance /= 2;
		}
		else if (info.isRefractive == 1)
		{
			radiance = lightPayload.shadowValue;
			//radiance /= 2;
		}
		else
		{
			radiance += lightPayload.shadowValue * 0.9f;
			//radiance /= 2;
		}
		
	}

	//Set Final Values
	payload.color = color;
	payload.shadowValue = radiance;
}
