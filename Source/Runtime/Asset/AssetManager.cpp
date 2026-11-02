#include "EnginePCH.h"
#include "AssetManager.h"
#include "Render/Buffer.h"
#include "Render/Material.h"

#include "Render/Vertex.h"
#include "Render/Texture2D.h"
#include "Render/RenderCommand.h"

#include "Render/RenderResourceManager.h"

#include "Asset/ObjImporter/ObjImporter.h"
#include "Asset/FBXImporter/FbxImporter.h"

#include "Render/GeometryGenerator.h"
#include "UObject/UObjectGlobals.h"

#include "Render/ImageLoader.h"
#include "Core/ResourcePaths.h"
#include <cctype>


namespace
{
	FString GetAssetExtension(const FString& Path) {
		const std::u8string UTF8Path{ Path.begin(), Path.end() };
		FString Extension{ std::filesystem::path{ UTF8Path }.extension().string() };
		for (char& Character : Extension) {
			Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
		}
		return Extension;
	}

	FString ToUTF8String(const std::filesystem::path& Path)
	{
		const std::u8string UTF8Path = Path.generic_u8string();
		return FString{ UTF8Path.begin(), UTF8Path.end() };
	}

	UStaticMesh* CreateStaticMesh(const FStaticMeshData* MeshData)
	{
		if (!MeshData || !MeshData->Vertices.Num() || !MeshData->Indices.Num())
			return nullptr;

		FStaticMeshData Optimized = *MeshData;

		// Mesh에 MaterialSlot 없을 경우 Default Material 할당
		if (Optimized.MaterialSlots.IsEmpty())
		{
			FStaticMaterialSlot DefaultSlot;
			DefaultSlot.Name = "Default";
			Optimized.MaterialSlots.Add(DefaultSlot);
		}

		// Mesh에 Section이 없을 경우 전체 메쉬을 섹션 하나로 세팅
		if (Optimized.Sections.IsEmpty())
		{
			FStaticMeshSection DefaultSection;
			DefaultSection.StartIndex = 0;
			DefaultSection.IndexCount = static_cast<uint32>(Optimized.Indices.Num());
			DefaultSection.MaterialSlotIndex = 0;

			Optimized.Sections.Add(DefaultSection);
		}

		Optimized.BuildTangentSpace();
		Optimized.OptimizeTriangleOrderForVertexCache();

		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Optimized.Vertices.GetData(), sizeof(FVertex) * static_cast<uint32>(Optimized.Vertices.size()), sizeof(FVertex));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(Optimized.Indices.GetData(), static_cast<uint32>(Optimized.Indices.size()));
		if (VB == nullptr || IB == nullptr) return nullptr;

		UStaticMesh* Mesh = NewObject<UStaticMesh>();
		Mesh->MeshData = std::move(Optimized);

		// Vertex/Index GPU 업로드
		Mesh->VertexBuffer = std::move(VB);
		Mesh->IndexBuffer = std::move(IB);

		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return nullptr;
		}

		// MaterialSlots를 실제 UMaterial로 변환
		for (const FStaticMaterialSlot& Slot : Mesh->MeshData.MaterialSlots)
		{
			UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
			if (!Material)
			{
				return nullptr;
			}
			Material->BaseColor = Slot.BaseColor;
			Material->SetBlendState(Material->BaseColor.W < 1.0f ? EBlendState::AlphaBlend : EBlendState::Opaque);

			if (!Slot.DiffuseTexturePath.empty())
			{
				UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.DiffuseTexturePath);

				if (Texture)
				{
					Material->Textures[0] = Texture;
				}
			}

			if (!Slot.NormalTexturePath.empty()) {
				if (UTexture2D* NormalTexture{ UAssetManager::Get().LoadTexture(Slot.NormalTexturePath) }) {
					Material->Textures[1] = NormalTexture;
				}
			}

			Mesh->Materials.Add(Material);
		}

		Mesh->MeshData.BuildTriangleBVH();
		Mesh->RebuildRenderData();

		return Mesh;
	}

	USkeletalMesh* CreateSkeletalMesh(const FSkeletalMeshData* MeshData)
	{
		if (!MeshData || MeshData->Meshes.IsEmpty()) return nullptr;

		TArray<FVertex> RenderVertices;
		TArray<uint32> RenderIndices;
		TArray<FSkeletalMeshSection> RenderSections;

		uint32 BaseMaterial = 0;

		for (int i = 0; i < MeshData->Meshes.Num(); i++) // 전체 Mesh에 대하여 VertexBuffer,IndexBuffer 종합하여 추가
		{
			uint32 BaseVertex = static_cast<uint32>(RenderVertices.Num());
			uint32 BaseIndex = RenderIndices.Num();

			for (int j = 0; j < MeshData->Meshes[i].Vertices.Num(); j++)
			{
				RenderVertices.Add(MeshData->Meshes[i].Vertices[j].Vertex);
			}

			for (int j = 0; j < MeshData->Meshes[i].Indices.Num(); j++)
			{
				RenderIndices.Add(BaseVertex + MeshData->Meshes[i].Indices[j]); // Mesh마다 Index가 0으로 돌아가므로 겹치는것을 방지하기 위해 BaseVertex만큼 Offset 추가
			}
			for (int j = 0; j < MeshData->Meshes[i].Sections.Num(); j++)
			{
				FSkeletalMeshSection Section = MeshData->Meshes[i].Sections[j];
				Section.StartIndex += BaseIndex;	// Mesh마다 Index 시작 겹치므로 Offset 추가
				Section.MaterialSlotIndex += BaseMaterial;	// Mesh마다 Material의 Index 시작 겹치므로 Offset 추가
				RenderSections.Add(Section);
			}
			BaseMaterial += static_cast<uint32>(MeshData->Meshes[i].MaterialSlots.Num());	// 한 Mesh의 MaterialSlot 추가가 끝날때마다 BaseMaterial에 전체 슬롯 개수만큼 더해줌 => 현재의 전체 Material 개수 표시
		}

		if (RenderVertices.IsEmpty() || RenderIndices.IsEmpty()) return nullptr;

		BuildMeshTangentSpace(RenderVertices, RenderIndices);

		TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(RenderVertices.GetData(), sizeof(FVertex) * static_cast<uint32>(RenderVertices.size()), sizeof(FVertex));
		TUniquePtr<FIndexBuffer> IB = RenderCommand::CreateStaticIndexBuffer(RenderIndices.GetData(), static_cast<uint32>(RenderIndices.size()));
		
		if (VB == nullptr || IB == nullptr) return nullptr;

		USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
		Mesh->MeshData = *MeshData;

		// Vertex/Index GPU 업로드
		Mesh->VertexBuffer = std::move(VB);
		Mesh->IndexBuffer = std::move(IB);
		Mesh->Sections = std::move(RenderSections);	// Section 정보 저장

		UMaterial* DefaultMaterial = UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");
		if (!DefaultMaterial)
		{
			return nullptr;
		}

		// MaterialSlots를 실제 UMaterial로 변환
		for (int k = 0; k < Mesh->MeshData.Meshes.Num(); k++)
		{
			for (const FSkeletalMaterialSlot& Slot : Mesh->MeshData.Meshes[k].MaterialSlots)
			{
				UMaterial* Material = UMaterial::CreateInstance(DefaultMaterial);
				if (!Material)
				{
					return nullptr;
				}
				Material->BaseColor = Slot.BaseColor;
				Material->SetBlendState(Material->BaseColor.W < 1.0f ? EBlendState::AlphaBlend : EBlendState::Opaque);

				if (!Slot.BaseColorTexturePath.empty())
				{
					UTexture2D* Texture = UAssetManager::Get().LoadTexture(Slot.BaseColorTexturePath);

					if (Texture)
					{
						Material->Textures[0] = Texture;
					}
				}

				if (!Slot.NormalTexturePath.empty())
				{
					if (UTexture2D * NormalTexture{ UAssetManager::Get().LoadTexture(Slot.NormalTexturePath) })
					{
						Material->Textures[1] = NormalTexture;
					}
				}

				Mesh->Materials.Add(Material);
			}
		}
		// 생성한 메시의 Bounds를 계산하고 등록된 Proxy에 변경을 알린다.
		Mesh->RebuildRenderData();
		return Mesh;
	}


	FString MakeAssetKey(const FString& Path)
	{
		return fs::relative(Path, "Assets").generic_string();
	}
}

UAssetManager& UAssetManager::Get()
{
	static UAssetManager* Instance = NewObject<UAssetManager>();
	return *Instance;
}

void UAssetManager::Init(const FAssetLoadProgress& OnProgress)
{
	FGeometryGenerator::CreateDefaultMeshDatas();
	// 머티리얼이 참조하므로 반드시 먼저 만든다
	Get().CreateDefaultTextures();
	Get().CreateDefaultMaterial();
	// Resources는 스캔하지 않으므로 엔진이 쓰는 기본 폰트는 경로로 직접 등록한다.
	Get().LoadFontAtlas(ResourcePaths::DefaultFont, ResourcePaths::DefaultFontAtlas);
	Get().ScanAssets("Assets", OnProgress);
	Get().CreateDefaultMeshes();
	Get().CreateParticleMaterial();
	Get().CreateEditorArrowMaterial();
}

void UAssetManager::ScanAssets(const fs::path& AssetRoot, const FAssetLoadProgress& OnProgress)
{
	if (!fs::exists(AssetRoot))
	{
		HTR_LOG(Error, "Asset root not found: {}", AssetRoot.generic_string());
		return;
	}

	TArray<fs::path> Files;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(AssetRoot))
	{
		if (Entry.is_regular_file())
		{
			Files.Add(Entry.path());
		}
	}

	const int32 Total = Files.Num();
	for (int32 i = 0; i < Total; ++i)
	{
		FString Key = ToUTF8String(fs::relative(Files[i], AssetRoot));
		FString Path = ToUTF8String(Files[i]);
		AssetPathMap.Add(Key, Path);
		LoadAsset(Key, Path);

		if (OnProgress)
		{
			OnProgress(i + 1, Total, Key);
		}
	}
}

void UAssetManager::LoadAsset(const FString& Key, const FString& Path) {
	const FString Extension{ GetAssetExtension(Path) };
	if (Extension == ".png" || Extension == ".dds") {
		if (UTexture2D* Texture{ LoadTexture(Path) }) {
			AssetMap[Key] = Texture;
		}
	}
	if (Extension == ".json") {
		const FString TexturePath{ fs::path{ Path }.replace_extension(".png").string() };
		if (UFont* Font{ LoadFontAtlas(Path, TexturePath) }) {
			AssetMap[Key] = Font;
		}
	}
	if (Extension == ".obj") {
		LoadObjStaticMesh(Path);
	}
	if (Extension == ".fbx")
	{
		LoadFbxSkeletonMesh(Path);
	}
}

void UAssetManager::CreateDefaultTextures() {
	// 텍스처가 지정되지 않은 머티리얼이 검게 나오지 않도록 하는 1x1 흰색 텍스처.
	// 셰이더에서 곱해도 결과가 변하지 않으므로 "텍스처 없음"의 기본값으로 쓴다.
	const uint32 WhitePixel{ 0xFFFFFFFF };

	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = 1;
	Desc.Height = 1;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_IMMUTABLE;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	TUniquePtr<FTexture2D> Resource{ RenderCommand::CreateTexture2D(Desc, &WhitePixel) };
	if (!Resource) {
		return;
	}

	UTexture2D* WhiteTexture{ NewObject<UTexture2D>() };
	WhiteTexture->SetResource(std::move(Resource));
	WhiteTexture->SetPath("WhiteTexture");

	AssetMap["WhiteTexture"] = WhiteTexture;

	const FVector4 NormalPixel{ 0.5f, 0.5f, 1.0f, 1.0f };
	Desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	TUniquePtr<FTexture2D> NormalResource{ RenderCommand::CreateTexture2D(Desc, &NormalPixel) };
	if (!NormalResource) {
		return;
	}

	UTexture2D* FlatNormalTexture{ NewObject<UTexture2D>() };
	FlatNormalTexture->SetResource(std::move(NormalResource));
	FlatNormalTexture->SetPath("FlatNormalTexture");
	AssetMap["FlatNormalTexture"] = FlatNormalTexture;
}

void UAssetManager::CreateDefaultMeshes()
{
	// 메시 데이터 업로드
	FStaticMeshData* CubeData = FGeometryGenerator::GetMeshData("Cube");
	RegisterAsset("Cube", CreateStaticMesh(CubeData));

	FStaticMeshData* ConeData = FGeometryGenerator::GetMeshData("Cone");
	RegisterAsset("Cone", CreateStaticMesh(ConeData));

	FStaticMeshData* SphereData = FGeometryGenerator::GetMeshData("Sphere");
	RegisterAsset("Sphere", CreateStaticMesh(SphereData));

	FStaticMeshData* CylinderData = FGeometryGenerator::GetMeshData("Cylinder");
	RegisterAsset("Cylinder", CreateStaticMesh(CylinderData));

	FStaticMeshData* PlaneData = FGeometryGenerator::GetMeshData("Plane");
	RegisterAsset("Plane", CreateStaticMesh(PlaneData));

	FStaticMeshData* ArrowMeshData = FGeometryGenerator::GetMeshData("Arrow");
	RegisterAsset("Arrow", CreateStaticMesh(ArrowMeshData));

	FStaticMeshData* RingMeshData = FGeometryGenerator::GetMeshData("Ring");
	RegisterAsset("Ring", CreateStaticMesh(RingMeshData));

	FStaticMeshData* ScaleBarData = FGeometryGenerator::GetMeshData("ScaleBar");
	RegisterAsset("ScaleBar", CreateStaticMesh(ScaleBarData));

	FStaticMeshData* GizmoSphereData = FGeometryGenerator::GetMeshData("GizmoSphere");
	RegisterAsset("GizmoSphere", CreateStaticMesh(GizmoSphereData));

	const FParticleVertex Vertices[] =
	{
		{ FVector(0.0f, -0.5f,  0.5f), FVector2(0.0f, 0.0f) },
		{ FVector(0.0f, 0.5f,  0.5f), FVector2(1.0f, 0.0f) },
		{ FVector(0.0f, 0.5f, -0.5f), FVector2(1.0f, 1.0f) },
		{ FVector(0.0f, -0.5f, -0.5f), FVector2(0.0f, 1.0f) }
	};

	const uint32 Indices[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	UStaticMesh* Mesh = NewObject<UStaticMesh>();
	Mesh->VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
		Vertices,
		sizeof(Vertices), sizeof(FParticleVertex));
	Mesh->IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
		Indices,
		ARRAYSIZE(Indices));

	FVertex Vertex{};
	Vertex.Position = FVector(0.0f, -0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, 0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, 0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);
	Vertex.Position = FVector(0.0f, -0.5f, -0.5f);
	Mesh->MeshData.Vertices.Add(Vertex);

	Mesh->MeshData.Indices = { 0, 1, 2, 0, 2, 3, 0,2,1, 0,3,2 };

	Mesh->MeshData.AABB.Min = FVector(-0.001f, -0.5, -0.5);
	Mesh->MeshData.AABB.Max = FVector(0.001f, 0.5, 0.5);
	Mesh->RebuildRenderData();

	RegisterAsset("ParticleQuad", Mesh);
}

void UAssetManager::CreateDefaultMaterial() {
	UMaterial* DefaultMat{ NewObject<UMaterial>() };
	DefaultMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/UberLit.hlsl");
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("WhiteTexture"));
	DefaultMat->Textures.Add(GetAssetByPath<UTexture2D>("FlatNormalTexture"));
	DefaultMat->ParamLayout = EMaterialParamLayout::StaticMesh;
	DefaultMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
	RegisterAsset("DefaultMaterial", DefaultMat);
}

void UAssetManager::CreateParticleMaterial()
{
	UMaterial* ParticleMat = NewObject<UMaterial>();
	ParticleMat->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/ParticleSubUVShader.hlsl");
	ParticleMat->Textures.Add(LoadTexture(ResourcePaths::DefaultParticleTexture));
	ParticleMat->BlendState = EBlendState::AlphaBlend;
	ParticleMat->DepthStencilState = EDepthStencilState::ReadOnly;
	ParticleMat->ParamLayout = EMaterialParamLayout::ParticleSubUV;
	ParticleMat->ParamBuffer = RenderCommand::CreateConstantBuffer(256);
	RegisterAsset("SubUVMaterial", ParticleMat);
}

void UAssetManager::CreateEditorArrowMaterial() {
	UMaterial* BaseMaterial{ GetAssetByPath<UMaterial>("DefaultMaterial") };
	UMaterial* ArrowMaterial{ UMaterial::CreateInstance(BaseMaterial) };
	ArrowMaterial->Shader = FRenderResourceManager::GetShaderProgram("Resources/Shader/EditorArrowShader.hlsl");
	ArrowMaterial->BaseColor = FVector4{ 1.0f, 1.0f, 1.0f, 1.0f };
	RegisterAsset("EditorArrowMaterial", ArrowMaterial);
}

void UAssetManager::Shutdown()
{
	Get().AssetMap.Empty();
}


void UAssetManager::RegisterAsset(const FString& Key, URenderAsset* Asset)
{
	Asset->SetPath(Key);
	AssetMap[Key] = Asset;
}

UTexture2D* UAssetManager::LoadTexture(const FString& InPath, bool GenerateMips) {
	if (URenderAsset** Found{ AssetMap.Find(InPath) }) {
		return Cast<UTexture2D>(*Found);
	}

	TUniquePtr<FTexture2D> Resource{};
	if (GetAssetExtension(InPath) == ".dds") {
		Resource = RenderCommand::LoadDDSTexture(InPath);
	} else {
		const FImageData Data{ ImageLoader::LoadAuto(InPath) };
		if (!Data.IsValid()) {
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC Desc{};
		Desc.Width = Data.Width;
		Desc.Height = Data.Height;
		Desc.ArraySize = 1;
		Desc.Format = Data.Format;
		Desc.SampleDesc.Count = 1;
		Desc.MipLevels = GenerateMips ? 0 : 1;
		Desc.Usage = GenerateMips ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (GenerateMips ? D3D11_BIND_RENDER_TARGET : 0);
		Desc.MiscFlags = GenerateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
		Resource = RenderCommand::CreateTexture2D(Desc, Data);
	}

	if (!Resource || !Resource->GetSRV()) {
		return nullptr;
	}

	UTexture2D* Asset{ NewObject<UTexture2D>() };
	Asset->SetResource(std::move(Resource));
	RegisterAsset(InPath, Asset);

	return Asset;
}

UFont* UAssetManager::LoadFontAtlas(const FString& JsonPath, const FString& AtlasTexturePath)
{
	UFont* Font = NewObject<UFont>();

	if (!Font->LoadFontAtlasJson(JsonPath))
	{
		return nullptr;
	}

	Font->AtlasTexture = LoadTexture(AtlasTexturePath, false);
	if (!Font->AtlasTexture)
	{
		return nullptr;
	}

	RegisterAsset(JsonPath, Font);
	return Font;
}

UStaticMesh* UAssetManager::LoadObjStaticMesh(const FString& Path)
{
	const FString Key = MakeAssetKey(Path);
	if (UStaticMesh* Cached = GetAssetByPath<UStaticMesh>(Key))
	{
		return Cached;
	}

	TUniquePtr<FStaticMeshData> Data = FObjImporter::LoadStaticMeshData(Path);
	UStaticMesh* Mesh = CreateStaticMesh(Data.get());   // Data가 nullptr이면 nullptr 반환
	if (!Mesh)
	{
		return nullptr;
	}

	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}

USkeletalMesh* UAssetManager::LoadFbxSkeletonMesh(const FString& Path)
{
	const FString Key = MakeAssetKey(Path);
	if (USkeletalMesh* Cached = GetAssetByPath<USkeletalMesh>(Key))
	{
		return Cached;
	}

	// Importer가 내부에서 FBX Scene을 생성하고, 엔진 데이터로 읽은 뒤 해제한다.
	FFbxImporter Importer;
	const FFbxImportResult ImportResult = Importer.ImportFBX(Path, Key);
	if (!ImportResult.bSuccess)
	{
		HTR_LOG(Error, "[FBX] Skeletal mesh import failed: {}", Path);
		return nullptr;
	}

	// Import 결과를 Cook한 뒤 GPU 버퍼와 실제 Material을 갖는 에셋을 만든다.
	FSkeletalMeshData Data;
	Importer.Cook(ImportResult, Data);
	USkeletalMesh* Mesh = CreateSkeletalMesh(&Data);
	if (!Mesh)
	{
		HTR_LOG(Error, "[FBX] Skeletal mesh resource creation failed: {}", Path);
		return nullptr;
	}

	// 생성에 성공한 에셋만 등록하고, 다음 로드에서는 캐시를 반환한다.
	Get().RegisterAsset(Key, Mesh);
	return Mesh;
}


bool UAssetManager::ReorientStaticMesh(UStaticMesh& Mesh, const std::function<FVector(const FVector&)>& Rotate)
{
	if (Mesh.GetLODCount() > 1 || Mesh.MeshData.Vertices.IsEmpty())
		return false;   // LOD가 이미 옛 방향으로 만들어졌으면 서로 어긋난다

	FStaticMeshData& Data = Mesh.MeshData;
	FBox Bounds{ FVector(FLT_MAX, FLT_MAX, FLT_MAX), FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
	for (FVertex& Vertex : Data.Vertices)
	{
		Vertex.Position = Rotate(Vertex.Position);
		Vertex.Normal = Rotate(Vertex.Normal);
		Bounds.Min = FVector(std::min(Bounds.Min.X, Vertex.Position.X), std::min(Bounds.Min.Y, Vertex.Position.Y), std::min(Bounds.Min.Z, Vertex.Position.Z));
		Bounds.Max = FVector(std::max(Bounds.Max.X, Vertex.Position.X), std::max(Bounds.Max.Y, Vertex.Position.Y), std::max(Bounds.Max.Z, Vertex.Position.Z));
	}
	Data.AABB = Bounds;
	Data.BuildTangentSpace();

	// 인덱스는 그대로다 (회전은 삼각형 감기 방향을 바꾸지 않는다). 정점 버퍼만 다시 올린다.
	TUniquePtr<FVertexBuffer> VB = RenderCommand::CreateStaticVertexBuffer(Data.Vertices.GetData(),
		sizeof(FVertex) * static_cast<uint32>(Data.Vertices.Num()), sizeof(FVertex));
	if (!VB)
		return false;
	Mesh.VertexBuffer = std::move(VB);

	Data.BuildTriangleBVH();   // 피킹용 삼각형 BVH도 새 좌표로
	return true;
}

void UAssetManager::ForEachObjStaticMesh(const std::function<void(const FString& Key, UStaticMesh& Mesh)>& Func)
{
	for (auto& [Key, Asset] : Get().AssetMap)
	{
		if (!Asset || !Asset->IsA<UStaticMesh>() || !fs::path(Key).extension().string().ends_with(".obj"))
			continue;
		Func(Key, *Cast<UStaticMesh>(Asset));
	}
}

FLODGenerateResult UAssetManager::GenerateStaticMeshLODs(UStaticMesh& Mesh, const FLODGenerateRequest& Request)
{
	TArray<FStaticMeshData> Generated;
	FLODGenerateResult Result = FStaticMeshLODGenerator::Generate(Mesh.GetMeshData(), Generated);

	if (!Result.bSuccess) return Result;

	if (!(Request.ScreenThresholds[0] >
		Request.ScreenThresholds[1] &&
		Request.ScreenThresholds[1] >
		Request.ScreenThresholds[2] &&
		Request.ScreenThresholds[2] > 0.0f))
	{
		Result.bSuccess = false;
		Result.FailureReason = "Screen thresholds must descend";
		return Result;
	}

	TArray<FStaticMeshLODResource> NewResources;

	for (FStaticMeshData& Data : Generated)
	{
		FStaticMeshLODResource Resource;
		Resource.Data = std::move(Data);

		Resource.Data.BuildTangentSpace();
		Resource.Data.OptimizeTriangleOrderForVertexCache();

		Resource.VertexBuffer = RenderCommand::CreateStaticVertexBuffer(
			Resource.Data.Vertices.GetData(),
			sizeof(FVertex) *
			static_cast<uint32>(Resource.Data.Vertices.Num()),
			sizeof(FVertex));

		Resource.IndexBuffer = RenderCommand::CreateStaticIndexBuffer(
			Resource.Data.Indices.GetData(),
			static_cast<uint32>(Resource.Data.Indices.Num()));

		if (!Resource.VertexBuffer || !Resource.IndexBuffer)
		{
			Result.bSuccess = false;
			Result.FailureReason = "LOD GPU buffer creation failed";
			return Result;
		}

		NewResources.Add(std::move(Resource));
	}

	// bSaveToAsset이면 이 지점에서 파일 저장.
	// 저장 실패 시 기존 Mesh.AdditionalLODs는 그대로 둔다.

	Mesh.AdditionalLODs = std::move(NewResources);
	Mesh.ScreenThresholds = Request.ScreenThresholds;
	Mesh.RebuildRenderData();
	return Result;
}
