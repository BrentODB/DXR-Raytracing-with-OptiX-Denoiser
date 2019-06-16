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
	void SetSeed(const uint value) {
		seed = int(value);
		//Cycle();
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
	int samplesPerInstance;
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
static uint MAX_DEPTH = 5;
static float MIN_T = 0.001f;

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
	float3 normal;
	uint depth;
	float shadowValue;
	float RayHitT;
	bool skipShading;
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
	float phi = 2.0f * 3.14159265f * random.y;

	return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + normal.xyz * sqrt(1 - random.x);
}

[shader("raygeneration")]
void rayGen()
{
	float3 rayDir;
	float3 origin;

	// Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
	GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

	uint2 launchIndex = DispatchRaysIndex();
	int idx = launchIndex.y * g_sceneCB.windowSize.x + launchIndex.x; //adjust width
	uint2 launchDim = DispatchRaysDimensions();

	float2 crd = float2(launchIndex);
	float2 dims = float2(launchDim);

	float2 d = ((crd / dims) * 2.f - 1.f);
	float aspectRatio = dims.x / dims.y;

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = rayDir;

	ray.TMin = MIN_T;
	ray.TMax = 100000;

	float4 ogColor = gOutput[launchIndex.xy];
	float4 ogAlbedo = gAlbedoBufferOutput[idx];
	float4 ogNormal = gNormalBufferOutput[idx];

	RayPayload payload;
	payload.skipShading = false;
	payload.depth = 0;
	payload.shadowValue = 0.0f;
	payload.color = float4(0, 0, 0, 1);
	//payload.color = ogColor;
	payload.gi = true;

	TraceRay(gRtScene, 0, ~0, 0 /* ray index*/, 0, 0, ray, payload);
	//float4 col = linearToSrgb(payload.color);
	float4 albedoColor = payload.color;
	float3 normalColor = payload.normal;

	payload.color.r = payload.shadowValue * payload.color.r;
	payload.color.g = payload.shadowValue * payload.color.g;
	payload.color.b = payload.shadowValue * payload.color.b;

	// Update the progressive result with the new color sample
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

	gOutput[launchIndex.xy] = float4(newValue, 1.0f);
	gBufferOutput[idx] = gOutput[launchIndex.xy];
	gAlbedoBufferOutput[idx] = float4(newAlbedoValue, 1.0f);
	gNormalBufferOutput[idx] = float4(newNormalValue, 1.0f);
}

[shader("miss")]
void miss(inout RayPayload payload)
{
	payload.color = float4(0.2f, 0.2f, 0.2f, 1.0f);
	payload.shadowValue = 0.0f;
}

struct IntersectionAttribs
{
	float2 baryCrd;
};

[shader("closesthit")]
void chs(inout RayPayload payload, in IntersectionAttribs attribs)
{
	//Set sunlight
	g_PointLights[0].position = g_sceneCB.lightPosition;

	//Seed
	counter = g_sceneCB.frameCount;
	float rand = nrand(float2(1.0f / (DispatchRaysIndex().x), 1.0f / (DispatchRaysIndex().y) * counter));
	uint seed = wang_hash(uint((DispatchRaysIndex().x * counter * DispatchRaysIndex().y * counter) / (g_sceneCB.sampleCount + 1)));
	rndGen.SetSeed(wang_hash(seed));

	float mainShadowValue = 0.0f;
	mainShadowValue = payload.shadowValue;
	float4 mainColorValue = float4(0, 0, 0, 0);
	payload.RayHitT = RayTCurrent();

	if (payload.skipShading)
	{
		return;
	}

	//Values
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
	//#Values

	float3 worldPosition = (WorldRayOrigin() + WorldRayDirection() * RayTCurrent()) /*SelfHitting*/;

	if (payload.depth > MAX_DEPTH)
	{
		if (false)
		{
			float4 colorAdditionFromLight = float4(0, 0, 0, 0);
			float shadowAdditionFromLight = 0.0f;
			float addCount = 1;
			for (uint i = 0; i < g_sceneCB.amountOfLights; i++)
			{
				float3 tempPos = g_PointLights[i].position;
				tempPos.z = g_PointLights[i].position.z + rndGen.GetRandomFloat(-g_PointLights[i].radius / 2, g_PointLights[i].radius / 2);
				tempPos.y = g_PointLights[i].position.y + rndGen.GetRandomFloat(-g_PointLights[i].radius / 2, g_PointLights[i].radius / 2);
				tempPos.x = g_PointLights[i].position.x + rndGen.GetRandomFloat(-g_PointLights[i].radius / 2, g_PointLights[i].radius / 2);

				float3 lightDirection = tempPos - worldPosition;
				float3 rayOrigin = worldPosition;
				float lightDistance = length(lightDirection);

				RayDesc rayDesc;
				rayDesc.Origin = rayOrigin;
				rayDesc.Direction = normalize(lightDirection);
				rayDesc.TMin = MIN_T;
				rayDesc.TMax = lightDistance;

				RayPayload lightPayload;
				lightPayload.skipShading = true;
				lightPayload.gi = false;
				TraceRay(gRtScene, RAY_FLAG_NONE, ~0, 0, 0, 1, rayDesc, lightPayload);
				if (lightPayload.RayHitT >= lightDistance)
				{
					addCount++;
					shadowAdditionFromLight += ((g_PointLights[i].intensity / (lightDistance * lightDistance)));
					colorAdditionFromLight += float4((g_PointLights[i].emissiveColor * g_PointLights[i].intensity) / (lightDistance * lightDistance), 0.0f);
				}
			}

			payload.shadowValue += (shadowAdditionFromLight / addCount);
			payload.color.xyz += (colorAdditionFromLight.xyz / addCount);
		}
		else
		{
			payload.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
			payload.shadowValue = 0.0f;
		}
		return;
	}

	//Diffuse textures
	int id = info.materialIndex;

	float4 color = payload.color;
	if (info.hasTexture == 1)
	{
		color = g_TextureArray[id].SampleLevel(g_s0, uvActual, 0);
	}
	else if (info.hasTexture == 0)
	{
		color.rgb = info.diffuseColor;
	}


	//Reflections
	if ((color.w > 0.5 && info.isReflective == 1))
	{
		RayDesc reflectedRayDesc;
		reflectedRayDesc.Origin = worldPosition;
		reflectedRayDesc.Direction = reflect(WorldRayDirection(), normToUse);
		reflectedRayDesc.TMin = MIN_T;
		reflectedRayDesc.TMax = 100000;

		RayPayload reflectedPayload;
		reflectedPayload.skipShading = false;
		reflectedPayload.gi = false;
		reflectedPayload.depth = payload.depth + 1;
		TraceRay(gRtScene, 0, ~0, 0, 0, 0, reflectedRayDesc, reflectedPayload);
		if (reflectedPayload.RayHitT < 100000)
		{
			if (info.hasTexture)
			{
				color.xyz += (reflectedPayload.color.xyz * 0.3f) * reflectedPayload.shadowValue;
			}
			else
			{
				color.xyz += (reflectedPayload.color.xyz) * reflectedPayload.shadowValue;
			}
		}
	}

	//Refractions
	if ((info.isRefractive == 1))
	{
		RayDesc refractedRayDesc;
		refractedRayDesc.Direction = normalize(refractionFunc(WorldRayDirection(), normToUse, 1.05f));
		float3 refractWP = (WorldRayOrigin() + WorldRayDirection() * RayTCurrent());
		refractedRayDesc.Origin = refractWP;
		refractedRayDesc.TMin = MIN_T;
		refractedRayDesc.TMax = 100000;

		RayPayload refractedPayload;
		refractedPayload.skipShading = false;
		refractedPayload.gi = false;
		//refractedPayload.shadowValue = mainShadowValue;
		refractedPayload.depth = payload.depth + 1;
		TraceRay(gRtScene, RAY_FLAG_NONE, ~0, 0, 0, 0, refractedRayDesc, refractedPayload);
		color.xyz = refractedPayload.color.xyz;
		mainShadowValue = refractedPayload.shadowValue;
	}

	//Transparency
	if (color.w < 0.5 && info.isRefractive != 1)
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
		transPayload.depth = payload.depth + 1;
		TraceRay(gRtScene, 0, ~0, 0, 0, 0, transRayDesc, transPayload);
		if (transPayload.RayHitT < 100000)
		{
			//color.xyz = transPayload.color.xyz * transPayload.shadowValue;
			color.xyz = transPayload.color.xyz;
			color.w = 0.0f;
			mainShadowValue = payload.shadowValue;
		}
	}

	//Shadows
	if (color.w > 0.5f)
	{
		uint samplesPerInstance = 1;
		float4 colorAdditionFromLight = float4(0, 0, 0, 0);
		float shadowAdditionFromLight = 0;
		float addCount = 0;
		for (uint i = 0; i < g_sceneCB.amountOfLights; i++)
		{
			for (uint j = 0; j < samplesPerInstance; j++)
			{
				float3 tempPos = g_PointLights[i].position;
				tempPos.z = g_PointLights[i].position.z + rndGen.GetRandomFloat(-g_PointLights[i].radius / 2, g_PointLights[i].radius / 2);
				tempPos.y = g_PointLights[i].position.y + rndGen.GetRandomFloat(-g_PointLights[i].radius / 2, g_PointLights[i].radius / 2);
				tempPos.x = g_PointLights[i].position.x + rndGen.GetRandomFloat(-g_PointLights[i].radius / 2, g_PointLights[i].radius / 2);

				float3 lightDirection = tempPos - worldPosition;
				float3 rayOrigin = worldPosition;
				float lightDistance = length(lightDirection);

				RayDesc rayDesc;
				float3 shadowWP = (WorldRayOrigin() + WorldRayDirection() * RayTCurrent()) + (normalize(normToUse) * 1.0f);
				rayDesc.Origin = shadowWP;
				rayDesc.Direction = normalize(lightDirection);
				rayDesc.TMin = MIN_T;
				rayDesc.TMax = lightDistance;

				RayPayload lightPayload;
				lightPayload.skipShading = true;
				lightPayload.gi = false;
				TraceRay(gRtScene, 0, ~0, 0, 0, 1, rayDesc, lightPayload);
				if (lightPayload.RayHitT >= lightDistance)
				{
					addCount++;
					shadowAdditionFromLight += ((g_PointLights[i].intensity) / (lightDistance * lightDistance));
					colorAdditionFromLight += float4((g_PointLights[i].emissiveColor * g_PointLights[i].intensity) / (lightDistance * lightDistance), 0.0f);
				}
			}
		}

		if (info.isRefractive == 1)
		{
			//mainShadowValue = 1.0f;
		}
		else
		{
			//shadowAdditionFromLight /= addCount;
			colorAdditionFromLight /= addCount;

			mainColorValue += colorAdditionFromLight;
			mainColorValue = clamp(mainColorValue, 0.0f, 1.0f);
			mainShadowValue += shadowAdditionFromLight;
			mainShadowValue = clamp(mainShadowValue, 0.0f, 1.0f);
		}

	}


	//Global Illumination
	uint samplesPerInstance = 1;
	uint randomRaysPerBounce = 1;
	float4 giColor = float4(0, 0, 0, 0);
	float giShadow = 0.0f;
	uint addCount = 0;
	for (uint j = 0; j < samplesPerInstance; j++)
	{
		for (uint i = 0; i < randomRaysPerBounce; i++)
		{
			float3 shadowWP = (WorldRayOrigin() + WorldRayDirection() * RayTCurrent());
			normToUse = CosineWeightedHemisphereSample(normToUse);

			float3 rayDirection = normToUse;
			float3 rayOrigin = worldPosition;

			RayDesc rayDesc;
			rayDesc.Origin = shadowWP;
			rayDesc.Direction = rayDirection;
			rayDesc.TMin = MIN_T;
			rayDesc.TMax = 100000;

			RayPayload lightPayload;
			lightPayload.skipShading = false;
			lightPayload.gi = true;
			lightPayload.depth = payload.depth + MAX_DEPTH; // 1 Deep
			TraceRay(gRtScene, 0, ~0, 0, 0, 0, rayDesc, lightPayload);
			if (lightPayload.RayHitT < 100000 && lightPayload.color.w > 0.5f)
			{
				addCount++;
				giColor.xyz += lightPayload.color.xyz;
				giShadow += lightPayload.shadowValue;
			}
			if (lightPayload.RayHitT < 100000 && lightPayload.color.w < 0.5f)
			{
				float3 shadowOPWP = (shadowWP + normalize(normToUse) * (lightPayload.RayHitT + 0.1f));
				RayDesc rayOPDesc;
				rayOPDesc.Origin = shadowOPWP;
				rayOPDesc.Direction = rayDirection;
				rayOPDesc.TMin = MIN_T;
				rayOPDesc.TMax = 100000;

				RayPayload lightOPPayload;
				lightOPPayload.skipShading = false;
				lightOPPayload.gi = true;
				lightOPPayload.depth = payload.depth + MAX_DEPTH; // 1 Deep
				TraceRay(gRtScene, 0, ~0, 0, 0, 0, rayOPDesc, lightOPPayload);
				if (lightOPPayload.RayHitT < 100000 && lightOPPayload.color.w > 0.5f)
				{
					giShadow += lightPayload.shadowValue;
				}
			}
		}
	}

	mainShadowValue += giShadow;
	//mainColorValue.xyz += giColor.xyz;
	//giColor.xyz /= addCount;

	color.xyz += mainColorValue.xyz;

	mainShadowValue = clamp(mainShadowValue, 0.0f, 1.0f);
	color.xyz = clamp(color.xyz, 0.0f, 1.0f);
	payload.color = color;
	payload.shadowValue = mainShadowValue;
	payload.normal = normToUse;
}
