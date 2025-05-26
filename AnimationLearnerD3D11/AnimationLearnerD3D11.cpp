#include <windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "App.h"
#include <memory>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib") 

// For std::cout
#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>

#include <map>
#include <string>
#include <chrono>


// 全局变量
HWND g_hWnd = nullptr;
D3D_FEATURE_LEVEL g_featureLevel = D3D_FEATURE_LEVEL_11_0;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
ID3D11Texture2D* g_pDepthStencil = nullptr;
ID3D11DepthStencilView* g_pDepthStencilView = nullptr;
ID3D11DepthStencilState* g_pDepthStencilState = nullptr;
std::chrono::steady_clock::time_point g_startTime;

UINT g_width = 1024, g_height = 768; // 全局变量

void UpdateConstant(App* App, float time);

void RedirectIOToConsole()
{
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    std::ios::sync_with_stdio();
}

void ResizeRenderTarget(UINT width, UINT height)
{
    g_width = width;
    g_height = height;

    if (!g_pSwapChain) return;

    // 释放旧的视图和缓冲区
    if (g_pImmediateContext) g_pImmediateContext->OMSetRenderTargets(0, 0, 0);
    if (g_pRenderTargetView) { g_pRenderTargetView->Release(); g_pRenderTargetView = nullptr; }
    if (g_pDepthStencilView) { g_pDepthStencilView->Release(); g_pDepthStencilView = nullptr; }
    if (g_pDepthStencil) { g_pDepthStencil->Release();     g_pDepthStencil = nullptr; }

    // 调整交换链缓冲区大小
    HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    // 重新获取后备缓冲区并创建RTV
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return;

    // 创建新的深度缓冲区和视图
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &g_pDepthStencil);
    if (FAILED(hr)) return;

    hr = g_pd3dDevice->CreateDepthStencilView(g_pDepthStencil, nullptr, &g_pDepthStencilView);
    if (FAILED(hr)) return;

    // 重新绑定渲染目标
    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, g_pDepthStencilView);

    // 更新视口
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports(1, &vp);
}


// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED)
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            // 这里调用你自定义的重建RTV/DSV的函数
            ResizeRenderTarget(width, height);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 初始化窗口
HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0,
                      GetModuleHandle(NULL), nullptr, nullptr, nullptr, nullptr,
                      L"DX11SampleClass", nullptr };
    RegisterClassEx(&wc);

    g_hWnd = CreateWindow(wc.lpszClassName, L"FBX Animation Loader",
        WS_OVERLAPPEDWINDOW, 100, 100, 1024, 768,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_hWnd)
        return E_FAIL;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    RedirectIOToConsole();

    return S_OK;
}

// 初始化 DirectX 11
HRESULT InitD3D()
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &g_featureLevel,
        &g_pImmediateContext);

    if (FAILED(hr))
        return hr;

    // 创建渲染目标视图
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr))
        return hr;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr))
        return hr;

    //g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);


    // 1. 创建深度缓冲区纹理
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &g_pDepthStencil);
    if (FAILED(hr))
        return hr;

    // 2. 创建深度视图
    hr = g_pd3dDevice->CreateDepthStencilView(g_pDepthStencil, nullptr, &g_pDepthStencilView);
    if (FAILED(hr))
        return hr;

    // 3. 设置为渲染目标（颜色 + 深度）
    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, g_pDepthStencilView);

    // 设置视口
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports(1, &vp);



    return S_OK;
}

// 清理资源
void Cleanup()
{
    if (g_pImmediateContext) g_pImmediateContext->ClearState();
    if (g_pRenderTargetView) g_pRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pImmediateContext) g_pImmediateContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

// 主消息循环和渲染
int Run(App* app)
{
    MSG msg = { 0 };
    g_startTime = std::chrono::steady_clock::now();

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {

            auto now = std::chrono::steady_clock::now();
            float time = std::chrono::duration<float>(now - g_startTime).count();
            UpdateConstant(app, time);

            // 清屏颜色
            float clearColor[4] = { 0.2f, 0.4f, 0.6f, 1.0f };
            g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);
            g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

            // 1. 设置着色器和输入布局
            g_pImmediateContext->VSSetShader(app->vertexShader, nullptr, 0);
            g_pImmediateContext->PSSetShader(app->pixelShader, nullptr, 0);
            g_pImmediateContext->IASetInputLayout(app->inputLayout);

            // 2. 设置缓冲区
            UINT stride = sizeof(Vertex);
            UINT offset = 0;
            g_pImmediateContext->IASetVertexBuffers(0, 1, &app->vertexBuffer, &stride, &offset);
            g_pImmediateContext->IASetIndexBuffer(app->indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // 3. 更新常量缓冲区
            ConstantBuffer cb;
            cb.world = DirectX::XMMatrixTranspose(app->cb.world);
            cb.view = DirectX::XMMatrixTranspose(app->cb.view);
            cb.proj = DirectX::XMMatrixTranspose(app->cb.proj);
            cb.lightDir = DirectX::XMFLOAT3(-0.5f, -1.0f, -0.3f); // 示例光照方向

            g_pImmediateContext->UpdateSubresource(app->constantBuffer, 0, nullptr, &app->cb, 0, 0);

            // 4. 绑定常量缓冲区到 Shader
            g_pImmediateContext->VSSetConstantBuffers(0, 1, &app->constantBuffer);
            g_pImmediateContext->PSSetConstantBuffers(0, 1, &app->constantBuffer);

            // 5. 绘制
            g_pImmediateContext->DrawIndexed((UINT)app->indices.size(), 0, 0);

            // 2. 渲染骨骼线
            // 禁用深度测试
            ID3D11DepthStencilState* pOldDS = nullptr;
            UINT oldStencilRef = 0;
            g_pImmediateContext->OMGetDepthStencilState(&pOldDS, &oldStencilRef);

            D3D11_DEPTH_STENCIL_DESC dsDesc = {};
            dsDesc.DepthEnable = FALSE; // 关闭深度
            dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            dsDesc.StencilEnable = FALSE;

            ID3D11DepthStencilState* pNoDepthDS = nullptr;
            g_pd3dDevice->CreateDepthStencilState(&dsDesc, &pNoDepthDS);
            g_pImmediateContext->OMSetDepthStencilState(pNoDepthDS, 0);

            // 设置骨骼线shader
            g_pImmediateContext->VSSetShader(app->boneLineVS, nullptr, 0);
            g_pImmediateContext->PSSetShader(app->boneLinePS, nullptr, 0);
            g_pImmediateContext->IASetInputLayout(app->boneLineLayout);

            // 绑定骨骼线顶点缓冲区
            UINT stride2 = sizeof(aiVector3D);
            UINT offset2 = 0;
            g_pImmediateContext->IASetVertexBuffers(0, 1, &app->boneLineVB, &stride2, &offset2);
            g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

            // 常量缓冲区（用同一个即可）
            g_pImmediateContext->VSSetConstantBuffers(0, 1, &app->constantBuffer);

            // 绘制
            g_pImmediateContext->Draw(app->boneLineVertexCount, 0);

            // 恢复深度状态
            g_pImmediateContext->OMSetDepthStencilState(pOldDS, oldStencilRef);
            if (pOldDS) pOldDS->Release();
            if (pNoDepthDS) pNoDepthDS->Release();

            // Present
            g_pSwapChain->Present(1, 0);
        }
    }
    return (int)msg.wParam;
}

// 递归遍历节点，收集骨骼节点的世界变换
void CollectBonePositions(aiNode* node, const aiMatrix4x4& parentTransform,
    std::map<std::string, aiVector3D>& bonePositions)
{
    aiMatrix4x4 globalTransform = parentTransform * node->mTransformation;
    bonePositions[node->mName.C_Str()] = aiVector3D(globalTransform.a4, globalTransform.b4, globalTransform.c4);

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        CollectBonePositions(node->mChildren[i], globalTransform, bonePositions);
}

// 收集骨骼连线
void CollectBoneLines(aiNode* node, const std::map<std::string, aiVector3D>& bonePositions,
    std::vector<aiVector3D>& lineVertices)
{
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        aiNode* child = node->mChildren[i];
        // 只要父子都在骨骼表里，就连线
        if (bonePositions.count(node->mName.C_Str()) && bonePositions.count(child->mName.C_Str()))
        {
            lineVertices.push_back(bonePositions.at(node->mName.C_Str()));
            lineVertices.push_back(bonePositions.at(child->mName.C_Str()));
        }
        CollectBoneLines(child, bonePositions, lineVertices);
    }
}

bool LoadModel(const std::string& filePath, App* App)
{
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(
        filePath,
        aiProcess_Triangulate | aiProcess_ConvertToLeftHanded |
        aiProcess_JoinIdenticalVertices | aiProcess_GenNormals
    );

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        MessageBoxA(nullptr, importer.GetErrorString(), "Assimp Error", MB_OK | MB_ICONERROR);
        return false;
    }

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        aiMesh* mesh = scene->mMeshes[i];

        size_t baseVertex = App->vertices.size();

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            aiVector3D pos = mesh->mVertices[v];
            aiVector3D normal = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0, 0, 0);
            aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D(0, 0, 0);

            App->vertices.push_back({
                { pos.x, pos.y, pos.z },
                { normal.x, normal.y, normal.z },
                { uv.x, uv.y }
                });
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
            {
                App->indices.push_back(baseVertex + face.mIndices[j]);
            }
        }

        // 代码加在这里，打印aiMesh* mesh的信息
        // 打印该 mesh 的所有骨骼名称和位置
        if (mesh->HasBones()) {
            std::cout << "Mesh " << i << " ("
                << (mesh->mName.length > 0 ? mesh->mName.C_Str() : "[Unnamed]")
                << ") has " << mesh->mNumBones << " bones:" << std::endl;
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                aiBone* bone = mesh->mBones[b];
                // mOffsetMatrix 的 a4, b4, c4 分别是 x, y, z 平移分量
                const aiMatrix4x4& m = bone->mOffsetMatrix;
                float x = m.a4, y = m.b4, z = m.c4;
                std::cout << "  Bone " << b << ": "
                    << (bone->mName.length > 0 ? bone->mName.C_Str() : "[Unnamed]")
                    << "  Offset(pos): (" << x << ", " << y << ", " << z << ")"
                    << std::endl;
            }

            // 1. 收集所有骨骼节点的世界空间位置
            std::map<std::string, aiVector3D> bonePositions;
            CollectBonePositions(scene->mRootNode, aiMatrix4x4(), bonePositions);

            // 2. 生成骨骼连线顶点
            std::vector<aiVector3D> boneLines;
            CollectBoneLines(scene->mRootNode, bonePositions, boneLines);

            // 3. 创建线段顶点缓冲区
            if (!boneLines.empty()) {
                D3D11_BUFFER_DESC bd = {};
                bd.Usage = D3D11_USAGE_DEFAULT;
                bd.ByteWidth = UINT(sizeof(aiVector3D) * boneLines.size());
                bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA initData = {};
                initData.pSysMem = boneLines.data();
                ID3D11Buffer* boneLineVB = nullptr;
                g_pd3dDevice->CreateBuffer(&bd, &initData, &boneLineVB);

                // 保存到 App 结构体里，渲染时用
                App->boneLineVB = boneLineVB;
                App->boneLineVertexCount = boneLines.size();
            }
        }
    }

    App->vbd = {};
    App->vbd.Usage = D3D11_USAGE_DEFAULT;
    App->vbd.ByteWidth = sizeof(Vertex) * App->vertices.size();
    App->vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    App->vinitData = {};
    App->vinitData.pSysMem = App->vertices.data();

    App->vertexBuffer = nullptr;
    g_pd3dDevice->CreateBuffer(&App->vbd, &App->vinitData, &App->vertexBuffer);

    App->ibd = {};
    App->ibd.Usage = D3D11_USAGE_DEFAULT;
    App->ibd.ByteWidth = sizeof(UINT) * App->indices.size();
    App->ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    App->iinitData = {};
    App->iinitData.pSysMem = App->indices.data();

    App->indexBuffer = nullptr;
    g_pd3dDevice->CreateBuffer(&App->ibd, &App->iinitData, &App->indexBuffer);

    App->constantBuffer = nullptr;
    App->cbd = {};
    App->cbd.Usage = D3D11_USAGE_DEFAULT;
    App->cbd.ByteWidth = sizeof(ConstantBuffer);
    App->cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    App->cbd.CPUAccessFlags = 0;
    g_pd3dDevice->CreateBuffer(&App->cbd, nullptr, &App->constantBuffer);

    return true;
}


bool InitShaders(App* app)
{
    // 编译 Vertex Shader
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompileFromFile(
        L"data/PhongShader.hlsl",
        nullptr, nullptr,
        "VSMain", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &vsBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return false;
    }

    // 创建 Vertex Shader
    hr = g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &app->vertexShader);
    if (FAILED(hr))
    {
        vsBlob->Release();
        return false;
    }

    // 编译 Pixel Shader
    ID3DBlob* psBlob = nullptr;
    hr = D3DCompileFromFile(
        L"data/PhongShader.hlsl",
        nullptr, nullptr,
        "PSMain", "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &psBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        vsBlob->Release();
        return false;
    }

    hr = g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &app->pixelShader);
    psBlob->Release();
    if (FAILED(hr))
    {
        vsBlob->Release();
        return false;
    }

    // 定义顶点输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                             D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(float) * 3,              D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, sizeof(float) * 6,              D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_pd3dDevice->CreateInputLayout(
        layout, ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &app->inputLayout);
    vsBlob->Release();

    if (FAILED(hr))
        return false;

    return true;
}

bool InitBoneLineShader(App* app)
{
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompileFromFile(
        L"data/BoneLineShader.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) return false;

    hr = g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &app->boneLineVS);
    if (FAILED(hr)) { vsBlob->Release(); return false; }

    hr = D3DCompileFromFile(
        L"data/BoneLineShader.hlsl", nullptr, nullptr,
        "PSMain", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) { vsBlob->Release(); return false; }

    hr = g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &app->boneLinePS);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    // 输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_pd3dDevice->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &app->boneLineLayout);

    vsBlob->Release();
    psBlob->Release();

    return SUCCEEDED(hr);
}


void UpdateConstant(App* App, float time)
{
    DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();

    // 圆周运动参数
    float radius = 400.0f;
    float height = 250.0f;
    float speed = 0.5f; // 每秒转多少圈（弧度/秒）

    // 计算相机位置
    float angle = speed * time; // 弧度
    float camX = radius * sinf(angle);
    float camZ = radius * cosf(angle);
    float camY = height;

    DirectX::XMVECTOR eyePosition = DirectX::XMVectorSet(camX, camY, camZ, 1.0f);

    // 看向模型中心
    DirectX::XMVECTOR focusPoint = DirectX::XMVectorSet(0.0f, 100.0f, 0.0f, 1.0f);
    DirectX::XMVECTOR upDirection = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX viewMatrix = DirectX::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

    float fovAngleY = DirectX::XMConvertToRadians(45.0f);
    float aspectRatio = (float)g_width / (float)g_height;
    float nearZ = 0.1f;
    float farZ = 5000.0f;

    DirectX::XMMATRIX projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(fovAngleY, aspectRatio, nearZ, farZ);

    App->cb.world = DirectX::XMMatrixTranspose(worldMatrix);
    App->cb.view = DirectX::XMMatrixTranspose(viewMatrix);
    App->cb.proj = DirectX::XMMatrixTranspose(projectionMatrix);
    App->cb.lightDir = { -0.5f, -0.5f, 0.5f };

    g_pImmediateContext->UpdateSubresource(App->constantBuffer, 0, nullptr, &App->cb, 0, 0);
    g_pImmediateContext->VSSetConstantBuffers(0, 1, &App->constantBuffer);
    g_pImmediateContext->PSSetConstantBuffers(0, 1, &App->constantBuffer);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    std::unique_ptr<App> app_inst = std::make_unique<App>();

    if (FAILED(InitWindow(hInstance, nCmdShow)))
        return 0;

    if (FAILED(InitD3D()))
    {
        Cleanup();
        return 0;
    }

    if (FAILED(InitShaders(app_inst.get())))
    {
		Cleanup();
		return 0;
	}

    if (!InitBoneLineShader(app_inst.get())) {
        Cleanup();
        return 0;
    }

    if (!LoadModel("data/Taunt.fbx", app_inst.get()))
    {
        Cleanup();
        return 0;
    }

    int ret = Run(app_inst.get());

    Cleanup();

    return ret;
}