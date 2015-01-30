#include "pch.h"
#include "SwapChainPanelRenderTarget.h"
using namespace Microsoft::WRL;


double scaleToPhysicalPixel(double logicalPixel) {
	return  logicalPixel;
}

YuvRenderer::SwapChainPanelRenderTarget::SwapChainPanelRenderTarget(Windows::UI::Xaml::Controls::SwapChainPanel ^ panel)
	:_panel(panel), _aspectRatio(static_cast<float>(panel->ActualWidth / panel->ActualHeight)),
	_targetWidth(scaleToPhysicalPixel(panel->ActualWidth)),
	_targetHeight(scaleToPhysicalPixel(panel->ActualHeight)),
	_needResize(false),
	_dispatcher(panel->Dispatcher)
{
	reinterpret_cast<IInspectable*>(panel)->QueryInterface<ISwapChainPanelNative>(&_panelNative);

	_resizeToken = panel->SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler([this](Platform::Object^ o, Windows::UI::Xaml::SizeChangedEventArgs^ args) {
		
		_targetWidth = (uint32_t)scaleToPhysicalPixel(_panel->ActualWidth );
		_targetHeight = (uint32_t)scaleToPhysicalPixel(_panel->ActualHeight);

		_aspectRatio = static_cast<float>(_panel->ActualWidth / _panel->ActualHeight);
		_needResize = true;
	});
}

YuvRenderer::SwapChainPanelRenderTarget::~SwapChainPanelRenderTarget()
{
	if (!_dispatcher->HasThreadAccess) {
		auto panel = _panelNative;
		auto xamlpanel = _panel;
		auto resizeToken = _resizeToken;
		concurrency::create_task(_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([panel, xamlpanel, resizeToken]() {
			panel->SetSwapChain(nullptr);
			xamlpanel->SizeChanged -= resizeToken;
		}))).get();
	}
	else {
		_panelNative->SetSwapChain(nullptr);
		_panel->SizeChanged -= _resizeToken;
	}
}


float YuvRenderer::SwapChainPanelRenderTarget::aspectRatio() const
{
	return _aspectRatio;
}

void YuvRenderer::SwapChainPanelRenderTarget::onBeginRender(ID3D11Device * device, ID3D11DeviceContext * deviceContext)
{
	if (_knownDevice != device) {
		_swapChain = nullptr;
		_rtv = nullptr;
		ComPtr<IDXGIFactory2> dxgiFactory;
		ComPtr<IDXGIDevice2> dxgiDevice;
		auto hr = device->QueryInterface(__uuidof(IDXGIDevice2), (void**)&dxgiDevice);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};


		ComPtr<IDXGIAdapter> pDXGIAdapter;
		hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void **)&pDXGIAdapter);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};
		ComPtr<IDXGIFactory2> pIDXGIFactory;
		hr = pDXGIAdapter->GetParent(__uuidof(IDXGIFactory2), (void **)&pIDXGIFactory);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
		swapChainDesc.Width = _targetWidth;
		swapChainDesc.Height = _targetHeight;
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 2;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE::DXGI_ALPHA_MODE_IGNORE;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.Flags = 0;

		hr = pIDXGIFactory->CreateSwapChainForComposition(device, &swapChainDesc, nullptr, &_swapChain);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};

		ComPtr<ID3D11Texture2D> backBuffer;
		hr = _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};
		hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, &_rtv);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};
		auto panel = _panelNative;
		auto swapchain = _swapChain;
		if (_dispatcher->HasThreadAccess) {
			panel->SetSwapChain(swapchain.Get());
		}
		else {
			concurrency::create_task(_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal, ref new Windows::UI::Core::DispatchedHandler([panel, swapchain]() {
				panel->SetSwapChain(swapchain.Get());
			}))).get();
		}
		_knownDevice = device;
		_needResize = false;
	}
	if (_needResize) {
		_needResize = false;
		deviceContext->OMSetRenderTargets(0, nullptr, nullptr);
		_rtv = nullptr;
		auto hr = _swapChain->ResizeBuffers(2, _targetWidth, _targetHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		}
		ComPtr<ID3D11Texture2D> backBuffer;
		hr = _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};
		hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, &_rtv);
		if (FAILED(hr)) {
			throw Platform::Exception::CreateException(hr);
		};
	}
	deviceContext->OMSetRenderTargets(1, _rtv.GetAddressOf(), nullptr);
	D3D11_VIEWPORT vp;
	vp.Height = _targetHeight;
	vp.Width = _targetWidth;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.MaxDepth = 1;
	vp.MinDepth = 0;
	deviceContext->RSSetViewports(1, &vp);
}

HRESULT YuvRenderer::SwapChainPanelRenderTarget::onEndRender()
{
	return _swapChain->Present(0, 0);
}
