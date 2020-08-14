#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <map>
#include <unordered_map>
#include <DirectXTex.h>
#include <wrl.h>
#include <string>
#include <functional>
#include <memory>

class DX12Wrapper {

	struct SceneData {
		DirectX::XMMATRIX view;
		DirectX::XMMATRIX proj;
		// Chapter 13
		DirectX::XMMATRIX shadow;
		DirectX::XMFLOAT3 eye;
	};

	SIZE _winSize;
	template<typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;
	
	//
	// DXGI
	//
	ComPtr < IDXGIFactory4> _dxgiFactory = nullptr;
	ComPtr < IDXGISwapChain4> _swapchain = nullptr;
	
	//
	// DirectX 12
	//
	ComPtr< ID3D12Device> _dev = nullptr;
	ComPtr < ID3D12CommandAllocator> _cmdAllocator = nullptr;
	ComPtr < ID3D12GraphicsCommandList> _cmdList = nullptr;
	ComPtr < ID3D12CommandQueue> _cmdQueue = nullptr;

	//
	// Display
	//	
	ComPtr<ID3D12Resource> _depthBuffer = nullptr;
	std::vector<ID3D12Resource*> _backBuffers;
	ComPtr<ID3D12DescriptorHeap> _rtvHeaps = nullptr;
	ComPtr<ID3D12DescriptorHeap> _dsvHeap = nullptr;
	std::unique_ptr<D3D12_VIEWPORT> _viewport;
	std::unique_ptr<D3D12_RECT> _scissorRect;

	//
	// Scene Data
	//	
	ComPtr<ID3D12Resource> _sceneConstBuff = nullptr;
	SceneData* _mappedSceneData;
	ComPtr<ID3D12DescriptorHeap> _sceneDescHeap = nullptr;

	//
	// Fence
	//
	ComPtr<ID3D12Fence> _fence = nullptr;
	UINT64 _fenceVal = 0;
	
	//
	// Load Texture Lambda
	//
	using LoadLambda_t = std::function<HRESULT (const std::wstring & path, DirectX::TexMetadata*, DirectX::ScratchImage&)>;
	std::map < std::string, LoadLambda_t> _loadLambdaTable;
	std::unordered_map<std::string, ComPtr<ID3D12Resource>> _textureTable;

	HRESULT	CreateFinalRenderTargets ();
	HRESULT CreateDepthStencilView ();
	HRESULT CreateSwapChain (const HWND& hwnd);
	HRESULT InitializeDXGIDevice ();
	HRESULT InitializeCommand ();
	HRESULT CreateSceneView ();
	void CreateTextureLoaderTable ();
	ID3D12Resource* CreateTextureFromFile (const char* texPath);

#pragma region Chapter 13

	DirectX::XMFLOAT3 _parallelLightVec;

#pragma endregion

public:
	DX12Wrapper (HWND hwnd);
	~DX12Wrapper ();

	void Update ();
	void BeginDraw ();
	void EndDraw ();

	ComPtr<ID3D12Resource> GetTextureByPath (const char* texPath);

	ComPtr<ID3D12Device> Device ();
	ComPtr<ID3D12GraphicsCommandList> CommandList ();
	ComPtr<IDXGISwapChain4> Swapchain ();

	void SetScene ();
};