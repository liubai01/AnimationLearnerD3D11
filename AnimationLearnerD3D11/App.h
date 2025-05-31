#pragma once
#include <vector>
#include <DirectXMath.h>
#include <windows.h>
#include <d3d11.h>
#include <map>
#include <string>
#include <assimp/scene.h>
#pragma comment(lib, "d3d11.lib")

struct BoneMatrixBuffer
{
    DirectX::XMMATRIX boneMatrices[128];
};

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texcoord;

    UINT boneIndices[4] = { 0 };
    float boneWeights[4] = { 0 };
};

struct ConstantBuffer
{
    DirectX::XMMATRIX world;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX proj;
    DirectX::XMFLOAT3 lightDir;
    UINT targetBoneIndex;
    float padding1[3];
};

struct BoneAnimCache {
    std::vector<aiVectorKey> positions;
    std::vector<aiQuatKey> rotations;
    std::vector<aiVectorKey> scalings;
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

    std::map<std::string, BoneAnimCache> boneAnimCache; // ¶¯»­Í¨µÀ»º´æ
    float animDuration = 0.0f;
    float animTicksPerSecond = 25.0f;

    aiScene* scene;

    ID3D11Buffer* boneMatrixBuffer = nullptr;
    BoneMatrixBuffer boneMatrixData;

    std::map<std::string, int> boneNameToIndex;
    std::map<std::string, aiMatrix4x4> boneOffsetMatrices;
};

