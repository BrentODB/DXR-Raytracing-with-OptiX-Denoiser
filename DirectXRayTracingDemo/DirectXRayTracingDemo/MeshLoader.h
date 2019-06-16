#pragma once
//#include "stdafx.h"
#include <vector>
#include <array>

#include "Externals/GLM/glm/glm.hpp"

struct Model 
{
	struct Mesh
	{
		unsigned int materialIndex;
		unsigned int vertexStride;
		unsigned int vertexStrideDepth;
		unsigned int vertexDataByteOffset;
		unsigned int vertexCount;
		unsigned int indexDataByteOffset;
		unsigned int indexCount = 0;

		unsigned int vertexDataByteOffsetDepth;
		unsigned int vertexCountDepth;
	};
	std::vector<Mesh> mMeshes;

	struct Material
	{
		XMFLOAT4 Diffuse;
		XMFLOAT3 Ambient;
		XMFLOAT4 Specular;

		std::string MatName;

		int DiffuseTextureID;
		int AmbientTextureID;
		int SpecularTextureID;

		bool HasDiffTexture;
		bool HasAmbientTexture;
		bool HasSpecularTexture;

		std::string diffuseTexturePath;
	};
	std::vector<Material> mMaterials;
	int mMaterialCount = 0;
};

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 tangent;
};

struct MeshInfo
{
	unsigned int vertexStride;
	unsigned int vertexDataByteOffset;
	unsigned int hasTexture;
	unsigned int indexStride;
	unsigned int indexDataByteOffset;
	unsigned int isReflective;
	unsigned int indexOffset;
	unsigned int materialIndex;
	glm::vec3 diffuseColor;
	unsigned int isRefractive;
	unsigned int emissiveStrength;
	glm::vec3 filler;
};

class MeshLoader
{
public:
	MeshLoader();
	~MeshLoader();

	void LoadModel(const std::string modelPath, std::vector<uint32_t>& indices, std::vector<Vertex>& vertices, float scale = 1.0f);
	void LoadTextures(ID3D12Device* pDevice, std::vector<D3D12_RESOURCE_DESC>& resourceDescArray, std::vector<D3D12_SUBRESOURCE_DATA>& subResourceArray, std::vector<std::string>& textureLocations);
	void CreateTextureDesc(ID3D12Device* pDevice, std::vector<D3D12_RESOURCE_DESC>& resourceDescArray, std::vector<D3D12_SUBRESOURCE_DATA>& subResourceArray, size_t Width, size_t Height, DXGI_FORMAT Format, const void* InitialData);
	void FlipVector(std::vector<Vertex> &vec);
	Model* mpModel;
	D3D12_CPU_DESCRIPTOR_HANDLE* mSRVs;
private:

};

