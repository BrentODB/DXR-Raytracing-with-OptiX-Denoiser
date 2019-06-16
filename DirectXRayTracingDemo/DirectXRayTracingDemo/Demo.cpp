#include "stdafx.h"
#include "Demo.h"

static dxc::DxcDllSupport gDxcDllHelper;

struct AccelerationStructureBuffers
{
	ComPtr<ID3D12Resource> pScratch;
	ComPtr<ID3D12Resource> pResult;
	ComPtr<ID3D12Resource> pInstanceDesc;    // Used only for top-level Acceleration Structure
};

struct DescriptorCombo
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtvDescriptorHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
};

void Demo::setDemoSettings()
{
	m_Scene = Scene::SPONZA;
	mPathTracing = false;
}

Demo::Demo(HWND window)
{
	mpMeshLoader = new MeshLoader();
	mpCamera = new Camera(WIDTH,HEIGHT);

	mWindow = window;
	createHeapProperties();

	setDemoSettings();
	initDXR();
#ifdef USE_PIX
	const char* formatStr = "Init OptiX";
	PIXBeginEvent(mpCmdList.Get(), UINT64(1), formatStr);
#endif // USE_PIX
	initOptiX(WIDTH, HEIGHT);
#ifdef USE_PIX
	PIXEndEvent(mpCmdList.Get());
#endif // USE_PIX
	createRaytracingInterfaces();
	createTextureResources();
	createPointLightResources();
	createColorDataResource();
#ifdef USE_PIX
	formatStr = "Build AS";
	PIXBeginEvent(mpCmdList.Get(), UINT64(1), formatStr);
#endif // USE_PIX
	createAccelerationStructures();
#ifdef USE_PIX
	PIXEndEvent(mpCmdList.Get());
#endif // USE_PIX
#ifdef USE_PIX
	formatStr = "Create PSO";
	PIXBeginEvent(mpCmdList.Get(), UINT64(1), formatStr);
#endif // USE_PIX
	createRtPipelineState();
#ifdef USE_PIX
	PIXEndEvent(mpCmdList.Get());
#endif // USE_PIX
	createConstantBuffers();
	createSRV();
	createShaderTable();
}

#define SAFE_RELEASE(x) if(x) { x->Release(); x = nullptr; } 
#define SAFE_DELETE(x) if(x) {delete x; x = nullptr; }

Demo::~Demo()
{
	//Wait for GPU
	if (mpCmdQueue && mpFence && mFenceEvent)
	{
		// Schedule a Signal command in the GPU queue.
		UINT64 fenceValue = mFenceValue;
		if (SUCCEEDED(mpCmdQueue->Signal(mpFence.Get(), fenceValue)))
		{
			// Wait until the Signal has been processed.
			if (SUCCEEDED(mpFence->SetEventOnCompletion(fenceValue, mFenceEvent)))
			{
				WaitForSingleObject(mFenceEvent, INFINITE);
				//WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);

				// Increment the fence value for the current frame.
				mFenceValue++;
			}
		}
	}

	delete mpMeshLoader;
	mpMeshLoader = nullptr;

	delete mpCamera;
	mpCamera = nullptr;

	//delete m_mappedConstantData;
	//m_mappedConstantData = nullptr;

	SAFE_RELEASE(mPerFrameConstants);
	mPerFrameConstants.Reset();

	for (size_t i = 0; i < mResourceArray.size(); i++)
	{
		mResourceArray[i].Reset();
	}
	for (size_t i = 0; i < mResourceUploadArray.size(); i++)
	{
		mResourceUploadArray[i].Reset();
	}

	SAFE_RELEASE(mpVertexUploadBuffer);
	SAFE_RELEASE(mpVertexBuffer);
	mpVertexBuffer.Reset();
	SAFE_RELEASE(mpIndexUploadBuffer);
	SAFE_RELEASE(mpIndexBuffer);
	mpIndexBuffer.Reset();
	SAFE_RELEASE(mMeshInfoBuffer);
	SAFE_RELEASE(mpMeshInfoStructureUploadBuffer);
	SAFE_RELEASE(mpMeshInfoStructureBuffer);
	mpMeshInfoStructureBuffer.Reset();
	SAFE_RELEASE(mPointLightInfoBuffer);
	SAFE_RELEASE(mpPointLightStructureUploadBuffer);
	SAFE_RELEASE(mpPointLightStructureBuffer);
	mpPointLightStructureBuffer.Reset();
	SAFE_RELEASE(mColorDataInfoBuffer);
	SAFE_RELEASE(mpColorDataStructureUploadBuffer);
	SAFE_RELEASE(mpColorDataStructureBuffer);
	mpColorDataStructureBuffer.Reset();
	
	SAFE_RELEASE(mpTopLevelAS);
	mpTopLevelAS.Reset();
	for (size_t i = 0; i < mpBottomLevelAS.size(); i++)
	{
		SAFE_RELEASE(mpBottomLevelAS[i]);
		mpBottomLevelAS[i].Reset();
	}
	
	mpCmdQueue.Reset();
	//SAFE_RELEASE(mpCmdQueue);
	mpSwapchain.Reset();
	SAFE_RELEASE(mpSwapchain);
	mpCmdList.Reset();
	SAFE_RELEASE(mpCmdList);
	//SAFE_RELEASE(mpFence);
	mpFence.Reset();
	
	for (size_t i = 0; i < mDefaultSwapChainBuffers; i++)
	{
		mFrameObjects[i].pCmdAllocator.Reset();
		mFrameObjects[i].pSwapChainBufferRTV.Reset();
		//SAFE_RELEASE(mFrameObjects[i].pSwapChainBufferRTV);
	}

	mpSrvUavHeap.Reset();
	SAFE_RELEASE(mpSrvUavHeap);
	mRtvHeap.pHeap.Reset();
	SAFE_RELEASE(mRtvHeap.pHeap);
	
	mpGlobalRootSig.Reset();
	SAFE_RELEASE(mpGlobalRootSig);
	mpShaderTable.Reset();
	SAFE_RELEASE(mpShaderTable);
	
	mpOutputResource.Reset();
	SAFE_RELEASE(mpOutputResource);
	mpBufferOutputResource.Reset();
	SAFE_RELEASE(mpBufferOutputResource);
	mpOutputAlbedoBuffer.Reset();
	SAFE_RELEASE(mpOutputAlbedoBuffer);
	mpOutputNormalBuffer.Reset();
	SAFE_RELEASE(mpOutputNormalBuffer);
	
	mpDXRDevice.Reset();
	SAFE_RELEASE(mpDXRDevice);
	mpDXRCmdList.Reset();
	SAFE_RELEASE(mpDXRCmdList);
	mpDXRPipelineState.Reset();
	SAFE_RELEASE(mpDXRPipelineState);
	textureUploadBuffer.Reset();
	SAFE_RELEASE(textureUploadBuffer);

	SAFE_RELEASE(readback_input_buffer);
	SAFE_RELEASE(readback_albedo_buffer);
	SAFE_RELEASE(readback_normal_buffer);

	SAFE_RELEASE(mpDevice);
#ifdef DEBUG
	IDXGIDebug1* pDebug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
	{
		pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
		pDebug->Release();
	}
#endif // DEBUG

	//Denoiser
	// Remove our gpu buffers
	input_buffer->destroy();
	normal_buffer->destroy();
	albedo_buffer->destroy();
	out_buffer->destroy();
	optix_context->destroy();
}

void Demo::Update()
{
#ifdef USE_PIX
	char const* formatStr = "FrameUpdate";
	PIXBeginEvent(mpCmdList.Get(), UINT64(1), formatStr);
#endif // USE_PIX
	mpCamera->Update();
	mFrameCount++;


	if (mpCamera->m_Moved)
	{
		mpCamera->m_Moved = false;
		mSampleCount = 1;
	}
	UpdateConstantBuffers();

	if ((mSampleCap && mSampleCount <= mSampleCapAmount) || !mSampleCap)
	{
		if (mSampleBool)
		{
			mSampleCount++;
		}
		DoRaytracing();
	}

#ifdef USE_PIX
	PIXEndEvent(mpCmdList.Get());
#endif // USE_PIX
}

uint32_t Demo::beginFrame()
{
	ID3D12DescriptorHeap* heaps[] = { mpSrvUavHeap.Get()};
	mpCmdList->SetDescriptorHeaps(ARRAYSIZE(heaps), heaps);

	return mpSwapchain->GetCurrentBackBufferIndex();
}

void Demo::DoRaytracing()
{
#ifdef USE_PIX
	char const* formatStr = "RenderFrame";
	PIXBeginEvent(mpCmdList.Get(), UINT64(1), formatStr);
#endif // USE_PIX
	uint32_t rtvIndex = beginFrame(); //Gets index of rtv that needs to be presented								  
	// Let's raytrace
	resourceBarrier(mpCmdList.Get(), mpOutputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	//resourceBarrier(mpCmdList.Get(), mpBufferOutputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// Dispatch
	D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
	raytraceDesc.Width = mWidth;
	raytraceDesc.Height = mHeight;
	raytraceDesc.Depth = 1;

	// RayGen is the first entry in the shader-table
	raytraceDesc.RayGenerationShaderRecord.StartAddress = mpShaderTable->GetGPUVirtualAddress() + 0 * mShaderTableEntrySize;
	raytraceDesc.RayGenerationShaderRecord.SizeInBytes = mShaderTableEntrySize;
	
	// Miss is the second entry in the shader-table
	size_t missOffset = 1 * mShaderTableEntrySize;
	raytraceDesc.MissShaderTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + missOffset;
	raytraceDesc.MissShaderTable.StrideInBytes = mShaderTableEntrySize;
	raytraceDesc.MissShaderTable.SizeInBytes = mShaderTableEntrySize;   // Only a single miss-entry

	// Hit is the third entry in the shader-table
	size_t hitOffset = 2 * mShaderTableEntrySize;
	raytraceDesc.HitGroupTable.StartAddress = mpShaderTable->GetGPUVirtualAddress() + hitOffset;
	raytraceDesc.HitGroupTable.StrideInBytes = mShaderTableEntrySize;
	raytraceDesc.HitGroupTable.SizeInBytes = mShaderTableEntrySize * mpMeshLoader->mpModel->mMeshes.size();
	
	// Bind the global root signature
	mpCmdList->SetComputeRootSignature(mpGlobalRootSig.Get());
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::OutputViewSlot, mUavDescriptorHandleGPU);
	mpCmdList->SetComputeRootConstantBufferView(GlobalRootSignatureParams::SceneConstantSlot, mCBVirtualAdress);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::VertexBuffer, mVertexBufferSRV);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::IndexBuffer, mIndexBufferSRV);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::MeshInfoBuffer, mMeshInfoBufferSRV);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::LightBuffer, mPointLightInfoBufferUAV);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::ColorDataBuffer, mColorDataInfoBufferUAV);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::BufferTexture, mBufferTextureUavDescriptorHandleGPU);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::AlbedoBuffer, mAlbedoBufferTextureUavDescriptorHandleGPU);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::NormalBuffer, mNormalBufferTextureUavDescriptorHandleGPU);
	mpCmdList->SetComputeRootDescriptorTable(GlobalRootSignatureParams::DiffuseTexture, mTextureGpuDescHandleArray[0]);

	mpCmdList->SetComputeRootShaderResourceView(GlobalRootSignatureParams::AccelerationStructureSlot, mpTopLevelAS->GetGPUVirtualAddress());
	mpDXRCmdList->SetPipelineState1(mpDXRPipelineState.Get());
	mpDXRCmdList->DispatchRays(&raytraceDesc);

	// Copy the results to the back-buffer
	resourceBarrier(mpCmdList.Get(), mpOutputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList.Get(), mpOutputAlbedoBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList.Get(), mpOutputNormalBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
	resourceBarrier(mpCmdList.Get(), mFrameObjects[rtvIndex].pSwapChainBufferRTV.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
	mpCmdList->CopyResource(mFrameObjects[rtvIndex].pSwapChainBufferRTV.Get(), mpOutputResource.Get());

	endFrame(rtvIndex);
}

void Demo::setupDenoisingStage(uint32_t rtvIndex)
{
	//************************************************************************************Denoise Output************************************************************************************
	//Copy Data from ID3D12Resource to Readback buffers
	mpCmdList->CopyResource(readback_input_buffer.Get(), mpBufferOutputResource.Get());
	mpCmdList->CopyResource(readback_albedo_buffer.Get(), mpOutputAlbedoBuffer.Get());
	mpCmdList->CopyResource(readback_normal_buffer.Get(), mpOutputNormalBuffer.Get());

	//Copy Data from readback
	uint8_t* noisypixeldata;
	readback_input_buffer.Get()->Map(0, nullptr, (void**)&noisypixeldata);

	uint8_t* albedoData;
	readback_albedo_buffer.Get()->Map(0, nullptr, (void**)&albedoData);

	uint8_t* normalData;
	readback_normal_buffer.Get()->Map(0, nullptr, (void**)&normalData);

	//Copy correct contents to buffers
	float* input = static_cast<float*>(input_buffer->map());
	memcpy(input, noisypixeldata, sizeof(float) * mWidth * mHeight * 4);
	input_buffer->unmap();

	float* albedo = static_cast<float*>(albedo_buffer->map());
	memcpy(albedo, albedoData, sizeof(float) * mWidth * mHeight * 4);
	albedo_buffer->unmap();

	float* normal = static_cast<float*>(normal_buffer->map());
	memcpy(normal, normalData, sizeof(float) * mWidth * mHeight * 4);
	normal_buffer->unmap();

	readback_input_buffer.Get()->Unmap(0, nullptr);
	readback_albedo_buffer.Get()->Unmap(0, nullptr);
	readback_normal_buffer.Get()->Unmap(0, nullptr);
}

void Demo::denoiseOutput(uint32_t rtvIndex)
{
	//Execute denoise
	optiXCommandList->execute();
	//Get Output from denoiser
	float* denoised_output = static_cast<float*>(out_buffer->map());
	for (int i = 0; i < mWidth * mHeight * 4; i++)
	{
		denoised_pixels[i] = static_cast<unsigned char>(denoised_output[i] * 255);
	}
	out_buffer->unmap();
	
	//Copy denoised image back
	D3D12_SUBRESOURCE_DATA subresource_data;
	subresource_data.pData = denoised_pixels;
	subresource_data.RowPitch = mWidth * 4;
	subresource_data.SlicePitch = subresource_data.RowPitch * mHeight;
	
	D3D12_RESOURCE_DESC texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT64>(mWidth), static_cast<UINT>(mHeight));
	
	UINT64 texture_upload_buffer_size;
	mpDevice->GetCopyableFootprints(&texture_desc, 0, 1, 0, nullptr, nullptr, nullptr, &texture_upload_buffer_size);
	
	HRESULT hres = mpDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(texture_upload_buffer_size), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&textureUploadBuffer));
	
	UpdateSubresources(mpCmdList.Get(), mFrameObjects[rtvIndex].pSwapChainBufferRTV.Get(), textureUploadBuffer.Get(), 0, 0, 1, &subresource_data);

	resourceBarrier(mpCmdList.Get(), mFrameObjects[rtvIndex].pSwapChainBufferRTV.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
}

void Demo::endFrame(uint32_t rtvIndex)
{
	HRESULT hres = 0;
	//neural network denoising magic  ---------------------------------------------------------------------------------------------------------------------------------------------------------------
	if (mDenoiseOutput)
	{
#ifdef USE_PIX
		char const* formatStr = "DenoiserCopies";
		PIXBeginEvent(mpCmdList.Get(), UINT64(1), formatStr);
#endif // USE_PIX
		setupDenoisingStage(rtvIndex);
#ifdef USE_PIX
		PIXEndEvent(mpCmdList.Get());
#endif // USE_PIX
		denoiseOutput(rtvIndex);
	}
	else
	{
		resourceBarrier(mpCmdList.Get(), mFrameObjects[rtvIndex].pSwapChainBufferRTV.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
	}

	mFenceValue = submitCommandList(mpCmdList.Get(), mpCmdQueue.Get(), mpFence.Get(), mFenceValue);

	hres = mpSwapchain->Present(0, 0);
	if (FAILED(hres))
	{
		hres = mpDevice->GetDeviceRemovedReason();
		throw std::runtime_error("Failed to present.");
	}

	//Prepare the command buffer for the next frame
	uint32_t bufferIndex = mpSwapchain->GetCurrentBackBufferIndex();

	//Make sure the new back buffer is ready
	if (mFenceValue > mDefaultSwapChainBuffers)
	{
		mpFence->SetEventOnCompletion(mFenceValue - mDefaultSwapChainBuffers + 1, mFenceEvent);
		WaitForSingleObject(mFenceEvent, INFINITE);
	}

	mFrameObjects[bufferIndex].pCmdAllocator->Reset();
	mpCmdList->Reset(mFrameObjects[bufferIndex].pCmdAllocator.Get(), nullptr);
#ifdef USE_PIX
	PIXEndEvent(mpCmdList.Get());
#endif // USE_PIX
}

void Demo::initDXR()
{
#ifdef _DEBUG
	ID3D12Debug* pDebug;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pDebug))))
	{
		pDebug->EnableDebugLayer();
		pDebug->Release();
	}
#endif // DEBUG

	//Create DXGI factory
	IDXGIFactory4* pDxgiFactory;
	CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory));

	//Create DX12 Objects
	if (FAILED(createDevice(pDxgiFactory)))
	{
		throw std::runtime_error("Failed create dxgi factory.");
	};

	createCommandQueue(mpDevice.Get());
	createDxgiSwapChain(pDxgiFactory, mWindow, WIDTH, HEIGHT, DXGI_FORMAT_R8G8B8A8_UNORM, mpCmdQueue.Get());

	if (m_Scene == Scene::SPONZA)
	{
		mpMeshLoader->LoadModel("Meshes/sponzaCrytek/sponza.obj", indices, vertices);
		meshPath = "Meshes/sponzaCrytek/";
	}
	else if(m_Scene == Scene::CORNELL)
	{
		mpMeshLoader->LoadModel("Meshes/CornellBox/CornellBox-Sphere.obj", indices, vertices, 100.0f);
		meshPath = "Meshes/CornellBox/";
	}
	else if (m_Scene == Scene::MINECRAFT)
	{
		mpMeshLoader->LoadModel("Meshes/lost-empire/lost_empire.obj", indices, vertices, 20.0f);
		meshPath = "Meshes/lost-empire/";
	}
	else if (m_Scene == Scene::RUNGHOLT)
	{
		mpMeshLoader->LoadModel("Meshes/rungholt/rungholt.obj", indices, vertices, 10.0f);
		meshPath = "Meshes/rungholt/";
	}
	else if (m_Scene == Scene::MIGUEL)
	{
		mpMeshLoader->LoadModel("Meshes/San_Miguel/san-miguel.obj", indices, vertices, 50.0f);
		meshPath = "Meshes/San_Miguel/";
	}
		
	mpMeshLoader->LoadTextures(mpDevice.Get(), mResourceDescArray, mSubResourceArray, mDDSTextureLocations);

	createDescriptorHeap(mpDevice.Get(), 2 /*gOutput SRV and UAV*/ + mDDSTextureLocations.size() + 2 /*meshinfo buffer srv and uav*/ + 2 /*vertex buffer srv and uav*/ + 2 /*index buffer srv and uav*/ + 2 /*gBufferOutput SRV and UAV*/ + 2 /*gAlbedoBufferOutput SRV and UAV*/ + 2 /*gNormalBufferOutput SRV and UAV*/, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

	createRTVDescriptorHeap(mpDevice.Get(), mDefaultRtvHeapSize, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, false);
	
	for (uint32_t i = 0; i < mDefaultSwapChainBuffers; i++)
	{
		mpDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mFrameObjects[i].pCmdAllocator));
		mpSwapchain->GetBuffer(i, IID_PPV_ARGS(&mFrameObjects[i].pSwapChainBufferRTV));
		//Descriptor heap for Render Target View
		mFrameObjects[i].rtvHandle = createRTVfCPU(mpDevice.Get(), mFrameObjects[i].pSwapChainBufferRTV.Get(), mRtvHeap.pHeap.Get(), mRtvHeap.usedEntries, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
	}

	//Create Command list
	mpDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mFrameObjects[0].pCmdAllocator.Get(), nullptr, IID_PPV_ARGS(&mpCmdList));

	//Create Fence
	mpDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mpFence));
	mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); //Empty Event
}

ID3D12Resource* createRBBuffer(ID3D12Resource* buffer, ID3D12Device* device, UINT buffer_size)
{
	if (buffer != nullptr)
	{
		buffer->Release();
	}

	D3D12_HEAP_PROPERTIES heap_properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
	D3D12_RESOURCE_DESC buffer_desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size);

	HRESULT hres = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&buffer));
	if (FAILED(hres))
	{
		throw std::runtime_error("Failed to create readback buffer!");
		return nullptr;
	}
	return buffer;
}

void Demo::initOptiX(int width, int height)
{
	// Setup the optix denoiser post processing stage
	optix_context = optix::Context::create();
	input_buffer = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, width, height);
	out_buffer = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, width, height);
	normal_buffer = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, width, height);
	albedo_buffer = optix_context->createBuffer(RT_BUFFER_INPUT_OUTPUT, RT_FORMAT_FLOAT4, width, height);

	denoiserStage = optix_context->createBuiltinPostProcessingStage("DLDenoiser");
	denoiserStage->declareVariable("input_buffer")->set(input_buffer);
	denoiserStage->declareVariable("output_buffer")->set(out_buffer);
	denoiserStage->declareVariable("input_albedo_buffer")->set(albedo_buffer);
	denoiserStage->declareVariable("input_normal_buffer")->set(normal_buffer);
	denoiserStage->declareVariable("blend");
	denoiserStage->declareVariable("hdr");   
		//--- OptiX has more variables

	optix::Variable v;
	v = denoiserStage->queryVariable("blend");
	v->setFloat(0.05f); // 0.0f = denoised, 1.0f = input.
	v = denoiserStage->queryVariable("hdr");
	v->setUint(1);

	// Add the denoiser to the new optix command list
	optiXCommandList = optix_context->createCommandList();
	optiXCommandList->appendPostprocessingStage(denoiserStage, width, height);
	optiXCommandList->finalize();

	// Compile context.
	optix_context->validate();
	optix_context->compile();

	//Executing here makes sure everything is initialized correctly
	optiXCommandList->execute();

	//Create read back buffers to store ID3D12Resource Data
	readback_input_buffer = createRBBuffer(readback_input_buffer.Get(), mpDevice.Get(), width * height * sizeof(float) * 4);
	readback_albedo_buffer = createRBBuffer(readback_albedo_buffer.Get(), mpDevice.Get(), width * height * sizeof(float) * 4);
	readback_normal_buffer = createRBBuffer(readback_normal_buffer.Get(), mpDevice.Get(), width * height * sizeof(float) * 4);
}

void Demo::createDxgiSwapChain(IDXGIFactory4* pFactory, HWND hwnd, uint32_t width, uint32_t height, DXGI_FORMAT format, ID3D12CommandQueue* pCommandQueue)
{
	HRESULT hr = 0;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = mDefaultSwapChainBuffers;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = format;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; //Don't block main thread
	swapChainDesc.SampleDesc.Count = 1;

	//Apparently CreateSwapChainForHwnd() doesn't like IDXGISwapChain3 so I'll do the conversion later in this function
	IDXGISwapChain1* pSwapChain1;
	hr = pFactory->CreateSwapChainForHwnd(pCommandQueue, hwnd, &swapChainDesc, nullptr, nullptr, &pSwapChain1);
	if (FAILED(hr))
	{
		throw std::runtime_error("Couldn't create swap chain!");
		exit(1);
	}

	//Conversion IDXGISwapChain1 to IDXGISwapChain3
	//IDXGISwapChain3* pSwapChain3;
	pSwapChain1->QueryInterface(IID_PPV_ARGS(&mpSwapchain));
	pSwapChain1->Release();
	//return mpSwapchain;
}

// Returns bool whether the device supports DirectX Raytracing tier.
inline bool IsDirectXRaytracingSupported(IDXGIAdapter1* adapter)
{
	ComPtr<ID3D12Device> testDevice;
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};

	HRESULT hr = SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice)))
		&& SUCCEEDED(testDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData)))
		&& featureSupportData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	testDevice.Reset();
	return hr;
}


HRESULT Demo::createDevice(IDXGIFactory4* pDxgiFactory)
{
	HRESULT hres = 0;
	IDXGIAdapter1* pAdapter;

	for (uint32_t i = 0; DXGI_ERROR_NOT_FOUND != pDxgiFactory->EnumAdapters1(i, &pAdapter); i++)
	{
		DXGI_ADAPTER_DESC1 desc;
		pAdapter->GetDesc1(&desc);

		// Skip SW adapters
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

		bool rt = IsDirectXRaytracingSupported(pAdapter);
		if (rt)
		{
			hres = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&mpDevice));
			pAdapter->Release();
			mAPIinUse = usedAPI::DXR;
		}	
		return 1;
	}
	return 0;
}

void Demo::createRaytracingInterfaces()
{
	HRESULT hres = 0;

	hres = mpDevice->QueryInterface(IID_PPV_ARGS(&mpDXRDevice));
	if (FAILED(hres))
	{
		throw std::runtime_error("Failed to create RayTracing device!");
	}
	hres = mpCmdList->QueryInterface(IID_PPV_ARGS(&mpDXRCmdList));
	if (FAILED(hres))
	{
		throw std::runtime_error("Failed to create RayTracing CmdList!");
	}
}

void Demo::createCommandQueue(ID3D12Device* pDevice)
{
	//ID3D12CommandQueue* pQueue;
	D3D12_COMMAND_QUEUE_DESC cqDesc = {};
	cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	pDevice->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&mpCmdQueue));
	//return pQueue;
}

void Demo::createDescriptorHeap(ID3D12Device* pDevice, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = count;
	desc.Type = type;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	
	//ID3D12DescriptorHeap* pHeap;
	pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mpSrvUavHeap));
	//return pHeap;
}

void Demo::createRTVDescriptorHeap(ID3D12Device* pDevice, uint32_t count, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = count;
	desc.Type = type;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	//ID3D12DescriptorHeap* pHeap;
	pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&mRtvHeap.pHeap));
	//return pHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE Demo::createRTVfCPU(ID3D12Device* pDevice, ID3D12Resource* pResource, ID3D12DescriptorHeap* pHeap, uint32_t& usedHeapEntries, DXGI_FORMAT format)
{
	D3D12_RENDER_TARGET_VIEW_DESC desc = {};
	desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	desc.Format = format;
	desc.Texture2D.MipSlice = 0;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = pHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += usedHeapEntries * pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	usedHeapEntries++;
	pDevice->CreateRenderTargetView(pResource, &desc, rtvHandle);
	return rtvHandle;
}

void Demo::recreateSRV()
{
	// Create the output resource. The dimensions and format should match the swap-chain
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Height = mHeight;
	resDesc.Width = mWidth;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	mpDevice->CreateCommittedResource(&mDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mpOutputResource)); // Starting as copy-source to simplify onFrameRender()	
																																							   // Create an SRV/UAV descriptor heap. Need 2 entries - 1 SRV for the scene and 1 UAV for the output
	mpDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(mWidth * mHeight * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mpBufferOutputResource));
	mpDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(mWidth * mHeight * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mpOutputAlbedoBuffer));
	mpDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(mWidth * mHeight * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mpOutputNormalBuffer));

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), mDescriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mpDevice->CreateUnorderedAccessView(mpOutputResource.Get(), nullptr, &uavDesc, uavDescriptorHandle);
	mUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mDescriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	//Another UAV as buffer
	D3D12_CPU_DESCRIPTOR_HANDLE bufferUavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), mInputSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	D3D12_UNORDERED_ACCESS_VIEW_DESC bufferUavDesc = {};
	bufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	bufferUavDesc.Buffer.CounterOffsetInBytes = 0;
	bufferUavDesc.Buffer.FirstElement = 0;
	bufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	bufferUavDesc.Buffer.NumElements = mWidth * mHeight;
	bufferUavDesc.Buffer.StructureByteStride = 16;
	bufferUavDesc.Format = DXGI_FORMAT_UNKNOWN;
	mpDevice->CreateUnorderedAccessView(mpBufferOutputResource.Get(), nullptr, &bufferUavDesc, bufferUavDescriptorHandle);
	mBufferTextureUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mInputSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	//Another UAV as buffer -> Albedo
	D3D12_CPU_DESCRIPTOR_HANDLE albedobufferUavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), mAlbedoSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	D3D12_UNORDERED_ACCESS_VIEW_DESC albedobufferUavDesc = {};
	albedobufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	albedobufferUavDesc.Buffer.CounterOffsetInBytes = 0;
	albedobufferUavDesc.Buffer.FirstElement = 0;
	albedobufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	albedobufferUavDesc.Buffer.NumElements = mWidth * mHeight;
	albedobufferUavDesc.Buffer.StructureByteStride = 16;
	albedobufferUavDesc.Format = DXGI_FORMAT_UNKNOWN;
	mpDevice->CreateUnorderedAccessView(mpOutputAlbedoBuffer.Get(), nullptr, &albedobufferUavDesc, albedobufferUavDescriptorHandle);
	mAlbedoBufferTextureUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mAlbedoSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	//Another UAV as buffer -> Normal
	D3D12_CPU_DESCRIPTOR_HANDLE normalbufferUavDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetCPUDescriptorHandleForHeapStart(), mNormalSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));;
	D3D12_UNORDERED_ACCESS_VIEW_DESC normalbufferUavDesc = {};
	normalbufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	normalbufferUavDesc.Buffer.CounterOffsetInBytes = 0;
	normalbufferUavDesc.Buffer.FirstElement = 0;
	normalbufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	normalbufferUavDesc.Buffer.NumElements = mWidth * mHeight;
	normalbufferUavDesc.Buffer.StructureByteStride = 16;
	normalbufferUavDesc.Format = DXGI_FORMAT_UNKNOWN;
	mpDevice->CreateUnorderedAccessView(mpOutputNormalBuffer.Get(), nullptr, &normalbufferUavDesc, normalbufferUavDescriptorHandle);
	mNormalBufferTextureUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mNormalSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

}

void Demo::createSRV()
{
	// Create the output resource. The dimensions and format should match the swap-chain
	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.DepthOrArraySize = 1;
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats can't be used with UAVs. We will convert to sRGB ourselves in the shader
	resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	resDesc.Height = mHeight;
	resDesc.Width = mWidth;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	mpDevice->CreateCommittedResource(&mDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mpOutputResource)); // Starting as copy-source to simplify onFrameRender()	
			
																																								   // Create an SRV/UAV descriptor heap. Need 2 entries - 1 SRV for the scene and 1 UAV for the output
	mpDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(mWidth * mHeight * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mpBufferOutputResource));
	mpDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(mWidth * mHeight * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mpOutputAlbedoBuffer));
	mpDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(mWidth * mHeight * sizeof(float) * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&mpOutputNormalBuffer));

	//mpDevice->CreateCommittedResource(&mDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&mpBufferOutputResource));
	// Create the UAV. Based on the root signature we created it should be the first entry

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptorHandle;
	mDescriptorHeapIndex = AllocateDescriptor(&uavDescriptorHandle, mDescriptorHeapIndex);
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mpDevice->CreateUnorderedAccessView(mpOutputResource.Get(), nullptr, &uavDesc, uavDescriptorHandle);
	mUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mDescriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	
	//Another UAV as buffer
	D3D12_CPU_DESCRIPTOR_HANDLE bufferUavDescriptorHandle;
	UINT slot = UINT_MAX; 
	mInputSlot = AllocateDescriptor(&bufferUavDescriptorHandle, slot);
	D3D12_UNORDERED_ACCESS_VIEW_DESC bufferUavDesc = {};
	bufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	bufferUavDesc.Buffer.CounterOffsetInBytes = 0;
	bufferUavDesc.Buffer.FirstElement = 0;
	bufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	bufferUavDesc.Buffer.NumElements = mWidth * mHeight;
	bufferUavDesc.Buffer.StructureByteStride = 16;
	bufferUavDesc.Format = DXGI_FORMAT_UNKNOWN;
	mpDevice->CreateUnorderedAccessView(mpBufferOutputResource.Get(), nullptr, &bufferUavDesc, bufferUavDescriptorHandle);
	mBufferTextureUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mInputSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	//Another UAV as buffer -> Albedo
	D3D12_CPU_DESCRIPTOR_HANDLE albedobufferUavDescriptorHandle;
	slot = UINT_MAX;
	mAlbedoSlot = AllocateDescriptor(&albedobufferUavDescriptorHandle, slot);
	D3D12_UNORDERED_ACCESS_VIEW_DESC albedobufferUavDesc = {};
	albedobufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	albedobufferUavDesc.Buffer.CounterOffsetInBytes = 0;
	albedobufferUavDesc.Buffer.FirstElement = 0;
	albedobufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	albedobufferUavDesc.Buffer.NumElements = mWidth * mHeight;
	albedobufferUavDesc.Buffer.StructureByteStride = 16;
	albedobufferUavDesc.Format = DXGI_FORMAT_UNKNOWN;
	mpDevice->CreateUnorderedAccessView(mpOutputAlbedoBuffer.Get(), nullptr, &albedobufferUavDesc, albedobufferUavDescriptorHandle);
	mAlbedoBufferTextureUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mAlbedoSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	//Another UAV as buffer -> Normal
	D3D12_CPU_DESCRIPTOR_HANDLE normalbufferUavDescriptorHandle;
	slot = UINT_MAX;
	mNormalSlot = AllocateDescriptor(&normalbufferUavDescriptorHandle, slot);
	D3D12_UNORDERED_ACCESS_VIEW_DESC normalbufferUavDesc = {};
	normalbufferUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	normalbufferUavDesc.Buffer.CounterOffsetInBytes = 0;
	normalbufferUavDesc.Buffer.FirstElement = 0;
	normalbufferUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
	normalbufferUavDesc.Buffer.NumElements = mWidth * mHeight;
	normalbufferUavDesc.Buffer.StructureByteStride = 16;
	normalbufferUavDesc.Format = DXGI_FORMAT_UNKNOWN;
	mpDevice->CreateUnorderedAccessView(mpOutputNormalBuffer.Get(), nullptr, &normalbufferUavDesc, normalbufferUavDescriptorHandle);
	mNormalBufferTextureUavDescriptorHandleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), mNormalSlot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
}

UINT Demo::AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor, UINT descriptorIndexToUse)
{
	auto descriptorHeapCpuBase = mpSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
	if (descriptorIndexToUse >= mpSrvUavHeap->GetDesc().NumDescriptors)
	{
		descriptorIndexToUse = mUavHeap.usedEntries++;
	}
	*cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	return descriptorIndexToUse;
}

void Demo::resourceBarrier(ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* pResource, D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = pResource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = stateBefore;
	barrier.Transition.StateAfter = stateAfter;
	pCmdList->ResourceBarrier(1, &barrier);
}

uint64_t Demo::submitCommandList(ID3D12GraphicsCommandList* pCmdList, ID3D12CommandQueue* pCmdQueue, ID3D12Fence* pFence, uint64_t fenceValue)
{
	pCmdList->Close();
	ID3D12CommandList* pGraphicsList = pCmdList;
	pCmdQueue->ExecuteCommandLists(1, &pGraphicsList);
	fenceValue++;
	pCmdQueue->Signal(pFence, fenceValue);
	return fenceValue;
}

void Demo::createHeapProperties()
{
	//UploadHeap
	mUploadHeapProps.Type = D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_UPLOAD;
	mUploadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL::D3D12_MEMORY_POOL_UNKNOWN;
	mUploadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY::D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	mUploadHeapProps.CreationNodeMask = 0;
	mUploadHeapProps.VisibleNodeMask = 0;

	//DefaultHeap
	mDefaultHeapProps.Type = D3D12_HEAP_TYPE::D3D12_HEAP_TYPE_DEFAULT;
	mDefaultHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL::D3D12_MEMORY_POOL_UNKNOWN;
	mDefaultHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY::D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	mDefaultHeapProps.CreationNodeMask = 0;
	mDefaultHeapProps.VisibleNodeMask = 0;
}

D3D12_RESOURCE_DESC Demo::createBufferVoid(ID3D12Device* pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps, ID3D12Resource* pResource)
{
	HRESULT hres = 0;

	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = flags;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = size;

	return bufDesc;
}

ID3D12Resource* Demo::createBuffer(ID3D12Device* pDevice, uint64_t size, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initState, const D3D12_HEAP_PROPERTIES& heapProps, ID3D12Resource* pResource)
{
	HRESULT hres = 0;

	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = flags;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = size;

	ID3D12Resource* pBuffer = nullptr; //DELETE ME!!!
	hres = pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, initState, nullptr, IID_PPV_ARGS(&pBuffer));
	if (SUCCEEDED(hres))
	{
		return pBuffer;
	}
	return nullptr;
}

void Demo::createTextureResources()
{
	mResourceUploadArray.resize(mDDSTextureLocations.size());
	mResourceArray.resize(mDDSTextureLocations.size());

	std::unique_ptr<uint8_t[]> ddsData;
	for (size_t i = 0; i < mDDSTextureLocations.size(); i++)
	{
		mSubResourceArray.clear();
		const unsigned BUFFER_SIZE = 99;
		char array1[BUFFER_SIZE];
		//strncpy_s(array1, "Meshes/CornellBox/", BUFFER_SIZE - 1); //-1 for null-termination
		//strncpy_s(array1, "Meshes/sponzaCrytek/", BUFFER_SIZE - 1); //-1 for null-termination
		strncpy_s(array1, meshPath.c_str(), BUFFER_SIZE - 1); //-1 for null-termination
		strncat_s(array1, mDDSTextureLocations[i].c_str(), BUFFER_SIZE - strlen(array1) - 1); //-1 for null-termination

		std::wstring wstringarr;
		for (int i = 0; i < strlen(array1); ++i)
			wstringarr += wchar_t(array1[i]);

		const wchar_t* wcharpointer = wstringarr.c_str();

		HRESULT hr = LoadDDSTextureFromFile(mpDevice.Get(), wcharpointer, &mResourceArray[i], ddsData, mSubResourceArray);
		if (FAILED(hr))
		{
			throw std::runtime_error("Failed to load a .dds file.");
		}
		//On default heap
		/*for (size_t i = 0; i < mDDSTextureLocations.size(); i++)
		{
			mpDevice->CreateCommittedResource(&mDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &mResourceDescArray[i],
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mResourceArray[i]));
		}*/
		//on Upload heap
		UINT64 textureUploadBufferSize;
		const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mResourceArray[i].Get(), 0, static_cast<UINT>(mSubResourceArray.size()));
		hr = mpDevice->CreateCommittedResource(&mUploadHeapProps, D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mResourceUploadArray[i]));
		if (FAILED(hr))
		{
			throw std::runtime_error("Failed to create texture upload buffer");
		}
		//Update subresources
		UpdateSubresources(mpCmdList.Get(), mResourceArray[i].Get(), mResourceUploadArray[i].Get(), 0, 0, mSubResourceArray.size(), mSubResourceArray.data());
		//Transition resource
		mpCmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mResourceArray[i].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

		// now we create a shader resource view (descriptor that points to the texture and describes it)
		/*D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = mResourceDescArray[i].Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = mSubResourceArray.size();*/

		D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptorHandle;
		UINT slot = UINT_MAX;
		slot = AllocateDescriptor(&srvDescriptorHandle, slot);
		CreateShaderResourceView(mpDevice.Get(), mResourceArray[i].Get(), srvDescriptorHandle);
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), slot, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
		mTextureGpuDescHandleArray.push_back(srvHandle);
	}
}

void Demo::createColorDataResource()
{
	//Color Data Array
	mColorDataInfoArray.resize(WIDTH*HEIGHT);


	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = mColorDataInfoArray.size() * sizeof(mColorDataInfoArray[0]);

	//Upload Buffer
	mpColorDataStructureUploadBuffer = createBuffer(mpDevice.Get(), sizeof(mColorDataInfoArray[0])* mColorDataInfoArray.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps);
	uint8_t* pUploadData;
	mpColorDataStructureUploadBuffer->Map(0, nullptr, (void**)&pUploadData);
	memcpy(pUploadData, mColorDataInfoArray.data(), sizeof(mColorDataInfoArray[0])* mColorDataInfoArray.size());
	mpColorDataStructureUploadBuffer->Unmap(0, nullptr);

	// For simplicity, we create the vertex buffer on the upload heap, but that's not required
	mpColorDataStructureBuffer = createBuffer(mpDevice.Get(), sizeof(mColorDataInfoArray[0]) * mColorDataInfoArray.size(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, mDefaultHeapProps);
	mpCmdList->CopyBufferRegion(mpColorDataStructureBuffer.Get(), 0, mpColorDataStructureUploadBuffer.Get(), 0, sizeof(mColorDataInfoArray[0]) * mColorDataInfoArray.size());
	resourceBarrier(mpCmdList.Get(), mpColorDataStructureBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.NumElements = mColorDataInfoArray.size();
	SRVDesc.Buffer.StructureByteStride = sizeof(colorData);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptor;
	UINT descriptorHeapIndex = 0;
	descriptorHeapIndex = AllocateDescriptor(&srvDescriptor, UINT_MAX);
	mpDevice->CreateShaderResourceView(mpColorDataStructureBuffer.Get(), &SRVDesc, srvDescriptor);
	mColorDataInfoBufferSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), descriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	mColorDataInfoHeapIndex = descriptorHeapIndex;

	D3D12_UNORDERED_ACCESS_VIEW_DESC ColorDataInfoUAVDesc = {};
	ColorDataInfoUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	ColorDataInfoUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	ColorDataInfoUAVDesc.Buffer.NumElements = mColorDataInfoArray.size();
	ColorDataInfoUAVDesc.Buffer.StructureByteStride = sizeof(colorData);
	ColorDataInfoUAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptor;
	descriptorHeapIndex = 0;
	descriptorHeapIndex = AllocateDescriptor(&uavDescriptor, UINT_MAX);
	mpDevice->CreateUnorderedAccessView(mpColorDataStructureBuffer.Get(), nullptr, &ColorDataInfoUAVDesc, uavDescriptor);
	mColorDataInfoBufferUAV = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), descriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	mColorDataInfoHeapIndex = descriptorHeapIndex;
}

void Demo::createPointLightResources()
{
	//Point Lights
	PointLightInfo plInfo;
	plInfo.position = glm::float3(4000, 6000, 2000);
	plInfo.emissiveColor = glm::float3(0.0, 0.0, 0.0);
	plInfo.radius = 1000;
	plInfo.intensity = 1000000000000000;
	mPointLightInfoArray.push_back(plInfo);
	if(m_Scene == Scene::SPONZA)
	{
		plInfo.position = glm::float3(-50, 800, -0);
		plInfo.emissiveColor = glm::float3(0.0, 0.0, 0.0);
		plInfo.radius = 10;
		plInfo.intensity = 100000;
		mPointLightInfoArray.push_back(plInfo);

		/*plInfo.position = glm::float3(400, 200, 450);
		plInfo.emissiveColor = glm::float3(1.0, 0.0, 0.0);
		plInfo.radius = 10;
		plInfo.intensity = 10000;
		mPointLightInfoArray.push_back(plInfo);*/

		plInfo.position = glm::float3(-500, 200, -450);
		plInfo.emissiveColor = glm::float3(0.0, 1.0, 0.0);
		plInfo.radius = 10;
		plInfo.intensity = 15000;
		mPointLightInfoArray.push_back(plInfo);
	}
	else if(m_Scene == Scene::CORNELL)
	{
		plInfo.position = glm::float3(0, 120.0f, 0);
		plInfo.emissiveColor = glm::float3(0.0, 0.0, 0.0);
		plInfo.radius = 3.0;
		plInfo.intensity = 4000;
		mPointLightInfoArray.push_back(plInfo);
	}
	else if (m_Scene == Scene::MINECRAFT)
	{
		plInfo.position = glm::float3(37.0f, 525.0f, -244.0f);
		plInfo.emissiveColor = glm::float3(0.0, 0.0, 0.0);
		plInfo.radius = 1.0;
		plInfo.intensity = 30000;
		mPointLightInfoArray.push_back(plInfo);
	}
	else if (m_Scene == Scene::RUNGHOLT)
	{
		/*plInfo.position = glm::float3(4000, 10000, 2000);
		plInfo.emissiveColor = glm::float3(0.0, 0.0, 0.0);
		plInfo.radius = 100;
		plInfo.intensity = 100000000;
		mPointLightInfoArray.push_back(plInfo);*/
	}
	else if (m_Scene == Scene::MIGUEL)
	{
		/*plInfo.position = glm::float3(4000, 10000, 2000);
		plInfo.emissiveColor = glm::float3(0.0, 0.0, 0.0);
		plInfo.radius = 100;
		plInfo.intensity = 200000000;
		mPointLightInfoArray.push_back(plInfo);*/
	}
	//dirty work around constant buffer issue
	mPointLightInfoArray[0].size = 0;
	mPointLightInfoArray[1].size = 0;

	//Upload Buffer
	mpPointLightStructureUploadBuffer = createBuffer(mpDevice.Get(), sizeof(mPointLightInfoArray[0])* mPointLightInfoArray.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps);
	//D3D12_RESOURCE_DESC desc = createBufferVoid(mpDevice.Get(), sizeof(mPointLightInfoArray[0])* mPointLightInfoArray.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps, mpPointLightStructureUploadBuffer.Get());
	//mpDevice->CreateCommittedResource(&mUploadHeapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mpPointLightStructureUploadBuffer));
	uint8_t* pUploadData;
	mpPointLightStructureUploadBuffer->Map(0, nullptr, (void**)&pUploadData);
	memcpy(pUploadData, mPointLightInfoArray.data(), sizeof(mPointLightInfoArray[0])* mPointLightInfoArray.size());
	mpPointLightStructureUploadBuffer->Unmap(0, nullptr);

	// For simplicity, we create the vertex buffer on the upload heap, but that's not required
	mpPointLightStructureBuffer = createBuffer(mpDevice.Get(), sizeof(mPointLightInfoArray[0]) * mPointLightInfoArray.size(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, mDefaultHeapProps);
	//D3D12_RESOURCE_DESC desc2 = createBufferVoid(mpDevice.Get(), sizeof(mPointLightInfoArray[0]) * mPointLightInfoArray.size(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, mDefaultHeapProps, mpPointLightStructureBuffer.Get());
	//mpDevice->CreateCommittedResource(&mDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &desc2, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&mpPointLightStructureBuffer));
	mpCmdList->CopyBufferRegion(mpPointLightStructureBuffer.Get(), 0, mpPointLightStructureUploadBuffer.Get(), 0, sizeof(mPointLightInfoArray[0]) * mPointLightInfoArray.size());
	resourceBarrier(mpCmdList.Get(), mpPointLightStructureBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.NumElements = mPointLightInfoArray.size();
	SRVDesc.Buffer.StructureByteStride = sizeof(PointLightInfo);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptor;
	UINT descriptorHeapIndex = 0;
	descriptorHeapIndex = AllocateDescriptor(&srvDescriptor, UINT_MAX);
	mpDevice->CreateShaderResourceView(mpPointLightStructureBuffer.Get(), &SRVDesc, srvDescriptor);
	mPointLightInfoBufferSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), descriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	mPointLightInfoHeapIndex = descriptorHeapIndex;

	D3D12_UNORDERED_ACCESS_VIEW_DESC PointLightInfoUAVDesc = {};
	PointLightInfoUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	PointLightInfoUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	PointLightInfoUAVDesc.Buffer.NumElements = mPointLightInfoArray.size();
	PointLightInfoUAVDesc.Buffer.StructureByteStride = sizeof(PointLightInfo);
	PointLightInfoUAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE uavDescriptor;
	descriptorHeapIndex = 0;
	descriptorHeapIndex = AllocateDescriptor(&uavDescriptor, UINT_MAX);
	mpDevice->CreateUnorderedAccessView(mpPointLightStructureBuffer.Get(), nullptr, &PointLightInfoUAVDesc, uavDescriptor);
	mPointLightInfoBufferUAV = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), descriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	mPointLightInfoHeapIndex = descriptorHeapIndex;
}

Demo::VertexIndexCombo Demo::createVertexAndIndexBuffer(ID3D12Device* pDevice)
{
	Demo::VertexIndexCombo combo;
	//QuickRotation(vertices);

	//Upload Buffer
	mpVertexUploadBuffer = createBuffer(pDevice, sizeof(vertices[0])* vertices.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps);
	uint8_t* pUploadData;
	mpVertexUploadBuffer->Map(0, nullptr, (void**)&pUploadData);
	memcpy(pUploadData, vertices.data(), sizeof(vertices[0])* vertices.size());
	mpVertexUploadBuffer->Unmap(0, nullptr);

	mpVertexBuffer = createBuffer(pDevice, sizeof(vertices[0]) * vertices.size(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, mDefaultHeapProps);
	uint8_t* pData;
	mpCmdList->CopyBufferRegion(mpVertexBuffer.Get(), 0, mpVertexUploadBuffer.Get(), 0, sizeof(vertices[0]) * vertices.size());
	resourceBarrier(mpCmdList.Get(), mpVertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
	combo.pVertex = mpVertexBuffer;

	//Create vertexBuffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.NumElements = vertices.size();
	SRVDesc.Buffer.StructureByteStride = sizeof(Vertex);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptor;
	UINT descriptorHeapIndex = 0;
	descriptorHeapIndex = AllocateDescriptor(&srvDescriptor, UINT_MAX);
	mpDevice->CreateShaderResourceView(mpVertexBuffer.Get(), &SRVDesc, srvDescriptor);
	mVertexBufferSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), descriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	mVertexBufferHeapIndex = descriptorHeapIndex;

	D3D12_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.Buffer.NumElements = vertices.size();
	UAVDesc.Buffer.StructureByteStride = sizeof(Vertex);
	UAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	//Upload Buffer
	mpIndexUploadBuffer = createBuffer(pDevice, sizeof(indices[0])* indices.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps);
	uint8_t* pUploadIndData;
	mpIndexUploadBuffer->Map(0, nullptr, (void**)&pUploadIndData);
	memcpy(pUploadIndData, indices.data(), sizeof(indices[0])* indices.size());
	mpIndexUploadBuffer->Unmap(0, nullptr);

	//Index Buffer
	mpIndexBuffer = createBuffer(pDevice, sizeof(indices[0])* indices.size(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, mDefaultHeapProps);
	mpCmdList->CopyBufferRegion(mpIndexBuffer.Get(), 0, mpIndexUploadBuffer.Get(), 0, sizeof(indices[0])* indices.size());
	resourceBarrier(mpCmdList.Get(), mpIndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
	combo.pIndex = mpIndexBuffer;

	//Create indexBuffer SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDescIndex = {};
	SRVDescIndex.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDescIndex.Format = DXGI_FORMAT_UNKNOWN;
	SRVDescIndex.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDescIndex.Buffer.NumElements = indices.size();
	SRVDescIndex.Buffer.StructureByteStride = sizeof(uint32_t);
	SRVDescIndex.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptorIndex;
	descriptorHeapIndex = 0;
	descriptorHeapIndex = AllocateDescriptor(&srvDescriptorIndex, UINT_MAX);
	mpDevice->CreateShaderResourceView(mpIndexBuffer.Get(), &SRVDescIndex, srvDescriptorIndex);
	mIndexBufferSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), descriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	mIndexBufferHeapIndex = descriptorHeapIndex;

	D3D12_UNORDERED_ACCESS_VIEW_DESC indexUAVDesc = {};
	indexUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	indexUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	indexUAVDesc.Buffer.NumElements = indices.size();
	indexUAVDesc.Buffer.StructureByteStride = sizeof(uint32_t);
	indexUAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

	return combo;
}

void Demo::setMeshInfo(int index)
{
	//Create MeshInfo struct to use in shader
	MeshInfo meshInfo;
	meshInfo.vertexStride = sizeof(Vertex);
	meshInfo.vertexDataByteOffset = mpMeshLoader->mpModel->mMeshes[index].vertexDataByteOffset;
	//meshInfo.vertexCount = mpMeshLoader->mpModel->mMeshes[index].vertexCount;
	meshInfo.indexStride = sizeof(uint32_t);
	meshInfo.indexDataByteOffset = mpMeshLoader->mpModel->mMeshes[index].indexDataByteOffset;
	//meshInfo.indexCount = mpMeshLoader->mpModel->mMeshes[index].indexCount;
	meshInfo.isReflective = 0;
	meshInfo.isRefractive = 0;
	meshInfo.emissiveStrength = 0;
	meshInfo.hasTexture = 1;
	meshInfo.indexOffset = mpMeshLoader->mpModel->mMeshes[index].indexDataByteOffset / sizeof(uint32_t);
	meshInfo.materialIndex = mpMeshLoader->mpModel->mMeshes[index].materialIndex;
	std::string path = mDDSTextureLocations[meshInfo.materialIndex];
	std::string def = ("default.dds");
	std::size_t found = path.find(def);
	if (found != std::string::npos)
	{
		meshInfo.hasTexture = 0;
		meshInfo.diffuseColor = glm::vec3(mpMeshLoader->mpModel->mMaterials[meshInfo.materialIndex].Diffuse.x, mpMeshLoader->mpModel->mMaterials[meshInfo.materialIndex].Diffuse.y, mpMeshLoader->mpModel->mMaterials[meshInfo.materialIndex].Diffuse.z);
	}
	if (m_Scene == Scene::SPONZA)
	{
		if (index == 8)
		{
			meshInfo.isReflective = 1;
		}
		//23 is lionHead, 21 metal baskets, 22 corner ornaments, 13 metal bars, 9 pillars
		if (index == 21 || index == 13)
		{
			meshInfo.emissiveStrength = 0.0f;
		}
	}
	if (m_Scene == Scene::CORNELL)
	{
		if (index == 0)
		{
			meshInfo.isReflective = 1;
		}
		if (index == 1)
		{
			meshInfo.isRefractive = 1;
		}
		//7 is cornell light
		if (index == 7)
		{
			meshInfo.emissiveStrength = 1.0f;
		}
	}
	if (m_Scene == Scene::MINECRAFT)
	{
		if (index == 0)
		{
			meshInfo.isReflective = 1;
		}
		//7 is cornell light
		if (index == 7)
		{
			meshInfo.emissiveStrength = 0.9f;
		}
	}
	if (m_Scene == Scene::RUNGHOLT)
	{
		if (index == 6)
		{
			meshInfo.isReflective = 1;
		}
		//7 is cornell light
		if (index == 7)
		{
			meshInfo.emissiveStrength = 1.0f;
		}
	}
	if (m_Scene == Scene::MIGUEL)
	{
		if (index == 0)
		{
			meshInfo.isReflective = 1;
		}
		//7 is cornell light
		if (index == 7)
		{
			meshInfo.emissiveStrength = 0.9f;
		}
	}
	mMeshesInfoArray.push_back(meshInfo);
}

AccelerationStructureBuffers Demo::createBottomLevelAS(ID3D12Device* pDevice, ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* pVertexBuffer, int index)
{
	setMeshInfo(index);

	D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
	geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
	geomDesc.Triangles.VertexBuffer.StartAddress = pVertexBuffer->GetGPUVirtualAddress() + mpMeshLoader->mpModel->mMeshes[index].vertexDataByteOffset;
	geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
	geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
	geomDesc.Triangles.VertexCount = mpMeshLoader->mpModel->mMeshes[index].vertexCount;
	geomDesc.Triangles.IndexBuffer = mpIndexBuffer->GetGPUVirtualAddress() + mpMeshLoader->mpModel->mMeshes[index].indexDataByteOffset;
	geomDesc.Triangles.IndexCount = mpMeshLoader->mpModel->mMeshes[index].indexCount;
	geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
	geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = {};
	bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	bottomLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	bottomLevelInputs.NumDescs = 1;
	bottomLevelInputs.pGeometryDescs = &geomDesc;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	mpDXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &info);

	// Create the buffers. They need to support UAV, and since we are going to immediately use them, we create them with an unordered-access state
	AccelerationStructureBuffers buffers;
	buffers.pScratch = createBuffer(pDevice, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, mDefaultHeapProps);
	
	D3D12_RESOURCE_STATES initialResourceState;
	initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	buffers.pResult = createBuffer(pDevice, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, initialResourceState, mDefaultHeapProps);

	// Create the bottom-level AS
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	asDesc.Inputs = bottomLevelInputs;
	asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
	asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();
	mBottomLevelSize.push_back(info.ResultDataMaxSizeInBytes);

	mpDXRCmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = buffers.pResult.Get();
	pCmdList->ResourceBarrier(1, &uavBarrier);

	return buffers;
}

AccelerationStructureBuffers Demo::createTopLevelAS(ID3D12Device* pDevice, ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* pBottomLevelAS, uint64_t& tlasSize)
{
	// First, get the size of the TLAS buffers and create them
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {};
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	topLevelInputs.NumDescs = mpMeshLoader->mpModel->mMeshes.size();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	mpDXRDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &info);

	// Create the buffers
	AccelerationStructureBuffers buffers;
	buffers.pScratch = createBuffer(pDevice, info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, mDefaultHeapProps);
	
	D3D12_RESOURCE_STATES initialResourceState;
	initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	buffers.pResult = createBuffer(pDevice, info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, initialResourceState, mDefaultHeapProps);
	tlasSize = info.ResultDataMaxSizeInBytes;

	// The instance desc should be inside a buffer, create and map the buffer
	buffers.pInstanceDesc = createBuffer(pDevice, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mpMeshLoader->mpModel->mMeshes.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps);
	D3D12_RAYTRACING_INSTANCE_DESC* pInstanceDescs;
	buffers.pInstanceDesc->Map(0, nullptr, (void**)&pInstanceDescs);
	ZeroMemory(pInstanceDescs, sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * mpMeshLoader->mpModel->mMeshes.size());


	// Initialize the instance desc. We only have a single instance
	for (size_t i = 0; i < mpMeshLoader->mpModel->mMeshes.size(); i++)
	{
		pInstanceDescs[i].InstanceID = i;                            // This value will be exposed to the shader via InstanceID()
		pInstanceDescs[i].InstanceContributionToHitGroupIndex = i;   // This is the offset inside the shader-table.
		pInstanceDescs[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;

		glm::mat4 m = glm::mat4(); // Identity matrix
		memcpy(pInstanceDescs[i].Transform, &m, sizeof(pInstanceDescs[i].Transform));

		UINT numBufferElements = static_cast<UINT>(mBottomLevelSize[i]) / sizeof(UINT32);
		pInstanceDescs[i].AccelerationStructure = mpBottomLevelAS[i]->GetGPUVirtualAddress();
		pInstanceDescs[i].InstanceMask = 0xFF;
	}

	// Unmap
	buffers.pInstanceDesc->Unmap(0, nullptr);

	// Create the Top Level Acceleration Structure
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC asDesc = {};
	topLevelInputs.InstanceDescs = buffers.pInstanceDesc->GetGPUVirtualAddress();
	asDesc.Inputs = topLevelInputs;
	asDesc.DestAccelerationStructureData = buffers.pResult->GetGPUVirtualAddress();
	asDesc.ScratchAccelerationStructureData = buffers.pScratch->GetGPUVirtualAddress();

	mpDXRCmdList->BuildRaytracingAccelerationStructure(&asDesc, 0, nullptr);

	// We need to insert a UAV barrier before using the acceleration structures in a raytracing operation
	D3D12_RESOURCE_BARRIER uavBarrier = {};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = buffers.pResult.Get();
	pCmdList->ResourceBarrier(1, &uavBarrier);

	return buffers;
}

void Demo::createAccelerationStructures()
{
#ifdef USE_PIX
	char const* formatStr = "Acceleration Structures";
	PIXBeginEvent(mpCmdList.Get(), UINT64(2), formatStr);
#endif // USE_PIX

	Demo::VertexIndexCombo combo = createVertexAndIndexBuffer(*mpDevice.GetAddressOf());
	mpVertexBuffer = combo.pVertex;
	mpIndexBuffer = combo.pIndex;

	mpBottomLevelAS.resize(mpMeshLoader->mpModel->mMeshes.size());
	for (size_t i = 0; i < mpMeshLoader->mpModel->mMeshes.size(); i++)
	{
		// Store the AS buffers. The rest of the buffers will be released once we exit the function
		mpBottomLevelAS[i] = createBottomLevelAS(mpDevice.Get(), mpCmdList.Get(), mpVertexBuffer.Get(), i).pResult;
	}
	
	// Store the AS buffers. The rest of the buffers will be released once we exit the function
	mpTopLevelAS = createTopLevelAS(mpDevice.Get(), mpCmdList.Get(), mpBottomLevelAS[mTlasSize].Get(), mTlasSize).pResult;

	//Mesh Info structure buffer
	createMeshInfoStructureBuffer(mMeshesInfoArray);

	// The tutorial doesn't have any resource lifetime management, so we flush and sync here. This is not required by the DXR spec - you can submit the list whenever you like as long as you take care of the resources lifetime.
	mFenceValue = submitCommandList(mpCmdList.Get(), mpCmdQueue.Get(), mpFence.Get(), mFenceValue);
	mpFence->SetEventOnCompletion(mFenceValue, mFenceEvent);
	WaitForSingleObject(mFenceEvent, INFINITE);
	uint32_t bufferIndex = mpSwapchain->GetCurrentBackBufferIndex();
	mpCmdList->Reset(mFrameObjects[0].pCmdAllocator.Get(), nullptr);

#ifdef USE_PIX
	PIXEndEvent(mpCmdList.Get());
#endif // USE_PIX
}
/***********************************************************************PIPELINE************************************************************************************************/
template<typename BlobType>
inline std::string convertBlobToString(BlobType* pBlob)
{
	std::vector<char> infoLog(pBlob->GetBufferSize() + 1);
	memcpy(infoLog.data(), pBlob->GetBufferPointer(), pBlob->GetBufferSize());
	infoLog[pBlob->GetBufferSize()] = 0;
	return std::string(infoLog.data());
}

ComPtr<IDxcBlob> Demo::compileLibrary(const WCHAR* filename, const WCHAR* targetString)
{
	// Initialize the helper
	gDxcDllHelper.Initialize();
	IDxcCompiler* pCompiler;
	IDxcLibrary* pLibrary;
	gDxcDllHelper.CreateInstance(CLSID_DxcCompiler, &pCompiler);
	gDxcDllHelper.CreateInstance(CLSID_DxcLibrary, &pLibrary);

	// Open and read the file
	std::ifstream shaderFile(filename);
	if (shaderFile.good() == false)
	{
		throw std::runtime_error("Can't open file DXIL file.");
	}
	std::stringstream strStream;
	strStream << shaderFile.rdbuf();
	std::string shader = strStream.str();

	// Create blob from the string
	IDxcBlobEncoding* pTextBlob;
	pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)shader.c_str(), (uint32_t)shader.size(), 0, &pTextBlob);

	// Compile
	IDxcOperationResult* pResult;
	pCompiler->Compile(pTextBlob, filename, L"", targetString, nullptr, 0, nullptr, 0, nullptr, &pResult);

	// Verify the result
	HRESULT resultCode;
	pResult->GetStatus(&resultCode);
	if (FAILED(resultCode))
	{
		IDxcBlobEncoding* blob;
		pResult->GetErrorBuffer(&blob);
		std::string errorStr = convertBlobToString(blob);
		OutputDebugStringA(errorStr.c_str());
		throw std::runtime_error(errorStr);
	}

	ComPtr<IDxcBlob> pBlob;
	pResult->GetResult(&pBlob);
	return pBlob;
}

void Demo::createMeshInfoStructureBuffer(std::vector<MeshInfo> meshInfoArray)
{
	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Alignment = 0;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	bufDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufDesc.Height = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.SampleDesc.Quality = 0;
	bufDesc.Width = meshInfoArray.size() * sizeof(meshInfoArray[0]);

	//Upload Buffer
	mpMeshInfoStructureUploadBuffer = createBuffer(mpDevice.Get(), sizeof(meshInfoArray[0])* meshInfoArray.size(), D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps);
	uint8_t* pUploadData;
	mpMeshInfoStructureUploadBuffer->Map(0, nullptr, (void**)&pUploadData);
	memcpy(pUploadData, meshInfoArray.data(), sizeof(meshInfoArray[0])* meshInfoArray.size());
	mpMeshInfoStructureUploadBuffer->Unmap(0, nullptr);

	// For simplicity, we create the vertex buffer on the upload heap, but that's not required
	mpMeshInfoStructureBuffer = createBuffer(mpDevice.Get(), sizeof(meshInfoArray[0]) * meshInfoArray.size(), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST, mDefaultHeapProps);
	mpCmdList->CopyBufferRegion(mpMeshInfoStructureBuffer.Get(), 0, mpMeshInfoStructureUploadBuffer.Get(), 0, sizeof(meshInfoArray[0]) * meshInfoArray.size());
	resourceBarrier(mpCmdList.Get(), mpMeshInfoStructureBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);

	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	SRVDesc.Buffer.NumElements = meshInfoArray.size();
	SRVDesc.Buffer.StructureByteStride = sizeof(MeshInfo);
	SRVDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	D3D12_CPU_DESCRIPTOR_HANDLE srvDescriptor;
	UINT descriptorHeapIndex = 0;
	descriptorHeapIndex =  AllocateDescriptor(&srvDescriptor, UINT_MAX);
	mpDevice->CreateShaderResourceView(mpMeshInfoStructureBuffer.Get(), &SRVDesc, srvDescriptor);
	mMeshInfoBufferSRV = CD3DX12_GPU_DESCRIPTOR_HANDLE(mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart(), descriptorHeapIndex, mpDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
	mMeshInfoHeapIndex = descriptorHeapIndex;

	D3D12_UNORDERED_ACCESS_VIEW_DESC meshInfoUAVDesc = {};
	meshInfoUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	meshInfoUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	meshInfoUAVDesc.Buffer.NumElements = meshInfoArray.size();
	meshInfoUAVDesc.Buffer.StructureByteStride = sizeof(MeshInfo);
	meshInfoUAVDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
}

void createRootSignature(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
{
	ID3DBlob* pSigBlob;
	ID3DBlob* pErrorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &pSigBlob, &pErrorBlob);
	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to serialize Root Signature.");
	}

	hr = pDevice->CreateRootSignature(0, pSigBlob->GetBufferPointer(), pSigBlob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig)));
	if (FAILED(hr))
	{
		throw std::runtime_error("Failed to serialize Root Signature.");
	}
}

struct RootSignatureDesc
{
	D3D12_ROOT_SIGNATURE_DESC desc = {};
	std::vector<D3D12_DESCRIPTOR_RANGE> range;
	std::vector<D3D12_ROOT_PARAMETER> rootParams;
};

struct DxilLibrary
{
	DxilLibrary(IDxcBlob* pBlob, const WCHAR* entryPoint[], uint32_t entryPointCount) : pShaderBlob(pBlob)
	{
		stateSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		stateSubobject.pDesc = &dxilLibDesc;

		dxilLibDesc = {};
		exportDesc.resize(entryPointCount);
		exportName.resize(entryPointCount);
		if (pBlob)
		{
			dxilLibDesc.DXILLibrary.pShaderBytecode = pBlob->GetBufferPointer();
			dxilLibDesc.DXILLibrary.BytecodeLength = pBlob->GetBufferSize();
			dxilLibDesc.NumExports = entryPointCount;
			dxilLibDesc.pExports = exportDesc.data();

			for (uint32_t i = 0; i < entryPointCount; i++)
			{
				exportName[i] = entryPoint[i];
				exportDesc[i].Name = exportName[i].c_str();
				exportDesc[i].Flags = D3D12_EXPORT_FLAG_NONE;
				exportDesc[i].ExportToRename = nullptr;
			}
		}
	};

	DxilLibrary() : DxilLibrary(nullptr, nullptr, 0) {}

	D3D12_DXIL_LIBRARY_DESC dxilLibDesc = {};
	D3D12_STATE_SUBOBJECT stateSubobject{};
	IDxcBlob* pShaderBlob;
	std::vector<D3D12_EXPORT_DESC> exportDesc;
	std::vector<std::wstring> exportName;
};

static const WCHAR* kRayGenShader = L"rayGen";
static const WCHAR* kMissShader = L"miss";
static const WCHAR* kClosestHitShader = L"chs";
static const WCHAR* kHitGroup = L"HitGroup";

DxilLibrary Demo::createDxilLibrary()
{
	// Compile the shader
	ComPtr<IDxcBlob> pDxilLib;
	if (mPathTracing)
	{
		pDxilLib = compileLibrary(L"Data/PathTracingShaders.hlsl", L"lib_6_3");
	}
	else
	{
		pDxilLib = compileLibrary(L"Data/Shaders.hlsl", L"lib_6_3");
	}
	const WCHAR* entryPoints[] = { kRayGenShader, kMissShader, kClosestHitShader };
	return DxilLibrary(pDxilLib.Get(), entryPoints, (sizeof(entryPoints)/sizeof(entryPoints[0])));
}

struct HitProgram
{
	HitProgram(LPCWSTR ahsExport, LPCWSTR chsExport, const std::wstring& name) : exportName(name)
	{
		desc = {};
		desc.AnyHitShaderImport = ahsExport;
		desc.ClosestHitShaderImport = chsExport;
		desc.HitGroupExport = exportName.c_str();

		subObject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		subObject.pDesc = &desc;
	}

	std::wstring exportName;
	D3D12_HIT_GROUP_DESC desc;
	D3D12_STATE_SUBOBJECT subObject;
};

struct ExportAssociation
{
	ExportAssociation(const WCHAR* exportNames[], uint32_t exportCount, const D3D12_STATE_SUBOBJECT* pSubobjectToAssociate)
	{
		association.NumExports = exportCount;
		association.pExports = exportNames;
		association.pSubobjectToAssociate = pSubobjectToAssociate;

		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
		subobject.pDesc = &association;
	}

	D3D12_STATE_SUBOBJECT subobject = {};
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION association = {};
};

struct LocalRootSignature
{
	LocalRootSignature(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc)
	{
		createRootSignature(pDevice, desc, &pRootSig);
		pRootSig->QueryInterface(IID_PPV_ARGS(&pInterface));
		subobject.pDesc = &pInterface;
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
	}
	ComPtr<ID3D12RootSignature> pRootSig;
	ID3D12RootSignature* pInterface = nullptr;
	D3D12_STATE_SUBOBJECT subobject = {};
};

struct GlobalRootSignature
{
	GlobalRootSignature(ID3D12Device* pDevice, const D3D12_ROOT_SIGNATURE_DESC& desc)
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[10]; // Perfomance TIP: Order from most frequent to least frequent.
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);  // 1 output texture
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);	// 1 vertex buffer
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);	// 1 index buffer
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // 1 meshInfo buffer
		ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4); // 1 lightInfo buffer
		ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5); // 1 colordata buffer
		ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);  // 1 buffer texture
		ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);  // 1 Albedo buffer texture
		ranges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);  // 1 Normal buffer texture
		ranges[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 6); // x texture array

		D3D12_STATIC_SAMPLER_DESC defaultSampler = {};
		defaultSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		defaultSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		defaultSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		defaultSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		defaultSampler.MaxAnisotropy = 0;
		defaultSampler.MipLODBias = 0.0f;
		defaultSampler.MinLOD = 0.0f;
		defaultSampler.MaxLOD = D3D12_FLOAT32_MAX;
		defaultSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		defaultSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		defaultSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		defaultSampler.ShaderRegister = 0;
		//defaultSampler.RegisterSpace = 0;

		CD3DX12_ROOT_PARAMETER rootParameters[GlobalRootSignatureParams::Count - 1];
		rootParameters[GlobalRootSignatureParams::OutputViewSlot].InitAsDescriptorTable(1, &ranges[0]);
		rootParameters[GlobalRootSignatureParams::AccelerationStructureSlot].InitAsShaderResourceView(0);
		rootParameters[GlobalRootSignatureParams::SceneConstantSlot].InitAsConstantBufferView(0);
		rootParameters[GlobalRootSignatureParams::VertexBuffer].InitAsDescriptorTable(1, &ranges[1]);
		rootParameters[GlobalRootSignatureParams::IndexBuffer].InitAsDescriptorTable(1, &ranges[2]);
		rootParameters[GlobalRootSignatureParams::MeshInfoBuffer].InitAsDescriptorTable(1, &ranges[3]);
		rootParameters[GlobalRootSignatureParams::LightBuffer].InitAsDescriptorTable(1, &ranges[4]);
		rootParameters[GlobalRootSignatureParams::ColorDataBuffer].InitAsDescriptorTable(1, &ranges[5]);
		rootParameters[GlobalRootSignatureParams::BufferTexture].InitAsDescriptorTable(1, &ranges[6]);
		rootParameters[GlobalRootSignatureParams::AlbedoBuffer].InitAsDescriptorTable(1, &ranges[7]);
		rootParameters[GlobalRootSignatureParams::NormalBuffer].InitAsDescriptorTable(1, &ranges[8]);
		rootParameters[GlobalRootSignatureParams::DiffuseTexture].InitAsDescriptorTable(1, &ranges[9]);
		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters, 1, &defaultSampler);
		createRootSignature(pDevice, globalRootSignatureDesc, &pRootSig);
		pRootSig->QueryInterface(IID_PPV_ARGS(&pInterface));
		subobject.pDesc = &pInterface;
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	}
	ComPtr<ID3D12RootSignature> pRootSig;
	ID3D12RootSignature* pInterface = nullptr;
	D3D12_STATE_SUBOBJECT subobject = {};
};

struct ShaderConfig
{
	ShaderConfig(uint32_t maxAttributeSizeInBytes, uint32_t maxPayloadSizeInBytes)
	{
		shaderConfig.MaxAttributeSizeInBytes = maxAttributeSizeInBytes;
		shaderConfig.MaxPayloadSizeInBytes = maxPayloadSizeInBytes;

		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
		subobject.pDesc = &shaderConfig;
	}

	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
	D3D12_STATE_SUBOBJECT subobject = {};
};

struct PipelineConfig
{
	PipelineConfig(uint32_t maxTraceRecursionDepth)
	{
		config.MaxTraceRecursionDepth = maxTraceRecursionDepth;

		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
		subobject.pDesc = &config;
	}

	D3D12_RAYTRACING_PIPELINE_CONFIG config = {};
	D3D12_STATE_SUBOBJECT subobject = {};
};

void Demo::createRtPipelineState()
{
	HRESULT hres = 0;
	std::array<D3D12_STATE_SUBOBJECT, 6> subobjects;
	uint32_t index = 0;

	// Create the DXIL library
	DxilLibrary dxilLib = createDxilLibrary();
	subobjects[index++] = dxilLib.stateSubobject;

	HitProgram hitProgram(nullptr, kClosestHitShader, kHitGroup);
	subobjects[index++] = hitProgram.subObject;

	// Bind the payload size to the programs
	ShaderConfig shaderConfig(sizeof(float) * 2, sizeof(RayPayload));
	subobjects[index] = shaderConfig.subobject;
	uint32_t shaderConfigIndex = index++;
	const WCHAR* shaderExports[] = { kMissShader, kClosestHitShader, kRayGenShader };
	ExportAssociation configAssociation(shaderExports, (sizeof(shaderExports) / sizeof(shaderExports[0])), &(subobjects[shaderConfigIndex]));
	subobjects[index++] = configAssociation.subobject;

	// Create the pipeline config
	PipelineConfig config(31);
	subobjects[index++] = config.subobject;

	// Create and set the global root signature
	GlobalRootSignature root(mpDevice.Get(), {});
	mpGlobalRootSig = root.pRootSig;
	subobjects[index++] = root.subobject;
	root.pRootSig->Release();

	// Create the state
	D3D12_STATE_OBJECT_DESC desc;
	desc.NumSubobjects = index;
	desc.pSubobjects = subobjects.data();
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

	hres = mpDXRDevice->CreateStateObject(&desc, IID_PPV_ARGS(&mpDXRPipelineState));
	if (FAILED(hres))
	{
		throw std::runtime_error("Failed to create DXR pipeline state.");
	}
}

void Demo::createConstantBuffers()
{
	HRESULT hres;
	// Create the constant buffer memory and map the CPU and GPU addresses
	const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

	// Allocate one constant buffer per frame, since it gets updated every frame.
	size_t cbSize = mDefaultSwapChainBuffers * sizeof(AlignedSceneConstantBuffer);
	const D3D12_RESOURCE_DESC constantBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

	hres = mpDevice->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&constantBufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&mPerFrameConstants));
	if (FAILED(hres))
	{
		throw std::runtime_error("Couldn't create committed resource for per frame constant data.");
	}

	// Map the constant buffer and cache its heap pointers.
	// We don't unmap this until the app closes. Keeping buffer mapped for the lifetime of the resource is okay.
	CD3DX12_RANGE readRange(0, 0);        // We do not intend to read from this resource on the CPU.
	hres = mPerFrameConstants->Map(0, nullptr, reinterpret_cast<void**>(&m_mappedConstantData));
	if (FAILED(hres))
	{
		throw std::runtime_error("Could map per frame constant data.");
	}

}

void Demo::UpdateConstantBuffers()
{
	UINT frameIndex = mpSwapchain->GetCurrentBackBufferIndex();
	
	mSceneCB[frameIndex].cameraPosition.x = mpCamera->m_CameraPos.x;
	mSceneCB[frameIndex].cameraPosition.y = mpCamera->m_CameraPos.y;
	mSceneCB[frameIndex].cameraPosition.z = mpCamera->m_CameraPos.z;
	mSceneCB[frameIndex].cameraPosition.w = 1;

	mSceneCB[frameIndex].cameraToWorld = mpCamera->GetViewProjInverseMatrix();
	mSceneCB[frameIndex].amountOfLights = mPointLightInfoArray.size();
	mSceneCB[frameIndex].sampleCount = mSampleCount;
	mSceneCB[frameIndex].frameCount = mFrameCount;
	//mSceneCB[frameIndex].samplesPerInstance = mSampleCapAmount;
	if (mMoveLight)
	{
		MoveLight(frameIndex);
	}

	// Copy the updated scene constant buffer to GPU.
	memcpy(&m_mappedConstantData[frameIndex].constants, &mSceneCB[frameIndex], sizeof(mSceneCB[frameIndex]));
	mCBVirtualAdress = mPerFrameConstants->GetGPUVirtualAddress() + frameIndex * sizeof(m_mappedConstantData[0]);
}

inline uint32_t Align(uint32_t size, uint32_t alignment)
{
	return (size + (alignment - 1)) & ~(alignment - 1);
}

void Demo::createShaderTable()
{
	/** The shader-table layout is as follows:
		Entry 0 - Ray-gen program
		Entry 1 - Miss program
		Entry 2 - Hit program
		All entries in the shader-table must have the same size, so we will choose it based on the largest required entry.
		The ray-gen program requires the largest entry - sizeof(program identifier) + 8 bytes for a descriptor-table.
		The entry size must be aligned up to D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT
	*/

	// Calculate the size and create the buffer
	uint32_t progIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	progIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	mShaderTableEntrySize = progIdSize;
	mShaderTableEntrySize += 8; // The ray-gen's descriptor table
	mShaderTableEntrySize = ((mShaderTableEntrySize + D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1) / D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT) * D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
	uint32_t shaderTableSize = (mShaderTableEntrySize * 2) + (mShaderTableEntrySize * mpMeshLoader->mpModel->mMeshes.size());

	// For simplicity, we create the shader-table on the upload heap. You can also create it on the default heap
	mpShaderTable = createBuffer(*mpDevice.GetAddressOf(), shaderTableSize, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, mUploadHeapProps);

	// Map the buffer
	uint8_t* pData;
	mpShaderTable->Map(0, nullptr, (void**)&pData);
	ComPtr<ID3D12StateObjectPropertiesPrototype> pRtsoProps;
	mpDXRPipelineState->QueryInterface(IID_PPV_ARGS(&pRtsoProps));

	// Entry 0 - ray-gen program ID and descriptor data
	memcpy(pData, pRtsoProps->GetShaderIdentifier(kRayGenShader), progIdSize);
	uint64_t heapStart = mpSrvUavHeap->GetGPUDescriptorHandleForHeapStart().ptr;
	*(uint64_t*)(pData + progIdSize) = heapStart;

	// Entry 1 - miss program
	memcpy(pData + mShaderTableEntrySize, pRtsoProps->GetShaderIdentifier(kMissShader), progIdSize);

	//Entry 2 - hit program
	for (size_t i = 0; i < mpMeshLoader->mpModel->mMeshes.size(); i++)
	{
		uint8_t* pHitEntry = pData + mShaderTableEntrySize * (i + 2); // +2 skips the ray-gen and miss entries
		memcpy(pHitEntry, pRtsoProps->GetShaderIdentifier(kHitGroup), progIdSize);
	}

	// Unmap
	mpShaderTable->Unmap(0, nullptr);
}

//Moves Sun Light
void Demo::MoveLight(UINT frameIndex)
{
	if (mLightPosValue >= 360.0f)
	{
		mLightPosValue = 0.0f;
	}
	mLightPosValue += mpCamera->mTime / 2.0f;
	float scale = 1.5f;
	float valueA = 2.0f;
	float valueB = 0.5f;
	float valueC = 0.1f;
	float valueX = scale * valueA * sin(mLightPosValue + ((valueB - 1) / valueB)*(XM_PIDIV2));
	float valueY = scale * valueC * sin(mLightPosValue);
	float valueZ = scale * valueB * sin(mLightPosValue);
	//mPointLightInfoArray[1].position = glm::float3(4000*valueX, 10000, 2000*valueZ);
	mSceneCB[0].lightPosition = glm::float3(4000 * valueX, 10000, 2000 * valueZ);
	mSceneCB[1].lightPosition = glm::float3(4000 * valueX, 10000, 2000 * valueZ);
	mpCamera->m_Moved = true;
}

//Math
void Demo::QuickRotation(std::vector<Vertex>& vertices)
{
	//Define Model Rotation
	model = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, 0.0f));
	model = glm::rotate(model, glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f));

	for (size_t i = 0; i < vertices.size(); i++)
	{
		vertices[i].pos = model * glm::vec4(vertices[i].pos, 1);
	}

}

void Demo::WaitForGPU()
{
	//Wait for GPU
	if (mpCmdQueue && mpFence && mFenceEvent)
	{
		// Schedule a Signal command in the GPU queue.
		UINT64 fenceValue = mFenceValue;
		if (SUCCEEDED(mpCmdQueue->Signal(mpFence.Get(), fenceValue)))
		{
			// Wait until the Signal has been processed.
			if (SUCCEEDED(mpFence->SetEventOnCompletion(fenceValue, mFenceEvent)))
			{
				WaitForSingleObject(mFenceEvent, INFINITE);
				//WaitForSingleObjectEx(mFenceEvent, INFINITE, FALSE);

				// Increment the fence value for the current frame.
				mFenceValue++;
			}
		}
	}
}

bool skipOnWindowInit = true;
void Demo::onResize(int heightWND, int widthWND)
{
	if (!skipOnWindowInit)
	{
		mWidth = widthWND;
		mHeight = heightWND;

		WaitForGPU();

		for (UINT n = 0; n < mDefaultSwapChainBuffers; n++)
		{
			mFrameObjects[n].pSwapChainBufferRTV.Reset();
		}
		DXGI_SWAP_CHAIN_DESC desc = {};
		mpSwapchain->GetDesc(&desc);
		mpSwapchain->ResizeBuffers(mDefaultSwapChainBuffers, widthWND, heightWND, desc.BufferDesc.Format, desc.Flags);

		// Create a RTV for each frame.
		for (UINT n = 0; n < mDefaultSwapChainBuffers; n++)
		{
			mpSwapchain->GetBuffer(n, IID_PPV_ARGS(&mFrameObjects[n].pSwapChainBufferRTV));
			mpDevice->CreateRenderTargetView(mFrameObjects[n].pSwapChainBufferRTV.Get(), nullptr, mFrameObjects[n].rtvHandle);
			mSceneCB[n].windowSize = glm::vec2(mWidth, mHeight);
		}

		//Denoiser
		input_buffer->destroy();
		normal_buffer->destroy();
		albedo_buffer->destroy();
		out_buffer->destroy();
		optix_context->destroy();

		delete denoised_pixels;
		denoised_pixels = nullptr;

		denoised_pixels = new unsigned char[widthWND * heightWND * 4];
		
		initOptiX(widthWND, heightWND);
		recreateSRV();

		//Clear Samples
		mpCamera->m_Moved = true;
	}
	else
	{
		skipOnWindowInit = false;
	}
}
