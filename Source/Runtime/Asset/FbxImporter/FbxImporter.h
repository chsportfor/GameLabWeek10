#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"

struct FSkeletalMeshData;
struct FStaticMeshData;

enum class EFbxImportFlags : uint32
{
	None           = 0,
	Material       = 1 << 0, // 머티리얼 정보 임포트 (IMP_FBX_MATERIAL)
	Texture        = 1 << 1, // 텍스처 정보 임포트 (IMP_FBX_TEXTURE)
	Link           = 1 << 2, // 스켈레톤(본) 및 스킨 디포머/가중치 임포트 (IMP_FBX_LINK)
	Shape          = 1 << 3, // 블렌드 셰이프/모프 타깃 임포트 (IMP_FBX_SHAPE)
	Animation      = 1 << 4, // 애니메이션 커브/키프레임 임포트 (IMP_FBX_ANIMATION)
	GlobalSettings = 1 << 5, // 축, 단위 등 글로벌 설정 임포트 (IMP_FBX_GLOBAL_SETTINGS)

	StaticMesh     = (1 << 0) | (1 << 1) | (1 << 5),
	SkeletalMesh   = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 5),
	AnimationOnly  = (1 << 4) | (1 << 5),
	All            = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5),
};
DEFINE_ENUM_OPERATORS(EFbxImportFlags);

namespace fbxsdk
{
	class FbxManager;
	class FbxIOSettings;
	class FbxScene;
}

class FFbxImporter
{
public:
	static FFbxImporter* GetInstance();

	bool Initialize();
	void Shutdown();
	bool IsInitialized() const { return SdkManager != nullptr; }

	fbxsdk::FbxManager* GetManager() const { return SdkManager; }
	fbxsdk::FbxIOSettings* GetIOSettings() const { return IOSettings; }

	// 비트 플래그를 받아 FbxIOSettings 속성들을 적용
	void ApplyIOSettings(EFbxImportFlags Flags);

	// 개별 플래그 추가/제거
	void EnableImportFlag(EFbxImportFlags Flag)  { ApplyIOSettings(CurrentImportFlags | Flag); }
	void DisableImportFlag(EFbxImportFlags Flag) { ApplyIOSettings(CurrentImportFlags & ~Flag); }
	bool HasImportFlag(EFbxImportFlags Flag) const { return HasFlag(CurrentImportFlags, Flag); }

	// 현재 설정된 플래그 조회
	EFbxImportFlags GetCurrentImportFlags() const { return CurrentImportFlags; }

	// FBX 파일 정보 출력 (콘솔 fbxinfo 명령어용 간략 인스펙션)
	static bool LoadAndPrintFbxScene(const FString& FilePath, EFbxImportFlags Flags = EFbxImportFlags::All);

	// 바이너리 캐시 우선 로드
	static TUniquePtr<FSkeletalMeshData> LoadSkeletalMeshData(const FString& FilePath);
	static TUniquePtr<FStaticMeshData> LoadStaticMeshData(const FString& FilePath);

	// FBX 직접 파싱
	bool ImportSkeletalMesh(const FString& FilePath, FSkeletalMeshData& OutMeshData);
	bool ImportStaticMesh(const FString& FilePath, FStaticMeshData& OutMeshData);

private:
	FFbxImporter();
	~FFbxImporter();

	FFbxImporter(const FFbxImporter&) = delete;
	FFbxImporter& operator=(const FFbxImporter&) = delete;

	fbxsdk::FbxManager* SdkManager = nullptr;
	fbxsdk::FbxIOSettings* IOSettings = nullptr;
	EFbxImportFlags CurrentImportFlags = EFbxImportFlags::All;

	fbxsdk::FbxScene* LoadFbxScene(const FString& FilePath, EFbxImportFlags Flags);
	void PreprocessScene(fbxsdk::FbxScene* Scene);
	bool ProcessSkeleton(fbxsdk::FbxScene* Scene, FSkeletalMeshData& OutMesh);
	bool ProcessGeometry(fbxsdk::FbxScene* Scene, FSkeletalMeshData& OutMesh, const FString& FilePath = "");
	bool ProcessStaticGeometry(fbxsdk::FbxScene* Scene, FStaticMeshData& OutMesh, const FString& FilePath = "");
};
