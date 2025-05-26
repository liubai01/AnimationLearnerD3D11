#pragma once
#include <vector>
#include <DirectXMath.h>
#include <windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")


struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texcoord;
};

struct ConstantBuffer
{
    DirectX::XMMATRIX world;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX proj;
    DirectX::XMFLOAT3 lightDir;
    float padding1;
    DirectX::XMFLOAT3 cameraPos;
    float padding2;
};

class App
{
 public:
	std::vector<Vertex> vertices;
	std::vector<UINT> indices;

    D3D11_BUFFER_DESC vbd = {};
    D3D11_SUBRESOURCE_DATA vinitData = {};
    ID3D11Buffer* vertexBuffer = nullptr;

    D3D11_BUFFER_DESC ibd = {};
    D3D11_SUBRESOURCE_DATA iinitData = {};
    ID3D11Buffer* indexBuffer = nullptr;

    ID3D11Buffer* constantBuffer = nullptr;
    D3D11_BUFFER_DESC cbd = {};
    ConstantBuffer cb;

    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;

    ID3D11VertexShader* boneLineVS = nullptr;
    ID3D11PixelShader* boneLinePS = nullptr;
    ID3D11InputLayout* boneLineLayout = nullptr;

    ID3D11Buffer* boneLineVB = nullptr;
    size_t boneLineVertexCount = 0;
};

