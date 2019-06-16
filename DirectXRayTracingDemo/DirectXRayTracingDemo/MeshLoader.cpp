#include "stdafx.h"
#include "MeshLoader.h"
#include <string>
#include <unordered_map>

#define ASSIMP_IMPLEMENTATION
#include <assimp\Importer.hpp>
#include <assimp\scene.h>
#include <assimp\postprocess.h>
#include <assimp\cimport.h>
#include <assimp\material.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "Externals/GLM/glm/gtc/matrix_transform.hpp"
#include "Externals/GLM/glm/gtc/type_ptr.hpp"

using namespace glm;
MeshLoader::MeshLoader()
{

}


MeshLoader::~MeshLoader()
{
}

void MeshLoader::LoadModel(const std::string modelPath, std::vector<uint32_t>& indices, std::vector<Vertex>& vertices, float scale)
{
	mpModel = new Model();
	//Load model using ASSIMP
	const aiScene* scene;
	Assimp::Importer importer;

	//assimp flags for loading model
	static const int assimpFlags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_GenSmoothNormals;
	//File reading in c++
	scene = importer.ReadFile(modelPath.c_str(), assimpFlags);

	//Creating Vertex Buffer from loaded data
	//Iterate through all the meshes
	mpModel->mMeshes.reserve(scene->mNumMeshes);
	for (uint32_t i = 0; i < scene->mNumMeshes; i++)
	{
		Model::Mesh mesh;
		for (uint32_t j = 0; j < scene->mMeshes[i]->mNumVertices; j++)
		{
			Vertex vertex;
			//glm has a make function to convert the assimp vectors to glm
			aiVector3D pos = scene->mMeshes[i]->mVertices[j];
			vertex.pos = vec3(pos.x, pos.y, pos.z);
			if (scene->mMeshes[i]->mTextureCoords[0] != NULL)
			{
				vertex.uv = make_vec2(&scene->mMeshes[i]->mTextureCoords[0][j].x);
			}
			mesh.vertexCount = scene->mMeshes[i]->mNumVertices;
			vertex.uv.y = 1.0f - vertex.uv.y;
			//vertex.uv.x = 1.0f - vertex.uv.x;
			vertex.pos *= scale;
			//Normals
			aiVector3D *pNormal = &scene->mMeshes[i]->mNormals[j];
			if (pNormal != NULL)
			{
				vertex.normal = glm::vec3(pNormal->x, pNormal->y, pNormal->z);
			}

			vertices.push_back(vertex);
		}

		for (uint32_t h = 0u; h < scene->mMeshes[i]->mNumFaces; h++)
		{
			assert(scene->mMeshes[i]->mFaces[h].mIndices[0u]);
			indices.push_back(scene->mMeshes[i]->mFaces[h].mIndices[0u]);
			indices.push_back(scene->mMeshes[i]->mFaces[h].mIndices[1u]);
			indices.push_back(scene->mMeshes[i]->mFaces[h].mIndices[2u]);
			mesh.indexCount += scene->mMeshes[i]->mFaces[h].mNumIndices;
		}
		
		if (i != 0)
		{
			mesh.indexDataByteOffset = mpModel->mMeshes[i - 1].indexDataByteOffset + (mpModel->mMeshes[i - 1].indexCount * sizeof(uint32_t));
			mesh.vertexDataByteOffset = mpModel->mMeshes[i - 1].vertexDataByteOffset + (mpModel->mMeshes[i - 1].vertexCount * sizeof(Vertex));
		}
		else
		{
			mesh.indexDataByteOffset = 0;
			mesh.vertexDataByteOffset = 0;
		}
		mesh.materialIndex = scene->mMeshes[i]->mMaterialIndex;
		mpModel->mMeshes.push_back(mesh);
		mpModel->mMaterialCount = scene->mNumMaterials;
	}
	//size_t vertexArraySize = m_pBuffer->vertices.size() * sizeof(Vertex);

	for (size_t i = 0; i < mpModel->mMaterialCount; i++)
	{
		mpModel->mMaterials.reserve(mpModel->mMaterialCount);

		Model::Material material;
		aiMaterial* aiMaterial = scene->mMaterials[i];
		aiColor4D diffuseColor;
		aiColor4D ambientColor;
		aiColor4D specularColor;

		std::string mtlPath = modelPath;
		for (size_t i = 0; i < 3; i++)
		{
			mtlPath.pop_back();
		}
		char* temp1 = "m";
		char* temp2 = "t";
		char* temp3 = "l";
		mtlPath.push_back(*temp1);
		mtlPath.push_back(*temp2);
		mtlPath.push_back(*temp3);

		aiString name;
		aiGetMaterialString(aiMaterial, AI_MATKEY_NAME, &name);
		material.MatName = name.C_Str();
		aiString diffPath;
		aiMaterial->GetTexture(aiTextureType_DIFFUSE, 0, &diffPath);
		material.diffuseTexturePath = diffPath.C_Str();
		aiGetMaterialColor(aiMaterial, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor);
		aiGetMaterialColor(aiMaterial, AI_MATKEY_COLOR_AMBIENT, &ambientColor);
		aiGetMaterialColor(aiMaterial, AI_MATKEY_COLOR_SPECULAR, &specularColor);

		material.Diffuse = XMFLOAT4(diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a);
		material.Ambient = XMFLOAT3(ambientColor.r, ambientColor.g, ambientColor.b);
		material.Specular = XMFLOAT4(specularColor.r, specularColor.g, specularColor.b, specularColor.a);
		mpModel->mMaterials.push_back(material);
	}

}

void MeshLoader::FlipVector(std::vector<Vertex> &vec)
{
	//Flip indices
	std::vector<Vertex> indicesBackw = vec;
	vec.clear();
	for (size_t i = indicesBackw.size()-1; i > 0; i--)
	{
		vec.push_back(indicesBackw[i]);
	}
	vec.push_back(indicesBackw[0]);
}

void MeshLoader::LoadTextures(ID3D12Device* pDevice, std::vector<D3D12_RESOURCE_DESC>& resourceDescArray, std::vector<D3D12_SUBRESOURCE_DATA>& subResourceArray, std::vector<std::string>& textureLocations)
{
	//DDS
	for (size_t i = 0; i < mpModel->mMaterialCount; i++)
	{
		std::string str = mpModel->mMaterials[i].diffuseTexturePath;
		for (size_t i = 0; i < 3; i++)
		{
			if (str != "")
			{
				str.pop_back();
			}
		}
		if (str != "")
		{
			char* temp1 = "d";
			char* temp2 = "d";
			char* temp3 = "s";
			str.push_back(*temp1);
			str.push_back(*temp2);
			str.push_back(*temp3);

			textureLocations.push_back(str);
		}
		else
		{
			textureLocations.push_back("../default.dds");
		}
	}

	
	mSRVs = new D3D12_CPU_DESCRIPTOR_HANDLE[mpModel->mMaterialCount]; //only diffuse atm
}

void MeshLoader::CreateTextureDesc(ID3D12Device* pDevice, std::vector<D3D12_RESOURCE_DESC>& resourceDescArray, std::vector<D3D12_SUBRESOURCE_DATA>& subResourceArray, size_t Width, size_t Height, DXGI_FORMAT Format, const void* InitialData)
{
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = Width;
	texDesc.Height = (UINT)Height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = Format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	resourceDescArray.push_back(texDesc);

	D3D12_SUBRESOURCE_DATA texResource;
	texResource.pData = InitialData;
	texResource.RowPitch = (Width * 32)/8; //DXGI_FORMAT_R8G8B8A8_UNORM
	texResource.SlicePitch = texResource.RowPitch * Height;

	subResourceArray.push_back(texResource);
}