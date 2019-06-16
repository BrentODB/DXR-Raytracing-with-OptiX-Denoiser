#include "stdafx.h"
#pragma once
#include "Includes.h"
#include "Externals/DXR/include/D3D12RaytracingHelpers.hpp"
#include "Externals/DXCAPI/dxcapi.use.h"
#include "Externals/DXR/include/dxcapi.h"
#include "MeshLoader.h"
#include "Camera.h"


namespace GlobalRootSignatureParams {
	enum Value {
		OutputViewSlot = 0,
		AccelerationStructureSlot = 1,
		SceneConstantSlot = 2,
		VertexBuffer = 3,
		IndexBuffer = 4,
		MeshInfoBuffer = 5,
		LightBuffer = 6,
		ColorDataBuffer = 7,
		BufferTexture = 8,
		AlbedoBuffer = 9,
		NormalBuffer = 10,
		DiffuseTexture = 11,
		TextureSampler = 12,
		Count
	};
}

struct RayPayload
{
	glm::float4 color;
	bool skipShading;
	glm::float3 normal;
	float RayHitT;
	float shadowValue;
	glm::uint depth;
	bool gi;
	glm::uint rayCount;
};

struct PointLightInfo
{
	glm::float3 position;
	float radius;
	glm::float3 emissiveColor;
	float intensity;
	float size;
};

struct colorData
{
	glm::float4 color;
};

struct AccelerationStructureBuffers;
struct DescriptorCombo;
struct RootSignatureDesc;
struct DxilLibrary;

class Demo
{
public:
	Demo(HWND window);
	~Demo();

	void Update();
	void DoRaytracing();
	void onResize(int heightWND, int widthWND);

	void initDXR();
	void initOptiX(int width, int height);
	void setupDenoisingStage(uint32_t rtvIndex);
	void denoiseOutput(uint32_t rtvIndex);
	uint32_t beginFrame();
	void endFrame(uint32_t rtvIndex);

	void createDxgiSwapChain(IDXGIFactory4* pFactory, HWND hwnd, uint32_t width, uint32_t height, DXGI_FORMAT format, ID3D12CommandQueue* pCommandQueue);
	HRESULT createDevice(IDXGIFactory4* pDxgiFactory);
	void createCommandQueue(ID3D12Device* pDevice);
	void createDescriptorHeap(ID3D12Device* pDevice, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible);
	void createRTVDescriptorHeap(ID3D12Device* pDevice, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible);
	D3D12_CPU_DESCRIPTOR_HANDLE createRTVfCPU(ID3D12Device* pDevice, ID3D12Resource* pResource, ID3D12DescriptorHeap* pHeap, uint32_t& usedHeapEntries, DXGI_FORMAT format);
	void resourceBarrier(ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* pResource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);
	uint64_t submitCommandList(ID3D12GraphicsCommandList* pCmdList, ID3D12CommandQueue* pCmdQueue, ID3D12Fence* pFence, uint64_t fenceValue);
	UINT AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse);

	//Ray Tracing
	void createRaytracingInterfaces();
	void createHeapProperties();
	void createAccelerationStructures();
	ID3D12Resource* createBuffer(ID3D12Device* pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps, ID3D12Resource* pResource = nullptr);
	D3D12_RESOURCE_DESC Demo::createBufferVoid(ID3D12Device* pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps, ID3D12Resource* pResource = nullptr);
	struct VertexIndexCombo
	{
		ComPtr<ID3D12Resource> pVertex;
		ComPtr<ID3D12Resource> pIndex;
	};
	VertexIndexCombo createVertexAndIndexBuffer(ID3D12Device* pDevice);
	AccelerationStructureBuffers createBottomLevelAS(ID3D12Device* pDevice, ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* pVertexBuffer, int index);
	AccelerationStructureBuffers createTopLevelAS(ID3D12Device* pDevice, ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* pBottomLevelAS, uint64_t &tlasSize);
	void setMeshInfo(int index);
	void setDemoSettings();
	void createRtPipelineState();
	void createConstantBuffers();
	void UpdateConstantBuffers();
	void recreateSRV();
	void createSRV();
	void createTextureResources();
	void createMeshInfoStructureBuffer(std::vector<MeshInfo> meshInfoArray);
	void createPointLightResources();
	void createColorDataResource();
	void WaitForGPU();

	//Math
	void QuickRotation(std::vector<Vertex>& vertices);

	//Shaders
	DxilLibrary createDxilLibrary();
	ComPtr<IDxcBlob> compileLibrary(const WCHAR* filename, const WCHAR* targetString);
	void createShaderTable();

	//Shader Buffers
	ComPtr<ID3D12Resource> mpVertexUploadBuffer;
	ComPtr<ID3D12Resource> mpVertexBuffer;
	ComPtr<ID3D12Resource> mpIndexUploadBuffer;
	ComPtr<ID3D12Resource> mpIndexBuffer;
	ComPtr<ID3D12Resource> mpMeshInfoStructureUploadBuffer;
	ComPtr<ID3D12Resource> mpMeshInfoStructureBuffer;
	ComPtr<ID3D12Resource> mpPointLightStructureUploadBuffer;
	ComPtr<ID3D12Resource> mpPointLightStructureBuffer;
	ComPtr<ID3D12Resource> mpColorDataStructureUploadBuffer;
	ComPtr<ID3D12Resource> mpColorDataStructureBuffer;
	
	//Acceleration Strutures
	ComPtr<ID3D12Resource> mpTopLevelAS;
	std::vector<ComPtr<ID3D12Resource>> mpBottomLevelAS;
	uint64_t mTlasSize;
	D3D12_HEAP_PROPERTIES mUploadHeapProps;
	D3D12_HEAP_PROPERTIES mDefaultHeapProps;
	std::vector<UINT64> mBottomLevelSize;
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;

	HWND mWindow;
	usedAPI mAPIinUse = usedAPI::DXR;
	bool mPathTracing = false;

	ComPtr<ID3D12Device5> mpDXRDevice;
	ComPtr<ID3D12Device> mpDevice;
	ComPtr<ID3D12CommandQueue> mpCmdQueue;
	ComPtr<IDXGISwapChain3> mpSwapchain;
	ComPtr<ID3D12GraphicsCommandList> mpCmdList;
	ComPtr<ID3D12GraphicsCommandList4> mpDXRCmdList;
	ComPtr<ID3D12Fence>	mpFence;
	HANDLE mFenceEvent;
	uint64_t mFenceValue = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE mUavDescriptorHandleGPU;
	D3D12_GPU_DESCRIPTOR_HANDLE mBufferTextureUavDescriptorHandleGPU;

	static const int mDefaultSwapChainBuffers = 2;
	struct FrameObject
	{
		ComPtr<ID3D12CommandAllocator> pCmdAllocator;
		ComPtr<ID3D12Resource> pSwapChainBufferRTV;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	}mFrameObjects[mDefaultSwapChainBuffers];

	struct HeapData
	{
		ComPtr<ID3D12DescriptorHeap> pHeap;
		uint32_t usedEntries = 0;
	};

	ComPtr<ID3D12DescriptorHeap> mpSrvUavHeap;
	HeapData mRtvHeap;
	HeapData mUavHeap;
	uint64_t mDescriptorHeapIndex = UINT64_MAX;
	static const uint32_t mDefaultRtvHeapSize = 4;

	ComPtr<ID3D12StateObject> mpDXRPipelineState;
	ComPtr<ID3D12RootSignature> mpGlobalRootSig;

	ComPtr<ID3D12Resource> mpShaderTable;
	uint32_t mShaderTableEntrySize = 0;

	ComPtr<ID3D12Resource> mpOutputResource;
	ComPtr<ID3D12Resource> mpBufferOutputResource;

	MeshLoader* mpMeshLoader;
	Camera* mpCamera;

	//ConstantBuffers
	struct SceneConstantBuffer
	{
		XMFLOAT4X4 cameraToWorld;
		glm::float4 cameraPosition;
		glm::float3 lightPosition = glm::float3(0,2000,0);
		float filler;
		glm::float3 lightDirection = glm::float3(0.2, 0.6f, 0.3);
		glm::uint amountOfLights;
		glm::uint sampleCount;
		glm::uint frameCount;
		glm::vec2 windowSize = glm::float2(WIDTH, HEIGHT);
		int samplesPerInstance = 1;
	};

	// We'll allocate space for several of these and they will need to be padded for alignment.
	//static_assert(sizeof(SceneConstantBuffer) < D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, "Checking the size here.");

	union AlignedSceneConstantBuffer
	{
		SceneConstantBuffer constants;
		uint8_t alignmentPadding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT];
	};
	AlignedSceneConstantBuffer*  m_mappedConstantData;
	SceneConstantBuffer mSceneCB[mDefaultSwapChainBuffers];
	ComPtr<ID3D12Resource> mPerFrameConstants;
	glm::uint mSampleCount;
	glm::uint mRayCount;
	glm::uint mFrameCount;

	D3D12_GPU_VIRTUAL_ADDRESS mCBVirtualAdress;

	D3D12_GPU_DESCRIPTOR_HANDLE mSceneSrvs;
	std::vector<D3D12_RESOURCE_DESC> mResourceDescArray;
	std::vector<D3D12_SUBRESOURCE_DATA> mSubResourceArray;
	std::vector<ComPtr<ID3D12Resource>> mResourceArray;
	std::vector<ComPtr<ID3D12Resource>> mResourceUploadArray;
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mTextureGpuDescHandleArray;
	std::vector<std::string> mDDSTextureLocations;

	ComPtr<ID3D12Resource> mMeshInfoBuffer;
	std::vector<MeshInfo> mMeshesInfoArray;
	D3D12_GPU_DESCRIPTOR_HANDLE mMeshInfoBufferSRV;
	UINT mMeshInfoHeapIndex;
	D3D12_GPU_DESCRIPTOR_HANDLE mVertexBufferSRV;
	UINT mVertexBufferHeapIndex;
	D3D12_GPU_DESCRIPTOR_HANDLE mIndexBufferSRV;
	UINT mIndexBufferHeapIndex;
	ComPtr<ID3D12Resource> mPointLightInfoBuffer;
	std::vector<PointLightInfo> mPointLightInfoArray;
	D3D12_GPU_DESCRIPTOR_HANDLE mPointLightInfoBufferSRV;
	D3D12_GPU_DESCRIPTOR_HANDLE mPointLightInfoBufferUAV;
	UINT mPointLightInfoHeapIndex;
	ComPtr<ID3D12Resource> mColorDataInfoBuffer;
	std::vector<colorData> mColorDataInfoArray;
	D3D12_GPU_DESCRIPTOR_HANDLE mColorDataInfoBufferSRV;
	D3D12_GPU_DESCRIPTOR_HANDLE mColorDataInfoBufferUAV;
	UINT mColorDataInfoHeapIndex;

	Scene m_Scene;
	std::string meshPath;
	glm::mat4 model = {};
	
	//OptiX
	optix::Context optix_context;
	optix::Buffer input_buffer;
	ComPtr<ID3D12Resource> readback_input_buffer;
	UINT mInputSlot;
	optix::Buffer normal_buffer;
	ComPtr<ID3D12Resource> readback_normal_buffer;
	ComPtr<ID3D12Resource> mpOutputNormalBuffer;
	D3D12_GPU_DESCRIPTOR_HANDLE mNormalBufferTextureUavDescriptorHandleGPU;
	UINT mNormalSlot;
	optix::Buffer albedo_buffer;
	ComPtr<ID3D12Resource> readback_albedo_buffer;
	ComPtr<ID3D12Resource> mpOutputAlbedoBuffer;
	D3D12_GPU_DESCRIPTOR_HANDLE mAlbedoBufferTextureUavDescriptorHandleGPU;
	UINT mAlbedoSlot;

	optix::Buffer out_buffer;
	optix::PostprocessingStage denoiserStage;
	optix::CommandList optiXCommandList;
	unsigned char* denoised_pixels = new unsigned char[WIDTH * HEIGHT * 4];
	ComPtr<ID3D12Resource> textureUploadBuffer;
	bool mDenoiseOutput = true;

	//Resizing
	int mWidth = WIDTH;
	int mHeight = HEIGHT;

	//Testing
	bool mSampleBool = true;
	void MoveLight(UINT frameIndex);
	float mLightPosValue = 0.0f;
	bool mMoveLight = true;
	bool mSampleCap = false;
	int mSampleCapAmount = 1;
};

