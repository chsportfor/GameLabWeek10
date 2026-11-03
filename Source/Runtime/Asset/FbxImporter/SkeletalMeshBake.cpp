#include "EnginePCH.h"
#include "SkeletalMeshBake.h"

#include <cstring>
#include <fstream>
#include <type_traits>

#include "Render/SkeletalMeshData.h"
#include "Core/EngineLog.h"

static_assert(sizeof(FSkeletalVertex) == 64);
static_assert(std::is_trivially_copyable_v<FSkeletalVertex>);
static_assert(std::is_trivially_copyable_v<FSkelMeshSection>);
static_assert(sizeof(FMatrix) == 64);
static_assert(std::is_trivially_copyable_v<FTransform>);
static_assert(std::is_trivially_copyable_v<FBox>);

namespace
{
	constexpr uint32 SkeletalBakeVersion{ 3 };

	struct FBakeHeader
	{
		// 'S'keletal 'K' 'B'a'K'e
		char Magic[4] = { 'S', 'K', 'B', 'K' };
		uint32 Version = SkeletalBakeVersion;
	};

	template<typename T>
	void WritePod(std::ofstream& Out, const T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T> || std::is_same_v<T, FMatrix>, "invalid plain old data");
		Out.write(reinterpret_cast<const char*>(&Value), sizeof(T));
	}

	template<typename T>
	bool ReadPod(std::ifstream& In, T& Value)
	{
		static_assert(std::is_trivially_copyable_v<T> || std::is_same_v<T, FMatrix>);
		In.read(reinterpret_cast<char*>(&Value), sizeof(T));
		return static_cast<bool>(In);
	}

	template<typename T>
	void WriteArray(std::ofstream& Out, const TArray<T>& Array)
	{
		const uint32 Count = static_cast<uint32>(Array.Num());
		WritePod(Out, Count);
		if (Count > 0)
		{
			Out.write(reinterpret_cast<const char*>(Array.GetData()), sizeof(T) * Count);
		}
	}

	template<typename T>
	bool ReadArray(std::ifstream& In, TArray<T>& Array, uint32 MaxCount)
	{
		uint32 Count = 0;
		if (!ReadPod(In, Count) || Count > MaxCount)
		{
			return false;
		}

		Array.SetNum(static_cast<int32>(Count));
		if (Count > 0)
		{
			In.read(reinterpret_cast<char*>(Array.GetData()), sizeof(T) * Count);
		}
		return static_cast<bool>(In);
	}

	void WriteString(std::ofstream& Out, const FString& Str)
	{
		const uint32 Length = static_cast<uint32>(Str.size());
		WritePod(Out, Length);
		if (Length > 0)
		{
			Out.write(Str.data(), Length);
		}
	}

	bool ReadString(std::ifstream& In, FString& Str)
	{
		uint32 Length = 0;
		if (!ReadPod(In, Length) || Length > 4096)
		{
			return false;
		}
		Str.resize(Length);
		if (Length > 0)
		{
			In.read(Str.data(), Length);
		}
		return static_cast<bool>(In);
	}
}

TUniquePtr<FSkeletalMeshData> FSkeletalMeshBake::ReadBaked(const FString& BinPath)
{
	std::ifstream In(BinPath, std::ios::binary);
	if (!In)
	{
		return nullptr;
	}

	TUniquePtr<FSkeletalMeshData> Data = MakeUnique<FSkeletalMeshData>();
	constexpr uint32 MaxCount = 1000000;

	FBakeHeader Header;
	const FBakeHeader Expected;
	if (!ReadPod<FBakeHeader>(In, Header) ||
		std::memcmp(Header.Magic, Expected.Magic, sizeof(Header.Magic)) != 0 ||
		Header.Version != Expected.Version)
	{
		HTR_LOG(Warning, "[BAKE] {}: invalid skeletal header or version, re-importing", BinPath);
		return nullptr;
	}

	if (!ReadArray<FSkeletalVertex>(In, Data->Vertices, MaxCount) ||
		!ReadArray<uint32>(In, Data->Indices, MaxCount) ||
		!ReadArray<FSkelMeshSection>(In, Data->Sections, MaxCount))
	{
		return nullptr;
	}

	uint32 MaterialSlotsNum = 0;
	if (!ReadPod<uint32>(In, MaterialSlotsNum) || MaterialSlotsNum > 1024)
	{
		return nullptr;
	}

	for (uint32 i = 0; i < MaterialSlotsNum; ++i)
	{
		FSkeletalMaterialSlot Slot{};
		if (!ReadString(In, Slot.Name) ||
			!ReadPod<FVector4>(In, Slot.BaseColor) ||
			!ReadString(In, Slot.DiffuseTexturePath) ||
			!ReadString(In, Slot.NormalTexturePath))
		{
			return nullptr;
		}
		Data->MaterialSlots.Add(std::move(Slot));
	}

	if (!ReadPod<FBox>(In, Data->AABB))
	{
		return nullptr;
	}

	uint32 BoneCount = 0;
	if (!ReadPod<uint32>(In, BoneCount) || BoneCount > 4096)
	{
		return nullptr;
	}

	for (uint32 i = 0; i < BoneCount; ++i)
	{
		FBoneInfo Bone{};
		if (!ReadString(In, Bone.Name) ||
			!ReadPod<int32>(In, Bone.ParentIndex) ||
			!ReadPod<FTransform>(In, Bone.LocalTransform))
		{
			return nullptr;
		}
		Data->Bones.Add(std::move(Bone));
	}

	if (!ReadArray<FMatrix>(In, Data->InverseBindPoseMatrices, 4096))
	{
		return nullptr;
	}

	FString Error;
	if (!Data->Validate(Error))
	{
		HTR_LOG(Warning, "[BAKE] {}: {}, re-importing", BinPath, Error);
		return nullptr;
	}

	HTR_LOG(Info, "[BAKE] {}: skeletal mesh loaded", BinPath);
	return Data;
}

void FSkeletalMeshBake::WriteBaked(const FString& BinPath, const FSkeletalMeshData& Data)
{
	std::ofstream Out(BinPath, std::ios::binary);
	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: cannot open for writing", BinPath);
		return;
	}

	WritePod<FBakeHeader>(Out, FBakeHeader());
	WriteArray<FSkeletalVertex>(Out, Data.Vertices);
	WriteArray<uint32>(Out, Data.Indices);
	WriteArray<FSkelMeshSection>(Out, Data.Sections);

	// MaterialSlots
	WritePod<uint32>(Out, static_cast<uint32>(Data.MaterialSlots.Num()));
	for (const auto& Slot : Data.MaterialSlots)
	{
		WriteString(Out, Slot.Name);
		WritePod<FVector4>(Out, Slot.BaseColor);
		WriteString(Out, Slot.DiffuseTexturePath);
		WriteString(Out, Slot.NormalTexturePath);
	}

	// AABB
	WritePod<FBox>(Out, Data.AABB);

	// Bones
	WritePod<uint32>(Out, static_cast<uint32>(Data.Bones.Num()));
	for (const auto& Bone : Data.Bones)
	{
		WriteString(Out, Bone.Name);
		WritePod<int32>(Out, Bone.ParentIndex);
		WritePod<FTransform>(Out, Bone.LocalTransform);
	}

	// InverseBindPoseMatrices
	WriteArray<FMatrix>(Out, Data.InverseBindPoseMatrices);

	if (!Out)
	{
		HTR_LOG(Warning, "[BAKE] {}: skeletal mesh write failed", BinPath);
	}
}
