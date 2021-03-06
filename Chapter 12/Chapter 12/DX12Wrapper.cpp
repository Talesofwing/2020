#include "DX12Wrapper.h"
#include "Application.h"

#include <cassert>
#include <d3dx12.h>

#pragma comment(lib, "DirectXTex.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace Microsoft::WRL;
using namespace std;
using namespace DirectX;

namespace {

	string GetTexturePathFromModelAndTexPath (const std::string& modelPath, const char* texPath) {
		int pathIndex1 = modelPath.rfind ('/');
		int pathIndex2 = modelPath.rfind ('\\');
		auto pathIndex = max (pathIndex1, pathIndex2);
		auto folderPath = modelPath.substr (0, pathIndex + 1);
		return folderPath + texPath;
	}

	string GetExtension (const std::string& path) {
		int idx = path.rfind ('.');
		return path.substr (idx + 1, path.length () - idx - 1);
	}

	wstring GetExtension (const std::wstring& path) {
		int idx = path.rfind (L'.');
		return path.substr (idx + 1, path.length () - idx - 1);
	}

	pair<string, string>
		SplitFileName (const std::string& path, const char splitter = '*') {
		int idx = path.find (splitter);
		pair<string, string> ret;
		ret.first = path.substr (0, idx);
		ret.second = path.substr (idx + 1, path.length () - idx - 1);
		return ret;
	}

	std::wstring GetWideStringFromString (const std::string& str) {
		auto num1 = MultiByteToWideChar (CP_ACP,
										 MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
										 str.c_str (), -1, nullptr, 0);

		std::wstring wstr;
		wstr.resize (num1);

		auto num2 = MultiByteToWideChar (CP_ACP,
										 MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
										 str.c_str (), -1, &wstr[0], num1);

		assert (num1 == num2);
		return wstr;
	}

	void EnableDebugLayer () {
		ComPtr<ID3D12Debug> debugLayer = nullptr;
		auto result = D3D12GetDebugInterface (IID_PPV_ARGS (&debugLayer));
		debugLayer->EnableDebugLayer ();
	}

}

DX12Wrapper::DX12Wrapper (HWND hwnd) : _hwnd(hwnd),
			_eye (0, 15, -25),
		    _target (0, 10, 0), 
			_up(0, 1, 0) {}

HRESULT DX12Wrapper::CreateDepthStencilView () {
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	auto hr = _swapchain->GetDesc1 (&desc);

	D3D12_RESOURCE_DESC resdesc = {};
	resdesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resdesc.DepthOrArraySize = 1;
	resdesc.Width = desc.Width;
	resdesc.Height = desc.Height;
	resdesc.Format = DXGI_FORMAT_D32_FLOAT;
	resdesc.SampleDesc.Count = 1;
	resdesc.SampleDesc.Quality = 0;
	resdesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	resdesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resdesc.MipLevels = 1;
	resdesc.Alignment = 0;

	auto depthHeapProp = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);

	CD3DX12_CLEAR_VALUE depthClearValue (DXGI_FORMAT_D32_FLOAT, 1.0f, 0);

	hr = _dev->CreateCommittedResource (
		&depthHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resdesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS (_depthBuffer.ReleaseAndGetAddressOf ())
	);

	if (FAILED (hr)) {
		return hr;
	}

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	hr = _dev->CreateDescriptorHeap (&dsvHeapDesc, IID_PPV_ARGS (_dsvHeap.ReleaseAndGetAddressOf ()));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	_dev->CreateDepthStencilView (_depthBuffer.Get (), &dsvDesc, _dsvHeap->GetCPUDescriptorHandleForHeapStart ());

	return S_OK;
}

DX12Wrapper::~DX12Wrapper () {}

ComPtr<ID3D12Resource> DX12Wrapper::GetTextureByPath (const char* texpath) {
	auto it = _textureTable.find (texpath);
	if (it != _textureTable.end ()) {
		return _textureTable[texpath];
	} else {
		return ComPtr<ID3D12Resource> (CreateTextureFromFile (texpath));
	}
}

void DX12Wrapper::CreateTextureLoaderTable () {
	_loadLambdaTable["sph"] = _loadLambdaTable["spa"] = _loadLambdaTable["bmp"] = _loadLambdaTable["png"] = _loadLambdaTable["jpg"] = [](const wstring& path, TexMetadata* meta, ScratchImage& img)->HRESULT {
		return LoadFromWICFile (path.c_str (), WIC_FLAGS_NONE, meta, img);
	};

	_loadLambdaTable["tga"] = [](const wstring& path, TexMetadata* meta, ScratchImage& img)->HRESULT {
		return LoadFromTGAFile (path.c_str (), meta, img);
	};

	_loadLambdaTable["dds"] = [](const wstring& path, TexMetadata* meta, ScratchImage& img)->HRESULT {
		return LoadFromDDSFile (path.c_str (), DDS_FLAGS_NONE, meta, img);
	};
}

ID3D12Resource* DX12Wrapper::CreateTextureFromFile (const char* texpath) {
	string texPath = texpath;
	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	auto wtexpath = GetWideStringFromString (texPath);
	auto ext = GetExtension (texPath);
	auto hr = _loadLambdaTable[ext] (wtexpath,
										 &metadata,
										 scratchImg);

	if (FAILED (hr)) {
		return nullptr;
	}

	auto img = scratchImg.GetImage (0, 0, 0);

	auto texHeapProp = CD3DX12_HEAP_PROPERTIES (D3D12_CPU_PAGE_PROPERTY_WRITE_BACK, D3D12_MEMORY_POOL_L0);
	auto resDesc = CD3DX12_RESOURCE_DESC::Tex2D (metadata.format, metadata.width, metadata.height, metadata.arraySize, metadata.mipLevels);

	ID3D12Resource* texbuffer = nullptr;
	hr = _dev->CreateCommittedResource (
		&texHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS (&texbuffer)
	);

	if (FAILED (hr)) {
		return nullptr;
	}

	hr = texbuffer->WriteToSubresource (0,
										nullptr,
										img->pixels,
										img->rowPitch,
										img->slicePitch
	);

	if (FAILED (hr)) {
		return nullptr;
	}

	return texbuffer;
}

HRESULT DX12Wrapper::InitializeDXGIDevice () {
	UINT flagsDXGI = 0;
	flagsDXGI |= DXGI_CREATE_FACTORY_DEBUG;
	auto hr = CreateDXGIFactory2 (flagsDXGI, IID_PPV_ARGS (_dxgiFactory.ReleaseAndGetAddressOf ()));

	D3D_FEATURE_LEVEL levels[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

	if (FAILED (hr)) {
		return hr;
	}

	std::vector <IDXGIAdapter*> adapters;
	IDXGIAdapter* tmpAdapter = nullptr;
	for (int i = 0; _dxgiFactory->EnumAdapters (i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
		adapters.push_back (tmpAdapter);
	}

	for (auto adpt : adapters) {
		DXGI_ADAPTER_DESC adesc = {};
		adpt->GetDesc (&adesc);
		std::wstring strDesc = adesc.Description;
		if (strDesc.find (L"NVIDIA") != std::string::npos) {
			tmpAdapter = adpt;
			break;
		}
	}

	hr = S_FALSE;

	D3D_FEATURE_LEVEL featureLevel;
	for (auto l : levels) {
		if (SUCCEEDED (D3D12CreateDevice (tmpAdapter, l, IID_PPV_ARGS (_dev.ReleaseAndGetAddressOf ())))) {
			featureLevel = l;
			hr = S_OK;
			break;
		}
	}

	return hr;
}

HRESULT DX12Wrapper::CreateSwapChain (const HWND& hwnd) {
	RECT rc = {};
	::GetWindowRect (hwnd, &rc);

	Application& app = Application::Instance ();
	auto wsize = app.GetWindowSize ();

	DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
	swapchainDesc.Width = wsize.width;
	swapchainDesc.Height = wsize.height;
	swapchainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchainDesc.Stereo = false;
	swapchainDesc.SampleDesc.Count = 1;
	swapchainDesc.SampleDesc.Quality = 0;
	swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchainDesc.BufferCount = 2;
	swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	auto hr = _dxgiFactory->CreateSwapChainForHwnd (_cmdQueue.Get (),
														hwnd,
														&swapchainDesc,
														nullptr,
														nullptr,
														(IDXGISwapChain1**)_swapchain.ReleaseAndGetAddressOf ());

	assert (SUCCEEDED (hr));

	return hr;
}

HRESULT DX12Wrapper::InitializeCommand () {
	auto hr = _dev->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS (_cmdAllocator.ReleaseAndGetAddressOf ()));
	if (FAILED (hr)) {
		assert (0);
		return hr;
	}

	hr = _dev->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_DIRECT, _cmdAllocator.Get (), nullptr, IID_PPV_ARGS (_cmdList.ReleaseAndGetAddressOf ()));
	if (FAILED (hr)) {
		assert (0);
		return hr;
	}

	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmdQueueDesc.NodeMask = 0;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	hr = _dev->CreateCommandQueue (&cmdQueueDesc, IID_PPV_ARGS (_cmdQueue.ReleaseAndGetAddressOf ()));

	assert (SUCCEEDED (hr));

	return hr;
}

HRESULT DX12Wrapper::CreateSceneView () {
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	auto hr = _swapchain->GetDesc1 (&desc);

	hr = _dev->CreateCommittedResource (
		&CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer ((sizeof (SceneData) + 0xff) & ~0xff),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS (_sceneConstBuff.ReleaseAndGetAddressOf ())
	);

	if (FAILED (hr)) {
		assert (SUCCEEDED (hr));
		return hr;
	}

	_mappedSceneData = nullptr;
	hr = _sceneConstBuff->Map (0, nullptr, (void**)&_mappedSceneData);

	XMFLOAT3 eye (5, 10, -30);
	XMFLOAT3 target (0, 10, 0);
	XMFLOAT3 up (0, 1, 0);
	_mappedSceneData->view = XMMatrixLookAtLH (XMLoadFloat3 (&eye), XMLoadFloat3 (&target), XMLoadFloat3 (&up));
	_mappedSceneData->proj = XMMatrixPerspectiveFovLH (XM_PIDIV4,
													   static_cast<float>(desc.Width) / static_cast<float>(desc.Height),
													   0.1f,
													   1000.0f
	);
	_mappedSceneData->eye = eye;

	D3D12_DESCRIPTOR_HEAP_DESC descHeapDesc = {};
	descHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	descHeapDesc.NodeMask = 0;
	descHeapDesc.NumDescriptors = 1;
	descHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	hr = _dev->CreateDescriptorHeap (&descHeapDesc, IID_PPV_ARGS (_sceneDescHeap.ReleaseAndGetAddressOf ()));

	auto heapHandle = _sceneDescHeap->GetCPUDescriptorHandleForHeapStart ();

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = _sceneConstBuff->GetGPUVirtualAddress ();
	cbvDesc.SizeInBytes = _sceneConstBuff->GetDesc ().Width;
	_dev->CreateConstantBufferView (&cbvDesc, heapHandle);

	return hr;
}

HRESULT DX12Wrapper::CreateFinalRenderTargets () {
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	auto hr = _swapchain->GetDesc1 (&desc);

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heapDesc.NodeMask = 0;
	heapDesc.NumDescriptors = 2;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	hr = _dev->CreateDescriptorHeap (&heapDesc, IID_PPV_ARGS (_rtvHeap.ReleaseAndGetAddressOf ()));
	if (FAILED (hr)) {
		SUCCEEDED (hr);
		return hr;
	}

	DXGI_SWAP_CHAIN_DESC swcDesc = {};
	hr = _swapchain->GetDesc (&swcDesc);
	_backBuffers.resize (swcDesc.BufferCount);

	D3D12_CPU_DESCRIPTOR_HANDLE handle = _rtvHeap->GetCPUDescriptorHandleForHeapStart ();

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	for (int i = 0; i < swcDesc.BufferCount; ++i) {
		hr = _swapchain->GetBuffer (i, IID_PPV_ARGS (&_backBuffers[i]));
		assert (SUCCEEDED (hr));
		rtvDesc.Format = _backBuffers[i]->GetDesc ().Format;
		_dev->CreateRenderTargetView (_backBuffers[i], &rtvDesc, handle);
		handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	//_viewport.reset (new CD3DX12_VIEWPORT (_backBuffers[0]));
	//_scissorRect.reset (new CD3DX12_RECT (0, 0, desc.Width, desc.Height));

	return hr;
}

ComPtr< ID3D12Device> DX12Wrapper::Device () {
	return _dev;
}

ComPtr < ID3D12GraphicsCommandList> DX12Wrapper::CommandList () {
	return _cmdList;
}

//void DX12Wrapper::Update () {}
//
//void DX12Wrapper::BeginDraw () {
//	auto bbIdx = _swapchain->GetCurrentBackBufferIndex ();
//
//	_cmdList->ResourceBarrier (1, &CD3DX12_RESOURCE_BARRIER::Transition (_backBuffers[bbIdx],
//						D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
//
//
//	auto rtvH = _rtvHeap->GetCPUDescriptorHandleForHeapStart ();
//	rtvH.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
//
//	auto dsvH = _dsvHeap->GetCPUDescriptorHandleForHeapStart ();
//	_cmdList->OMSetRenderTargets (1, &rtvH, false, &dsvH);
//	_cmdList->ClearDepthStencilView (dsvH, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
//
//	float clearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
//	_cmdList->ClearRenderTargetView (rtvH, clearColor, 0, nullptr);
//
//	_cmdList->RSSetViewports (1, _viewport.get ());
//	_cmdList->RSSetScissorRects (1, _scissorRect.get ());
//}

//void DX12Wrapper::SetScene () {
//	ID3D12DescriptorHeap* sceneheaps[] = {_sceneDescHeap.Get ()};
//	_cmdList->SetDescriptorHeaps (1, sceneheaps);
//	_cmdList->SetGraphicsRootDescriptorTable (0, _sceneDescHeap->GetGPUDescriptorHandleForHeapStart ());
//}

//void DX12Wrapper::EndDraw () {
//	auto bbIdx = _swapchain->GetCurrentBackBufferIndex ();
//	_cmdList->ResourceBarrier (1, &CD3DX12_RESOURCE_BARRIER::Transition (_backBuffers[bbIdx],
//						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
//
//	_cmdList->Close ();
//
//	ID3D12CommandList* cmdlists[] = {_cmdList.Get ()};
//	_cmdQueue->ExecuteCommandLists (1, cmdlists);
//	_cmdQueue->Signal (_fence.Get (), ++_fenceVal);
//
//	if (_fence->GetCompletedValue () < _fenceVal) {
//		auto event = CreateEvent (nullptr, false, false, nullptr);
//		_fence->SetEventOnCompletion (_fenceVal, event);
//		WaitForSingleObject (event, INFINITE);
//		CloseHandle (event);
//	}
//
//	_cmdAllocator->Reset ();
//	_cmdList->Reset (_cmdAllocator.Get (), nullptr);
//}

ComPtr < IDXGISwapChain4> DX12Wrapper::Swapchain () {
	return _swapchain;
}

#pragma region Chapter 12

bool DX12Wrapper::Init () {
#ifdef _DEBUG
	EnableDebugLayer ();
#endif

	if (FAILED (InitializeDXGIDevice ())) {
		assert (0);
		return false;
	}

	if (FAILED (InitializeCommand ())) {
		assert (0);
		return false;
	}

	if (FAILED (CreateSwapChain (_hwnd))) {
		assert (0);
		return false;
	}

	if (FAILED (CreateFinalRenderTargets ())) {
		assert (0);
		return false;
	}

	if (FAILED (CreateSceneView ())) {
		assert (0);
		return false;
	}

	CreateTextureLoaderTable ();

	if (FAILED (CreateDepthStencilView ())) {
		assert (0);
		return false;
	}

	if (FAILED (_dev->CreateFence (_fenceVal, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (_fence.ReleaseAndGetAddressOf ())))) {
		assert (0);
		return false;
	}

	//
	// Chapter 12
	//	
	
	if (!CreateBokehParamResource ()) {
		assert (0);
		return false;
	}

	if (!CreateEffectBufferAndView ()) {
		assert (0);
		return false;
	}

	if (!CreatePeraResourcesAndView ()) {
		assert (0);
		return false;
	}

	if (!CreatePeraVertex ()) {
		assert (0);
		return false;
	}

	if (!CreatePeraPipeline ()) {
		assert (0);
		return false;
	}

	return true;
}

bool DX12Wrapper::CreateBokehParamResource () {
	auto weights = D3D::GetGaussianWeights (8, 5.0f);

	auto result = _dev->CreateCommittedResource (
		&CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer (D3D::AligmentedValue (sizeof (weights[0] * weights.size ()), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS (_bokehParamResource.ReleaseAndGetAddressOf ())
	);

	if (FAILED(result)) {
		assert (0);
		return false;
	}

	float* mappedWeight = nullptr;
	result = _bokehParamResource->Map (0, nullptr, (void**)&mappedWeight);
	
	if (FAILED (result)) {
		assert (0);
		return false;
	}

	copy (weights.begin (), weights.end (), mappedWeight);
	_bokehParamResource->Unmap (0, nullptr);

	return true;
}

bool DX12Wrapper::CreatePeraVertex () {
	struct PeraVertex {
		XMFLOAT3 pos;
		XMFLOAT2 uv;
	};

	PeraVertex pv[4] = {
		{{-1, -1, 0.1}, {0, 1}},
		{{-1,  1, 0.1}, {0, 0}},
		{{ 1, -1, 0.1}, {1, 1}},
		{{ 1,  1, 0.1}, {1, 0}}
	};
	
	auto result = _dev->CreateCommittedResource (
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer (sizeof (pv)),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS (_peraVB.ReleaseAndGetAddressOf ())
	);

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	PeraVertex* mappedPera = nullptr;
	_peraVB->Map (0, nullptr, (void**)&mappedPera);
	copy (begin (pv), end (pv), mappedPera);
	_peraVB->Unmap (0, nullptr);

	_peraVBV.BufferLocation = _peraVB->GetGPUVirtualAddress ();
	_peraVBV.SizeInBytes = sizeof (pv);
	_peraVBV.StrideInBytes = sizeof (PeraVertex);

	return true;
}

bool DX12Wrapper::CreatePeraPipeline () {
	ComPtr<ID3DBlob> vs;
	ComPtr<ID3DBlob> ps;
	ComPtr<ID3DBlob> errBlob;

	auto result = D3DCompileFromFile (
		L"PeraVertex.hlsl", nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"main", "vs_5_0", 0, 0,
		vs.ReleaseAndGetAddressOf (),
		errBlob.ReleaseAndGetAddressOf ()
	);

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	result = D3DCompileFromFile (
		L"PeraPixel.hlsl", nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"main", "ps_5_0", 0, 0,
		ps.ReleaseAndGetAddressOf (),
		errBlob.ReleaseAndGetAddressOf ()
	);

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	D3D12_INPUT_ELEMENT_DESC layout[2] = {
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc = {};
	gpsDesc.InputLayout.NumElements = _countof (layout);
	gpsDesc.InputLayout.pInputElementDescs = layout;
	gpsDesc.VS = CD3DX12_SHADER_BYTECODE (vs.Get ());
	gpsDesc.PS = CD3DX12_SHADER_BYTECODE (ps.Get ());
	gpsDesc.BlendState = CD3DX12_BLEND_DESC (D3D12_DEFAULT);
	gpsDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpsDesc.NumRenderTargets = 1;
	gpsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpsDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gpsDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	gpsDesc.SampleDesc.Count = 1;
	gpsDesc.SampleDesc.Quality = 0;
	gpsDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	D3D12_DESCRIPTOR_RANGE range[3] = {};
	range[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	range[0].BaseShaderRegister = 0;
	range[0].NumDescriptors = 1;

	range[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range[1].BaseShaderRegister = 0;
	range[1].NumDescriptors = 1;

	range[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range[2].BaseShaderRegister = 1;
	range[2].NumDescriptors = 1;

	D3D12_ROOT_PARAMETER rp[3] = {};
	rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rp[0].DescriptorTable.pDescriptorRanges = &range[0];
	rp[0].DescriptorTable.NumDescriptorRanges = 1;

	rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rp[1].DescriptorTable.pDescriptorRanges = &range[1];
	rp[1].DescriptorTable.NumDescriptorRanges = 1;

	rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rp[2].DescriptorTable.pDescriptorRanges = &range[2];
	rp[2].DescriptorTable.NumDescriptorRanges = 1;

	D3D12_STATIC_SAMPLER_DESC sampler = CD3DX12_STATIC_SAMPLER_DESC (0);

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 3;
	rsDesc.pParameters = rp;
	rsDesc.NumStaticSamplers = 1;
	rsDesc.pStaticSamplers = &sampler;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> rsBlob;
	result = D3D12SerializeRootSignature (&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, rsBlob.ReleaseAndGetAddressOf (), errBlob.ReleaseAndGetAddressOf ());

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	result = _dev->CreateRootSignature (0, rsBlob->GetBufferPointer (), rsBlob->GetBufferSize (), IID_PPV_ARGS (_peraRS.ReleaseAndGetAddressOf ()));

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	gpsDesc.pRootSignature = _peraRS.Get ();
	result = _dev->CreateGraphicsPipelineState (&gpsDesc, IID_PPV_ARGS (_peraPipeline.ReleaseAndGetAddressOf ()));

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	result = D3DCompileFromFile (
		L"PeraPixel.hlsl", nullptr,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		"VerticalBokehPS", "ps_5_0", 0, 0,
		ps.ReleaseAndGetAddressOf (),
		errBlob.ReleaseAndGetAddressOf ()
	);
	
	if (FAILED (result)) {
		assert (0);
		return false;
	}

	gpsDesc.PS = CD3DX12_SHADER_BYTECODE (ps.Get ());

	result = _dev->CreateGraphicsPipelineState (
		&gpsDesc,
		IID_PPV_ARGS (_peraPipeline2.ReleaseAndGetAddressOf ())
	);

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	return true;
}

bool DX12Wrapper::CreatePeraResourcesAndView () {
	//
	// Create Buffer
	//

	auto heapDesc = _rtvHeap->GetDesc ();
	
	auto& bbuff = _backBuffers [0];
	auto resDesc = bbuff->GetDesc ();

	D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);

	float clsClr[4] = {0.5f, 0.5f, 0.5f, 1.0f};
	D3D12_CLEAR_VALUE clearValue = CD3DX12_CLEAR_VALUE (DXGI_FORMAT_R8G8B8A8_UNORM, clsClr);

	auto result = _dev->CreateCommittedResource (
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS (_peraResource.ReleaseAndGetAddressOf ())
	);

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	result = _dev->CreateCommittedResource (
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS (_peraResource2.ReleaseAndGetAddressOf ())
	);

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	//
	// Create rtv & srv heap & view
	//
		
	heapDesc.NumDescriptors = 2;	// 2 rtv

	result = _dev->CreateDescriptorHeap (&heapDesc, IID_PPV_ARGS (_peraRTVHeap.ReleaseAndGetAddressOf ()));

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	auto handle = _peraRTVHeap->GetCPUDescriptorHandleForHeapStart ();

	_dev->CreateRenderTargetView (_peraResource.Get (), &rtvDesc, handle);

	handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	//handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	printf ("%i\n", _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
	printf ("%i\n", _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	_dev->CreateRenderTargetView (
		_peraResource2.Get (),
		&rtvDesc, 
		handle
	);

	heapDesc.NumDescriptors = 3;	// 2 srv, 1 cbv
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NodeMask = 0;

	result = _dev->CreateDescriptorHeap (&heapDesc, IID_PPV_ARGS (_peraRegisterHeap.ReleaseAndGetAddressOf ()));

	if (FAILED (result)) {
		assert (0);
		return false;
	}
	
	// CBV
	handle = _peraRegisterHeap->GetCPUDescriptorHandleForHeapStart ();

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = _bokehParamResource->GetGPUVirtualAddress ();
	cbvDesc.SizeInBytes = _bokehParamResource->GetDesc ().Width;
	_dev->CreateConstantBufferView (&cbvDesc, handle);

	// SRV
	handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = rtvDesc.Format;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	_dev->CreateShaderResourceView (_peraResource.Get (), &srvDesc, handle);

	handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_dev->CreateShaderResourceView (_peraResource2.Get (), &srvDesc, handle);

	return true;
}

bool DX12Wrapper::PreDrawToPera1 () {
	_cmdList->ResourceBarrier (1, &CD3DX12_RESOURCE_BARRIER::Transition (_peraResource.Get (),
																		 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
																		 D3D12_RESOURCE_STATE_RENDER_TARGET));

	auto rtvHeapPointer = _peraRTVHeap->GetCPUDescriptorHandleForHeapStart ();
	auto dsvHeapPointer = _dsvHeap->GetCPUDescriptorHandleForHeapStart ();

	_cmdList->OMSetRenderTargets (1, &rtvHeapPointer, false, &dsvHeapPointer);

	float clsClr[4] = {0.2f, 0.5f, 0.5f, 1.0f};
	_cmdList->ClearRenderTargetView (rtvHeapPointer, clsClr, 0, nullptr);
	_cmdList->ClearDepthStencilView (dsvHeapPointer, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	auto wsize = Application::Instance ().GetWindowSize ();

	D3D12_VIEWPORT vp = CD3DX12_VIEWPORT (0.0f, 0.0f, wsize.width, wsize.height);
	_cmdList->RSSetViewports (1, &vp);

	CD3DX12_RECT rc (0, 0, wsize.width, wsize.height);
	_cmdList->RSSetScissorRects (1, &rc);

	return true;
}

void DX12Wrapper::DrawToPera1 (shared_ptr<PMDRenderer> renderer) {
	ID3D12DescriptorHeap* sceneheaps[] = {_sceneDescHeap.Get ()};
	_cmdList->SetDescriptorHeaps (1, sceneheaps);
	_cmdList->SetGraphicsRootDescriptorTable (0, _sceneDescHeap->GetGPUDescriptorHandleForHeapStart ());
}

void DX12Wrapper::PostDrawToPera1 () {
	_cmdList->ResourceBarrier (1, &CD3DX12_RESOURCE_BARRIER::Transition (_peraResource.Get (),
																		 D3D12_RESOURCE_STATE_RENDER_TARGET,
																		 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
}

void DX12Wrapper::DrawHorizontalBokeh () {
	Barrier (_peraResource2.Get (), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	auto rtvHeapPointer = _peraRTVHeap->GetCPUDescriptorHandleForHeapStart ();

	rtvHeapPointer.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	_cmdList->OMSetRenderTargets (1, &rtvHeapPointer, false, nullptr);

	float clsClr[4] = {0.5f, 0.5f, 0.5f, 1.0f};
	_cmdList->ClearRenderTargetView (rtvHeapPointer, clsClr, 0, nullptr);

	Application& app = Application::Instance ();
	auto wsize = app.GetWindowSize ();

	D3D12_VIEWPORT vp = CD3DX12_VIEWPORT (0.0f, 0.0f, wsize.width, wsize.height);
	_cmdList->RSSetViewports (1, &vp);

	CD3DX12_RECT rc (0, 0, wsize.width, wsize.height);
	_cmdList->RSSetScissorRects (1, &rc);

	_cmdList->SetGraphicsRootSignature (_peraRS.Get ());
	_cmdList->SetDescriptorHeaps (1, _peraRegisterHeap.GetAddressOf ());

	auto handle = _peraRegisterHeap->GetGPUDescriptorHandleForHeapStart ();
	_cmdList->SetGraphicsRootDescriptorTable (0, handle);

	handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_cmdList->SetGraphicsRootDescriptorTable (1, handle);

	_cmdList->SetPipelineState (_peraPipeline.Get ());
	_cmdList->IASetPrimitiveTopology (D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	_cmdList->IASetVertexBuffers (0, 1, &_peraVBV);
	_cmdList->DrawInstanced (4, 1, 0, 0);

	Barrier (_peraResource2.Get (), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

bool DX12Wrapper::Clear () {
	auto bbIdx = _swapchain->GetCurrentBackBufferIndex ();

	Barrier (_backBuffers[bbIdx],
			 D3D12_RESOURCE_STATE_PRESENT,
			 D3D12_RESOURCE_STATE_RENDER_TARGET);

	auto rtvHeapPointer = _rtvHeap->GetCPUDescriptorHandleForHeapStart ();
	rtvHeapPointer.ptr += bbIdx * _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	_cmdList->OMSetRenderTargets (1, &rtvHeapPointer, false, nullptr);

	float clsClr[4] = {0.2f, 0.5f, 0.5f, 1.0f};
	_cmdList->ClearRenderTargetView (rtvHeapPointer, clsClr, 0, nullptr);

	return true;
}

// Draw to back buffer
void DX12Wrapper::Draw (shared_ptr<PMDRenderer> renderer) {
	auto wsize = Application::Instance ().GetWindowSize ();

	D3D12_VIEWPORT vp = CD3DX12_VIEWPORT (0.0f, 0.0f, wsize.width, wsize.height);
	_cmdList->RSSetViewports (1, &vp);

	CD3DX12_RECT rc (0, 0, wsize.width, wsize.height);
	_cmdList->RSSetScissorRects (1, &rc);

	// First, set root signature
	_cmdList->SetGraphicsRootSignature (_peraRS.Get ());

	_cmdList->SetDescriptorHeaps (1, _peraRegisterHeap.GetAddressOf ());

	auto handle = _peraRegisterHeap->GetGPUDescriptorHandleForHeapStart ();
	_cmdList->SetGraphicsRootDescriptorTable (0, handle);
	// use rtv_2 (srv_2)
	handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	handle.ptr += _dev->GetDescriptorHandleIncrementSize (D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	_cmdList->SetGraphicsRootDescriptorTable (1, handle);
	_cmdList->SetPipelineState (_peraPipeline2.Get ());
	_cmdList->IASetPrimitiveTopology (D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	_cmdList->IASetVertexBuffers (0, 1, &_peraVBV);

	_cmdList->SetDescriptorHeaps (1, _effectSRVHeap.GetAddressOf ());
	_cmdList->SetGraphicsRootDescriptorTable (2, _effectSRVHeap->GetGPUDescriptorHandleForHeapStart ());

	_cmdList->DrawInstanced (4, 1, 0, 0);
}

void DX12Wrapper::Flip () {
	auto bbIdx = _swapchain->GetCurrentBackBufferIndex ();

	Barrier (_backBuffers[bbIdx],
			 D3D12_RESOURCE_STATE_RENDER_TARGET,
			 D3D12_RESOURCE_STATE_PRESENT);

	_cmdList->Close ();
	ID3D12CommandList* cmds[] = {_cmdList.Get ()};
	_cmdQueue->ExecuteCommandLists (1, cmds);

	WaitForCommandQueue ();

	_cmdAllocator->Reset ();
	_cmdList->Reset (_cmdAllocator.Get (), nullptr);

	auto result = _swapchain->Present (0, 0);
	assert (SUCCEEDED (result));
}

void DX12Wrapper::Barrier (ID3D12Resource* p,
						   D3D12_RESOURCE_STATES before,
						   D3D12_RESOURCE_STATES after) {
	_cmdList->ResourceBarrier (1, &CD3DX12_RESOURCE_BARRIER::Transition (p, before, after, 0));
}

void DX12Wrapper::WaitForCommandQueue () {
	_cmdQueue->Signal (_fence.Get (), ++_fenceVal);
	if (_fence->GetCompletedValue () < _fenceVal) {
		auto event = CreateEvent (nullptr, false, false, nullptr);
		_fence->SetEventOnCompletion (_fenceVal, event);
		WaitForSingleObject (event, INFINITE);
		CloseHandle (event);
	}
}

bool DX12Wrapper::CreateEffectBufferAndView () {
	if (!LoadPictureFromFile ("normal/normalmap.jpg", _effectTexBuffer)) {
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	heapDesc.NumDescriptors = 1;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;	
	
	auto result = _dev->CreateDescriptorHeap (
		&heapDesc,
		IID_PPV_ARGS (&_effectSRVHeap)
	);

	if (FAILED (result)) {
		assert (0);
		return false;
	}

	auto desc = _effectTexBuffer->GetDesc ();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = desc.Format;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	_dev->CreateShaderResourceView (
		_effectTexBuffer.Get (),
		&srvDesc,
		_effectSRVHeap->GetCPUDescriptorHandleForHeapStart ()
	);
}

bool DX12Wrapper::LoadPictureFromFile (string filepath, ComPtr<ID3D12Resource>& buff) {
	auto it = _textureTable.find (filepath);
	if (it != _textureTable.end ()) {
		buff = _textureTable[filepath];
		return true;
	}

	TexMetadata metadata = {};
	ScratchImage scratchImg = {};
	HRESULT result = S_OK;

	bool isDXT = false;
	auto ext = GetExtension (filepath);
	wstring wPath = GetWideStringFromString (filepath);
	if (ext == "tga") {
		result = LoadFromTGAFile (wPath.c_str (),
								  &metadata,
								  scratchImg);
	} else if (ext == "dds") {
		result = LoadFromDDSFile (wPath.c_str (), DDS_FLAGS_NONE,
								  &metadata,
								  scratchImg);
		isDXT = true;
	} else {
		result = LoadFromWICFile (wPath.c_str (),
								  WIC_FLAGS_NONE,
								  &metadata,
								  scratchImg);
	}
	assert (SUCCEEDED (result));
	if (FAILED (result)) {
		return false;
	}
	auto img = scratchImg.GetImage (0, 0, 0);

	bool isDescrete = true;

	if (!CreateTextureFromImageData (img, buff, isDescrete)) {
		return false;
	}

	if (!isDescrete) {
		result = buff->WriteToSubresource (0,
										   nullptr,
										   img->pixels,
										   img->rowPitch,
										   img->slicePitch);
	} else {
		D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_UPLOAD);

		D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer (
			D3D::AligmentedValue (img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * img->height);
		Microsoft::WRL::ComPtr<ID3D12Resource> internalBuffer = nullptr;
		result = _dev->CreateCommittedResource (&heapProp,
												D3D12_HEAP_FLAG_NONE,
												&resDesc,
												D3D12_RESOURCE_STATE_GENERIC_READ,
												nullptr,
												IID_PPV_ARGS (internalBuffer.ReleaseAndGetAddressOf ()));
		assert (SUCCEEDED (result));
		if (FAILED (result)) {
			return false;
		}
		uint8_t* mappedInternal = nullptr;
		internalBuffer->Map (0, nullptr, (void**)&mappedInternal);
		auto address = img->pixels;
		uint32_t height = isDXT ? img->height / 4 : img->height;
		for (int i = 0; i < height; ++i) {
			copy_n (address, img->rowPitch, mappedInternal);
			address += img->rowPitch;
			mappedInternal += D3D::AligmentedValue (img->rowPitch,
											   D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		}
		internalBuffer->Unmap (0, nullptr);

		D3D12_TEXTURE_COPY_LOCATION src = {}, dst = {};
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.pResource = internalBuffer.Get ();
		src.PlacedFootprint.Offset = 0;
		src.PlacedFootprint.Footprint.Width = img->width;
		src.PlacedFootprint.Footprint.Height = img->height;
		src.PlacedFootprint.Footprint.RowPitch =
			D3D::AligmentedValue (img->rowPitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		src.PlacedFootprint.Footprint.Depth = metadata.depth;
		src.PlacedFootprint.Footprint.Format = img->format;

		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		dst.pResource = buff.Get ();

		_cmdList->CopyTextureRegion (&dst, 0, 0, 0, &src, nullptr);

		Barrier (buff.Get (), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		_cmdList->Close ();
		ID3D12CommandList* cmds[] = {_cmdList.Get ()};
		_cmdQueue->ExecuteCommandLists (1, cmds);
		WaitForCommandQueue ();
		_cmdAllocator->Reset ();
		_cmdList->Reset (_cmdAllocator.Get (), nullptr);
	}

	assert (SUCCEEDED (result));
	if (FAILED (result)) {
		return false;
	}

	_textureTable[filepath] = buff;

	return SUCCEEDED (result);
}

bool DX12Wrapper::CreateTextureFromImageData (const DirectX::Image* img, ComPtr<ID3D12Resource>& buff, bool isDiscrete) {
	D3D12_HEAP_PROPERTIES heapprop = {};

	if (isDiscrete) {
		heapprop = CD3DX12_HEAP_PROPERTIES (D3D12_HEAP_TYPE_DEFAULT);
	} else {
		heapprop.Type = D3D12_HEAP_TYPE_CUSTOM;
		heapprop.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
		heapprop.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
		heapprop.CreationNodeMask = 0;
		heapprop.VisibleNodeMask = 0;
	}

	//最終書き込み先リソースの設定
	D3D12_RESOURCE_DESC resdesc =
		CD3DX12_RESOURCE_DESC::Tex2D (img->format, img->width, img->height);

	auto result = S_OK;
	if (isDiscrete) {
		result = _dev->CreateCommittedResource (&heapprop,
												D3D12_HEAP_FLAG_NONE,
												&resdesc,
												D3D12_RESOURCE_STATE_COPY_DEST,
												nullptr,
												IID_PPV_ARGS (buff.ReleaseAndGetAddressOf ()));
	} else {
		result = _dev->CreateCommittedResource (&heapprop,
												D3D12_HEAP_FLAG_NONE,
												&resdesc,
												D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
												nullptr,
												IID_PPV_ARGS (buff.ReleaseAndGetAddressOf ()));
	}

	assert (SUCCEEDED (result));
	if (FAILED (result)) {
		return false;
	}

	return true;
}

#pragma endregion