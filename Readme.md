# AnimationLearnerD3D11

A Direct3D11-based animation learner project that demonstrates loading, animating, and skinning a skeletal FBX model using the [Assimp](https://github.com/assimp/assimp) library. Developed with Visual Studio 2022 on Windows 11, this project walks through essential animation pipeline steps such as model importing, bone visualization, skeletal animation, and Linear Blend Skinning (LBS).

![image-20250601011732235](assets/image-20250601011732235.png)

This relates to my latest post on [zhihu(chinese)](https://zhuanlan.zhihu.com/p/1912302424607143569).

## 📌 Features

![image-20250601012233917](assets/image-20250601012233917.png)

- Load static FBX models via Assimp
- Visualize skeletal hierarchy
- Animate bones without skinning
- Apply Linear Blend Skinning (LBS) on the GPU
- Use classic Phong shading for lighting

## 🛠 Requirements

- Windows 11
- Visual Studio 2022
- DirectX 11 SDK
- Assimp (compiled as DLL)



# 🦴 Linear Blending Skinning (LBS) Overview

This demo implements **Linear Blending Skinning (LBS)** using **Direct3D 11 (D3D11)**. It is a commonly used algorithm in skeletal animation to deform meshes according to bone transformations.

## 💡 What is Linear Blending Skinning?

Linear Blending Skinning (also called **Smooth Skinning** or **Matrix Palette Skinning**) deforms each vertex by blending the transformations of multiple bones, weighted by their influence.

It is widely supported in game engines and graphics pipelines due to its simplicity and real-time efficiency.

## 🧮 Algorithm Summary

![image-20250601012405617](assets/image-20250601012405617.png)

Linear Blending Skinning (LBS) is a very simple and intuitive algorithm. To understand it, one mainly needs to grasp the concept of **mesh space** and **bone/joint space**. It is based on the following assumptions:

**Assumption 1**: If a vertex is influenced by a single bone, then it remains **static relative to that bone** (i.e., it moves rigidly with the bone).

**Assumption 2**: If a vertex near a joint is influenced by **multiple bones**, the artist can specify how much each bone contributes to the final deformation by **painting weights**.

## 📐 The LBS Formula

In LBS, the final skinned position of a vertex is computed using the following formula:
$$
v' = \sum_{i=1}^{n} w_i \cdot M_i \cdot B_i^{-1} \cdot v
$$
Where:

- **$v$**: The position of the vertex in **bind pose**
  - In this project, it's the vertex position stored in `aiMesh`.
- **$w_i$**: The weight of bone $i$ affecting the vertex, with the constraint $\sum w_i = 1$
  - This is represented in the `aiMesh`'s `mBones` array, specifically within the `aiVertexWeight` structure.
- **$M_i$**: The current transformation matrix of bone $i$, usually transforming from the bone’s **local space** to **model space**
  - This matrix is computed per frame, by sampling the animation data (`aiNodeAnim`) for each bone (if it exists), evaluating the local transform at the current time, and recursively combining transforms from the root to the current node.
- **$B_i^{-1}$**: The inverse **bind pose matrix** of bone $i$, transforming from **model space** to the bone's **local space**
  - This is the **bone offset matrix**, which can be directly accessed as `aimesh::mBones[idx]::mOffsetMatrix`. It represents the bone's transform in the bind pose (e.g., A-pose or T-pose).

## 🚀 D3D11 Implementation

This project uses:

- **StructuredBuffers** to store bone matrices and skinning weights.
- **Vertex Shader (VS)** to apply the blending logic.
- Efficient use of **constant buffers** and **shader resource views** to minimize CPU-GPU synchronization.

### Skinning in Vertex Shader

The skinning is fully performed on the **GPU**. The vertex shader:

1. Fetches the bone indices and weights for the current vertex.
2. Applies each bone's transformation to the vertex.
3. Blends the results using weights.
4. Outputs the final skinned position and normal.

## 📚 Implementation Highlights

### ✅ Static Model Import

- Load static FBX model in T-pose using Assimp
- Skip materials, apply basic Phong shading
- See [this commit](https://github.com/liubai01/AnimationLearnerD3D11/commit/fd692236946dda99be815293eb0bced116690114)

### ✅ Bone Hierarchy Visualization

- Load bone structure and draw bones as green lines
- Disable depth testing to ensure visibility
- See [this commit](https://github.com/liubai01/AnimationLearnerD3D11/commit/63ec58661ac09bfd44ba2673f545077aaa510ad1)

### ✅ Skeleton Animation (without skinning)

- Extract animation channels (`aiNodeAnim`) and keyframes
- Perform position, rotation, and scale interpolation per frame
- Traverse bone hierarchy to accumulate global transforms
- See [this commit](https://github.com/liubai01/AnimationLearnerD3D11/commit/01709501d3de971dcc39016c53b6c86fe2928287)



### ✅ Linear Blend Skinning (LBS)

- Implemented on GPU using a constant buffer with up to 128 bone matrices

- On CPU, traverse bone hierarchy to calculate:

  ```
  boneMatrix[i] = globalTransform * inverseBindPose
  ```

- Upload matrices via `UpdateSubresource` to GPU

- See [this commit](https://github.com/liubai01/AnimationLearnerD3D11/commit/40119dd5abc6e0ac37ead3752d1b7509206c93ed)

## 📄 License

This project is open-source under the MIT License.

------

## 🙌 Acknowledgments

- [Assimp](https://github.com/assimp/assimp)
- [Mixamo](https://www.mixamo.com/)
- DirectX11 tutorials and community